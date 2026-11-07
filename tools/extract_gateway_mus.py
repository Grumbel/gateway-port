#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Extract a validated MT-32 MIDI or AdLib MIDIPlay/MDI song from a MUS archive.

The original archive is read-only. Output contains proprietary MIDI data and
must remain outside the repository, e.g. beneath build/ or in /tmp.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct


DIRECTORY_ENTRIES = 60
DESCRIPTOR_BYTES = 12
FIRST_PAYLOAD = DIRECTORY_ENTRIES * 2 * DESCRIPTOR_BYTES + 1
DESCRIPTOR = struct.Struct("<IHhHH")


@dataclass(frozen=True)
class Song:
    index: int
    device: str
    offset: int
    size: int
    midi_size: int
    tracks: int
    division: int


@dataclass(frozen=True)
class MidiStats:
    notes: int
    instrument_definitions: int
    tempo_changes: int
    duration_ms: int


def read_vlq(data: bytes, pos: int, end: int) -> tuple[int, int]:
    value = 0
    for _ in range(4):
        if pos >= end:
            raise ValueError("truncated MIDI variable-length quantity")
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7f)
        if byte < 0x80:
            return value, pos
    raise ValueError("MIDI variable-length quantity exceeds four bytes")


def inspect_adlib_midi(midi: bytes) -> MidiStats:
    """Inspect the type-0 AdLib stream without interpreting its FM patch bytes."""
    if midi[:4] != b"MThd" or len(midi) < 22:
        raise ValueError("invalid MIDI header")
    header_size, fmt, tracks, division = struct.unpack_from(">IHHH", midi, 4)
    if (header_size, fmt, tracks) != (6, 0, 1) or division == 0:
        raise ValueError("not an AdLib type-0 MIDI stream")
    start = 8 + header_size
    if midi[start:start + 4] != b"MTrk":
        raise ValueError("missing MIDI event track")
    end = start + 8 + struct.unpack_from(">I", midi, start + 4)[0]
    # AdLib MIDIPlay/MDI stores 00 FF 2F 00 *outside* the declared MTrk
    # chunk. All 37 Gateway streams use exactly this arrangement.
    if end + 4 != len(midi) or midi[end:] != b"\0\xff\x2f\0":
        raise ValueError("AdLib MIDI stream lacks its external end-of-track")
    pos = start + 8
    running_status = 0
    tempo = 500000  # SMF default microseconds per quarter-note
    elapsed_us = 0.0
    notes = patches = tempos = 0
    while pos < end:
        ticks, pos = read_vlq(midi, pos, end)
        elapsed_us += ticks * tempo / division
        if pos >= end:
            raise ValueError("truncated MIDI event")
        status = midi[pos]
        if status >= 0x80:
            pos += 1
            if status < 0xf0:
                running_status = status
            else:
                running_status = 0
        else:
            if not 0x80 <= running_status <= 0xef:
                raise ValueError("data byte without MIDI running status")
            status = running_status
        if status == 0xff:
            if pos >= end:
                raise ValueError("missing MIDI meta event type")
            event_type = midi[pos]
            pos += 1
            size, pos = read_vlq(midi, pos, end)
            if pos + size > end:
                raise ValueError("MIDI meta event crosses track boundary")
            payload = midi[pos:pos + size]
            pos += size
            if event_type == 0x51:
                if size != 3:
                    raise ValueError("bad MIDI tempo event")
                tempo = int.from_bytes(payload, "big")
                tempos += 1
            elif event_type == 0x7f and payload.startswith(b"\0\0\x3f\0\x01"):
                if size != 34:
                    raise ValueError("bad AdLib FM instrument payload")
                patches += 1
            elif event_type == 0x2f:
                raise ValueError("unexpected in-track AdLib end-of-track")
        elif status in (0xf0, 0xf7):
            size, pos = read_vlq(midi, pos, end)
            if pos + size > end:
                raise ValueError("MIDI SysEx crosses track boundary")
            pos += size
        elif 0x80 <= status <= 0xef:
            count = 1 if status & 0xf0 in (0xc0, 0xd0) else 2
            if pos + count > end or any(byte & 0x80 for byte in midi[pos:pos + count]):
                raise ValueError("invalid MIDI channel event data")
            if status & 0xf0 == 0x90 and midi[pos + 1] != 0:
                notes += 1
            pos += count
        else:
            raise ValueError(f"unsupported MIDI event 0x{status:02x}")
    return MidiStats(notes, patches, tempos, round(elapsed_us / 1000))


def read_midi(data: bytes, start: int, end: int, expected_format: int) -> tuple[int, int, int]:
    if end > len(data) or start < FIRST_PAYLOAD or end - start < 22:
        raise ValueError("MIDI descriptor is outside the MUS payload")
    if data[start:start + 4] != b"MThd":
        raise ValueError("MIDI header not found at the descriptor offset")
    header_size, midi_format, tracks, division = struct.unpack_from(">IHHH", data, start + 4)
    if header_size != 6 or midi_format != expected_format or tracks == 0:
        raise ValueError("unexpected MIDI header, format or track count")
    offset = start + 8 + header_size
    for _ in range(tracks):
        if offset + 8 > end or data[offset:offset + 4] != b"MTrk":
            raise ValueError("invalid MIDI track header")
        size = struct.unpack_from(">I", data, offset + 4)[0]
        offset += size + 8
        if offset > end:
            raise ValueError("MIDI track exceeds descriptor bounds")
    expected_trailer = b"" if expected_format == 1 else b"\0\xff\x2f\0"
    if data[offset:end] != expected_trailer:
        raise ValueError("unexpected bytes after the final MIDI track")
    return end - start, tracks, division


def parse_archive(data: bytes) -> list[Song]:
    if len(data) < FIRST_PAYLOAD or data[FIRST_PAYLOAD - 1] != 0:
        raise ValueError("truncated or invalid MUS directory")
    songs = []
    expected_offset = FIRST_PAYLOAD
    empty_seen = False
    for song in range(DIRECTORY_ENTRIES):
        descriptors = []
        for part, device in enumerate(("mt32", "adlib")):
            offset, size, tag, a, b = DESCRIPTOR.unpack_from(
                data, song * 2 * DESCRIPTOR_BYTES + part * DESCRIPTOR_BYTES)
            descriptors.append((offset, size, tag, a, b))
            if offset == 0:
                if (size, tag, a, b) != (0, -1, 0, 0):
                    raise ValueError(f"unexpected empty descriptor in song {song}")
                continue
            if empty_seen or tag != (1 if part == 0 else 0):
                raise ValueError(f"misplaced/unknown song descriptor {song}/{device}")
            if offset != expected_offset or offset + size > len(data):
                raise ValueError(f"invalid song offset/size {song}/{device}")
            midi_size, tracks, division = read_midi(
                data, offset, offset + size, 1 if part == 0 else 0)
            songs.append(Song(song, device, offset, size, midi_size, tracks, division))
            # All adjacent streams are separated by one NUL. The final
            # descriptor, unlike intermediate descriptors, ends at EOF.
            expected_offset = offset + size
            if expected_offset < len(data):
                if data[expected_offset] != 0:
                    raise ValueError(f"missing song separator at {song}/{device}")
                expected_offset += 1
        if not any(d[0] for d in descriptors):
            empty_seen = True
        elif any(d[0] == 0 for d in descriptors):
            raise ValueError(f"half-empty song descriptor pair {song}")
    if not songs or expected_offset != len(data):
        raise ValueError("MUS content does not end after the final song")
    return songs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="original GATE_004.MUS (or another verified MUS)")
    parser.add_argument("--list", action="store_true", help="list descriptors without extracting any music")
    parser.add_argument("--inspect", action="store_true", help="report AdLib notes, patches and duration")
    parser.add_argument("--song", type=int, default=0, help="zero-based song index (default 0, intro)")
    parser.add_argument("--device", choices=("adlib", "mt32"), default="adlib")
    parser.add_argument("--output", type=Path, help="private .mdi or .mid output path")
    args = parser.parse_args()
    try:
        data = args.archive.read_bytes()
        songs = parse_archive(data)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.list:
        for song in songs:
            print(f"{song.index:02d} {song.device:5s} "
                  f"bytes={song.midi_size} tracks={song.tracks} division={song.division}")
    if args.output or args.inspect:
        match = next((s for s in songs if s.index == args.song and s.device == args.device), None)
        if match is None:
            parser.error("requested song/device does not exist")
        midi = data[match.offset:match.offset + match.midi_size]
        if args.inspect:
            if match.device != "adlib":
                parser.error("--inspect currently supports AdLib tracks only")
            try:
                stats = inspect_adlib_midi(midi)
            except ValueError as exc:
                parser.error(str(exc))
            print(f"song {match.index} AdLib: {stats.notes} note-ons, "
                  f"{stats.instrument_definitions} FM patches, "
                  f"{stats.tempo_changes} tempo changes, "
                  f"{stats.duration_ms / 1000:.3f}s")
        if args.output:
            if args.output.resolve() == args.archive.resolve():
                parser.error("output must not overwrite the original MUS archive")
            args.output.write_bytes(midi)
            print(f"extracted song {match.index} {match.device} as {args.output} "
                  f"({match.midi_size} bytes)")
    elif not args.list:
        parser.error("provide --list or --output PATH")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""MUS container and embedded SMF bounds checks, without proprietary bytes."""

import importlib.util
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools/extract_gateway_mus.py"
SPEC = importlib.util.spec_from_file_location("extract_gateway_mus", SCRIPT)
MUS = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = MUS
SPEC.loader.exec_module(MUS)


def smf(fmt, division, end_in_track=True):
    track = b"\x00\xff\x2f\x00" if end_in_track else b""
    return (b"MThd" + struct.pack(">IHHH", 6, fmt, 1, division) +
            b"MTrk" + struct.pack(">I", len(track)) + track)


def fixture():
    directory = bytearray(MUS.FIRST_PAYLOAD)
    for i in range(120):
        MUS.DESCRIPTOR.pack_into(directory, i * 12, 0, 0, -1, 0, 0)
    mt32 = smf(1, 120)
    adlib = smf(0, 420, end_in_track=False)
    start = MUS.FIRST_PAYLOAD
    MUS.DESCRIPTOR.pack_into(directory, 0, start, len(mt32), 1, 5, 4)
    MUS.DESCRIPTOR.pack_into(directory, 12, start + len(mt32) + 1,
                             len(adlib) + 4, 0, 0, 0)
    return bytes(directory) + mt32 + b"\0" + adlib + b"\0\xff\x2f\0"


class MusArchiveTests(unittest.TestCase):
    def test_valid_pair_and_external_adlib_eot(self):
        songs = MUS.parse_archive(fixture())
        self.assertEqual([s.device for s in songs], ["mt32", "adlib"])
        self.assertEqual([s.midi_size for s in songs], [26, 26])
        self.assertEqual([s.division for s in songs], [120, 420])

    def test_reject_noncontiguous_descriptor(self):
        data = bytearray(fixture())
        MUS.DESCRIPTOR.pack_into(data, 12, 5000, 30, 0, 0, 0)
        with self.assertRaisesRegex(ValueError, "invalid song offset"):
            MUS.parse_archive(data)

    def test_reject_early_track_eof(self):
        data = bytearray(fixture())
        track_start = MUS.FIRST_PAYLOAD + 14 + 4
        struct.pack_into(">I", data, track_start, 1000)
        with self.assertRaisesRegex(ValueError, "track exceeds"):
            MUS.parse_archive(data)

    def test_reject_missing_separator(self):
        data = bytearray(fixture())
        data[MUS.FIRST_PAYLOAD + 26] = 1
        with self.assertRaisesRegex(ValueError, "missing song separator"):
            MUS.parse_archive(data)

    def test_external_adlib_end_marker(self):
        data = fixture()
        songs = MUS.parse_archive(data)
        adlib = songs[1]
        stream = data[adlib.offset:adlib.offset + adlib.size]
        self.assertEqual(stream[-4:], b"\0\xff\x2f\0")
        stats = MUS.inspect_adlib_midi(stream)
        self.assertEqual((stats.notes, stats.duration_ms), (0, 0))
        with self.assertRaisesRegex(ValueError, "external end-of-track"):
            MUS.inspect_adlib_midi(stream[:-4])


if __name__ == "__main__":
    unittest.main()

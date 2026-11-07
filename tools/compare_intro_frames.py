#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Compare original DOS probe PPMs to the source-C intro's private GVR1.

The original guest clock and reconstructed source-C virtual clock are not
yet calibrated. Nearest and best-matching frames are reported separately;
an optional offset can be supplied without assuming it is always 22 seconds.
No binary game data enters the repository or generated reports.
"""

import argparse
import mmap
from pathlib import Path
import struct


FRAME_PIXELS = 320 * 200
PALETTE_BYTES = 256 * 3
FRAME_BYTES = 4 + FRAME_PIXELS + PALETTE_BYTES
PPM_HEADER = b"P6\n320 200\n255\n"


def rgb_lut(palette):
    colors = []
    for i in range(0, PALETTE_BYTES, 3):
        colors.append(bytes((int(c) << 2 | int(c) >> 4 for c in palette[i:i + 3])))
    return colors


def compare_pixels(recording, frame, expected):
    offset = 4 + frame * FRAME_BYTES + 4
    indices = recording[offset:offset + FRAME_PIXELS]
    palette = recording[offset + FRAME_PIXELS:offset + FRAME_PIXELS + PALETTE_BYTES]
    colors = rgb_lut(palette)
    exact = 0
    for i, index in enumerate(indices):
        pos = i * 3
        exact += colors[index] == expected[pos:pos + 3]
    return exact


def compare(recording, timestamps, guest_time, image, offset_ms, window_ms):
    desired = guest_time - offset_ms
    nearest = min(range(len(timestamps)), key=lambda i: abs(timestamps[i] - desired))
    candidates = [i for i, ms in enumerate(timestamps) if abs(ms - desired) <= window_ms]
    if not candidates:
        candidates = [nearest]
    # Try the nearest timestamps first and stop at an exact RGB match. No
    # later frame can improve 64,000/64,000, and nearest-first sorts ties.
    candidates.sort(key=lambda i: abs(timestamps[i] - desired))
    nearest_score = None
    best_score = -1
    best_index = nearest
    searched = 0
    for i in candidates:
        score = compare_pixels(recording, i, image)
        searched += 1
        if i == nearest:
            nearest_score = score
        if score > best_score:
            best_score, best_index = score, i
        if score == FRAME_PIXELS:
            break
    if nearest_score is None:
        nearest_score = compare_pixels(recording, nearest, image)
    print(f"guest {guest_time} ms -> source target {desired} ms: "
          f"nearest {timestamps[nearest]} ms {nearest_score}/{FRAME_PIXELS} exact "
          f"({100 * nearest_score / FRAME_PIXELS:.2f}%); "
          f"best {timestamps[best_index]} ms {best_score}/{FRAME_PIXELS} "
          f"({100 * best_score / FRAME_PIXELS:.2f}%), "
          f"{searched}/{len(candidates)} candidate frames searched")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path, help="private source-C GVR1 capture")
    parser.add_argument("samples", nargs="+", metavar="GUEST-MS:PROBE.ppm")
    parser.add_argument("--offset-ms", type=int, default=0,
                        help="estimated guest minus source-C clock offset (default 0)")
    parser.add_argument("--window-ms", type=int, default=2000,
                        help="search best-matching source-C frame within this time range")
    args = parser.parse_args()
    with args.recording.open("rb") as capture, mmap.mmap(capture.fileno(), 0, access=mmap.ACCESS_READ) as recording:
        if recording[:4] != b"GVR1" or (len(recording) - 4) % FRAME_BYTES:
            parser.error("not a complete GVR1 capture")
        frame_count = (len(recording) - 4) // FRAME_BYTES
        timestamps = [struct.unpack_from("<I", recording, 4 + i * FRAME_BYTES)[0]
                      for i in range(frame_count)]
        if not timestamps:
            parser.error("empty GVR1 capture")
        for sample in args.samples:
            guest_ms_text, separator, filename = sample.partition(":")
            if not separator:
                parser.error(f"expected GUEST-MS:PROBE.ppm: {sample}")
            data = Path(filename).read_bytes()
            if not data.startswith(PPM_HEADER) or len(data) != len(PPM_HEADER) + FRAME_PIXELS * 3:
                parser.error(f"not an unmodified 320x200 P6 probe PPM: {filename}")
            compare(recording, timestamps, int(guest_ms_text), data[len(PPM_HEADER):],
                    args.offset_ms, args.window_ms)


if __name__ == "__main__":
    main()

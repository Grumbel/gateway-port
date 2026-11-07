#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Private frame comparator semantics, without game bytes."""

import importlib.util
from pathlib import Path
import struct
import unittest


MODULE = Path(__file__).resolve().parents[1] / "tools/compare_intro_frames.py"
SPEC = importlib.util.spec_from_file_location("compare_intro_frames", MODULE)
COMPARATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPARATOR)


class FrameComparisonTests(unittest.TestCase):
    def test_exact_6bit_vga_palette(self):
        palette = bytearray(768)
        palette[3:6] = (63, 16, 7)
        frame = bytes([1]) * COMPARATOR.FRAME_PIXELS
        recording = b"GVR1" + struct.pack("<I", 123) + frame + palette
        rgb = bytes([255, 65, 28]) * COMPARATOR.FRAME_PIXELS
        self.assertEqual(COMPARATOR.compare_pixels(recording, 0, rgb), 64000)
        changed = bytearray(rgb)
        changed[0] = 254
        self.assertEqual(COMPARATOR.compare_pixels(recording, 0, changed), 63999)

    def test_dac_changes_even_with_same_indices(self):
        palette = bytearray(768)
        palette[3:6] = (63, 16, 7)
        indices = bytes([1]) * COMPARATOR.FRAME_PIXELS
        recording = b"GVR1" + struct.pack("<I", 123) + indices + palette
        expected = bytes([255, 65, 28]) * COMPARATOR.FRAME_PIXELS
        self.assertEqual(COMPARATOR.compare_pixels(recording, 0, expected), 64000)
        palette[3] = 62
        mismatched = b"GVR1" + struct.pack("<I", 123) + indices + palette
        self.assertEqual(COMPARATOR.compare_pixels(mismatched, 0, expected), 0)


if __name__ == "__main__":
    unittest.main()

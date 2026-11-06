#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Cross-check source-shaped RTL file wrappers against the original binary.

Only offsets, function boundaries and call identities are checked in Git;
the copyrighted original code and relocated memory remain private inputs.
"""

import hashlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_intro_ghidra import RTL_SHA256

FILE_BASE = 0xe4f1
SEGMENT = 0x0c33
LOAD_SEGMENT = 0x1000

# Original file start, original end (inclusive), linked offset.
ROUTINES = (
    (0xe4ff, 0xe510, 0x00e), (0xe511, 0xe530, 0x020),
    (0xe531, 0xe581, 0x040), (0xe582, 0xe59a, 0x091),
    (0xe59b, 0xe5c4, 0x0aa), (0xe5c5, 0xe5e2, 0x0d4),
    (0xe5e3, 0xe5fb, 0x0f2), (0xe5fc, 0xe61d, 0x10b),
    (0xe61e, 0xe636, 0x12d),
    (0xe637, 0xe64f, 0x146), (0xe650, 0xe668, 0x15f),
    (0xe669, 0xe68a, 0x178), (0xe68b, 0xe69a, 0x19a),
)

# Offsets of original FAR CALL opcodes and verified linked targets.
FAR_CALLS = (
    (0x02d, 0x12a5, 0x3b52), (0x06a, 0x12a5, 0x3366),
    (0x085, 0x1c29, 0x0000), (0x0a0, 0x1c29, 0x0074),
    (0x0bC, 0x1c29, 0x0036), (0x0e6, 0x1c29, 0x0036),
    (0x101, 0x1c29, 0x0036), (0x11e, 0x1c29, 0x0036),
    (0x13c, 0x1c29, 0x0055), (0x155, 0x1c29, 0x0055),
    (0x16e, 0x1c29, 0x0055), (0x18b, 0x1c29, 0x0055),
    (0x1a0, 0x1c29, 0x0028),
)


class GatewayRtlFileBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not original.is_file():
            raise unittest.SkipTest('original GATE.RTL absent')
        cls.rtl = original.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != RTL_SHA256:
            raise unittest.SkipTest('different GATE.RTL revision')
        snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        cls.ram = snapshot.read_bytes() if snapshot.is_file() else None

    def test_original_function_boundaries(self):
        for start, end, entry in ROUTINES:
            with self.subTest(start=hex(start)):
                self.assertEqual(start - FILE_BASE, entry)
                self.assertEqual(self.rtl[start:start + 3], b'\x55\x8b\xec')
                self.assertEqual(self.rtl[end], 0xcb)  # RETF
                if self.ram is not None:
                    linear = (LOAD_SEGMENT + SEGMENT) * 16 + entry
                    self.assertEqual(self.ram[linear:linear + 3],
                                     self.rtl[start:start + 3])
                    self.assertEqual(self.ram[linear + end - start], 0xcb)

    def test_all_far_call_targets(self):
        if self.ram is None:
            self.skipTest('private linked DOS snapshot absent')
        origin = (LOAD_SEGMENT + SEGMENT) * 16
        for at, segment, offset in FAR_CALLS:
            with self.subTest(site=hex(at)):
                self.assertEqual(self.rtl[FILE_BASE + at], 0x9a)
                self.assertEqual(self.rtl[FILE_BASE + at + 1:FILE_BASE + at + 5],
                                 b'\x00' * 4)
                self.assertEqual(self.ram[origin + at], 0x9a)
                actual_offset = int.from_bytes(self.ram[origin + at + 1:
                                                        origin + at + 3], 'little')
                actual_segment = int.from_bytes(self.ram[origin + at + 3:
                                                         origin + at + 5], 'little')
                self.assertEqual((actual_segment, actual_offset), (segment, offset))


if __name__ == '__main__':
    unittest.main()

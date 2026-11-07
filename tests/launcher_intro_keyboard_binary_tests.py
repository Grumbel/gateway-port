#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pin the original linked DOS keyboard wrappers in a private RAM capture.

The original bytes remain outside the repository. Source C is verified by
separate executable tests, not assumed correct from this provenance check.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IntroKeyboardEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        capture = ROOT / 'build/intro-analysis/at-22000.bin'
        if not capture.is_file():
            raise unittest.SkipTest('private DOS capture unavailable')
        cls.ram = capture.read_bytes()
        if len(cls.ram) != 0x100000:
            raise unittest.SkipTest('unexpected DOS capture length')

    def test_read_one_and_conditionally_buffer(self):
        ram = self.ram
        base = (0x0df2 + 0x1000) * 16
        self.assertEqual(ram[base + 0x00f8], 0x55)   # PUSH BP
        self.assertEqual(ram[base + 0x00f9], 0x8b)
        self.assertEqual(ram[base + 0x00fe], 0x0e)   # near call to pending
        self.assertEqual(ram[base + 0x00ff], 0xe8)
        self.assertEqual(ram[base + 0x0106], 0x0e)   # near call to read
        self.assertEqual(ram[base + 0x0107], 0xe8)
        for op, expected in ((0x00ff, 0x000a), (0x0107, 0x0030)):
            displacement = int.from_bytes(ram[base + op + 1:base + op + 3],
                                          'little', signed=True)
            self.assertEqual(op + 3 + displacement, expected)
        self.assertEqual(ram[base + 0x010d], 0x3d)  # compare Space
        self.assertEqual(int.from_bytes(ram[base + 0x010e:base + 0x0110],
                                        'little'), 0x20)
        self.assertEqual(ram[base + 0x0112], 0x3d)  # compare CR
        self.assertEqual(int.from_bytes(ram[base + 0x0113:base + 0x0115],
                                        'little'), 0x0d)
        self.assertEqual(ram[base + 0x0117], 0xa3)  # MOV DS:1340, AX
        self.assertEqual(int.from_bytes(ram[base + 0x0118:base + 0x011a],
                                        'little'), 0x1340)
        self.assertEqual(ram[base + 0x0127], 0xcb)  # RETF

    def test_drain_loop_entry_and_branch(self):
        ram = self.ram
        base = (0x0df2 + 0x1000) * 16
        self.assertEqual(ram[base + 0x0128], 0xeb)  # jump to condition
        self.assertEqual(ram[base + 0x0129], 0x04)
        for call, expected in ((0x012b, 0x0030), (0x012f, 0x000a)):
            self.assertEqual(ram[base + call], 0xe8)
            displacement = int.from_bytes(ram[base + call + 1:base + call + 3],
                                          'little', signed=True)
            self.assertEqual(call + 3 + displacement, expected)
        self.assertEqual(ram[base + 0x0134], 0x75)  # JNZ read loop
        displacement = int.from_bytes(ram[base + 0x0135:base + 0x0136],
                                      'little', signed=True)
        self.assertEqual(0x0136 + displacement, 0x012a)
        self.assertEqual(ram[base + 0x0136], 0xcb)  # RETF


if __name__ == '__main__':
    unittest.main()

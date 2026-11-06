#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pinned locations of the DOS file thunk bank in a live guest snapshot.

These are *absolute guest* segments, not the +0x1000-relative mapped RTL
banks. No original runtime bytes are checked into the repository.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DosResidentFileServices(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = ROOT / 'build/intro-analysis/at-22000.bin'
        if not cls.path.is_file():
            raise unittest.SkipTest('private DOS intro RAM snapshot unavailable')
        cls.ram = cls.path.read_bytes()
        if len(cls.ram) != 1024 * 1024:
            raise unittest.SkipTest('unexpected DOS RAM snapshot format')

    def test_five_real_far_entries(self):
        origin = 0x1c29 * 16
        functions = ((0x0000, 0x0027), (0x0028, 0x0035),
                     (0x0036, 0x0054), (0x0055, 0x0073),
                     (0x0074, 0x0093))
        for start, end in functions:
            with self.subTest(entry=start):
                self.assertEqual(self.ram[origin + start:origin + start + 3],
                                 b'\x55\x8b\xec')
                self.assertEqual(self.ram[origin + end], 0xcb)

    def test_original_interrupts_and_flags(self):
        origin = 0x1c29 * 16
        for site in (0x001d, 0x0030, 0x004a, 0x0069, 0x0085):
            with self.subTest(int21_site=site):
                self.assertEqual(self.ram[origin + site:origin + site + 2],
                                 b'\xcd\x21')
        # On error, open and read/write zero AX; seek returns 0xffff.
        self.assertEqual(self.ram[origin + 0x21:origin + 0x23],
                         b'\x2b\xc0')
        self.assertEqual(self.ram[origin + 0x8d:origin + 0x90],
                         b'\xb8\xff\xff')

    def test_actual_rasterizer_address_is_not_raw_copy(self):
        # 24a7:000c holds an independently loaded VFX prologue, while the
        # previously inspected 34a7c belongs to another unrelocated copy.
        renderer = 0x24a7 * 16 + 0x000c
        self.assertEqual(self.ram[renderer:renderer + 5],
                         b'\x55\x8b\xec\x83\xec')
        self.assertEqual(self.ram[renderer + 0x16:renderer + 0x1b],
                         b'\x9a\x20\x01\xa7\x24')


if __name__ == '__main__':
    unittest.main()

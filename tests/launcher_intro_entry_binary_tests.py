#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Prove the original EXE intro entry and the bounded setup breakpoint."""

import hashlib
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXE_HASH = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'


class OriginalIntroEntry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos/GATE.EXE'
        if not original.is_file():
            raise unittest.SkipTest('original GATE.EXE absent')
        cls.exe = original.read_bytes()
        if hashlib.sha256(cls.exe).hexdigest() != EXE_HASH:
            raise unittest.SkipTest('different GATE.EXE revision')

    def test_prologue_and_five_writes(self):
        e = self.exe
        self.assertEqual(e[0x1302:0x1304], bytes.fromhex('33 c0'))
        for site, selector, destination, value in (
            (0x1309, 0x4e46, 0x0660, 0x0401),
            (0x1314, 0x4e48, 0x065c, 0x007f),
            (0x131f, 0x4e4a, 0x065e, 1),
            (0x132a, 0x4e4c, 0x064e, 0),
        ):
            self.assertEqual(e[site:site + 4],
                             bytes.fromhex('8e 06') + selector.to_bytes(2, 'little'))
            self.assertEqual(e[site + 4:site + 11],
                             bytes.fromhex('26 c7 06') +
                             destination.to_bytes(2, 'little') +
                             value.to_bytes(2, 'little'))
        self.assertEqual(e[0x1339:0x133e], bytes.fromhex('9a 94 00 2c 0e'))
        self.assertEqual(e[0x134c:0x1351], bytes.fromhex('9a ec 01 46 0a'))

    def test_live_entry_and_setup_breakpoints(self):
        entry = ROOT / 'build/intro-analysis/intro-entry.bin'
        after = ROOT / 'build/intro-analysis/intro-setup-end.bin'
        if not entry.is_file() or not after.is_file():
            self.skipTest('private intro entry snapshots absent')
        start, end = entry.read_bytes(), after.read_bytes()
        self.assertEqual(len(start), 0x100000)
        self.assertEqual(len(end), 0x100000)
        # The loaded EXE intro file offset 1302 is 1000:0768;
        # original 134c is 1000:07b2.
        self.assertEqual(start[0x10768:0x1076b], bytes.fromhex('33 c0 9a'))
        self.assertEqual(end[0x107b2], 0x9a)
        ds = 0x2a7a << 4
        for offset, value in ((0x660, 0x0401), (0x65c, 0x007f),
                              (0x65e, 1), (0x64e, 0), (0x658, 0x524e)):
            self.assertEqual(int.from_bytes(end[ds + offset:ds + offset + 2],
                                            'little'), value)


if __name__ == '__main__':
    unittest.main()

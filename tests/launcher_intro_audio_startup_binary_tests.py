#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pin the two branch predicates of the original music startup routine."""

from pathlib import Path
import hashlib
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IntroAudioStartupTests(unittest.TestCase):
    def test_original_rtl_music_entry(self):
        original = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not original.is_file():
            self.skipTest('original GATE.RTL not supplied')
        rtl = original.read_bytes()
        self.assertEqual(hashlib.sha256(rtl).hexdigest(),
                         'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566')
        self.assertEqual(rtl[0xc3c5:0xc3d9].hex(),
                         'f6064a00067503e92a03833e4800007503e92003')
        self.assertEqual(rtl[0xc6f9:0xc6ff].hex(), '5e5f8be55dcb')

    def test_relocated_discriminators(self):
        memory = ROOT / 'build/intro-analysis/at-22000.bin'
        if not memory.is_file():
            self.skipTest('original linked memory capture missing')
        data = memory.read_bytes()
        self.assertEqual(len(data), 0x100000)
        start = (0x1a46 << 4) + 0x01f4
        self.assertEqual(data[start:start + 5], bytes.fromhex('f606620606'))
        self.assertEqual(data[start + 10:start + 15],
                         bytes.fromhex('833e600600'))


if __name__ == '__main__':
    unittest.main()

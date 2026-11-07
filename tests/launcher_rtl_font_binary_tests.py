#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Original-byte evidence for the recovered GATE.RTL font-selection cluster."""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
RTL_SHA256 = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'


def word(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


class RtlFontBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not source.is_file():
            raise unittest.SkipTest('original RTL unavailable')
        cls.rtl = source.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != RTL_SHA256:
            raise unittest.SkipTest('RTL hash does not match pinned original')

    def test_exact_cluster_boundaries_and_original_calls(self):
        rtl = self.rtl
        for entry in (0xe771, 0xe79b, 0xe801, 0xe873):
            self.assertEqual(rtl[entry:entry + 3], b'\x55\x8b\xec')
        for end in (0xe79a, 0xe800, 0xe872, 0xebcf):
            self.assertEqual(rtl[end], 0xcb)
        self.assertEqual(0xe873 - 0x018d, 0xebf7 - 0x0511)
        for site, target in ((0xe788, 0xe79b), (0xe862, 0xe79b),
                             (0xe8f2, 0xe801), (0xea9b, 0xe771),
                             (0xeb81, 0xec0a), (0xeb90, 0xebf7)):
            self.assertEqual(rtl[site - 1:site + 1], b'\x0e\xe8')
            signed = struct.unpack_from('<h', rtl, site + 1)[0]
            self.assertEqual(site + 3 + signed, target)
        # Original source stores two 30-byte cache slots at relocated DS:8.
        for site in (0xe782, 0xe82a, 0xe885, 0xe8b9):
            self.assertEqual(rtl[site:site + 3], b'\x05\x08\x00')
        self.assertEqual(rtl[0xe897:0xe89a], b'\x26\xff\x07')
        self.assertEqual(rtl[0xe8dc:0xe8e1], b'\x26\xc7\x07\x01\x00')
        self.assertEqual(rtl[0xeb36:0xeb3a], b'\xf7\xeb\xf7\xe9')

    def test_live_linked_font_data_and_services(self):
        capture = ROOT / 'build/intro-analysis/at-22000.bin'
        if not capture.is_file():
            self.skipTest('original DOS guest-memory capture unavailable')
        ram = capture.read_bytes()
        origin = (0x1000 + 0x0c4d) * 16 + 0x018d - 0xe873
        self.assertEqual(ram[origin + 0xe873:origin + 0xe87a],
                         self.rtl[0xe873:0xe87a])
        for site, linked in ((0xe7c7, (0x0138, 0x1e2c)),
                             (0xe7da, (0x0138, 0x1e2c)),
                             (0xe7ed, (0x0138, 0x1e2c)),
                             (0xe92f, (0x0040, 0x1c33)),
                             (0xe9c4, (0x00aa, 0x1c33)),
                             (0xea66, (0x00aa, 0x1c33)),
                             (0xea83, (0x0008, 0x1e2c)),
                             (0xeaa1, (0x019a, 0x1c33)),
                             (0xeacc, (0x00f2, 0x1c33)),
                             (0xeae2, (0x0008, 0x1e2c)),
                             (0xeb0b, (0x00f2, 0x1c33)),
                             (0xeb3e, (0x0008, 0x1e2c)),
                             (0xeb69, (0x00f2, 0x1c33)),
                             (0xeb74, (0x019a, 0x1c33))):
            self.assertEqual(ram[origin + site], 0x9a)
            self.assertEqual((word(ram, origin + site + 1),
                              word(ram, origin + site + 3)), linked)
        for site, original, relocated in ((0xe783, 0x0008, 0x54ce),
                                          (0xe809, 0x0008, 0x54ce),
                                          (0xe8e8, 0x0004, 0x54ca),
                                          (0xe8ec, 0x0006, 0x54cc),
                                          (0xe8c2 + 2, 0x0002, 0x4d84),
                                          (0xe997 + 2, 0x0004, 0x4d86),
                                          (0xeb87 + 2, 0x0006, 0x4d88),
                                          (0xeb93 + 2, 0x0008, 0x4d8a)):
            self.assertEqual(word(self.rtl, site), original)
            self.assertEqual(word(ram, origin + site), relocated)
        ds_base = 0x2a7a * 16
        slot = ds_base + 0x54ce
        self.assertEqual(ram[slot:slot + 4], b'\x03\x00\x02\x00')
        self.assertEqual(ram[slot + 30:slot + 34], b'\x01\x00\x03\x05')
        self.assertEqual((word(ram, ds_base + 0x54ca),
                          word(ram, ds_base + 0x54cc)), (0x54ec, 0x2a7a))


if __name__ == '__main__':
    unittest.main()

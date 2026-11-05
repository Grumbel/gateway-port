#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Independent original-byte evidence for five manually decompiled RTL routines.

Optionally checks the linked relocation targets against a private, already
captured DOS guest-memory image; no original file bytes enter source control.
"""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
RTL_HASH = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'
EXE_HASH = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'


def word(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


class LauncherRtlBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos'
        files = [('GATE.RTL', RTL_HASH), ('GATE.EXE', EXE_HASH)]
        for name, expected in files:
            path = original / name
            if not path.exists():
                raise unittest.SkipTest('original DOS data unavailable')
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise unittest.SkipTest(f'{name} does not match pinned executable')
            setattr(cls, 'rtl' if name.endswith('.RTL') else 'exe', data)

    def test_linked_grammar_and_original_function_boundaries(self):
        rtl, exe = self.rtl, self.exe
        # One common segment origin ties independently encoded far-call
        # addresses to five exact original RTL file offsets.
        linked_base = 0xec0a - 0x0524
        for original, linked in ((0xebf7, 0x0511), (0xec0a, 0x0524),
                                 (0xeca4, 0x05be), (0xed39, 0x0653),
                                 (0xed7e, 0x0698)):
            self.assertEqual(original - linked, linked_base)

        for site, offset in ((0x0eb0, 0x0511), (0x0ed3, 0x0653),
                             (0x0ee0, 0x0698), (0x10d0, 0x05be),
                             (0x112f, 0x0524), (0x1256, 0x05be),
                             (0x1280, 0x0653), (0x128d, 0x0698)):
            self.assertEqual(exe[site], 0x9a)
            self.assertEqual((word(exe, site + 1), word(exe, site + 3)),
                             (offset, 0x0c4d))

        self.assertEqual(rtl[0xebf7:0xebfb], b'\xc4\x1e\x04\x00')
        for start, end in ((0xec0a, 0xeca3), (0xeca4, 0xece9),
                           (0xed39, 0xed7d), (0xed7e, 0xedfe)):
            self.assertEqual(rtl[start:start + 3], b'\x55\x8b\xec')
            self.assertEqual(rtl[end], 0xcb)  # far return
        self.assertEqual(rtl[0xec09], 0xcb)
        # Both loops call their own same-segment function via PUSH CS/CALL.
        for at, target in ((0xecd1, 0xec0a), (0xede4, 0xedff)):
            self.assertEqual(rtl[at - 1:at + 1], b'\x0e\xe8')
            displacement = struct.unpack_from('<h', rtl, at + 1)[0]
            self.assertEqual(at + 3 + displacement, target)

    def test_exact_signed_font_arithmetic_and_pointer_scans(self):
        rtl = self.rtl
        self.assertEqual(rtl[0xebfb:0xec0a],
                         bytes.fromhex('268a4709988bc8268a470a9803c1cb'))
        self.assertEqual(rtl[0xec11:0xec1e],
                         bytes.fromhex('8b46063d0900743f3d0a00744a'))
        self.assertEqual(rtl[0xec22:0xec27], b'\x26\x80\x7f\x0c\x00')
        self.assertEqual(rtl[0xec27], 0x7d)  # signed JGE
        self.assertEqual(rtl[0xec73:0xec78], b'\x26\x80\x7f\x0b\x00')
        self.assertEqual(rtl[0xec78], 0x7d)
        self.assertEqual(rtl[0xec45:0xec56],
                         bytes.fromhex('b104d3fa83e20f03c28b4efc83e10f03c1'))
        # Normalize far pointer, increment BP+6 offset only, CBW per byte.
        self.assertEqual(rtl[0xecb0:0xecb5], b'\x9a\x00\x00\x00\x00')
        self.assertEqual(rtl[0xecc5:0xeccf],
                         bytes.fromhex('c45e06ff4606268a0798'))
        self.assertEqual(rtl[0xedd0:0xedd4], b'\x26\xa1\x00\x00')
        self.assertEqual(rtl[0xeded:0xedf4],
                         bytes.fromhex('ff4606268a0798'))

    def test_optional_live_linked_mapping_and_actual_font_header(self):
        path = ROOT / 'build/intro-analysis/at-22000.bin'
        if not path.is_file():
            self.skipTest('private 22000ms DOS guest-memory capture absent')
        ram = path.read_bytes()
        entry = (0x1000 + 0x0c4d) * 16 + 0x0524
        self.assertEqual(ram[entry:entry + 18], self.rtl[0xec0a:0xec0a + 18])
        origin = entry - 0xec0a
        # Independently relocated DS operands, not constants in GATE.RTL:
        # the loaded linker resolves the active font and cursor globals.
        self.assertEqual(word(ram, origin + 0xebf7 + 2), 0x54ca)
        self.assertEqual(word(ram, origin + 0xec1e + 2), 0x54ca)
        self.assertEqual(word(ram, origin + 0xed48 + 2), 0x54ca)
        self.assertEqual(word(ram, origin + 0xed3f + 1), 0x550a)
        self.assertEqual(word(ram, origin + 0xed45 + 1), 0x550c)

        ds = 0x2a7a  # only this particular captured program's DS
        ptr = ds * 16 + 0x54ca
        offset, segment = struct.unpack_from('<HH', ram, ptr)
        font_at = segment * 16 + offset
        data = (ROOT.parent / 'gateway-dos/GATE_305.FNT').read_bytes()
        self.assertEqual(ram[font_at + 9:font_at + 13], data[5:9])
        self.assertEqual((ram[font_at + 3], ram[font_at + 11], ram[font_at + 12]),
                         (5, 0xff, 0xff))
        widths_offset, widths_segment = struct.unpack_from('<HH', ram, font_at + 14)
        spacing_offset, spacing_segment = struct.unpack_from('<HH', ram, font_at + 18)
        widths_at = widths_segment * 16 + widths_offset
        spacing_at = spacing_segment * 16 + spacing_offset
        self.assertEqual(ram[widths_at:widths_at + 128], data[10:138])
        self.assertEqual(ram[spacing_at:spacing_at + 128], data[138:266])


if __name__ == '__main__':
    unittest.main()

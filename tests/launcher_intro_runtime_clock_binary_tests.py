#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Original bytes and linked globals for the runtime intro clock cluster."""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
RTL_SHA256 = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'


class OriginalRuntimeClock(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not path.is_file():
            raise unittest.SkipTest('original GATE.RTL absent')
        cls.rtl = path.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != RTL_SHA256:
            raise unittest.SkipTest('different RTL revision')

    def test_bounded_source_routines(self):
        r = self.rtl
        self.assertEqual(r[0x8cc8:0x8ccb], bytes.fromhex('55 8b ec'))
        self.assertEqual(r[0x8d9a], 0xcb)
        self.assertEqual(r[0x8d42:0x8d47], bytes.fromhex('f6 87 01 00 04'))
        self.assertEqual(r[0x8d52:0x8d58], bytes.fromhex('46 83 fe 03 7c db'))
        self.assertEqual(r[0x8fa2:0x8fa5], bytes.fromhex('55 8b ec'))
        self.assertEqual(r[0x8ff8], 0xcb)
        self.assertEqual(r[0x933a:0x933d], bytes.fromhex('55 8b ec'))
        self.assertEqual(r[0x936b:0x936e], bytes.fromhex('ca 08 00'))
        self.assertEqual(r[0x934d:0x934f], bytes.fromhex('f7 e3'))
        self.assertEqual(r[0x9364:0x9366], bytes.fromhex('f7 e3'))
        self.assertEqual(r[0x8fc0:0x8fc3], bytes.fromhex('b8 e8 03'))
        self.assertEqual(r[0x8fe8:0x8ff0],
                         bytes.fromhex('99 01 46 f2 11 56 f4 8b'))

    def test_live_far_call_and_baseline_offsets(self):
        path = ROOT / 'build/intro-analysis/at-22000.bin'
        if not path.is_file():
            self.skipTest('private linked DOS memory capture absent')
        mem = path.read_bytes()
        self.assertEqual(len(mem), 1024 * 1024)
        # RTL file 8fa2 is loaded at original relative 02a5:38c4.
        start = 0x12a50 + 0x38c4
        self.assertEqual(mem[start:start + 3], bytes.fromhex('55 8b ec'))
        self.assertEqual(mem[start + (0x8fb8 - 0x8fa2)], 0x9a)
        self.assertEqual(mem[start + (0x8fd6 - 0x8fa2)], 0x9a)
        self.assertEqual(struct.unpack_from('<HH', mem, start + 0x17),
                         (0x34d8, 0x12a5))
        self.assertEqual(struct.unpack_from('<HH', mem, start + 0x35),
                         (0x3c5c, 0x12a5))
        self.assertEqual(struct.unpack_from('<H', mem, start + 0x8)[0], 0x4e24)
        self.assertEqual(struct.unpack_from('<H', mem, start + 0x2c)[0], 0x3584)
        self.assertEqual(struct.unpack_from('<H', mem, start + 0x30)[0], 0x3586)

    def test_linked_timezone_initializer_and_runtime_imports(self):
        path = ROOT / 'build/intro-analysis/at-22000.bin'
        if not path.is_file():
            self.skipTest('private linked DOS memory capture absent')
        mem = path.read_bytes()
        start = 0x12a50 + 0x35ea  # RTL:8cc8 relative to 8cb8 -> 35da
        self.assertEqual(mem[start:start + 3], bytes.fromhex('55 8b ec'))
        self.assertEqual(mem[start + (0x8d9a - 0x8cc8)], 0xcb)
        # getenv, FAR strncpy, FAR atol, multiply DX:AX, FAR strncpy.
        for file_call, target in [(0x8cd4, 0x2cd0), (0x8d03, 0x2bd2),
                                  (0x8d1b, 0x2c38), (0x8d25, 0x3c5c),
                                  (0x8d75, 0x2bd2)]:
            with self.subTest(file_offset=hex(file_call)):
                callsite = start + file_call - 0x8cc8
                self.assertEqual(mem[callsite], 0x9a)
                self.assertEqual(struct.unpack_from('<HH', mem, callsite + 1),
                                 (target, 0x12a5))
        self.assertEqual(mem[start + 0x7a:start + 0x7f],
                         bytes.fromhex('f6 87 f7 33 04'))  # digit flag
        self.assertEqual(mem[start + 0xcb:start + 0xce],
                         bytes.fromhex('a3 3c 35'))  # DST present
        # This relocated C-runtime ctype table really distinguishes digits
        # from the plus sign and zone letters via bit 2 (0x04).
        ctype_at = 0x2a7a0 + 0x33f7
        for character in '0123456789':
            self.assertTrue(mem[ctype_at + ord(character)] & 4)
        for character in '+P':
            self.assertFalse(mem[ctype_at + ord(character)] & 4)


if __name__ == '__main__':
    unittest.main()

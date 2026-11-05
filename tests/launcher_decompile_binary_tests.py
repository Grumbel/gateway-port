#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Check the recovered C function boundaries and calls against original x86.

This reads the user's GATE.EXE; no original executable bytes or story text
are embedded in the source or tests. Host C mock tests are separate.
"""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'


def u16(exe, offset):
    return struct.unpack_from('<H', exe, offset)[0]


def far_call(exe, site, segment, offset):
    assert exe[site] == 0x9a, f'not a far call at 0x{site:x}'
    assert (u16(exe, site + 3), u16(exe, site + 1)) == (segment, offset)


def near_call(exe, site, target):
    assert exe[site] == 0xe8, f'not a near call at 0x{site:x}'
    displacement = struct.unpack_from('<h', exe, site + 1)[0]
    assert site + 3 + displacement == target


class LauncherDecompileBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT.parent / 'gateway-dos' / 'GATE.EXE'
        if not path.exists():
            raise unittest.SkipTest('original Gateway DOS data not available')
        cls.exe = path.read_bytes()
        if hashlib.sha256(cls.exe).hexdigest() != EXPECTED_SHA256:
            raise unittest.SkipTest('different launcher executable: identities unverified')

    def test_function_boundaries_and_far_returns(self):
        exe = self.exe
        for start, end in ((0x0e37, 0x0f17), (0x0f18, 0x0f60),
                           (0x1c11, 0x1c6b), (0x1c6c, 0x1caa)):
            self.assertEqual(exe[start], 0x55)  # push bp
            self.assertEqual(exe[end], 0xcb)    # retf

    def test_input_handler_call_graph(self):
        exe = self.exe
        for site, seg, off in (
            (0x0e42, 0x0df2, 0x000a), (0x0e4e, 0x0df2, 0x0030),
            (0x0e5b, 0x123f, 0x0004), (0x0e83, 0x0c4d, 0x018d),
            (0x0e98, 0x0c4d, 0x061b), (0x0ea5, 0x0c4d, 0x05be),
            (0x0eb0, 0x0c4d, 0x0511), (0x0ed3, 0x0c4d, 0x0653),
            (0x0ee0, 0x0c4d, 0x0698), (0x0eff, 0x0df2, 0x00f8),
            (0x0f06, 0x0e48, 0x07fe),
        ):
            far_call(exe, site, seg, off)
        near_call(exe, 0x0ef9, 0x0f18)
        self.assertEqual(u16(exe, 0x0ef5), 3500)

    def test_interruptible_clock_uses_signed_high_word(self):
        exe = self.exe
        far_call(exe, 0x0f23, 0x02a5, 0x38c4)
        far_call(exe, 0x0f3a, 0x02a5, 0x38c4)
        far_call(exe, 0x0f4b, 0x0df2, 0x000a)
        far_call(exe, 0x0f54, 0x0e48, 0x07fe)
        self.assertEqual(exe[0x0f42], 0x7f)  # JG: signed high word
        self.assertEqual(exe[0x0f44], 0x7c)  # JL: signed high word
        self.assertEqual(exe[0x0f49], 0x73)  # JAE: unsigned low word

    def test_palette_loops(self):
        exe = self.exe
        far_call(exe, 0x1c1c, 0x0df2, 0x000a)
        far_call(exe, 0x1c4c, 0x0cdc, 0x04d2)
        far_call(exe, 0x1c96, 0x0cdc, 0x04d2)
        near_call(exe, 0x1c62, 0x0f18)
        self.assertEqual(u16(exe, 0x1c26), 3)      # signed IMUL operand
        self.assertEqual(u16(exe, 0x1c2c), 45)     # DAC component offset
        self.assertEqual(exe[0x1c5a], 63)         # inclusive loop
        self.assertEqual(u16(exe, 0x1c5e), 500)   # final delay
        self.assertEqual(exe[0x1ca4], 64)         # exclusive loop


if __name__ == '__main__':
    unittest.main()

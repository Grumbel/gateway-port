#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Independent original 8086 evidence for the source-shaped flag services.

No original executable bytes, state dumps or strings are stored in the tree.
"""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXE_SHA256 = '1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb'


def word(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def near_call(data, at, target):
    assert data[at] == 0xe8
    disp = struct.unpack_from('<h', data, at + 1)[0]
    assert at + 3 + disp == target


class GatemainFlagsDecompileBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos/GATEMAIN.EXE'
        if not original.is_file():
            raise unittest.SkipTest('original DOS executable unavailable')
        cls.exe = original.read_bytes()
        if hashlib.sha256(cls.exe).hexdigest() != EXE_SHA256:
            raise unittest.SkipTest('GATEMAIN.EXE does not match pinned original')

    def test_exact_original_function_boundaries(self):
        exe = self.exe
        for start, end in ((0x27db, 0x284a), (0x284b, 0x28e3),
                           (0x28e4, 0x291d), (0x291e, 0x2950),
                           (0x2951, 0x2985)):
            self.assertEqual(exe[start], 0x55)  # PUSH BP
            self.assertEqual(exe[start + 1], 0x8b)
            self.assertEqual(exe[end], 0xcb)    # RETF
        near_call(exe, 0x2870, 0x27db)
        for at in (0x28f6, 0x2930, 0x2963):
            near_call(exe, at, 0x284b)

    def test_object_record_type_jump_table_and_16bit_pointer_add(self):
        exe = self.exe
        self.assertEqual(exe[0x27e6], 0x7e)  # signed JLE obj <= 0
        self.assertEqual(exe[0x27f3], 0x77)  # unsigned JA obj > count
        self.assertEqual(word(exe, 0x27ee), 0x38dc)  # count offset
        self.assertEqual(word(exe, 0x27f6), 6)  # signed IMUL 6
        self.assertEqual(word(exe, 0x2804), 0xbc)
        self.assertEqual(word(exe, 0x2809), 0xbe)
        self.assertEqual(exe[0x281e], 0x77)  # type-1 unsigned > 7
        self.assertEqual(exe[0x2823], 0x2e)  # CS override on jump table
        # The switch is compiled as eight identical near targets (IP:02ea),
        # which map to original file 0x2828 after subtracting 0x253e.
        for at in range(0x2833, 0x2843, 2):
            self.assertEqual(word(exe, at), 0x02ea)
        self.assertEqual(exe[0x282e], 0x05)  # ADD AX,imm16
        self.assertEqual(word(exe, 0x282f), 4)

    def test_property_applicability_bit_and_return_value(self):
        exe = self.exe
        self.assertEqual(exe[0x285d], 0x7e)  # signed JLE
        self.assertEqual(exe[0x286a], 0x77)  # unsigned JA
        self.assertEqual(exe[0x2880], 0x74)  # property==0 -> invalid
        self.assertEqual(exe[0x288b], 0xd3)  # SHL SI,CL (2)
        self.assertEqual(word(exe, 0x2894), 0x16)  # property type mask
        self.assertEqual(exe[0x28aa], 0xfe)  # DEC CL (type-1)
        self.assertEqual(exe[0x28af], 0xd3)  # SHL AX,CL
        self.assertEqual(exe[0x28b4], 0x74)  # not applicable
        self.assertEqual(word(exe, 0x28bd), 0x14)  # bit index byte
        self.assertEqual(word(exe, 0x28c8), 7)  # bit & 7
        self.assertEqual(exe[0x28d2], 0xd3)  # SHR AX,CL (3)
        self.assertEqual(exe[0x28d4], 0x03)  # ADD AX, [BP-4]

    def test_flag_test_set_clear_use_exact_byte_operations(self):
        exe = self.exe
        self.assertEqual(exe[0x2911], 0xd3)  # SHR AX,CL
        self.assertEqual(word(exe, 0x2914), 1)  # AND AX,1
        self.assertEqual(exe[0x2948], 0xd2)  # SHL AL,CL
        self.assertEqual(exe[0x294a], 0x26)  # OR byte ES:[BX],AL
        self.assertEqual(exe[0x297b], 0xd2)  # SHL AL,CL
        self.assertEqual(exe[0x297d], 0xf6)  # NOT AL
        self.assertEqual(exe[0x297f], 0x26)  # AND byte ES:[BX],AL
        for branch in (0x2904, 0x293e, 0x2971):
            self.assertEqual(exe[branch], 0x74)  # far pointer null check


if __name__ == '__main__':
    unittest.main()

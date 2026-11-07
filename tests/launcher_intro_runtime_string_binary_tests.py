#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Check the distinct FAR strlen against pinned original GATE.RTL bytes."""

import hashlib
from pathlib import Path
import unittest


RTL_SHA256 = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'


class OriginalRtlStringLength(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[2] / 'gateway-dos/GATE.RTL'
        if not path.is_file():
            raise unittest.SkipTest('original GATE.RTL absent')
        cls.rtl = path.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != RTL_SHA256:
            raise unittest.SkipTest('different GATE.RTL revision')

    def test_separate_bounded_far_strlen(self):
        code = self.rtl[0x8298:0x82af]
        self.assertEqual(len(code), 23)
        self.assertEqual(code[:3], bytes.fromhex('55 8b ec'))  # frame
        self.assertEqual(code[3:5], bytes.fromhex('8b d7'))  # preserve DI
        self.assertEqual(code[5:8], bytes.fromhex('c4 7e 06'))  # LES DI, argument
        self.assertEqual(code[8:13], bytes.fromhex('33 c0 b9 ff ff'))
        self.assertEqual(code[13:15], bytes.fromhex('f2 ae'))  # REPNE SCASB
        self.assertEqual(code[15:18], bytes.fromhex('f7 d1 49'))  # ~CX-1
        self.assertEqual(code[18:], bytes.fromhex('91 8b fa 5d cb'))


if __name__ == '__main__':
    unittest.main()

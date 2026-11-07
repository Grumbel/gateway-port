#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Pinned original call graph for GATE.RTL:8bb6 time/calendar sampler."""

import hashlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
RTL_HASH = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'

# Original RTL file offsets and actual relocated 12a5:xxxx FAR targets.
SAMPLER_CALLS = {
    0x8bbd: 0x35da,  # runtime time/date setup
    0x8bd0: 0x3bc0,  # signed 32-bit quotient, divisor 60
    0x8bea: 0x346a,  # time/date format selector '*'
    0x8c41: 0x346a,  # time/date format selector ','
    0x8c78: 0x37a0,  # calendar fields -> 32-bit time count
    0x8c99: 0x36be,  # daylight-saving-time predicate
}


class ClockSamplerOriginalEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT.parent / 'gateway-dos/GATE.RTL'
        if not path.is_file():
            raise unittest.SkipTest('original GATE.RTL absent')
        cls.rtl = path.read_bytes()
        if hashlib.sha256(cls.rtl).hexdigest() != RTL_HASH:
            raise unittest.SkipTest('different original RTL revision')

    def test_exact_sampler_bounds_and_call_sites(self):
        data = self.rtl
        self.assertEqual(data[0x8bb6:0x8bb9], bytes.fromhex('55 8b ec'))
        self.assertEqual(data[0x8cb7], 0xcb)
        self.assertEqual(data[0x8cb8:0x8cbb], bytes.fromhex('83 3e 00'))
        for file_offset in SAMPLER_CALLS:
            self.assertEqual(data[file_offset:file_offset + 5],
                             bytes.fromhex('9a 00 00 00 00'))
        self.assertEqual(data[0x8bc2:0x8bc5], bytes.fromhex('b8 3c 00'))
        self.assertEqual(data[0x8bd5:0x8bd8], bytes.fromhex('c4 5e 06'))
        self.assertEqual(data[0x8c58:0x8c5b], bytes.fromhex('c4 5e 06'))
        self.assertEqual(data[0x929e:0x92a1], bytes.fromhex('55 8b ec'))
        self.assertEqual(data[0x9337:0x933a], bytes.fromhex('ca 08 00'))
        self.assertEqual(data[0x92e2:0x92e4], bytes.fromhex('f7 f1'))
        self.assertEqual(data[0x9306:0x9308], bytes.fromhex('f7 f1'))

    def test_all_six_linked_call_targets(self):
        capture = ROOT / 'build/intro-analysis/at-22000.bin'
        if not capture.is_file():
            self.skipTest('linked RAM snapshot absent')
        memory = capture.read_bytes()
        self.assertEqual(len(memory), 1024 * 1024)
        # File 8bb6 belongs to the verified RTL bank file_base=56de,
        # relative segment 02a5 at default DOS image load 1000h.
        for file_offset, target_offset in SAMPLER_CALLS.items():
            physical = 0x12a50 + file_offset - 0x56de
            self.assertEqual(memory[physical], 0x9a)
            self.assertEqual(struct.unpack_from('<HH', memory, physical + 1),
                             (target_offset, 0x12a5))


if __name__ == '__main__':
    unittest.main()

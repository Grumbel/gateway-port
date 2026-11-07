#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Inspect the original launcher -> GATEMAIN DOS PSP command tail.

The optional full RAM input is a private original-game capture under build/.
No game data or executable bytes are included in this source test.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PSP_PHYSICAL = 0x0FF00
DEFAULT_HANDOFF = '0 258 9 2 816 1 7 544'


def command_tail(ram, psp):
    if psp < 0 or psp + 0x100 > len(ram):
        raise ValueError('PSP does not fit in guest memory')
    count = ram[psp + 0x80]
    if count > 126:
        raise ValueError('invalid DOS command tail length')
    tail = ram[psp + 0x81:psp + 0x81 + count]
    if ram[psp + 0x81 + count] != 0x0d:
        raise ValueError('DOS command tail has no CR terminator')
    return tail.decode('ascii').lstrip(' ')


class Handoff(unittest.TestCase):
    def test_synthetic_psp(self):
        ram = bytearray(0x200)
        text = b' 0 258 9 2 816 1 7 544'
        ram[0x80] = len(text)
        ram[0x81:0x81 + len(text)] = text
        ram[0x81 + len(text)] = 0x0d
        self.assertEqual(command_tail(ram, 0), DEFAULT_HANDOFF)

    def test_reject_invalid_psp(self):
        ram = bytearray(0x100)
        with self.assertRaisesRegex(ValueError, 'fit'):
            command_tail(ram, 1)
        ram[0x80] = 127
        with self.assertRaisesRegex(ValueError, 'length'):
            command_tail(ram, 0)
        ram[0x80] = 1
        ram[0x81] = ord('a')
        with self.assertRaisesRegex(ValueError, 'terminator'):
            command_tail(ram, 0)

    def test_original_overlay_psp(self):
        original_ram = ROOT / 'build/intro-analysis/original-child-340s.bin'
        if not original_ram.is_file():
            self.skipTest('private original DOS post-handoff capture absent')
        ram = original_ram.read_bytes()
        self.assertEqual(len(ram), 1024 * 1024)
        self.assertEqual(command_tail(ram, PSP_PHYSICAL), DEFAULT_HANDOFF)


if __name__ == '__main__':
    unittest.main()

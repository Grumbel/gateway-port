#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare private original guest snapshots around the startup music FAR call.

Regenerate with gateway-probe --headless --read-only --deterministic-clock
--milliseconds 10000 --break-at 107b2 / 107b7 and --dump-prefix into
build/intro-analysis. Never commit snapshots or extracted game data.
"""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SNAPSHOTS = ROOT / 'build/intro-analysis'


class IntroMusicBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        before = SNAPSHOTS / 'intro-setup-end.bin'
        after = SNAPSHOTS / 'after-startup.bin'
        if not before.is_file() or not after.is_file():
            raise unittest.SkipTest('private before/after audio snapshots absent')
        cls.before = before.read_bytes()
        cls.after = after.read_bytes()
        if len(cls.before) != 0x100000 or len(cls.after) != 0x100000:
            raise AssertionError('expected complete 1 MiB guest RAM images')

    def test_music_installs_only_irq8_vector(self):
        changed = [i for i in range(0, 0x400, 4)
                   if self.before[i:i + 4] != self.after[i:i + 4]]
        self.assertEqual(changed, [8 * 4])
        self.assertEqual(self.before[32:36].hex(), '000100f0')
        self.assertEqual(self.after[32:36].hex(), 'ca002c17')

    def test_music_does_not_modify_vga_memory(self):
        self.assertEqual(self.before[0xa0000:0xb0000],
                         self.after[0xa0000:0xb0000])

    def test_music_changes_device_initialized_flag(self):
        ds = 0x2a7a << 4
        self.assertEqual(self.before[ds + 0x0662], 0x0a)
        self.assertEqual(self.after[ds + 0x0662], 0x5a)
        self.assertEqual(self.before[ds + 0x0660:ds + 0x0662],
                         self.after[ds + 0x0660:ds + 0x0662])
        self.assertEqual(self.after[ds + 0x0660:ds + 0x0662],
                         bytes.fromhex('0104'))


if __name__ == '__main__':
    unittest.main()

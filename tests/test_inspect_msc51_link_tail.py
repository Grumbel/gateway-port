#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic source matching for the unresolved Microsoft C launcher tail."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from inspect_msc51_link_tail import TAIL_ENTRIES, inspect


class LinkTailTests(unittest.TestCase):
    def test_resident_targets_have_independent_live_source_matches(self):
        exe = bytearray(b'MZ' + bytes(512))
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        originals = {'music_init': 0xc3bd, 'control_key': 0x1000,
                     'intro_diagnostic': 0x1600}
        for name, segment, offset, _, _ in TAIL_ENTRIES:
            source = originals[name]
            signature = bytes((i * 19 + source // 16 + 1) & 255 for i in range(32))
            rtl[source:source + 32] = signature
            physical = (0x1000 + segment) * 16 + offset
            ram[physical:physical + 32] = signature
        rows = inspect(exe, rtl, ram)
        self.assertEqual(len(rows), 3)
        for row in rows:
            match = row['live_source_match']
            self.assertIsNotNone(match, row['name'])
            self.assertEqual(match['file'], 'rtl')
            self.assertEqual(match['offset'], originals[row['name']])
            self.assertEqual(match['matched_bytes'], 16)
        self.assertTrue(rows[0]['independent_original_entry']['matches_live_16_bytes'])

    def test_overwritten_entry_does_not_claim_source_identity(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        rtl[0xc3bd:0xc3cd] = bytes(range(1, 17))
        rows = inspect(exe, rtl, bytes(1 << 20))
        self.assertTrue(all(row['live_source_match'] is None for row in rows))
        self.assertFalse(rows[0]['independent_original_entry']['matches_live_16_bytes'])
        self.assertFalse(rows[0]['relocated_music_guard'])

    def test_music_entry_accepts_only_the_original_relocated_ds_guard(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        physical = (0x1000 + 0x0a46) * 16 + 0x01ec
        rtl[0xc3c5:0xc3ca] = bytes.fromhex('f6064a0006')
        rtl[0xc3cf:0xc3d4] = bytes.fromhex('833e480000')
        ram[physical + 8:physical + 13] = bytes.fromhex('f606620606')
        ram[physical + 18:physical + 23] = bytes.fromhex('833e600600')
        self.assertTrue(inspect(exe, rtl, ram)[0]['relocated_music_guard'])
        ram[physical + 18] ^= 1
        self.assertFalse(inspect(exe, rtl, ram)[0]['relocated_music_guard'])

    def test_invalid_snapshot_is_reported_without_a_traceback(self):
        with self.assertRaisesRegex(ValueError, '1-MiB'):
            inspect(b'MZ', b'', b'bad')
        with tempfile.TemporaryDirectory() as folder:
            missing = Path(folder) / 'absent.bin'
            command = [sys.executable, str(ROOT / 'tools/inspect_msc51_link_tail.py'),
                       '--exe', 'unused.exe', '--rtl', 'unused.rtl', '--ram', str(missing)]
            run = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertIn('--capture-labels', run.stderr)
            self.assertNotIn('Traceback', run.stderr)


if __name__ == '__main__':
    unittest.main()

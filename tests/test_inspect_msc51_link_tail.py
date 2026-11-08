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

from inspect_msc51_link_tail import (TAIL_ENTRIES, RELOCATED_RTL_ENTRIES, inspect,
                                     match_relocated_far_entry)


class LinkTailTests(unittest.TestCase):
    def test_resident_targets_have_raw_window_candidates(self):
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
            candidate = row['source_candidate']
            self.assertIsNotNone(candidate, row['name'])
            self.assertEqual(candidate['file'], 'rtl')
            self.assertEqual(candidate['offset'], originals[row['name']])
            if row['name'] == 'music_init':
                self.assertIsNone(match)  # No independent relocated guard.
            else:
                # A generic byte-window candidate cannot establish the
                # known original FAR entry's relocated CALL operands.
                self.assertEqual(row['entry_status'],
                                 'source_entry_candidate_not_semantically_verified')
                self.assertEqual(match['offset'], originals[row['name']])
            self.assertEqual(candidate['entry_prefix_bytes'], 16)
        self.assertTrue(rows[0]['independent_original_entry']['matches_live_16_bytes'])
        self.assertEqual(rows[0]['entry_status'],
                         'original_music_entry_replaced_or_not_loaded')

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
        rtl[0xc3bd:0xc3c5] = bytes(range(1, 9))
        ram[physical:physical + 8] = bytes(range(1, 9))
        ram[physical + 8:physical + 13] = bytes.fromhex('f606620606')
        ram[physical + 18:physical + 23] = bytes.fromhex('833e600600')
        audio = inspect(exe, rtl, ram)[0]
        self.assertTrue(audio['relocated_music_guard'])
        self.assertEqual(audio['live_source_match']['offset'], 0xc3bd)
        ram[physical + 18] ^= 1
        self.assertFalse(inspect(exe, rtl, ram)[0]['relocated_music_guard'])

    def test_internal_source_window_does_not_validate_a_function_entry(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        name, segment, offset, _, _ = TAIL_ENTRIES[1]
        self.assertEqual(name, 'control_key')
        physical = (0x1000 + segment) * 16 + offset
        source = 0xeeba
        # A relocated call operand at the alleged entry precedes an intact
        # executable code window. The matcher finds it, but it is NOT an
        # original function entry (as in the user's 39-second snapshot).
        rtl[source:source + 8] = bytes(range(1, 9))
        ram[physical:physical + 8] = bytes(range(21, 29))
        signature = bytes((i * 13 + 53) & 255 for i in range(16))
        rtl[source + 8:source + 24] = signature
        ram[physical + 8:physical + 24] = signature
        row = inspect(exe, rtl, ram)[1]
        self.assertIsNotNone(row['source_candidate'])
        self.assertEqual(row['source_candidate']['entry_prefix_bytes'], 0)
        self.assertIsNone(row['live_source_match'])
        self.assertEqual(row['entry_status'], 'entry_unmapped_or_relocated')

    def test_known_relocated_far_entries_are_matched_at_original_boundary(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        for name in ('control_key', 'intro_diagnostic'):
            entry_offset, length, calls = RELOCATED_RTL_ENTRIES[name]
            original = bytearray((i * 13 + 29) & 255 for i in range(length))
            for start in calls:
                original[start:start + 5] = bytes([0x9a, 0, 0, 0, 0])
            rtl[entry_offset:entry_offset + length] = original
            linked = bytearray(original)
            for index, start in enumerate(calls):
                linked[start + 1:start + 5] = bytes([index + 1, 2, 3, 4])
            _, segment, offset, _, _ = next(entry for entry in TAIL_ENTRIES
                                            if entry[0] == name)
            physical = (0x1000 + segment) * 16 + offset
            ram[physical:physical + length] = linked
            row = next(item for item in inspect(exe, rtl, ram) if item['name'] == name)
            self.assertTrue(row['relocated_original_entry']['matched'])
            self.assertEqual(row['live_source_match']['offset'], entry_offset)
            self.assertEqual(row['entry_status'], 'relocated_original_entry')
            self.assertEqual(len(row['relocated_far_calls']), len(calls))
            self.assertEqual(row['relocated_far_calls'][0]['target_offset'], 0x0201)
            self.assertEqual(row['relocated_far_calls'][0]['target_segment'], 0x0403)
            linked[0 if calls[0] != 0 else length - 1] ^= 1
            self.assertFalse(match_relocated_far_entry(original, linked, calls, length))
            linked[0 if calls[0] != 0 else length - 1] ^= 1
            linked[calls[0] + 1] ^= 1
            self.assertTrue(match_relocated_far_entry(original, linked, calls, length))
            linked[calls[0]] ^= 1
            self.assertFalse(match_relocated_far_entry(original, linked, calls, length))

    def test_music_independent_identity_is_not_overwritten_by_candidate(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        rtl[0xc3bd:0xc3cd] = bytes(range(1, 17))
        source = 0x7ce0
        signature = bytes((i * 17 + 51) & 255 for i in range(32))
        rtl[source:source + 32] = signature
        _, segment, offset, _, _ = TAIL_ENTRIES[0]
        physical = (0x1000 + segment) * 16 + offset
        ram[physical:physical + 32] = signature
        music = inspect(exe, rtl, ram)[0]
        self.assertEqual(music['source_candidate']['offset'], source)
        self.assertEqual(music['independent_original_entry']['offset'], 0xc3bd)
        self.assertFalse(music['independent_original_entry']['matches_live_16_bytes'])
        self.assertIsNone(music['live_source_match'])

    def test_invalid_snapshot_is_reported_without_a_traceback(self):
        with self.assertRaisesRegex(ValueError, '1-MiB'):
            inspect(b'MZ', b'', b'bad')
        with tempfile.TemporaryDirectory() as folder:
            missing = Path(folder) / 'absent.bin'
            command = [sys.executable, str(ROOT / 'tools/inspect_msc51_link_tail.py'),
                       '--exe', 'unused.exe', '--rtl', 'unused.rtl', '--ram', str(missing)]
            run = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertIn('nix run -L .#probe --', run.stderr)
            self.assertIn('--dump-at 22000:', run.stderr)
            self.assertIn('at-22000.bin', run.stderr)
            self.assertNotIn('Traceback', run.stderr)


if __name__ == '__main__':
    unittest.main()

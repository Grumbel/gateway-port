#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic source matching for the unresolved Microsoft C launcher tail."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from inspect_msc51_link_tail import (TAIL_ENTRIES, RELOCATED_RTL_ENTRIES,
                                     EXTENDED_RTL_ENTRIES, RELOCATED_CALLEE_ENTRIES, inspect,
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

    def test_callee_origin_requires_validated_resident_bank(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        bank = dict(segment=0x0c4d, file_base=0xe000, size=128,
                    witnesses=(0xe020,))
        bank_bytes = bytes((i * 17 + 7) & 255 for i in range(128))
        rtl[0xe000:0xe080] = bank_bytes
        bank_physical = (0x1000 + bank['segment']) * 16
        ram[bank_physical:bank_physical + 128] = bank_bytes

        entry, length, calls = RELOCATED_RTL_ENTRIES['control_key']
        original = bytearray((i * 11 + 23) & 255 for i in range(length))
        for start in calls:
            original[start:start + 5] = bytes([0x9a, 0, 0, 0, 0])
        rtl[entry:entry + length] = original
        linked = bytearray(original)
        linked[1:5] = bytes([0x20, 0, 0x4d, 0x1c])
        for start in calls[1:]:
            linked[start + 1:start + 5] = bytes([1, 2, 3, 4])
        _, segment, offset, _, _ = TAIL_ENTRIES[1]
        physical = (0x1000 + segment) * 16 + offset
        ram[physical:physical + length] = linked

        with patch('inspect_msc51_link_tail.RTL_BANKS', (bank,)):
            row = inspect(exe, rtl, ram)[1]
        call = row['relocated_far_calls'][0]
        projection = call['original_source_projection']
        self.assertEqual(projection['offset'], 0xe020)
        self.assertEqual(projection['matching_entry_prefix_bytes'], 16)

        ram[bank_physical:bank_physical + 128] = bytes(128)
        with patch('inspect_msc51_link_tail.RTL_BANKS', (bank,)):
            row = inspect(exe, rtl, ram)[1]
        self.assertNotIn('original_source_projection', row['relocated_far_calls'][0])

    def test_nested_callee_matches_fixed_bytes_and_shared_relocated_ds_word(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        entry, length, calls = RELOCATED_RTL_ENTRIES['control_key']
        original = bytearray((index * 7 + 23) & 255 for index in range(length))
        for start in calls:
            original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        rtl[entry:entry + length] = original
        linked = bytearray(original)
        for start in calls:
            linked[start + 1:start + 5] = b'\x01\x00\x03\x10'
        # The third FAR call's destination is recovered from the user's
        # 22-second snapshot. Only this callee has a bounded RTL template.
        linked[calls[2] + 1:calls[2] + 5] = b'\x02\x02\x48\x1e'
        _, seg, off, _, _ = TAIL_ENTRIES[1]
        ram[(0x1000 + seg) * 16 + off:((0x1000 + seg) * 16 + off) + length] = linked

        relative_segment, callee_offset = (0x0e48, 0x0202)
        source_offset, size, fars, words, pairs = \
            RELOCATED_CALLEE_ENTRIES[relative_segment, callee_offset]
        callee_original = bytearray((index * 11 + 31) & 255 for index in range(size))
        for start in fars:
            callee_original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        for start in words:
            callee_original[start:start + 2] = b'\x00\x00'
        for first, second in pairs:
            callee_original[first:first + 2] = b'\x00\x00'
            callee_original[second:second + 2] = (b'\x00\x00' if first == 6
                                                  else b'\x80\x00')
        rtl[source_offset:source_offset + size] = callee_original
        callee_linked = bytearray(callee_original)
        for index, start in enumerate(fars):
            callee_linked[start + 1:start + 5] = bytes((index + 7, 0, 0x11, 0x22))
        for start in words:
            callee_linked[start:start + 2] = b'\x44\x06'
        for first, second in pairs:
            callee_linked[first:first + 2] = (b'\x44\x06' if first == 6
                                               else b'\x5c\x55')
            callee_linked[second:second + 2] = (b'\x44\x06' if first == 6
                                                 else b'\xdc\x55')
        physical = (0x1000 + relative_segment) * 16 + callee_offset
        ram[physical:physical + size] = callee_linked

        # Independently validate a bank also reached by the nested calls,
        # so the same run returns their original file-offset projections.
        bank = dict(segment=0x1211, file_base=0x14000, size=128,
                    witnesses=(0x14020,))
        bank_bytes = bytes((index * 13 + 1) & 255 for index in range(128))
        rtl[bank['file_base']:bank['file_base'] + 128] = bank_bytes
        bank_physical = (0x1000 + bank['segment']) * 16
        ram[bank_physical:bank_physical + 128] = bank_bytes

        with patch('inspect_msc51_link_tail.RTL_BANKS', (bank,)):
            row = inspect(exe, rtl, ram)[1]
        call = row['relocated_far_calls'][2]
        self.assertEqual(call['relocated_source_match']['offset'], source_offset)
        self.assertEqual(call['relocated_source_match']['matched_nonrelocated_bytes'], 22)
        self.assertEqual(call['relocated_source_match']['relocated_word_pairs'][0]
                         ['runtime_values'], [0x0644, 0x0644])
        self.assertEqual(call['relocated_source_match']['relocated_word_pairs'][1]
                         ['runtime_values'], [0x555c, 0x55dc])
        self.assertEqual(len(call['callee_far_calls']), 2)
        self.assertEqual(call['callee_far_calls'][0]['original_call_offset'],
                         source_offset + fars[0])
        self.assertEqual(call['callee_far_calls'][1]['target_offset'], 8)
        self.assertEqual(call['callee_far_calls'][0]['original_source_projection']['offset'],
                         bank['file_base'] + 7)
        self.assertEqual(call['callee_far_calls'][1]['original_source_projection']['offset'],
                         bank['file_base'] + 8)

        # An unchanged source byte outside a relocation must still match.
        ram[physical + 11] ^= 1
        with patch('inspect_msc51_link_tail.RTL_BANKS', (bank,)):
            call = inspect(exe, rtl, ram)[1]['relocated_far_calls'][2]
        self.assertNotIn('relocated_source_match', call)
        self.assertEqual(len(bytes.fromhex(call['live_entry_hex'])), 48)
        ram[physical + 11] ^= 1
        # Both DS references point to one original word; different relocated
        # addresses are not consistent with this exact callee template.
        ram[physical + pairs[0][1]] ^= 1
        with patch('inspect_msc51_link_tail.RTL_BANKS', (bank,)):
            call = inspect(exe, rtl, ram)[1]['relocated_far_calls'][2]
        self.assertNotIn('relocated_source_match', call)

    def test_relative_word_relocations_preserve_original_delta(self):
        for key, (entry, length, calls, words, pairs) in RELOCATED_CALLEE_ENTRIES.items():
            if len({position for pair in pairs for position in pair}) < 2 * len(pairs):
                # Shared WORD operands require consistent transitive deltas;
                # the first Ctrl-Break cleanup has a dedicated group test.
                continue
            original = bytearray((index * 3 + 17) & 255 for index in range(length))
            for start in calls:
                original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
            for start in words:
                original[start:start + 2] = b'\x00\x00'
            for pair_index, (first, second) in enumerate(pairs):
                original[first:first + 2] = (0x40 * pair_index).to_bytes(2, 'little')
                original[second:second + 2] = (
                    0x40 * pair_index + (0x80 if pair_index else 0)).to_bytes(2, 'little')
            linked = bytearray(original)
            for start in calls:
                linked[start + 1:start + 5] = b'\x07\x00\x11\x22'
            for start in words:
                linked[start:start + 2] = b'\x33\x12'
            for pair_index, (first, second) in enumerate(pairs):
                base = 0x133e + 0x100 * pair_index
                linked[first:first + 2] = base.to_bytes(2, 'little')
                linked[second:second + 2] = (
                    base + (0x80 if pair_index else 0)).to_bytes(2, 'little')
            self.assertTrue(match_relocated_far_entry(original, linked, calls,
                                                      length, words, pairs), hex(entry))
            if pairs:
                linked[pairs[-1][1]] ^= 1
                self.assertFalse(match_relocated_far_entry(original, linked, calls,
                                                           length, words, pairs), hex(entry))

    def test_vfx_cleanup_callee_reveals_named_heap_release(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        control_entry, control_size, control_calls = RELOCATED_RTL_ENTRIES['control_key']
        control = bytearray((index * 7 + 13) & 255 for index in range(control_size))
        for start in control_calls:
            control[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        rtl[control_entry:control_entry + control_size] = control
        linked_control = bytearray(control)
        for start in control_calls:
            linked_control[start + 1:start + 5] = b'\x01\x00\xa5\x12'
        linked_control[control_calls[2] + 1:control_calls[2] + 5] = \
            b'\x02\x02\x48\x1e'
        _, seg, off, _, _ = TAIL_ENTRIES[1]
        ram[(0x1000 + seg) * 16 + off:((0x1000 + seg) * 16 + off) +
            control_size] = linked_control

        helper = RELOCATED_CALLEE_ENTRIES[0x0e48, 0x0202]
        source, size, fars, words, pairs = helper
        original = bytearray((index * 11 + 17) & 255 for index in range(size))
        for start in fars:
            original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        for first, second in pairs:
            original[first:first + 2] = b'\x00\x00'
            original[second:second + 2] = (b'\x80\x00' if first == 12
                                                else b'\x00\x00')
        rtl[source:source + size] = original
        linked = bytearray(original)
        for start in fars:
            linked[start + 1:start + 5] = b'\x6c\x01\x5d\x24'
        for first, second in pairs:
            pair_start = (b'\x5c\x55' if first == 12 else b'\xd2\x13')
            linked[first:first + 2] = pair_start
            linked[second:second + 2] = (b'\xdc\x55' if first == 12
                                          else pair_start)
        physical = (0x1000 + 0x0e48) * 16 + 0x0202
        ram[physical:physical + size] = linked

        source, size, fars, words, pairs = RELOCATED_CALLEE_ENTRIES[0x145d, 0x016c]
        original = bytearray((index * 13 + 7) & 255 for index in range(size))
        for start in fars:
            original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        for start in words:
            original[start:start + 2] = b'\x00\x00'
        rtl[source:source + size] = original
        linked = bytearray(original)
        linked[fars[0] + 1:fars[0] + 5] = b'\x48\x00\x3a\x25'
        linked[words[0]:words[0] + 2] = b'\x7a\x2a'
        physical = (0x1000 + 0x145d) * 16 + 0x016c
        ram[physical:physical + size] = linked

        heap_source, heap_size, heap_fars, heap_words, heap_pairs = \
            RELOCATED_CALLEE_ENTRIES[0x153a, 0x0048]
        self.assertEqual((heap_source, heap_size, heap_fars, heap_words, heap_pairs),
                         (0x18f23, 49, (), (11,), ()))
        heap_original = bytearray((index * 17 + 9) & 255
                                  for index in range(heap_size))
        heap_original[11:13] = b'\x00\x00'
        rtl[heap_source:heap_source + heap_size] = heap_original
        heap_linked = bytearray(heap_original)
        heap_linked[11:13] = b'\x7a\x2a'
        heap_physical = (0x1000 + 0x153a) * 16 + 0x0048
        ram[heap_physical:heap_physical + heap_size] = heap_linked
        row = inspect(exe, rtl, ram)[1]
        helper_calls = row['relocated_far_calls'][2]['callee_far_calls']
        self.assertEqual(len(helper_calls), 2)
        for call in helper_calls:
            self.assertEqual(call['relocated_source_match']['offset'], 0x180fe)
            self.assertEqual(call['relocated_source_match']['matched_nonrelocated_bytes'],
                             80)
            self.assertEqual(len(call['callee_far_calls']), 1)
            heap = call['callee_far_calls'][0]
            self.assertEqual(heap['existing_port_symbol'],
                             'gate_linked_release_vfx_pixels')
            self.assertEqual(heap['relocated_source_match']['offset'], heap_source)
            self.assertEqual(heap['relocated_source_match']['matched_nonrelocated_bytes'],
                             47)
            self.assertEqual(heap['callee_far_calls'], [])

        # Do not label a modified heap body as an original RTL source. The
        # known port name may be reported independently of byte identity.
        ram[heap_physical + 19] ^= 1
        row = inspect(exe, rtl, ram)[1]
        for helper in row['relocated_far_calls'][2]['callee_far_calls']:
            heap = helper['callee_far_calls'][0]
            self.assertEqual(heap['existing_port_symbol'],
                             'gate_linked_release_vfx_pixels')
            self.assertNotIn('relocated_source_match', heap)
            self.assertEqual(heap['live_entry_hex'],
                             ram[heap_physical:heap_physical + 48].hex())

    def test_first_control_cleanup_matches_two_distinct_ds_relocations(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        source, length, fars, words, pairs = RELOCATED_CALLEE_ENTRIES[0x121b, 0x021f]
        self.assertEqual((source, length), (0x15706, 36))
        original = bytearray((index * 23 + 7) & 255 for index in range(length))
        original[15:20] = b'\x9a\x00\x00\x00\x00'
        for index, value in ((2, 0x0016), (25, 0x0016), (31, 0x0018),
                             (9, 0x0004), (13, 0x0002)):
            original[index:index + 2] = value.to_bytes(2, 'little')
        rtl[source:source + length] = original
        relocated = bytearray(original)
        relocated[15:20] = b'\x9a\x62\x0f\xa5\x12'
        for index, value in ((2, 0x156e), (25, 0x156e), (31, 0x1570),
                             (9, 0x6152), (13, 0x6150)):
            relocated[index:index + 2] = value.to_bytes(2, 'little')
        self.assertTrue(match_relocated_far_entry(original, relocated, fars,
                                                  length, words, pairs))

        control_source, control_length, control_calls = RELOCATED_RTL_ENTRIES['control_key']
        control = bytearray((index * 11 + 13) & 255 for index in range(control_length))
        for start in control_calls:
            control[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        rtl[control_source:control_source + control_length] = control
        linked_control = bytearray(control)
        for start in control_calls:
            linked_control[start + 1:start + 5] = b'\x01\x00\xa5\x12'
        linked_control[control_calls[0] + 1:control_calls[0] + 5] = \
            b'\x1f\x02\x1b\x22'
        _, segment, offset, _, _ = TAIL_ENTRIES[1]
        control_physical = (0x1000 + segment) * 16 + offset
        ram[control_physical:control_physical + control_length] = linked_control
        callee_physical = (0x1000 + 0x121b) * 16 + 0x021f
        ram[callee_physical:callee_physical + length] = relocated

        nested_source, nested_length, nested_fars, nested_words, nested_pairs = \
            RELOCATED_CALLEE_ENTRIES[0x02a5, 0x0f62]
        self.assertEqual((nested_source, nested_length), (0x6640, 48))
        nested_original = bytearray((index * 13 + 21) & 255
                                    for index in range(nested_length))
        nested_original[nested_fars[0]:nested_fars[0] + 5] = \
            b'\x9a\x00\x00\x00\x00'
        rtl[nested_source:nested_source + nested_length] = nested_original
        nested_linked = bytearray(nested_original)
        nested_linked[nested_fars[0]:nested_fars[0] + 5] = \
            b'\x9a\xda\x17\xa5\x12'
        nested_physical = (0x1000 + 0x02a5) * 16 + 0x0f62
        ram[nested_physical:nested_physical + nested_length] = nested_linked

        flush_source, flush_length, flush_fars, flush_words, flush_pairs = \
            RELOCATED_CALLEE_ENTRIES[0x02a5, 0x17da]
        self.assertEqual((flush_source, flush_length, flush_fars, flush_words,
                          flush_pairs), (0x6eb8, 142, (87,), (32, 50), ()))
        flush_original = bytearray((index * 19 + 31) & 255
                                   for index in range(flush_length))
        for start in flush_fars:
            flush_original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        for start in flush_words:
            flush_original[start:start + 2] = b'\x00\x00'
        rtl[flush_source:flush_source + flush_length] = flush_original
        flush_linked = bytearray(flush_original)
        flush_linked[flush_words[0]:flush_words[0] + 2] = b'\x11\x31'
        flush_linked[flush_words[1]:flush_words[1] + 2] = b'\x77\x31'
        flush_linked[flush_fars[0]:flush_fars[0] + 5] = \
            b'\x9a\x86\x26\xa5\x12'
        flush_physical = (0x1000 + 0x02a5) * 16 + 0x17da
        ram[flush_physical:flush_physical + flush_length] = flush_linked

        writer_source, writer_length, writer_fars, writer_words, writer_pairs = \
            RELOCATED_CALLEE_ENTRIES[0x02a5, 0x2686]
        self.assertEqual((writer_source, writer_length, writer_fars,
                          writer_words, writer_pairs),
                         (0x7d64, 100, (89,), (11,), ((24, 42),)))
        writer_original = bytearray((index * 17 + 3) & 255
                                    for index in range(writer_length))
        writer_original[writer_fars[0]:writer_fars[0] + 5] = \
            b'\x9a\x00\x00\x00\x00'
        writer_original[writer_words[0]:writer_words[0] + 2] = b'\x00\x00'
        for index in writer_pairs[0]:
            writer_original[index:index + 2] = b'\x00\x00'
        rtl[writer_source:writer_source + writer_length] = writer_original
        writer_linked = bytearray(writer_original)
        writer_linked[writer_words[0]:writer_words[0] + 2] = b'\xee\x2b'
        for index in writer_pairs[0]:
            writer_linked[index:index + 2] = b'\xf0\x2b'
        writer_linked[writer_fars[0]:writer_fars[0] + 5] = \
            b'\x9a\x77\x55\x22\x44'
        writer_physical = (0x1000 + 0x02a5) * 16 + 0x2686
        ram[writer_physical:writer_physical + writer_length] = writer_linked

        cleanup = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0]
        self.assertEqual(cleanup['relocated_source_match']['offset'], source)
        self.assertEqual(cleanup['relocated_source_match']['matched_nonrelocated_bytes'],
                         22)  # Shared DS:0016 is counted only once.
        self.assertEqual(cleanup['callee_far_calls'][0]['target_offset'], 0x0f62)
        deeper = cleanup['callee_far_calls'][0]
        self.assertEqual(deeper['relocated_source_match']['offset'], nested_source)
        self.assertEqual(deeper['relocated_source_match']['matched_nonrelocated_bytes'],
                         44)
        self.assertEqual(deeper['callee_far_calls'][0]['original_call_offset'],
                         nested_source + nested_fars[0])
        self.assertEqual(deeper['callee_far_calls'][0]['target_segment'], 0x12a5)
        flush = deeper['callee_far_calls'][0]
        self.assertEqual(flush['relocated_source_match']['offset'], flush_source)
        self.assertEqual(flush['relocated_source_match']['matched_nonrelocated_bytes'],
                         134)
        self.assertEqual(len(flush['callee_far_calls']), 1)
        self.assertEqual(flush['callee_far_calls'][0]['original_call_offset'],
                         flush_source + flush_fars[0])
        self.assertEqual(flush['callee_far_calls'][0]['target_segment'], 0x12a5)
        writer = flush['callee_far_calls'][0]
        self.assertEqual(writer['relocated_source_match']['offset'], writer_source)
        self.assertEqual(writer['relocated_source_match']['matched_nonrelocated_bytes'],
                         90)
        self.assertEqual(writer['relocated_source_match']['relocated_word_pairs'][0]
                         ['runtime_values'], [0x2bf0, 0x2bf0])
        self.assertEqual(writer['callee_far_calls'][0]['original_call_offset'],
                         writer_source + writer_fars[0])
        self.assertEqual(writer['callee_far_calls'][0]['target_offset'], 0x5577)

        # Two references to the same descriptor flag field must remain
        # equal after LINK; masking them independently would be too weak.
        ram[writer_physical + writer_pairs[0][1]] ^= 1
        writer = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0] \
            ['callee_far_calls'][0]['callee_far_calls'][0]['callee_far_calls'][0]
        self.assertNotIn('relocated_source_match', writer)
        self.assertNotIn('callee_far_calls', writer)
        ram[writer_physical + writer_pairs[0][1]] ^= 1

        # A stable instruction changed beyond the 48-byte raw entry window
        # rejects the longer CRT writer signature and its nested FAR call.
        ram[writer_physical + 60] ^= 1
        writer = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0] \
            ['callee_far_calls'][0]['callee_far_calls'][0]['callee_far_calls'][0]
        self.assertNotIn('relocated_source_match', writer)
        self.assertNotIn('callee_far_calls', writer)
        self.assertEqual(writer['live_entry_hex'],
                         ram[writer_physical:writer_physical + 48].hex())
        ram[writer_physical + 60] ^= 1

        # A differing, non-relocated stream instruction must reject the
        # whole flush identity without inventing a downstream target.
        ram[flush_physical + 58] ^= 1
        flush = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0] \
            ['callee_far_calls'][0]['callee_far_calls'][0]
        self.assertNotIn('relocated_source_match', flush)
        self.assertNotIn('callee_far_calls', flush)
        self.assertEqual(flush['live_entry_hex'],
                         ram[flush_physical:flush_physical + 48].hex())
        ram[flush_physical + 58] ^= 1

        # An unchanged first-48-byte instruction must still agree even when
        # the known bank independently projects the original entry.
        ram[nested_physical + 24] ^= 1
        deeper = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0]['callee_far_calls'][0]
        self.assertNotIn('relocated_source_match', deeper)
        self.assertEqual(deeper['live_entry_hex'],
                         ram[nested_physical:nested_physical + 48].hex())
        ram[nested_physical + 24] ^= 1

        # The adjacent flag word must keep the original +2 delta.
        ram[callee_physical + 31] ^= 1
        cleanup = inspect(exe, rtl, ram)[1]['relocated_far_calls'][0]
        self.assertNotIn('relocated_source_match', cleanup)
        self.assertEqual(cleanup['live_entry_hex'],
                         ram[callee_physical:callee_physical + 48].hex())

    def test_full_diagnostic_five_call_signature_falls_back_to_verified_prefix(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        entry, length, calls = EXTENDED_RTL_ENTRIES['intro_diagnostic']
        original = bytearray((index * 17 + 3) & 255 for index in range(length))
        for start in calls:
            original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
        rtl[entry:entry + length] = original
        linked = bytearray(original)
        for index, start in enumerate(calls):
            linked[start + 1:start + 5] = bytes((index + 1, 0, 0x20, 0x21))
        _, seg, off, _, _ = TAIL_ENTRIES[2]
        physical = (0x1000 + seg) * 16 + off
        ram[physical:physical + length] = linked

        row = inspect(exe, rtl, ram)[2]
        self.assertTrue(row['complete_original_entry']['matched'])
        self.assertEqual(row['complete_original_entry']['compared_nonrelocated_bytes'], 83)
        self.assertEqual(len(row['relocated_far_calls']), 5)
        self.assertEqual(row['relocated_far_calls'][-1]['original_call_offset'], 0x17115)

        # Keep the original 45-byte result when anything outside that
        # prefix differs; never report three extra calls as verified then.
        ram[physical + 50] ^= 1
        row = inspect(exe, rtl, ram)[2]
        self.assertTrue(row['relocated_original_entry']['matched'])
        self.assertFalse(row['complete_original_entry']['matched'])
        self.assertEqual(len(row['relocated_far_calls']), 2)

    def test_full_diagnostic_resolves_three_new_verified_callee_entries(self):
        exe = b'MZ' + bytes(100)
        rtl = bytearray(0x1b000)
        ram = bytearray(1 << 20)
        entry, length, calls = EXTENDED_RTL_ENTRIES['intro_diagnostic']
        original = bytearray((index * 17 + 13) & 255 for index in range(length))
        linked = bytearray(original)
        targets = [(0x0e48, 0x07fe), (0x0df2, 0x000a), (0x0df2, 0x0030)]
        for index, start in enumerate(calls):
            original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
            rel_seg, off = targets[index - 2] if index >= 2 else (0x02a5, 0x003e)
            linked[start:start + 5] = (b'\x9a' + off.to_bytes(2, 'little') +
                                        (0x1000 + rel_seg).to_bytes(2, 'little'))
        rtl[entry:entry + length] = original
        _, seg, off, _, _ = TAIL_ENTRIES[2]
        physical = (0x1000 + seg) * 16 + off
        ram[physical:physical + length] = linked

        for rel_seg, callee_off in targets:
            source_offset, size, fars, words, pairs = \
                RELOCATED_CALLEE_ENTRIES[rel_seg, callee_off]
            original = bytearray((index * 7 + 5) & 255 for index in range(size))
            for start in fars:
                original[start:start + 5] = b'\x9a\x00\x00\x00\x00'
            for start in words:
                original[start:start + 2] = b'\x00\x00'
            for first, second in pairs:
                original[first:first + 2] = b'\x02\x00'
                original[second:second + 2] = b'\x00\x00'
            rtl[source_offset:source_offset + size] = original
            relocated = bytearray(original)
            for start in fars:
                relocated[start + 1:start + 5] = b'\x01\x00\xa5\x12'
            for start in words:
                relocated[start:start + 2] = b'\xd2\x13'
            for first, second in pairs:
                relocated[first:first + 2] = b'\x40\x13'
                relocated[second:second + 2] = b'\x3e\x13'
            target_physical = (0x1000 + rel_seg) * 16 + callee_off
            ram[target_physical:target_physical + size] = relocated

        row = inspect(exe, rtl, ram)[2]
        self.assertTrue(row['complete_original_entry']['matched'])
        for call, (rel_seg, callee_off) in zip(row['relocated_far_calls'][2:], targets):
            self.assertEqual(call['relocated_source_match']['offset'],
                             RELOCATED_CALLEE_ENTRIES[rel_seg, callee_off][0])
            self.assertEqual(len(call['callee_far_calls']), 1)
        bank = row['relocated_far_calls'][3]['corroborated_resident_bank']
        self.assertEqual(bank['file_base'], 0x10543)
        self.assertEqual(bank['bank_relative_segment'], 0x0df2)
        self.assertEqual(bank['linked_entry_offsets'], [0x000a, 0x0030])
        self.assertNotIn('corroborated_resident_bank', row['relocated_far_calls'][2])

    def test_relocated_operand_mask_requires_source_placeholders(self):
        original = bytearray(b'\x90\x9a\x00\x00\x00\x00\x90\x00\x00\xcb')
        live = bytearray(original)
        live[2:6] = b'\x12\x34\x56\x78'
        live[7:9] = b'\x9a\xbc'
        self.assertTrue(match_relocated_far_entry(original, live, (1,), 10, (7,)))
        original[7] = 1
        self.assertFalse(match_relocated_far_entry(original, live, (1,), 10, (7,)))
        self.assertFalse(match_relocated_far_entry(original, live, (1,), 10, (9,)))

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

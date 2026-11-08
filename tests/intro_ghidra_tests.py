#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Intro Ghidra preparation tests; no Ghidra download or game bytes committed."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_intro_ghidra import (EXE_SHA256, RTL_SHA256, EXE_ENTRIES,
                                  RTL_BANKS, RTL_CORE_BANKS, RTL_GRAPHICS_BANKS,
                                  RTL_CANDIDATES,
                                  RAW_COPY_CANDIDATE, find_source_entries,
                                  source_bank, source_span, source_spans,
                                  validate_bank, observed_zero_word_changes,
                                  original_at_runtime_offset,
                                  resolve_linked_far_target,
                                  mz_relocations, prepare, staged_unlinked_copy)
from run_intro_ghidra import command, run


class IntroGhidraTests(unittest.TestCase):
    def test_renamed_symbols_resolve_to_original_offsets_only_when_defined(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'sample.c').write_text(
                'uint16_t gate_exe_0bb8(void) { return 0; }\n'
                'void gate_exe_handle_intro_input(void) { }\n'
                'void sample(void) { gate_exe_0f18(); }\n')
            (source / 'symbol-names.tsv').write_text(
                '# original\treadable\n'
                'gate_exe_0e37\tgate_exe_handle_intro_input\n'
                'gate_exe_0f18\tgate_exe_missing_definition\n')
            self.assertEqual(find_source_entries(source, 'gate_exe'),
                             {0x0bb8, 0x0e37})

    def test_graphics_banks_are_bounded_and_snapshot_verified(self):
        self.assertEqual([(bank['segment'], bank['file_base'], bank['size'])
                          for bank in RTL_GRAPHICS_BANKS],
                         [(0x1409, 0x179d9, 0x0430),
                          (0x144c, 0x17e6d, 0x0e00),
                          (0x152c, 0x18e00, 0x0ab0),
                          (0x15d7, 0x19997, 0x0d79)])
        rtl = bytes((i * 23 + i // 11) % 251 for i in range(0x21000))
        ram = bytearray(0x100000)
        original_segments = {(0x1000 + bank['segment']) for bank in RTL_CORE_BANKS}
        self.assertEqual(RTL_BANKS, RTL_CORE_BANKS + RTL_GRAPHICS_BANKS)
        for first, second in zip(RTL_GRAPHICS_BANKS, RTL_GRAPHICS_BANKS[1:]):
            self.assertEqual(first['segment'] * 16 + first['size'],
                             second['segment'] * 16)
        for bank in RTL_GRAPHICS_BANKS:
            self.assertNotIn(0x1000 + bank['segment'], original_segments)
            start = (0x1000 + bank['segment']) * 16
            for span in source_spans(bank):
                base = span['file_base']
                end = base + span['size']
                offset = span['offset']
                ram[start + offset:start + offset + span['size']] = rtl[base:end]
                self.assertIs(source_bank(RTL_GRAPHICS_BANKS, base), bank)
            self.assertTrue(all(source_span(bank, witness) is not None for witness
                                in bank['witnesses']))
            self.assertTrue(all(source_bank(RTL_GRAPHICS_BANKS, offset) == bank
                                for offset in bank['witnesses']))

        # Gaps at code fragment boundaries do not get invented source IDs.
        preceding = RTL_GRAPHICS_BANKS[0]
        self.assertIsNone(source_span(preceding, 0x17b14))
        self.assertEqual(source_span(preceding, 0x17b4f)['offset'], 0x0144)
        self.assertEqual(source_span(preceding, 0x17c30)['offset'], 0x021a)
        earlier = RTL_GRAPHICS_BANKS[1]
        self.assertIsNone(source_span(earlier, 0x17f82))
        self.assertIsNone(source_span(earlier, 0x18a2f))
        self.assertEqual(source_span(earlier, 0x17f98)['offset'], 0x0116)
        self.assertEqual(source_span(earlier, 0x18a95)['offset'], 0x0b04)
        b = RTL_GRAPHICS_BANKS[3]
        self.assertIsNone(source_span(b, 0x19ec1))
        self.assertIsNone(source_span(b, 0x1a6ed))
        self.assertEqual(source_span(b, 0x19ec2)['offset'], 0x04ae)
        self.assertEqual(source_span(b, 0x1a6ee)['offset'], 0x0c16)
        self.assertEqual(source_span(RTL_GRAPHICS_BANKS[2], 0x194d7)['offset'],
                         0x0668)

        # Segment immediates in one synthetic bank differ from the source;
        # preserve successful validation using separate invariant witnesses.
        b_start = (0x1000 + b['segment']) * 16
        for offset in (0x0004, 0x006b, 0x00d6, 0x0133, 0x0189, 0x01e3,
                       0x0272, 0x0440, 0x04ae, 0x04de, 0x0519, 0x0c16):
            ram[b_start + offset + 11:b_start + offset + 13] = b'\x7a\x2a'
            span = next(s for s in source_spans(b)
                        if s['offset'] <= offset < s['offset'] + s['size'])
            self.assertIn(span['file_base'] + offset - span['offset'], RTL_CANDIDATES)
        for bank in RTL_GRAPHICS_BANKS:
            start, matches, witnesses = validate_bank(rtl, bytes(ram), bank, 0x1000)
            self.assertEqual(start, (0x1000 + bank['segment']) * 16)
            self.assertGreater(matches, sum(span['size'] for span
                                            in source_spans(bank)) * 0.6)
            self.assertEqual(witnesses, list(bank['witnesses']))

        damaged = bytearray(ram)
        # A separate fragment must fail validation even if the rest of the
        # same bank still matches the source extremely well.
        later = source_spans(b)[1]
        damaged[b_start + later['offset']:b_start + later['offset'] + later['size']] = (
            bytes(later['size']))
        with self.assertRaisesRegex(ValueError, 'source span'):
            validate_bank(rtl, bytes(damaged), b, 0x1000)

    def test_zero_word_candidates_do_not_silently_classify_other_changes(self):
        bank = dict(segment=0x123, file_base=0x20, size=32)
        rtl = bytearray(range(80))
        for offset in (4, 5, 10, 11, 20, 21, 24):
            rtl[0x20 + offset] = 0
        ram = bytearray(0x100000)
        start = (0x1000 + bank['segment']) * 16
        ram[start:start + 32] = rtl[0x20:0x40]
        ram[start + 4:start + 6] = b'\x7a\x2a'
        ram[start + 10:start + 12] = b'\x20\x03'
        ram[start + 20] = 0x25  # only the low byte of a zero-source word
        ram[start + 24] = 0x30  # next byte is not source-zero; not a word
        ram[start + 26] ^= 1    # original nonzero byte changed
        report = observed_zero_word_changes(bytes(rtl), bytes(ram), bank, 0x1000)
        self.assertEqual([(item['runtime_offset'], item['observed_value'],
                           item['changed_bytes']) for item in report['candidate_words']],
                         [(4, 0x2a7a, 2), (10, 0x0320, 2), (20, 0x25, 1)])
        self.assertEqual(report['candidate_changed_bytes'], 5)
        self.assertEqual(report['unclassified_bytes'], 2)
        self.assertEqual({entry['runtime_offset'] for entry
                          in report['unclassified']}, {24, 26})

    def test_direct_moffs16_opcode_requires_source_and_live_agreement(self):
        bank = dict(segment=0x123, file_base=0x20, size=32)
        rtl = bytearray(b'\x90' * 80)
        rtl[0x23:0x26] = b'\xa1\x00\x00'
        ram = bytearray(0x100000)
        start = (0x1000 + bank['segment']) * 16
        ram[start:start + 32] = rtl[0x20:0x40]
        ram[start + 4:start + 6] = b'\xd2\x15'
        good = observed_zero_word_changes(bytes(rtl), bytes(ram), bank, 0x1000)
        self.assertEqual(good['context_counts'], {'direct-memory-displacement': 1})
        ram[start + 3] = 0x90  # opcode no longer agrees with original file
        changed = observed_zero_word_changes(bytes(rtl), bytes(ram), bank, 0x1000)
        self.assertEqual(changed['context_counts'], {})
        self.assertEqual(changed['unclassified_bytes'], 3)

    def test_mov_word_at_absolute_address_is_not_an_unresolved_word(self):
        bank = dict(segment=0x123, file_base=0x20, size=32)
        rtl = bytearray(b'\x90' * 80)
        rtl[0x23:0x29] = b'\xc7\x06\x00\x00\x00\x00'
        ram = bytearray(0x100000)
        start = (0x1000 + bank['segment']) * 16
        ram[start:start + 32] = rtl[0x20:0x40]
        ram[start + 5:start + 7] = b'\xd0\x15'
        verified = observed_zero_word_changes(bytes(rtl), bytes(ram),
                                              bank, 0x1000)
        self.assertEqual(verified['context_counts'],
                         {'direct-memory-displacement': 1})
        self.assertEqual(verified['candidate_words'][0]['runtime_offset'], 5)
        ram[start + 3] = 0x90  # No longer the original C7 /0 opcode.
        damaged = observed_zero_word_changes(bytes(rtl), bytes(ram),
                                             bank, 0x1000)
        self.assertEqual(damaged['context_counts'],
                         {'unresolved-zero-word': 1})
        self.assertEqual(damaged['unclassified_bytes'], 1)

    def test_far_target_physical_alias_respects_verified_fragment_gaps(self):
        banks = RTL_GRAPHICS_BANKS
        before = resolve_linked_far_target(banks, 0x1000, 0x244c, 0x0000)
        self.assertEqual(before, dict(linked_segment=0x144c,
                                      linked_offset=0,
                                      rtl_file_offset=0x17e6d))
        self.assertEqual(resolve_linked_far_target(
            banks, 0x1000, 0x2621, 0x030e),
            dict(linked_segment=0x15d7, linked_offset=0x07ae,
                 rtl_file_offset=0x19ec2 + (0x07ae - 0x04ae)))
        self.assertEqual(resolve_linked_far_target(
            banks, 0x1000, 0x25b7, 0x0000)['linked_segment'], 0x152c)
        self.assertIsNone(resolve_linked_far_target(
            banks, 0x1000, 0x245d, 0x0005))
        self.assertEqual(resolve_linked_far_target(
            banks, 0x1000, 0x2409, 0x00e8),
            dict(linked_segment=0x1409, linked_offset=0x00e8,
                 rtl_file_offset=0x179d9 + 0x00e8 - 0x0008))
        self.assertIsNone(original_at_runtime_offset(banks[0], 0x0007))
        self.assertIsNone(original_at_runtime_offset(banks[0], 0x0143))
        self.assertIsNone(original_at_runtime_offset(banks[1], 0x0115))
        self.assertIsNone(original_at_runtime_offset(banks[1], 0x0b03))

    def test_far_pointer_store_table_requires_matching_live_ds_memory(self):
        bank = dict(segment=0x123, file_base=0x20, size=96)
        rtl = bytearray(b'\x90' * 160)
        # MOV AX,0000h; MOV DS,AX; MOV BX,0000h; then two unlinked
        # C7 47 0C/0E FAR pointer stores with ADD BX,14h between them.
        code = (b'\xb8\0\0\x8e\xd8\xbb\0\0' +
                (b'\xc7\x47\x0c\0\0\xc7\x47\x0e\0\0'
                 b'\x83\xc3\x14') * 2)
        rtl[0x20:0x20 + len(code)] = code
        ram = bytearray(0x100000)
        start = (0x1000 + bank['segment']) * 16
        ram[start:start + bank['size']] = rtl[0x20:0x20 + bank['size']]
        for offset, value in ((1, 0x3031), (6, 0x0123),
                              (11, 0x1122), (16, 0x2345),
                              (24, 0x3344), (29, 0x2345)):
            ram[start + offset:start + offset + 2] = value.to_bytes(2, 'little')
        ds_start = 0x3031 * 16
        ram[ds_start + 0x12f:ds_start + 0x133] = b'\x22\x11\x45\x23'
        ram[ds_start + 0x143:ds_start + 0x147] = b'\x44\x33\x45\x23'
        result = observed_zero_word_changes(bytes(rtl), bytes(ram), bank, 0x1000)
        self.assertEqual(result['unclassified_bytes'], 0)
        self.assertEqual(result['context_counts'], {
            'far-table-offset-store': 2, 'far-table-segment-store': 2,
            'mov-ax-ds': 1, 'mov-bx-immediate': 1})
        self.assertEqual(len(result['far_pointer_tables']), 1)
        table = result['far_pointer_tables'][0]
        self.assertTrue(table['all_memory_matches'])
        self.assertEqual((table['runtime_ds'], table['base_bx'],
                          table['stride_bytes']), (0x3031, 0x0123, 0x14))
        self.assertEqual([entry['destination_ds_offset']
                          for entry in table['entries']], [0x12f, 0x143])

        # The code is still structurally consistent, but cannot claim a
        # verified table if one live destination no longer matches.
        ram[ds_start + 0x143] ^= 1
        bad = observed_zero_word_changes(bytes(rtl), bytes(ram), bank, 0x1000)
        self.assertFalse(bad['far_pointer_tables'][0]['all_memory_matches'])
        self.assertEqual(bad['context_counts']['mov-mem16-immediate'], 4)
        self.assertNotIn('far-table-offset-store', bad['context_counts'])

    def test_missing_or_invalid_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            bad_exe = base / 'wrong.exe'
            bad_exe.write_bytes(b'MZ' + b'\x00' * 100)
            with self.assertRaisesRegex(ValueError, 'pinned original'):
                prepare(bad_exe, bad_exe, bad_exe, base / 'out')
            with self.assertRaisesRegex(ValueError, 'relocation directory'):
                mz_relocations(b'MZ' + b'\x00' * 100)
            with self.assertRaisesRegex(ValueError, 'name'):
                run(bad_exe, bad_exe, bad_exe, base, project_name='not-safe!')

    def test_headless_command(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'intro-ghidra-plan.json').write_text(json.dumps({
                'exe_file': '/unused/GATE.EXE'}))
            result = command(['python3', '-m', 'pyghidra.ghidra_launch'],
                             base / 'intro-ghidra-plan.json', 'GatewayIntro')
            self.assertIn('ImportGatewayIntro.py', result)
            self.assertIn('DecompileGatewayIntro.py', result)
            self.assertEqual(result[result.index('-import') + 1], '/unused/GATE.EXE')
            self.assertIn('-preScript', result)
            self.assertIn('-postScript', result)


class RealGatewayIntroEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        original = ROOT.parent / 'gateway-dos'
        cls.exe = original / 'GATE.EXE'
        cls.rtl = original / 'GATE.RTL'
        cls.snapshot = ROOT / 'build/intro-analysis/at-22000.bin'
        for path, expected in ((cls.exe, EXE_SHA256), (cls.rtl, RTL_SHA256)):
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise unittest.SkipTest('pinned original Gateway intro assets absent')
        if not cls.snapshot.is_file():
            raise unittest.SkipTest('private linked DOS guest RAM snapshot absent')

    def test_relocated_import_is_file_accurate(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            plan_file, plan = prepare(self.exe, self.rtl, self.snapshot, output)
            self.assertEqual(json.loads(plan_file.read_text()), plan)
            self.assertEqual(len(plan['sections']), len(RTL_BANKS))
            self.assertEqual(plan['graphics_far_targets']['total'], 86)
            self.assertEqual(plan['graphics_far_targets']['mapped'], 86)
            self.assertFalse(any(entry['mapped_target'] is None for entry in
                                 plan['graphics_far_targets']['transfers']))
            self.assertEqual(sum(len(s['entries']) for s in plan['sections']),
                             110)
            self.assertEqual({e['file_offset'] for e in plan['exe_labels']},
                             set(EXE_ENTRIES))
            self.assertGreaterEqual(sum(len(s['entries']) for s in plan['sections']), 44)
            startup = next(section for section in plan['sections']
                           if section['segment'] == 0x0a46)
            self.assertEqual(startup['file_base'], 0xc1d1)
            self.assertEqual(startup['size'], 0xa80)
            self.assertEqual(len(startup['witnesses']), 6)
            self.assertGreater(startup['exact_byte_matches'], 0xa80 * 0.60)
            self.assertTrue(any(e['name'] == 'gate_rtl_play_preloaded_picture_sequence'
                                for s in plan['sections'] for e in s['entries']))
            graphics = {s['segment']: s for s in plan['sections']
                        if s['segment'] in (0x1409, 0x144c, 0x152c, 0x15d7)}
            self.assertEqual(set(graphics), {0x1409, 0x144c, 0x152c, 0x15d7})
            self.assertEqual((graphics[0x1409]['exact_byte_matches'],
                              graphics[0x1409]['size']), (1003, 0x0430))
            self.assertEqual((graphics[0x144c]['exact_byte_matches'],
                              graphics[0x144c]['size']), (3383, 0x0e00))
            self.assertEqual((graphics[0x152c]['exact_byte_matches'],
                              graphics[0x15d7]['exact_byte_matches']), (2600, 3198))
            self.assertEqual((graphics[0x152c]['size'],
                              graphics[0x15d7]['size']), (0x0ab0, 0x0d79))
            rtl = self.rtl.read_bytes()
            ram = self.snapshot.read_bytes()
            for segment, covered, sites, changed, unmapped in (
                    (0x1409, 1063, 31, 60, 9),
                    (0x144c, 3582, 109, 199, 2),
                    (0x152c, 2726, 64, 126, 10),
                    (0x15d7, 3443, 134, 245, 6)):
                data = graphics[segment]['zero_word_changes']
                self.assertEqual(data['covered_source_bytes'], covered)
                self.assertEqual(data['equal_source_bytes'],
                                 graphics[segment]['exact_byte_matches'])
                self.assertEqual(len(data['candidate_words']), sites)
                self.assertEqual(data['candidate_changed_bytes'], changed)
                self.assertEqual(data['unclassified_bytes'], 0)
                self.assertEqual(data['unmapped_live_bytes'], unmapped)
                contexts = data['context_counts']
                if segment == 0x1409:
                    self.assertEqual(contexts, {
                        'direct-memory-displacement': 7,
                        'far-call-offset': 8, 'far-call-segment': 8,
                        'mov-ax-ds': 6, 'mov-ax-es': 1,
                        'mov-di-immediate': 1})
                    self.assertEqual(data['far_pointer_tables'], [])
                elif segment == 0x144c:
                    self.assertEqual(contexts, {
                        'direct-memory-displacement': 9,
                        'far-call-offset': 21, 'far-call-segment': 22,
                        'mov-ax-ds': 9, 'mov-mem16-immediate': 48})
                    self.assertEqual(data['far_pointer_tables'], [])
                elif segment == 0x152c:
                    self.assertEqual(contexts, {
                        'far-call-offset': 3, 'far-call-segment': 14,
                        'mov-ax-ds': 18, 'mov-ax-es': 1,
                        'mov-di-immediate': 1, 'mov-mem16-immediate': 12,
                        'mov-si-immediate': 15})
                    self.assertEqual(data['far_pointer_tables'], [])
                else:
                    self.assertEqual(contexts, {
                        'cmp-ax-immediate': 1,
                        'direct-memory-displacement': 6,
                        'far-call-offset': 27, 'far-call-segment': 39,
                        'far-jump-offset': 3, 'far-jump-segment': 3,
                        'far-table-offset-store': 11,
                        'far-table-segment-store': 11,
                        'mov-ax-ds': 12, 'mov-bx-immediate': 2,
                        'mov-di-immediate': 1, 'mov-si-immediate': 18})
                    self.assertEqual(len(data['far_pointer_tables']), 1)
                    table = data['far_pointer_tables'][0]
                    self.assertTrue(table['all_memory_matches'])
                    self.assertEqual((table['runtime_ds'], table['base_bx'],
                                      table['stride_bytes']),
                                     (0x2a7a, 0x2a8f, 0x14))
                    self.assertEqual(len(table['entries']), 11)
                    self.assertEqual([entry['destination_ds_offset']
                                      for entry in table['entries']],
                                     [0x2a9b + 0x14 * i for i in range(11)])
                    self.assertEqual({entry['far_segment']
                                      for entry in table['entries']}, {0x2621})
                # The plan never embeds the original instruction stream:
                # each site is a tested on-disk zero word plus a live value.
                for candidate in data['candidate_words']:
                    source = candidate['rtl_file_offset']
                    live = ((0x1000 + segment) * 16 +
                            candidate['runtime_offset'])
                    self.assertEqual(rtl[source:source + 2], b'\x00\x00')
                    self.assertEqual(int.from_bytes(ram[live:live + 2], 'little'),
                                     candidate['observed_value'])
            self.assertEqual({entry['file_offset'] for entry
                              in graphics[0x1409]['entries']},
                             {off for off in RTL_CANDIDATES
                              if source_span(RTL_GRAPHICS_BANKS[0], off)})
            self.assertEqual({entry['file_offset'] for entry
                              in graphics[0x144c]['entries']},
                             {off for off in RTL_CANDIDATES
                              if source_span(RTL_GRAPHICS_BANKS[1], off)})
            self.assertEqual({entry['file_offset'] for entry
                              in graphics[0x152c]['entries']},
                             {off for off in RTL_CANDIDATES
                              if source_span(RTL_GRAPHICS_BANKS[2], off)})
            self.assertEqual({entry['file_offset'] for entry
                              in graphics[0x15d7]['entries']},
                             {off for off in RTL_CANDIDATES
                              if source_span(RTL_GRAPHICS_BANKS[3], off)})
            for section in graphics.values():
                for entry in section['entries']:
                    source = entry['file_offset']
                    if rtl[source:source + 3] != b'\x55\x8b\xec':
                        continue
                    live = ((0x1000 + section['segment']) * 16 +
                            entry['offset'])
                    self.assertEqual(rtl[source:source + 11],
                                     ram[live:live + 11],
                                     'original C prologue at RTL:%05x' % source)
            file_services = next(section for section in plan['sections']
                                 if section['segment'] == 0x0c33)
            self.assertEqual({item['file_offset'] for item in file_services['entries']},
                             {0xe4ff, 0xe511, 0xe531, 0xe582, 0xe59b,
                              0xe5c5, 0xe5e3, 0xe61e, 0xe637, 0xe650,
                              0xe5fc, 0xe669, 0xe68b})
            self.assertTrue(all(item['role'] == 'source-reconstructed'
                                for item in file_services['entries']))
            self.assertEqual(len(file_services['witnesses']), 4)
            snapshot = self.snapshot.read_bytes()
            for section in plan['sections']:
                with self.subTest(segment=section['segment']):
                    start = (plan['load_segment'] + section['segment']) * 16
                    image = snapshot[start:start + section['size']]
                    self.assertEqual(Path(section['relocated_file']).read_bytes(), image)
                    self.assertEqual(hashlib.sha256(image).hexdigest(),
                                     section['relocated_sha256'])
                    self.assertGreater(section['exact_byte_matches'],
                                       section['size'] * 0.60)
                    for entry in section['entries']:
                        span = next(s for s in section['source_spans']
                                    if s['file_base'] <= entry['file_offset'] <
                                    s['file_base'] + s['size'])
                        self.assertEqual(entry['offset'], span['offset'] +
                                         entry['file_offset'] - span['file_base'])
            linked_strlen = next(s for s in plan['sections']
                                 if s['segment'] == 0x02a5)
            self.assertEqual(linked_strlen['file_base'], 0x56de)
            self.assertIn(0x8298, [e['file_offset']
                                   for e in linked_strlen['entries']])
            self.assertEqual(plan['unlinked_staging_candidates'][0]['segment'],
                             RAW_COPY_CANDIDATE['segment'])
            self.assertNotIn(RAW_COPY_CANDIDATE['segment'],
                             {section['segment'] for section in plan['sections']})

    def test_staged_copy_cannot_be_imported_as_linked_code(self):
        ram = bytearray(self.snapshot.read_bytes())
        rtl = self.rtl.read_bytes()
        region = RAW_COPY_CANDIDATE
        start = (0x1000 + region['segment']) * 16
        base = region['file_base']
        self.assertEqual(ram[start:start + region['size']],
                         rtl[base:base + region['size']])
        self.assertEqual(rtl[base + 0x11:base + 0x15], b'\0' * 4)
        self.assertEqual(len(staged_unlinked_copy(rtl, ram, 0x1000)), 1)
        ram[start + 0x20] ^= 0xff
        self.assertEqual(staged_unlinked_copy(rtl, ram, 0x1000), [])

    def test_refuse_snapshot_not_matching_linked_bank(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            damaged = bytearray(self.snapshot.read_bytes())
            witness = RTL_BANKS[2]
            start = (0x1000 + witness['segment']) * 16
            damaged[start:start + witness['size']] = b'\x00' * witness['size']
            path = base / 'wrong.bin'
            path.write_bytes(damaged)
            with self.assertRaisesRegex(ValueError, 'does not contain RTL bank'):
                prepare(self.exe, self.rtl, path, base / 'out')

    def test_runner_refuses_stale_results(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'intro-ghidra-report.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                run(self.exe, self.rtl, self.snapshot, base)


if __name__ == '__main__':
    unittest.main()

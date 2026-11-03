#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Checks conservative native-story text-reference annotations."""
import sys
import unittest
from pathlib import Path
import hashlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disassemble_story import annotate, event_targets, text_id
from gateway_strings import GatewayStrings
from gateway_vocab import GatewayVocabulary
from summarize_story_trace import collect_actions, summarize
from inspect_action_dispatch import inspect_actions, map_action_entries, map_resident_actions
import struct


class StoryDisassemblyTests(unittest.TestCase):
    def test_token_split(self):
        self.assertEqual(text_id(0xb464, 0xf000), (45, 100))
        self.assertIsNone(text_id(0xb464, 0x1234))

    def test_call_sequence(self):
        lines = [
            '  72aa4: b8 64 b4              mov    ax,0xb464',
            '  72aa7: ba 00 f0              mov    dx,0xf000',
            '  72aaa: 52                    push   dx',
            '  72aab: 50                    push   ax',
            '  72aac: 9a 5b 10 cd 17        call   0x17cd:0x105b',
        ]
        self.assertIn('segment=45 string=100', annotate(lines)[-1])
        lines.insert(2, '  72aa9: 90                    nop')
        self.assertNotIn('story text', '\n'.join(annotate(lines)))

    def test_event_table_uses_section_relative_offsets(self):
        self.assertEqual(event_targets(b'\x35\x00\x8e\x02', 0x51480, 0, 2),
                         [(1, 0x514b5), (2, 0x5170e)])
        with self.assertRaises(ValueError):
            event_targets(b'\0', 0x51480, 0, 1)

    def test_synthetic_strings(self):
        # One segment, one 1-byte text. Root leaves select 'A' then NUL.
        # Bits 0 and 1 yield A, NUL and terminate without consuming padding.
        tree = [0xffbe, 0xffff]
        data = (struct.pack('<HHHHH', 1, 1, 1, 1, 2)
                + struct.pack('<2H', *tree) + struct.pack('<HH', 0, 0) + b'\x02')
        self.assertEqual(GatewayStrings(data).get(0, 0), 'A')

    def test_synthetic_vocabulary_and_link_group(self):
        tree = struct.pack('<2H', 0xffbe, 0xffff)
        data = (struct.pack('<H', 2) + tree + struct.pack('<H', 1)
                + b'\x02' + struct.pack('<H', 1) + struct.pack('<HH', 0, 1)
                + struct.pack('<H', 1) + struct.pack('<HHHH', 0, 1, 0, 7))
        vocabulary = GatewayVocabulary(data)
        self.assertEqual(vocabulary.entries[0]['text'], 'A')
        self.assertEqual(vocabulary.entries[0]['flags'], 1)
        self.assertEqual(vocabulary.groups, [{'key': 0, 'pairs': [(0, 7)]}])

    def test_story_trace_summary(self):
        lines = ['key ms=450 value=6f', 'key ms=470 value=70',
                 'key ms=500 value=1c0d',
                 'entry ms=501 pc=4a378 cs=49d8 stack=701,2c24,6, '
                 'story_object=124 story_event=6 story_command=3a',
                 'entry ms=510 pc=4a378 cs=49d8 stack=701,2c24,6, '
                 'story_object=124 story_event=6 story_command=3a',
                 'entry ms=520 pc=49db5 cs=49d8 stack=701,2c24,8, '
                 'story_object=ee story_event=8 story_command=5f',
                 'entry ms=521 pc=dead cs=1111']
        self.assertEqual(summarize(lines), [(0xee, 8, 0x5f, 1, 520, ('op',)),
                                            (0x124, 6, 0x3a, 2, 501, ('op',))])

    def test_legacy_entry_trace_without_command_annotations(self):
        lines = ['key ms=100 value=6f', 'key ms=110 value=70',
                 'key ms=120 value=65', 'key ms=130 value=6e',
                 'key ms=140 value=1c0d',
                 'entry ms=192 pc=4a378 cs=49d8 ds=5b46 '
                 'stack=701,2c24,6,2acd,2c24, frames=dbc0:dc28:4c0:1000,',
                 'entry ms=195 pc=4a378 cs=49d9 stack=701,2c24,8,',
                 'entry ms=200 pc=12345 cs=49d8 stack=701,2c24,8,']
        self.assertEqual(summarize(lines),
                         [(0x124, 6, None, 1, 192, ('open',))])

    def test_drawer_command_from_instruction_register(self):
        lines = ['key ms=100 value=1c0d',
                 'entry ms=192 pc=4a378 cs=49d8 ax=2acd '
                 'stack=701,2c24,6,2acd,2c24,',
                 'entry ms=192 pc=4a390 cs=49d8 ax=85 '
                 'stack=701,2c24,6,2acd,2c24,']
        self.assertEqual(summarize(lines),
                         [(0x124, 6, 0x85, 1, 192, ())])

    def test_action_ids_follow_completed_input(self):
        lines = ['key ms=100 value=6f', 'key ms=110 value=70',
                 'key ms=120 value=65', 'key ms=130 value=6e',
                 'key ms=140 value=1c0d',
                 'entry ms=185 pc=147a6 cs=1476 ax=71 stack=4da,1000,71,',
                 'entry ms=186 pc=147a6 cs=1477 ax=46 stack=4da,1000,46,',
                 'entry ms=3000 pc=147a6 cs=1476 ax=ff stack=4da,1000,ff,']
        self.assertEqual(collect_actions(lines), [(185, 'open', 0x71)])

    def test_native_action_directory_from_memory(self):
        memory = bytearray(0x20000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        struct.pack_into('<3H', memory, 0x12000 + 0x28 + 6 * 0x5f,
                         0x0b90, 0x148e, 0x0008)
        records = inspect_actions(memory, 0x1000, [0x5f])
        self.assertEqual(records, [dict(action=0x5f, handler_segment=0x148e,
                                        handler_offset=0x0b90, selector=0, trailer=8)])
        with self.assertRaises(ValueError):
            inspect_actions(memory, 0x1000, [196])

    def test_resident_action_mapping_checks_unique_prefixes_and_far_call_relocations(self):
        exe = bytearray(0x900)
        exe[:2] = b'MZ'
        memory = bytearray(0x20000)
        expected = {0x5f: 0x100, 0x6d: 0x200, 0x98: 0x300, 0xaf: 0x400}
        entries = []
        for action in range(1, 196):
            if action in expected:
                offset = expected[action]
                block = bytearray([action & 0xff] * 96)
                block[:8] = bytes((0x55, 0x8b, action, 0x2a, 0x03, 0x90, 0xcd, 0x13))
                block[14:19] = b'\x9a\x34\x12\xcd\x17'
                exe[offset:offset + 96] = block
                address = 0x10000 + offset - 0x80
                memory[address:address + 96] = block
                memory[address + 18] = 0x27
                entries.append(dict(action=action, handler_segment=0x1000,
                                    handler_offset=offset - 0x80))
            else:
                entries.append(dict(action=action, rtlink_reference=dict(section=0)))
        signature = hashlib.sha256(exe).hexdigest()
        mapped = map_resident_actions(entries, memory, exe, 0x1000, signature)
        self.assertEqual({item['action']: item['native_code']['file_offset'] for item in mapped
                          if 'native_code' in item}, expected)
        self.assertTrue(all(item['native_code']['filename'] == 'GATEMAIN.EXE' for item in mapped
                            if 'native_code' in item))

        with self.assertRaisesRegex(ValueError, 'fingerprinted main EXE'):
            map_resident_actions(entries, memory, exe, 0x1000, 'wrong hash')
        corrupted = bytearray(memory)
        corrupted[0x10000 + 0x100 - 0x80 + 40] ^= 1
        with self.assertRaisesRegex(ValueError, 'outside far-call relocation'):
            map_resident_actions(entries, corrupted, exe, 0x1000, signature)
        with self.assertRaisesRegex(ValueError, 'far-call relocation differs'):
            map_resident_actions(entries, memory, exe, 0x2000, signature)
        duplicated = bytearray(exe)
        duplicated[0x500:0x508] = exe[0x100:0x108]
        with self.assertRaisesRegex(ValueError, 'not unique'):
            map_resident_actions(entries, memory, duplicated, 0x1000,
                                 hashlib.sha256(duplicated).hexdigest())

    def test_action_trailer_is_not_a_word_with_next_selector(self):
        memory = bytearray(0x20000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        first = 0x12000 + 0x28 + 6 * 0x32
        memory[first - 1] = 0x04
        struct.pack_into('<2H', memory, first, 0x0102, 0x3040)
        memory[first + 4] = 0x24
        memory[first + 5] = 0x05
        entries = inspect_actions(memory, 0x1000, [0x32, 0x33])
        self.assertEqual(entries[0]['selector'], 0x04)
        self.assertEqual(entries[0]['trailer'], 0x24)
        self.assertEqual(entries[1]['selector'], 0x05)
        self.assertEqual(entries[0]['handler_offset'], 0x0102)
        self.assertEqual(entries[0]['handler_segment'], 0x3040)

    def test_selector_callback_is_a_separate_far_pointer_table(self):
        memory = bytearray(0x20000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        action_pointer = 0x12000 + 0x28 + 6 * 0x46
        memory[action_pointer - 1] = 8
        struct.pack_into('<2H', memory, action_pointer, 0x165f, 0x2c24)
        # BX=4*8 at callback site; CALL far ES:[BX-4] => table byte 0x1c.
        struct.pack_into('<2H', memory, 0x12000 + 0x1c, 0x0456, 0x1234)
        record, = inspect_actions(memory, 0x1000, [0x46])
        self.assertEqual(record['selector_callback'], {'segment': 0x1234, 'offset': 0x0456})
        memory[action_pointer - 1] = 0
        record, = inspect_actions(memory, 0x1000, [0x46])
        self.assertNotIn('selector_callback', record)

    def test_take_drop_selector_trampolines_map_to_original_overlay(self):
        memory = bytearray(0x40000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        for action, selector, pointer, target in [(0x46, 8, 0x1939, 0x27df),
                                                   (0x32, 4, 0x1961, 0x0434)]:
            table = 0x12000 + 0x28 + 6 * action
            memory[table - 1] = selector
            struct.pack_into('<2H', memory, table, 0x1500, 0x1234)
            struct.pack_into('<2H', memory, 0x12000 + 4 * (selector - 1),
                             pointer, 0x2c24)
            struct.pack_into('<BhB3H', memory, 0x2c240 + pointer, 0xe8,
                             0x058d - (pointer + 3), 0xea, target, 0x4661, 5)
        entries = inspect_actions(memory, 0x1000, [0x46, 0x32])
        self.assertEqual([(entry['selector_reference']['section'],
                           entry['selector_reference']['target_offset']) for entry in entries],
                         [(5, 0x27df), (5, 0x0434)])
        manifest = {'sections': [dict(id=5, filename='GATEMAIN.OVL',
                                     runtime_segment=0x4661, file_bytes=0x4000,
                                     image_file_offset=0xd3d0)]}
        mapped = map_action_entries(entries, manifest)
        self.assertEqual([entry['selector_native_code']['file_offset'] for entry in mapped],
                         [0xfbaf, 0xd804])
        # A pointer with a broken manager call cannot be guessed from target bytes.
        memory[0x2c240 + 0x1939] = 0x90
        self.assertNotIn('selector_reference', inspect_actions(memory, 0x1000, [0x46])[0])

    def test_action_rtlink_reference_comes_from_trampoline_operands(self):
        memory = bytearray(0x40000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        struct.pack_into('<3H', memory, 0x12000 + 0x28 + 6 * 0x1d,
                         0x17f9, 0x2c24, 3)
        code = 0x2c240 + 0x17f9
        struct.pack_into('<BhB3H', memory, code, 0xe8, 0x058d - (0x17f9 + 3),
                         0xea, 0x029b, 0x4661, 5)
        entries = inspect_actions(memory, 0x1000, [0x1d])
        self.assertEqual(entries[0]['rtlink_reference'],
                         dict(section=5, target_segment=0x4661, target_offset=0x029b))
        manifest = {'sections': [dict(id=5, filename='GATEMAIN.OVL',
                                     runtime_segment=0x4661, file_bytes=0x30a0,
                                     image_file_offset=0xd3d0)]}
        mapped = map_action_entries(entries, manifest)
        self.assertEqual(mapped[0]['native_code']['file_offset'], 0xd66b)
        with self.assertRaises(ValueError):
            map_action_entries(entries, {'sections': []})

        # An apparent IP/CS/section triple at the pointer is not enough:
        # without the executable CALL/JMP prefix this is NOT a trampoline.
        struct.pack_into('<3H', memory, code, 0x0cf7, 0x4661, 7)
        self.assertNotIn('rtlink_reference', inspect_actions(memory, 0x1000, [0x1d])[0])

    def test_action_trampoline_requires_rtlink_manager_gate(self):
        memory = bytearray(0x40000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        struct.pack_into('<2H', memory, 0x12000 + 0x28 + 6 * 0x71,
                         0x1c59, 0x2c24)
        code = 0x2c240 + 0x1c59
        struct.pack_into('<BhB3H', memory, code, 0xe8, -3, 0xea,
                         0x1572, 0x4661, 5)
        self.assertNotIn('rtlink_reference', inspect_actions(memory, 0x1000, [0x71])[0])

    def test_five_observed_actions_share_section_five(self):
        memory = bytearray(0x40000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        entries = [(0x1d, 0x17f9, 0x029b), (0x32, 0x1727, 0x05d7),
                   (0x46, 0x165f, 0x2b11), (0x53, 0x15dd, 0x14f4),
                   (0x71, 0x1c59, 0x1572)]
        for action, pointer, target in entries:
            struct.pack_into('<2H', memory, 0x12000 + 0x28 + 6 * action,
                             pointer, 0x2c24)
            struct.pack_into('<BhB3H', memory, 0x2c240 + pointer, 0xe8,
                             0x058d - (pointer + 3), 0xea, target, 0x4661, 5)
        records = inspect_actions(memory, 0x1000, [action for action, _, _ in entries])
        self.assertEqual([(record['action'], record['rtlink_reference']['section'],
                           record['rtlink_reference']['target_offset']) for record in records],
                         [(action, 5, target) for action, _, target in entries])


if __name__ == '__main__':
    unittest.main()

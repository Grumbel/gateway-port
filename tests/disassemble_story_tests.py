#!/usr/bin/env python3
"""Checks conservative native-story text-reference annotations."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disassemble_story import annotate, event_targets, text_id
from gateway_strings import GatewayStrings
from gateway_vocab import GatewayVocabulary
from summarize_story_trace import collect_actions, summarize
from inspect_action_dispatch import inspect_actions, map_action_entries
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
                                        handler_offset=0x0b90, extra_word=8)])
        with self.assertRaises(ValueError):
            inspect_actions(memory, 0x1000, [196])

    def test_action_rtlink_reference_is_data_not_trampoline_start(self):
        memory = bytearray(0x40000)
        struct.pack_into('<H', memory, 0x10000 + 0x8da8, 0x1200)
        struct.pack_into('<3H', memory, 0x12000 + 0x28 + 6 * 0x1d,
                         0x17f9, 0x2c24, 3)
        struct.pack_into('<3H', memory, 0x2c240 + 0x17f9, 0x0cf7, 0x4661, 7)
        entries = inspect_actions(memory, 0x1000, [0x1d])
        self.assertEqual(entries[0]['rtlink_reference'],
                         dict(section=7, target_segment=0x4661, target_offset=0x0cf7))
        manifest = {'sections': [dict(id=7, filename='GATEMAIN.OVL',
                                     runtime_segment=0x4661, file_bytes=0x32f0,
                                     image_file_offset=0x14150)]}
        mapped = map_action_entries(entries, manifest)
        self.assertEqual(mapped[0]['native_code']['file_offset'], 0x14e47)
        with self.assertRaises(ValueError):
            map_action_entries(entries, {'sections': []})


if __name__ == '__main__':
    unittest.main()

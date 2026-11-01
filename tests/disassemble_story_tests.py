#!/usr/bin/env python3
"""Checks conservative native-story text-reference annotations."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disassemble_story import annotate, event_targets, text_id
from gateway_strings import GatewayStrings
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


if __name__ == '__main__':
    unittest.main()

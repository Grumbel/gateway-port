#!/usr/bin/env python3
"""Checks conservative native-story text-reference annotations."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disassemble_story import annotate, text_id


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


if __name__ == '__main__':
    unittest.main()

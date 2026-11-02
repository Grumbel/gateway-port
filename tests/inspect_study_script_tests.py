#!/usr/bin/env python3
"""Asset-independent tests for native Study far-pointer table decoding."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from inspect_study_script import (decode_study_script, MEMORY_LIMIT,
                                  PROMPT_TABLE_OFFSET, RESPONSE_TABLE_OFFSET)


DS = 0x1000
TABLE = 0x2000


class FakeStrings:
    def get(self, segment, index):
        if segment != 28 or index > 200:
            raise IndexError('not in GATESTR')
        return 'token %d' % index


class StudyScriptTests(unittest.TestCase):
    def setUp(self):
        self.memory = bytearray(MEMORY_LIMIT)
        self.words(DS * 16 + 0x9652, TABLE)
        for stage in range(1, 20):
            self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4 * stage,
                       0x7000 + stage, 0xf000)
            self.words(TABLE * 16 + RESPONSE_TABLE_OFFSET + 4 * stage,
                       0x7010 + stage, 0xf000)

    def words(self, address, *words):
        struct.pack_into('<' + 'H' * len(words), self.memory, address, *words)

    def decode(self):
        return decode_study_script(self.memory, DS, FakeStrings())

    def test_all_nineteen_stages_and_two_table_strides(self):
        stages = self.decode()
        self.assertEqual(len(stages), 19)
        self.assertEqual(stages[0], dict(stage=1, instruction='token 1',
                                         response='token 17'))
        self.assertEqual(stages[-1], dict(stage=19, instruction='token 19',
                                          response='token 35'))

    def test_mixed_real_segment_and_f000_tokens(self):
        self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4, 0x40, 0x1200)
        self.memory[0x12040:0x12046] = b'feel\x82\0'
        self.words(TABLE * 16 + RESPONSE_TABLE_OFFSET + 4 * 19, 0x60, 0x1200)
        self.memory[0x12060:0x12064] = b'ok!\0'
        stages = self.decode()
        self.assertEqual(stages[0]['instruction'], 'feelé')
        self.assertEqual(stages[-1]['response'], 'ok!')

    def test_invalid_ds_and_missing_ram(self):
        for ds in (-1, 0, 0x10000):
            with self.subTest(ds=ds), self.assertRaisesRegex(ValueError, 'valid native DS'):
                decode_study_script(self.memory, ds, FakeStrings())
        with self.assertRaisesRegex(ValueError, 'conventional RAM'):
            decode_study_script(self.memory[:-1], DS, FakeStrings())
        with self.assertRaisesRegex(ValueError, 'table word outside'):
            decode_study_script(self.memory, 0xffff, FakeStrings())

    def test_null_and_invalid_table_pointer(self):
        for segment, pattern in ((0, 'segment is null'),
                                 (0xa000, 'table word outside')):
            with self.subTest(segment=segment):
                self.words(DS * 16 + 0x9652, segment)
                with self.assertRaisesRegex(ValueError, pattern):
                    self.decode()

    def test_null_out_of_ram_and_unterminated_c_strings(self):
        self.memory[0x12100:0x12100 + 4096] = b'x' * 4096
        for segment, offset, pattern in ((0, 0x40, 'string segment'),
                                         (0x9f00, 0xfffe, 'outside conventional'),
                                         (0x1200, 0x100, 'unterminated')):
            with self.subTest(segment=segment):
                self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4, offset, segment)
                with self.assertRaisesRegex(ValueError, pattern):
                    self.decode()

    def test_invalid_story_token_is_not_silent(self):
        self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4, 0xf001, 0xf000)
        with self.assertRaisesRegex(ValueError, 'invalid F000 Study text token'):
            self.decode()


if __name__ == '__main__':
    unittest.main()

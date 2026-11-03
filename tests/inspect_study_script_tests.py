#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Asset-independent tests for Study event-1 matcher and text tables."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from inspect_study_script import (decode_study_script, MEMORY_LIMIT,
                                  ACTION_TABLE_OFFSET, ALTERNATIVES_OFFSET,
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
        for stage in range(1, 21):
            address = TABLE * 16 + ACTION_TABLE_OFFSET + 4 * stage
            self.memory[address] = 0x84 if stage == 20 else 0x30 + stage
            self.words(address + 2, 0 if stage == 20 else 0x100 + stage)
            self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4 * stage,
                       0x7000 + stage, 0xf000)
            if stage <= 19:
                self.words(TABLE * 16 + RESPONSE_TABLE_OFFSET + 4 * stage,
                           0x7010 + stage, 0xf000)
        for index, (stage, first, second) in enumerate(((5, 0x55, 0x56),
                                                       (9, 0x18, 0),
                                                       (13, 0x1b, 0),
                                                       (14, 0x55, 0x56),
                                                       (19, 0x19, 0x18))):
            address = TABLE * 16 + ALTERNATIVES_OFFSET + 4 * index
            self.words(address, stage)
            self.memory[address + 2:address + 4] = bytes((first, second))

    def words(self, address, *words):
        struct.pack_into('<' + 'H' * len(words), self.memory, address, *words)

    def decode(self):
        return decode_study_script(self.memory, DS, FakeStrings())

    def test_nineteen_responses_and_terminal_action_table_strides(self):
        stages = self.decode()
        self.assertEqual(len(stages), 20)
        self.assertEqual(stages[0], dict(stage=1, instruction='token 1',
                                         response='token 17', action_code=0x31,
                                         target_object=0x101, alternative_codes=[]))
        self.assertEqual(stages[-2], dict(stage=19, instruction='token 19',
                                          response='token 35', action_code=0x43,
                                          target_object=0x113,
                                          alternative_codes=[0x19, 0x18]))
        self.assertEqual(stages[-1], dict(stage=20, instruction='token 20',
                                          response=None, action_code=0x84,
                                          target_object=0, alternative_codes=[]))
        self.assertEqual(stages[8]['alternative_codes'], [0x18, 0])
        self.assertEqual(stages[12]['alternative_codes'], [0x1b, 0])
        self.assertEqual(stages[6]['alternative_codes'], [])

    def test_rejects_duplicate_invalid_or_zero_alternative_stage(self):
        first_address = TABLE * 16 + ALTERNATIVES_OFFSET
        second_address = first_address + 4
        for malformed_stage in (0, 21, 0xffff, 5):
            with self.subTest(malformed_stage=malformed_stage):
                self.words(second_address, malformed_stage)
                with self.assertRaisesRegex(ValueError, 'alternative stage'):
                    self.decode()
        self.words(second_address, 9)
        self.assertEqual(len(self.decode()), 20)

    def test_mixed_real_segment_and_f000_tokens(self):
        self.words(TABLE * 16 + PROMPT_TABLE_OFFSET + 4, 0x40, 0x1200)
        self.memory[0x12040:0x12046] = b'feel\x82\0'
        self.words(TABLE * 16 + RESPONSE_TABLE_OFFSET + 4 * 19, 0x60, 0x1200)
        self.memory[0x12060:0x12064] = b'ok!\0'
        stages = self.decode()
        self.assertEqual(stages[0]['instruction'], 'feelé')
        self.assertEqual(stages[-2]['response'], 'ok!')

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

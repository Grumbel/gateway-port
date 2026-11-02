#!/usr/bin/env python3
"""Synthetic, asset-free guards for the original Study flag replay inspector."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from inspect_study_action_match import verify_study_action_match


FINAL = dict(study_counter=4, study_counter_address=0x62d64,
             player_location=0x2bc, player_enclosing_room=0x2bc)
STAGES = ((75235, 75356, 75234, 75261, '4a370'),
          (78361, 78501, 78360, 78407, '4a370'),
          (81141, 81212, 81115, 81141, '4bb1c'))


def sample_lines():
    lines = []
    for matched, cleared, room_event, response, origin in STAGES:
        lines.extend((
            'entry ms=%d pc=4a151 cs=4990 stack=701,2c24,1,0' % room_event,
            'entry ms=%d pc=4bab5 cs=4990 stack=x' % response,
            'byte-write ms=%d address=62d69 pc=%s cs=4990 size=1 before=0 value=1' %
            (matched, origin),
            'byte-write ms=%d address=62d69 pc=4bc4c cs=4990 size=1 before=1 value=0' %
            cleared,
        ))
    return lines


class StudyActionMatchTests(unittest.TestCase):
    def test_all_three_original_paths_are_distinguished(self):
        self.assertEqual(verify_study_action_match(sample_lines(), FINAL),
                         [75235, 78361, 81141])

    def test_missing_room_event_or_response_entry(self):
        for removed in ('pc=4a151', 'pc=4bab5'):
            with self.subTest(removed=removed):
                lines = [line for line in sample_lines()
                         if removed not in line or 'ms=7836' not in line and
                         'ms=78407' not in line]
                with self.assertRaisesRegex(ValueError, 'entry'):
                    verify_study_action_match(lines, FINAL)

    def test_bad_flag_origin_and_old_value(self):
        for old, new in (('pc=4a370', 'pc=4a371'),
                         ('before=0', 'before=2'),
                         ('value=1', 'value=0')):
            with self.subTest(old=old):
                lines = sample_lines()
                lines[2] = lines[2].replace(old, new)
                with self.assertRaisesRegex(ValueError, 'set the flag'):
                    verify_study_action_match(lines, FINAL)

    def test_bad_clear_and_missing_pair(self):
        for old, new in (('pc=4bc4c', 'pc=4bc4d'),
                         ('before=1', 'before=0'),
                         ('value=0', 'value=1')):
            with self.subTest(old=old):
                lines = sample_lines()
                lines[3] = lines[3].replace(old, new)
                with self.assertRaisesRegex(ValueError, 'clear flag'):
                    verify_study_action_match(lines, FINAL)
        with self.assertRaisesRegex(ValueError, 'exactly three'):
            verify_study_action_match(sample_lines()[:-1], FINAL)

    def test_late_outside_slot(self):
        lines = sample_lines()
        lines[2] = lines[2].replace('75235', '78235')
        with self.assertRaisesRegex(ValueError, 'set the flag'):
            verify_study_action_match(lines, FINAL)

    def test_wrong_segment_or_size(self):
        for old, new in (('cs=4990', 'cs=498f'), ('size=1', 'size=2')):
            with self.subTest(old=old):
                lines = sample_lines()
                lines[2] = lines[2].replace(old, new)
                with self.assertRaisesRegex(ValueError, 'section-51 byte-sized'):
                    verify_study_action_match(lines, FINAL)

    def test_unmodified_and_final_state_guard(self):
        with self.assertRaisesRegex(ValueError, 'unmodified'):
            verify_study_action_match(sample_lines() +
                                      ['research-poke16 ms=74000 address=62d64'], FINAL)
        for key, value in (('study_counter', 3), ('player_location', 0x2be),
                           ('study_counter_address', 0x1234)):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'standing'):
                verify_study_action_match(sample_lines(), {**FINAL, key: value})

    def test_writes_before_study_are_not_mistaken_for_matcher(self):
        lines = ['byte-write ms=3120 address=62d69 pc=6a841 cs=6941 '
                 'size=2 before=0 value=61'] + sample_lines()
        self.assertEqual(verify_study_action_match(lines, FINAL)[0], 75235)


if __name__ == '__main__':
    unittest.main()

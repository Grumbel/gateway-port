#!/usr/bin/env python3
"""Asset-free guards for twenty-stage Study state/response native inspector."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from inspect_study_response import (inspect_response_snapshot, verify_reminder,
                                    verify_terminal)


COMMON = dict(study_counter_address=0x62d64, previous_prompt_address=0x62d66,
              reminder_address=0x62d68, matched_address=0x62d69,
              terminal_action=0x84, terminal_object=0, terminal_prompt='sleep')
REMINDER_STATE = dict(**COMMON, study_counter=1, previous_prompt=1,
                      reminder=1, matched=0, player_location=0x2a1,
                      player_enclosing_room=0x29b)
TERMINAL_STATE = dict(**COMMON, study_counter=20, previous_prompt=0,
                      reminder=0, matched=1, player_location=0x29b,
                      player_enclosing_room=0x29b)


def byte_write(ms, address, pc, before, value):
    return ('byte-write ms=%d address=%x pc=%x cs=4990 size=%d before=%x value=%x' %
            (ms, address, pc, 1 if address >= 0x62d68 else 2, before, value))


def entry(ms, pc):
    return 'entry ms=%d pc=%x cs=4990 stack=0' % (ms, pc)


def refusal_lines():
    return [
        byte_write(72434, 0x62d64, 0x4bac7, 0, 1),
        byte_write(72484, 0x62d66, 0x4bc04, 0, 1),
        entry(72434, 0x4bab5),
        entry(75419, 0x4bab5),
        byte_write(78001, 0x62d68, 0x4bb7c, 0, 1),
        entry(78418, 0x4bab5),
        entry(78418, 0x4bb85),
        entry(81041, 0x4c194),
    ]


def completed_lines():
    lines = [byte_write(72434, 0x62d64, 0x4bac7, 0, 1),
             byte_write(72484, 0x62d66, 0x4bc04, 0, 1)]
    for stage in range(1, 20):
        ms = 72000 + stage * 3000 + 200
        origin = (0x4bb1c if stage in (3, 13) else
                  0x4a3e7 if stage == 9 else 0x4a370)
        lines.extend((byte_write(ms, 0x62d69, origin, 0, 1),
                      byte_write(ms + 50, 0x62d64, 0x4bc21, stage, stage + 1),
                      byte_write(ms + 120, 0x62d69, 0x4bc4c, 1, 0)))
        if stage == 1:
            lines.append(byte_write(ms + 120, 0x62d66, 0x4bc56, 1, 0))
    lines.extend((
        byte_write(132205, 0x62d69, 0x4a370, 0, 1),
        entry(132232, 0x4bab5),
        entry(132277, 0x4c194),
        'object-watch object=d3 type=7',
        'object-location-set ms=132438 object=d3 location=2bc changed=1 return=4319:16e7',
        'object-write ms=132438 object=d3 pc=11c32 offset=c size=2 before=be02 value=bc02',
        'object-location-set ms=132438 object=d3 location=29b changed=1 return=4319:16e7',
        'object-write ms=132438 object=d3 pc=11c32 offset=c size=2 before=bc02 value=9b02',
    ))
    return lines


class StudyResponseTests(unittest.TestCase):
    def test_native_refusal_path(self):
        result = verify_reminder(refusal_lines(), REMINDER_STATE)
        self.assertEqual(result['reminder_ms'], 78001)
        self.assertEqual(result['auxiliary_ms'], 81041)

    def test_native_terminal_action_path(self):
        result = verify_terminal(completed_lines(), TERMINAL_STATE)
        self.assertEqual(len(result['stages']), 19)
        self.assertEqual(result['stages'][8][1], 0x4a3e7)
        self.assertEqual(result['terminal_ms'], 132205)
        self.assertEqual(result['player_move_ms'], 132438)

    def test_native_action_order_and_posture_are_guarded(self):
        for old, new in (('pc=4a3e7', 'pc=4a370'),
                         ('pc=4bb1c', 'pc=4a370'),
                         ('before=1 value=2', 'before=1 value=3')):
            with self.subTest(old=old):
                lines = [line.replace(old, new) for line in completed_lines()]
                with self.assertRaisesRegex(ValueError, 'write order'):
                    verify_terminal(lines, TERMINAL_STATE)

    def test_native_completion_has_no_extra_progress_increment(self):
        lines = completed_lines()
        lines.insert(-7, byte_write(132210, 0x62d64, 0x4bc21, 20, 21))
        with self.assertRaisesRegex(ValueError, 'extra or missing'):
            verify_terminal(lines, TERMINAL_STATE)

    def test_wrong_terminal_state_and_original_table_are_rejected(self):
        for key, value in (('study_counter', 21), ('matched', 0),
                           ('terminal_action', 0x71), ('terminal_prompt', 'relax'),
                           ('previous_prompt_address', 0x62d67)):
            with self.subTest(key=key), self.assertRaisesRegex(
                    ValueError, 'state differs|table tail differ'):
                verify_terminal(completed_lines(), {**TERMINAL_STATE, key: value})

    def test_native_player_move_must_be_two_verified_service_writes(self):
        lines = [line.replace('before=bc02 value=9b02', 'before=be02 value=9b02')
                 for line in completed_lines()]
        with self.assertRaisesRegex(ValueError, 'two original player-location'):
            verify_terminal(lines, TERMINAL_STATE)

    def test_native_auxiliary_and_response_entries_are_required(self):
        for pc in ('pc=4c194', 'pc=4bab5'):
            lines = [line for line in completed_lines() if pc not in line]
            with self.subTest(pc=pc), self.assertRaisesRegex(ValueError, 'response/completion'):
                verify_terminal(lines, TERMINAL_STATE)

    def test_refusal_state_and_flag_write_rejected_if_wrong(self):
        lines = [line.replace('pc=4bb7c', 'pc=4bb7d') for line in refusal_lines()]
        with self.assertRaisesRegex(ValueError, 'write order'):
            verify_reminder(lines, REMINDER_STATE)
        with self.assertRaisesRegex(ValueError, 'final state'):
            verify_reminder(refusal_lines(), {**REMINDER_STATE, 'reminder': 0})

    def test_replay_never_allows_pokes(self):
        for verifier, lines, state in ((verify_reminder, refusal_lines(), REMINDER_STATE),
                                       (verify_terminal, completed_lines(), TERMINAL_STATE)):
            with self.assertRaisesRegex(ValueError, 'must not poke'):
                verifier(lines + ['research-poke16 ms=73000'], state)

    def test_pointer_based_snapshot_reads_terminal_table(self):
        ds = 0x1000
        memory = bytearray(0xa0000)
        for pointer in (0x967a, 0x967c, 0x9662):
            struct.pack_into('<H', memory, ds * 16 + pointer, 0x5b46)
        struct.pack_into('<H', memory, ds * 16 + 0x9652, 0x2000)
        memory[0x62d66:0x62d6a] = b'\x01\x00\x01\x00'
        memory[0x62d69] = 0
        memory[0x20000 + 0x2f8] = 0x84
        struct.pack_into('<HH', memory, 0x20000 + 0x3f2, 0x40, 0x2100)
        memory[0x21040:0x21046] = b'sleep\0'
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'native'
            prefix.with_suffix('.bin').write_bytes(memory)
            with patch('inspect_study_response.inspect_study_snapshot', return_value={
                    'native_data_segment': ds, 'study_counter': 1,
                    'study_counter_address': 0x62d64}):
                result = inspect_response_snapshot(prefix)
                self.assertEqual(result['terminal_prompt'], 'sleep')
                self.assertEqual(result['terminal_action'], 0x84)
                self.assertEqual(result['previous_prompt'], 1)


if __name__ == '__main__':
    unittest.main()

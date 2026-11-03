# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-free tests for the natural/controlled Study callback verifier."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_study_up import verify_pair


def move(ms, old, new):
    return [
        f'object-location-set ms={ms} object=d3 location={new:x} changed=1 '
        'return=4319:16e7',
        f'object-write ms={ms} object=d3 pc=11c32 cs=1157 ip=6c2 offset=c size=2 '
        f'before={old.to_bytes(2,"little").hex()} '
        f'value={new.to_bytes(2,"little").hex()}',
    ]


def callback(start, end, movement=()):
    return [
        f'entry ms={start} pc=15077 cs=148e room_callback_stage=scanner-call '
        'room_callback_id=2b room_callback_event=f',
        f'entry ms={start} pc=14759 cs=1474 room_callback_stage=gate-call '
        'room_callback_id=2b room_callback_event=f '
        'room_callback_target=2c24:13f3 room_callback_section=32',
        f'entry ms={start} pc=49bec cs=4990 ax=f bx=0',
        *movement,
        f'entry ms={end} pc=1475e cs=1474 room_callback_stage=gate-return '
        'room_callback_result=0',
        f'entry ms={end} pc=1507c cs=148e ax=0 '
        'room_callback_stage=scanner-return room_callback_result=0',
    ]


class StudyUpTests(unittest.TestCase):
    def setUp(self):
        setup = (['object-watch ms=100 object=d3 type=7 at=36f9:0 size=20'] +
                 move(60674, 0x29b, 0x2bc) + move(63801, 0x2bc, 0x2be))
        self.natural = setup + callback(75116, 75163)
        self.controlled = (setup + [
            'byte-write ms=72434 address=62d64 pc=4bac7 cs=4990 '
            'size=2 before=0 value=1',
            'research-poke16 ms=74000 address=62d64 before=1 value=3',
        ] + callback(75116, 75140, move(75140, 0x2be, 0x2bc)) + [
            'byte-write ms=75141 address=62d64 pc=4bc21 cs=4990 '
            'size=2 before=3 value=4',
        ] + callback(78116, 78137))
        self.natural_state = dict(study_counter=1, study_counter_address=0x62d64)
        self.controlled_state = dict(study_counter=4, study_counter_address=0x62d64,
                                     player_location=0x2bc, player_enclosing_room=0x2bc)

    def verify(self):
        return verify_pair(self.natural, self.controlled,
                           self.natural_state, self.controlled_state)

    def test_zero_ax_can_coexist_with_movement_in_controlled_path(self):
        report = self.verify()
        self.assertEqual(report['trampoline'], '2c24:13f3')
        self.assertEqual(report['natural_up_ms'], 75163)
        self.assertEqual(report['native_move_ms'], 75140)
        self.assertEqual(report['direct_up_ms'], 78137)
        self.assertEqual(report['returned_ax'], 0)

    def test_unmodified_replay_rejects_poke(self):
        self.natural.append('research-poke16 ms=74000 address=62d64 before=1 value=3')
        with self.assertRaisesRegex(ValueError, 'natural Study replay includes'):
            self.verify()

    def test_poke_must_match_one_exact_expected_word(self):
        self.controlled = [line.replace('before=1 value=3', 'before=0 value=3')
                           for line in self.controlled]
        with self.assertRaisesRegex(ValueError, 'exactly one audited'):
            self.verify()
        self.controlled.append('research-poke16 ms=74001 address=62d64 before=3 value=4')
        with self.assertRaisesRegex(ValueError, 'exactly one audited'):
            self.verify()

    def test_counter_initialization_must_be_native(self):
        self.controlled = [line for line in self.controlled
                           if 'pc=4bac7 ' not in line]
        with self.assertRaisesRegex(ValueError, 'counter initialization write'):
            self.verify()

    def test_original_callback_entry_and_ax_must_match(self):
        self.natural = [line for line in self.natural if 'pc=49bec ' not in line]
        with self.assertRaisesRegex(ValueError, 'did not execute the original'):
            self.verify()

    def test_ineligible_study_up_must_not_move(self):
        self.natural.extend(move(75140, 0x2be, 0x2bc))
        with self.assertRaisesRegex(ValueError, 'natural Study UP moved'):
            self.verify()

    def test_a_prior_couch_visit_is_not_enough_if_player_left(self):
        self.natural.extend(move(70000, 0x2be, 0x2bc))
        with self.assertRaisesRegex(ValueError, 'did not remain on the Study couch'):
            self.verify()

    def test_controlled_study_move_must_be_watched(self):
        self.controlled = [line for line in self.controlled if 'ms=75140 object=d3 ' not in line]
        with self.assertRaisesRegex(ValueError, 'lacks watched 0x2be'):
            self.verify()

    def test_direct_study_up_is_not_another_move(self):
        self.controlled.extend(move(78117, 0x2bc, 0x2be))
        with self.assertRaisesRegex(ValueError, 'direct Study UP unexpectedly moved'):
            self.verify()

    def test_direct_room_callback_requires_uninterrupted_study_location(self):
        self.controlled.extend(move(77000, 0x2bc, 0x29b))
        with self.assertRaisesRegex(ValueError, 'left Study between controlled'):
            self.verify()

    def test_final_counter_and_room_prove_positive_branch(self):
        self.controlled_state['study_counter'] = 3
        with self.assertRaisesRegex(ValueError, 'Study counter snapshot differs'):
            self.verify()
        self.controlled_state['study_counter'] = 4
        self.controlled_state['player_location'] = 0x2be
        with self.assertRaisesRegex(ValueError, 'did not finish in Study'):
            self.verify()


if __name__ == '__main__':
    unittest.main()

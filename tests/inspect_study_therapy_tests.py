#!/usr/bin/env python3
"""Asset-independent checks of all 19 native Study phases and DOWN gating."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from inspect_study_therapy import verify_pair


def move(ms, old, new):
    return [
        (ms, f'object-location-set ms={ms} object=d3 location={new:x} '
         'changed=1 return=4319:16e7'),
        (ms, f'object-write ms={ms} object=d3 pc=11c32 offset=c size=2 '
         f'before={old.to_bytes(2,"little").hex()} '
         f'value={new.to_bytes(2,"little").hex()}'),
    ]


def callback(ms, callback_id, movement=()):
    pointer = '2c24:13f3' if callback_id == 0x2b else '2c24:156f'
    entry = 0x49bec if callback_id == 0x2b else 0x49d7d
    return [
        (ms, f'entry ms={ms} pc=15077 cs=148e room_callback_stage=scanner-call '
         f'room_callback_id={callback_id:x} room_callback_event=f'),
        (ms, f'entry ms={ms} pc=14759 cs=1474 room_callback_stage=gate-call '
         f'room_callback_id={callback_id:x} room_callback_event=f '
         f'room_callback_target={pointer} room_callback_section=32'),
        (ms + 1, f'entry ms={ms + 1} pc={entry:x} cs=4990 ax=f bx=0'),
        *movement,
        (ms + 90, f'entry ms={ms + 90} pc=1475e cs=1474 '
         'room_callback_stage=gate-return room_callback_result=0'),
        (ms + 90, f'entry ms={ms + 90} pc=1507c cs=148e ax=0 '
         'room_callback_stage=scanner-return room_callback_result=0'),
    ]


def example(blocked):
    stages = 13 if blocked else 19
    events = [(10, 'object-watch ms=10 object=d3 type=7 at=36f9:0 size=20')]
    events += move(60674, 0x29b, 0x2bc) + move(63801, 0x2bc, 0x2be)
    events.append((72434, 'byte-write ms=72434 address=62d64 pc=4bac7 '
                          'size=2 before=0 value=1'))
    for stage in range(1, stages + 1):
        slot = 18 + stage + (1 if blocked and stage >= 10 else 0)
        ms = 18000 + slot * 3000 + 200
        events.append((ms, f'byte-write ms={ms} address=62d64 pc=4bc21 '
                           f'size=2 before={stage:x} value={stage + 1:x}'))
    events += callback(81100, 0x2b, move(81150, 0x2be, 0x2bc))
    events += move(99425, 0x2bc, 0x2bf)
    if blocked:
        events += callback(102100, 0x05)
    down_time = 114100 if blocked else 111100
    events += callback(down_time, 0x05, move(down_time + 50, 0x2bf, 0x2bc))
    if not blocked:
        events += move(129428, 0x2bc, 0x2be)
        events += callback(135100, 0x2b)
    # Keep the trace's order: a scanner call before its native service and
    # the matching scanner return, with stable order within each millisecond.
    return [line for _, line in sorted(events, key=lambda pair: pair[0])]


class StudyTherapyTests(unittest.TestCase):
    def setUp(self):
        self.full = example(False)
        self.gate = example(True)
        self.full_state = dict(study_counter=20, study_counter_address=0x62d64,
                               player_location=0x2be, player_enclosing_room=0x2bc)
        self.gate_state = dict(study_counter=14, study_counter_address=0x62d64,
                               player_location=0x2bc, player_enclosing_room=0x2bc)

    def verify(self):
        return verify_pair(self.full, self.gate, self.full_state, self.gate_state)

    def test_all_nineteen_stages_and_both_desk_branches(self):
        result = self.verify()
        self.assertEqual(result['full']['stages'], 19)
        self.assertEqual(result['full']['counter'], 20)
        self.assertEqual(result['gate']['stages'], 13)
        self.assertEqual(result['gate']['descend_ms'], 114150)

    def test_no_guest_state_pokes(self):
        self.full.append('research-poke16 ms=74000 address=62d64 before=1 value=3')
        with self.assertRaisesRegex(ValueError, 'unmodified native gameplay'):
            self.verify()

    def test_real_initialization_required(self):
        self.full = [line for line in self.full if 'pc=4bac7 ' not in line]
        with self.assertRaisesRegex(ValueError, 'initial 0 -> 1'):
            self.verify()

    def test_every_successive_increment_required(self):
        self.full = [line for line in self.full if 'before=8 value=9' not in line]
        with self.assertRaisesRegex(ValueError, 'stage increments'):
            self.verify()

    def test_no_skipped_counter_value(self):
        self.full = [line.replace('before=8 value=9', 'before=8 value=a')
                     for line in self.full]
        with self.assertRaisesRegex(ValueError, 'stage 8 counter'):
            self.verify()

    def test_increment_must_belong_to_expected_action_slot(self):
        self.full = [line.replace('ms=99200', 'ms=102200') for line in self.full]
        with self.assertRaisesRegex(ValueError, 'stage 9 counter'):
            self.verify()

    def test_unaccounted_native_state_write_rejected(self):
        self.full.append('byte-write ms=132000 address=62d64 pc=4ceff '
                         'size=2 before=14 value=15')
        with self.assertRaisesRegex(ValueError, 'unaccounted native Study counter'):
            self.verify()

    def test_missing_original_desk_down_entry(self):
        self.gate = [line for line in self.gate if 'pc=49d7d ' not in line]
        with self.assertRaisesRegex(ValueError, 'original event entry missing'):
            self.verify()

    def test_wrong_callback_ax_is_not_a_room_move(self):
        self.gate = [line.replace('ax=0 room_callback_stage=scanner-return',
                                  'ax=1 room_callback_stage=scanner-return')
                     for line in self.gate]
        with self.assertRaisesRegex(ValueError, 'return AX disagree'):
            self.verify()

    def test_early_down_cannot_leave_desk(self):
        self.gate.extend(line for _, line in move(102150, 0x2bf, 0x2bc))
        with self.assertRaisesRegex(ValueError, 'early Study desk DOWN'):
            self.verify()

    def test_successful_desk_down_requires_watched_write(self):
        self.gate = [line for line in self.gate
                     if 'ms=114150 object=d3' not in line]
        with self.assertRaisesRegex(ValueError, 'missing watched native player move'):
            self.verify()

    def test_final_up_after_stage_nineteen_must_be_blocked(self):
        self.full.extend(line for _, line in move(135150, 0x2be, 0x2bc))
        with self.assertRaisesRegex(ValueError, 'final Study UP'):
            self.verify()

    def test_snapshot_counter_and_location_required(self):
        self.full_state['study_counter'] = 19
        with self.assertRaisesRegex(ValueError, 'final counter/player'):
            self.verify()


if __name__ == '__main__':
    unittest.main()

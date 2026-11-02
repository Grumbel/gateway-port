#!/usr/bin/env python3
"""Asset-independent checks for the native VR-return branch verifier."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_vr_return import verify_beach_reward, verify_study_return


def entry(ms, pc, ax=1):
    return 'entry ms=%d pc=%x cs=4990 ax=%x' % (ms, pc, ax)


def move(ms, old, new):
    return [
        'object-location-set ms=%d object=d3 location=%x changed=1 return=4319:16e7' %
        (ms, new),
        ('object-write ms=%d object=d3 pc=11c32 offset=c size=2 '
         'before=%s value=%s' % (ms, old.to_bytes(2, 'little').hex(),
                                 new.to_bytes(2, 'little').hex())),
    ]


def study_lines(resumed):
    result = [
        'object-watch object=d3 type=7',
        'byte-write ms=3130 address=62d5a before=0 value=0',
        'byte-write ms=3130 address=62d80 before=0 value=0',
        'byte-write ms=3130 address=62d81 before=0 value=0',
        'byte-write ms=3130 address=62d87 before=0 value=0',
        entry(81041, 0x4c194),
        entry(81156, 0x4c1c0),
        entry(81156, 0x4c213),
        entry(81156, 0x4c2ae),
        entry(81201, 0x4c2d9),
        'object-location-set ms=81201 object=e4 location=0 changed=1',
        entry(81201, 0x4c2e9),
        'object-flag-set ms=81201 object=165 property=b changed=1',
        entry(81201, 0x4c357),
    ]
    result.extend(move(81201, 0x2be, 0x2bc))
    result.extend(move(81201, 0x2bc, 0x29b))
    if resumed:
        result.append(entry(84196, 0x4c36b))
        result.extend(move(84197, 0x29b, 0x2a1))
        result.append(entry(84197, 0x4c377))
    return result


def study_state(resumed, progress=1):
    return dict(study_counter=progress, beach_success_byte_return_view=0,
                matched=0 if progress == 1 else 1,
                result_word_7920=0, result_seen_7927=0,
                no_result_delay_44e4=0x38, cleanup_marker_3942=1,
                player_enclosing_room=0x29b, player_location=0x2a1 if resumed else 0x29b)


def beach_lines():
    return [
        'byte-write ms=109251 address=62d5a pc=4a908 before=0 value=1',
        entry(109251, 0x4c194),
        'object-location-set ms=111159 object=2a9 location=29b changed=1',
        'object-flag-clear ms=111159 object=2a9 property=8 changed=1',
    ]


class VrReturnTests(unittest.TestCase):
    def test_paused_vs_resumed_study_frames(self):
        paused = verify_study_return(study_lines(False), study_state(False),
                                     resumed=False, progress=1)
        self.assertIsNone(paused['resume_ms'])
        self.assertEqual(paused['transfer_ms'], 81201)
        resumed = verify_study_return(study_lines(True), study_state(True),
                                      resumed=True, progress=1)
        self.assertEqual(resumed['resume_ms'], 84196)
        self.assertGreater(resumed['resume_ms'], resumed['transfer_ms'])

    def test_refuses_frame_mistakenly_called_completed_or_paused(self):
        with self.assertRaisesRegex(ValueError, 'did not resume'):
            verify_study_return(study_lines(False), study_state(True),
                                resumed=True, progress=1)
        with self.assertRaisesRegex(ValueError, 'unexpectedly executed'):
            verify_study_return(study_lines(True), study_state(False),
                                resumed=False, progress=1)

    def test_study_branch_inputs_and_guard_are_checked(self):
        for key, value in (('study_counter', 20), ('result_word_7920', 1),
                           ('beach_success_byte_return_view', 1),
                           ('result_seen_7927', 1), ('matched', 1),
                           ('player_enclosing_room', 0x2bc)):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'snapshot differs'):
                verify_study_return(study_lines(True),
                                    {**study_state(True), key: value},
                                    resumed=True, progress=1)

        for key, value in (('no_result_delay_44e4', 0),
                           ('cleanup_marker_3942', 0)):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'timer/cleanup'):
                verify_study_return(study_lines(False),
                                    {**study_state(False), key: value},
                                    resumed=False, progress=1)

    def test_changed_result_byte_is_not_a_study_no_result_replay(self):
        lines = study_lines(True)
        lines.insert(5, 'byte-write ms=76000 address=62d80 before=0 value=7')
        with self.assertRaisesRegex(ValueError, 'result guard changed'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)

    def test_required_watches_and_original_branch_entries(self):
        lines = [line for line in study_lines(True) if 'address=62d87' not in line]
        with self.assertRaisesRegex(ValueError, 'not all watched'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = [line for line in study_lines(True) if 'pc=4c2ae' not in line]
        with self.assertRaisesRegex(ValueError, 'instruction 4c2ae'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = [line.replace('pc=4c1c0 cs=4990 ax=1', 'pc=4c1c0 cs=4990 ax=0')
                 for line in study_lines(True)]
        with self.assertRaisesRegex(ValueError, 'guard did not return true'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = study_lines(True)
        i, j = next(i for i, s in enumerate(lines) if 'pc=4c213' in s), \
               next(i for i, s in enumerate(lines) if 'pc=4c2ae' in s)
        lines[i], lines[j] = lines[j], lines[i]
        with self.assertRaisesRegex(ValueError, 'out of order'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)

    def test_player_moves_and_shared_cleanup_required(self):
        lines = [line for line in study_lines(True) if 'before=bc02 value=9b02' not in line]
        with self.assertRaisesRegex(ValueError, 'couch -> room -> Terminal'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = study_lines(True)
        i = next(i for i, s in enumerate(lines) if 'before=be02 value=bc02' in s)
        j = next(i for i, s in enumerate(lines) if 'before=bc02 value=9b02' in s)
        # Reversing the pair in one timestamp must not be accepted.
        lines[i-1:i+1], lines[j-1:j+1] = lines[j-1:j+1], lines[i-1:i+1]
        with self.assertRaisesRegex(ValueError, 'couch -> room -> Terminal'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = [line for line in study_lines(True) if 'object=e4' not in line]
        with self.assertRaisesRegex(ValueError, 'temporary object cleanup'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = [line for line in study_lines(True) if 'property=b' not in line]
        with self.assertRaisesRegex(ValueError, 'wall-property setter'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)
        lines = [line for line in study_lines(True) if 'before=9b02 value=a102' not in line]
        with self.assertRaisesRegex(ValueError, 'couch move'):
            verify_study_return(lines, study_state(True), resumed=True, progress=1)

    def test_research_pokes_are_explicitly_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unmodified'):
            verify_study_return(study_lines(True) + ['research-poke16 ms=73000'],
                                study_state(True), resumed=True, progress=1)
        with self.assertRaisesRegex(ValueError, 'unmodified'):
            verify_beach_reward(beach_lines() + ['research-poke16 ms=107000'],
                                dict(beach_success_byte_return_view=1, pin_location=0x29b))

    def test_original_beach_reward_order_and_guard(self):
        state = dict(beach_success_byte_return_view=1, pin_location=0x29b)
        self.assertEqual(verify_beach_reward(beach_lines(), state)['pin_ms'], 111159)
        with self.assertRaisesRegex(ValueError, 'result or pin location'):
            verify_beach_reward(beach_lines(), {**state, 'pin_location': 0x2a3})
        lines = [line for line in beach_lines() if 'property=8' not in line]
        with self.assertRaisesRegex(ValueError, 'pin visibility cleared'):
            verify_beach_reward(lines, state)
        lines = beach_lines()
        lines[2], lines[3] = lines[3], lines[2]
        with self.assertRaisesRegex(ValueError, 'pin moved to Terminal|pin visibility cleared'):
            verify_beach_reward(lines, state)


if __name__ == '__main__':
    unittest.main()

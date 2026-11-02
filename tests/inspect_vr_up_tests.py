"""Asset-independent negative and positive tests for original VR UP replays."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_vr_up import verify_pair


def move(ms, old, new):
    return [
        f'object-location-set ms={ms} object=d3 location={new:x} '
        'changed=1 return=4319:16e7',
        f'object-write ms={ms} object=d3 pc=11c32 cs=1157 ip=6c2 '
        f'offset=c size=2 before={old.to_bytes(2,"little").hex()} '
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


class VrUpTests(unittest.TestCase):
    def setUp(self):
        watch = ['object-watch ms=10 object=d3 type=7 at=36f9:0 size=20']
        self.terminal = (watch + move(100, 0x29b, 0x2a1) +
                         ['object-location-set ms=200 object=2a8 location=d3 changed=1'] +
                         callback(300, 330) +
                         ['object-location-set ms=400 object=2a8 location=2a1 changed=1'] +
                         callback(500, 530, move(520, 0x2a1, 0x29b)))
        self.beach = (watch + move(100, 0x2ae, 0x2b1) +
                      callback(300, 330, move(320, 0x2b1, 0x2ae)) +
                      callback(500, 530))
        self.terminal_state = dict(player_location=0x29b, player_enclosing_room=0x29b)
        self.beach_state = dict(player_location=0x2ae, player_enclosing_room=0x2ae)

    def verify(self):
        return verify_pair(self.terminal, self.beach,
                           self.terminal_state, self.beach_state)

    def test_two_rooms_share_callback_but_not_get_up_side_effects(self):
        report = self.verify()
        self.assertEqual(report['section'], 50)
        self.assertEqual(report['trampoline'], '2c24:13f3')
        self.assertEqual(report['collar_blocked_ms'], 330)
        self.assertEqual(report['terminal_rise_ms'], 520)
        self.assertEqual(report['beach_rise_ms'], 320)
        self.assertEqual(report['beach_direct_ms'], 530)
        self.assertEqual(report['returned_ax'], 0)

    def test_poke_is_rejected(self):
        self.beach.append('research-poke16 ms=100 address=1234 value=1')
        with self.assertRaisesRegex(ValueError, 'modified original guest memory'):
            self.verify()

    def test_missing_original_event_entry_is_rejected(self):
        self.terminal = [line for line in self.terminal if 'pc=49bec ' not in line]
        with self.assertRaisesRegex(ValueError, 'entry not observed'):
            self.verify()

    def test_nonzero_return_is_rejected(self):
        self.beach = [line.replace('ax=0 room_callback_stage=scanner-return',
                                    'ax=1 room_callback_stage=scanner-return')
                      for line in self.beach]
        with self.assertRaisesRegex(ValueError, 'return AX disagree'):
            self.verify()

    def test_collared_player_cannot_move(self):
        self.terminal[0:0] = move(310, 0x2a1, 0x29b)
        with self.assertRaisesRegex(ValueError, 'equipped VR Terminal UP unexpectedly moved'):
            self.verify()

    def test_missing_collar_removal_is_rejected(self):
        self.terminal = [line for line in self.terminal
                         if 'ms=400 object=2a8 ' not in line]
        with self.assertRaisesRegex(ValueError, 'collar was not removed'):
            self.verify()

    def test_missing_beach_get_up_write_is_rejected(self):
        self.beach = [line for line in self.beach if 'ms=320 ' not in line]
        with self.assertRaisesRegex(ValueError, 'nested Beach UP requires one watched'):
            self.verify()

    def test_direct_beach_move_is_rejected(self):
        self.beach.extend(move(510, 0x2ae, 0x2b1))
        with self.assertRaisesRegex(ValueError, 'direct Beach UP unexpectedly moved'):
            self.verify()

    def test_final_snapshot_must_match(self):
        self.terminal_state['player_enclosing_room'] = 0x2a1
        with self.assertRaisesRegex(ValueError, 'final native player_enclosing_room'):
            self.verify()


if __name__ == '__main__':
    unittest.main()

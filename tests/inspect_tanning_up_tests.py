"""Asset-independent checks for the paired, unmodified native Tanning callback."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_tanning_up import verify_pair


def callback(ms, returned):
    return [
        f'entry ms={ms} pc=15077 cs=148e room_callback_stage=scanner-call '
        'room_callback_id=2b room_callback_event=f',
        f'entry ms={ms} pc=14759 cs=1474 room_callback_stage=gate-call '
        'room_callback_id=2b room_callback_event=f '
        'room_callback_target=2c24:13f3 room_callback_section=32',
        f'entry ms={ms} pc=49bec cs=4990 ds=5b46',
        f'entry ms={returned} pc=1475e cs=1474 room_callback_stage=gate-return '
        'room_callback_result=0',
        f'entry ms={returned} pc=1507c cs=148e ax=0 '
        'room_callback_stage=scanner-return room_callback_result=0',
    ]


def move(ms, old, new):
    return [
        f'object-location-set ms={ms} object=d3 location={new:x} '
        'changed=1 return=4319:16e7',
        f'object-write ms={ms} object=d3 pc=11c32 cs=1157 ip=6c2 '
        f'offset=c size=2 before={old.to_bytes(2,"little").hex()} '
        f'value={new.to_bytes(2,"little").hex()}',
    ]


class TanningUpTests(unittest.TestCase):
    def setUp(self):
        self.state = dict(player_location=0x29a, player_enclosing_room=0x29a,
                          pin_carried=True, beach_success_byte=1,
                          drink_refresh_counter=5)
        self.standing = callback(138116, 138116)
        self.reclining = (['object-watch ms=100 object=d3 type=7 at=36f9:c size=20'] +
                          move(138428, 0x29a, 0x29c) + callback(141116, 141139))
        # The location setter executes inside the callback, before its AX=0 return.
        self.reclining = (self.reclining[:-2] + move(141139, 0x29c, 0x29a) +
                          self.reclining[-2:])

    def verify(self):
        return verify_pair(self.standing, self.reclining, self.state, self.state)

    def test_reclining_movement_is_distinct_from_callback_destination(self):
        report = self.verify()
        self.assertEqual(report['callback_id'], 0x2b)
        self.assertEqual(report['section'], 50)
        self.assertEqual(report['returned_ax'], 0)
        self.assertEqual(report['lie_ms'], 138428)
        self.assertEqual(report['native_get_up_ms'], 141139)

    def test_rejects_memory_poke_and_unmatched_original_handler(self):
        self.standing.append('research-poke16 ms=110000 address=100 before=0 value=1')
        with self.assertRaisesRegex(ValueError, 'modified original guest memory'):
            self.verify()
        self.standing.pop()
        self.standing = [line for line in self.standing if 'pc=49bec ' not in line]
        with self.assertRaisesRegex(ValueError, 'callback entry not observed'):
            self.verify()

    def test_rejects_changed_standing_room_and_missing_recline_write(self):
        self.standing.append('object-location-set ms=138117 object=d3 location=f6 '
                             'changed=1 return=4319:16e7')
        with self.assertRaisesRegex(ValueError, 'standing UP unexpectedly changed'):
            self.verify()
        self.standing.pop()
        self.reclining = [line for line in self.reclining if
                          'offset=c size=2 before=9c02 value=9a02' not in line]
        with self.assertRaisesRegex(ValueError, 'missing watched native couch-entry'):
            self.verify()

    def test_rejects_false_success_state(self):
        bad = dict(self.state, pin_carried=False)
        with self.assertRaisesRegex(ValueError, 'pin_carried'):
            verify_pair(self.standing, self.reclining, bad, self.state)


if __name__ == '__main__':
    unittest.main()

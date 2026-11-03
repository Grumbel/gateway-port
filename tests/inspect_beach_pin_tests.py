# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-independent checks for native Beach/VR state evidence."""

import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_beach_pin import (event_millis, snapshot_objects, status_at,
                               verify_beach_casino_chain)


class BeachPinTests(unittest.TestCase):
    def setUp(self):
        self.memory = bytearray(0xa0000)
        self.ds = 0x1000
        for pointer in (0x9644, 0x969c, 0x962a):
            struct.pack_into('<H', self.memory, self.ds * 16 + pointer, self.ds)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x78f6, 4)
        self.objects = [dict(object=i, relation_links=[location]) for i, location in
                        ((0xd3, 0x29b), (0x2a3, 0x2a0), (0x2a9, 0x2a3),
                         (0x2b4, 0x2b2))]
        self.objects.extend([dict(object=0x29b, type=1, relation_links=[]),
                             dict(object=0x2a1, type=2, relation_links=[0x29b])])

    def test_initial_reward_state_and_successful_transition(self):
        original = status_at(self.memory, self.ds, self.objects)
        self.assertEqual(original['beach_success_byte'], 0)
        self.assertEqual(original['drink_refresh_counter'], 4)
        self.assertFalse(original['pin_carried'])
        self.assertEqual(original['player_enclosing_room'], 0x29b)
        self.memory[self.ds * 16 + 0x78fa] = 1
        self.objects[2]['relation_links'] = [0x29b]
        awarded = status_at(self.memory, self.ds, self.objects)
        self.assertTrue(awarded['pin_on_terminal_floor'])
        self.assertEqual(awarded['beach_success_byte_return_view'], 1)
        self.objects[2]['relation_links'] = [0xd3]
        self.assertTrue(status_at(self.memory, self.ds, self.objects)['pin_carried'])

    def test_player_can_be_reclining_in_couch_inside_a_different_room(self):
        self.objects[0]['relation_links'] = [0x2a1]
        state = status_at(self.memory, self.ds, self.objects)
        self.assertEqual(state['player_location'], 0x2a1)
        self.assertEqual(state['player_enclosing_room'], 0x29b)
        self.objects[-1]['relation_links'] = [0x2a1]
        self.assertIsNone(status_at(self.memory, self.ds, self.objects)['player_enclosing_room'])

    def test_rejects_missing_pointer_and_ambiguous_native_relation(self):
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x9644, 0)
        with self.assertRaisesRegex(ValueError, 'state segment pointer'):
            status_at(self.memory, self.ds, self.objects)
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x9644, self.ds)
        self.objects[2]['relation_links'] = [0x2a3, 0xd3]
        with self.assertRaisesRegex(ValueError, 'no unique native location'):
            status_at(self.memory, self.ds, self.objects)

    def test_physical_address_startup_writes_have_distinct_timebase(self):
        self.assertEqual(event_millis('byte-write ms=3130 address=62d5a pc=6a841'), 3130)
        self.assertEqual(event_millis('byte-write ms=62000 address=62d5a pc=4a000'), 62000)
        with self.assertRaisesRegex(ValueError, 'malformed'):
            event_millis('byte-write address=62d5a')

    def test_snapshot_recovers_only_the_unique_observed_original_dispatch_ds(self):
        record = 'entry ms=21176 pc=147a6 cs=1476 ds=5b46 es=4cde ax=98'
        with patch('inspect_beach_pin.inspect_objects', side_effect=[ValueError('invalid'),
                                                                     self.objects]) as inspect:
            ds, objects = snapshot_objects(self.memory, 0x2c24, [record, record])
        self.assertEqual(ds, 0x5b46)
        self.assertEqual(objects, self.objects)
        self.assertEqual([call.args[1] for call in inspect.call_args_list],
                         [0x2c24, 0x5b46])
        with patch('inspect_beach_pin.inspect_objects', side_effect=ValueError('invalid')):
            with self.assertRaisesRegex(ValueError, 'no unique native dispatcher'):
                snapshot_objects(self.memory, 0x2c24, [record,
                    'entry ms=24176 pc=147a6 cs=1476 ds=4a00 es=4cde ax=98'])

    def test_native_reward_to_casino_chain_requires_original_ordered_events(self):
        self.memory[self.ds * 16 + 0x78fa] = 1
        struct.pack_into('<H', self.memory, self.ds * 16 + 0x78f6, 5)
        self.objects[0]['relation_links'] = [0x0f6]
        self.objects.append(dict(object=0x0f6, type=1, relation_links=[]))
        self.objects[2]['relation_links'] = [0xd3]
        state = status_at(self.memory, self.ds, self.objects)
        lines = [
            'byte-write ms=109250 address=62d5a pc=4a908 before=0 value=1',
            'object-write ms=111158 object=2a9 offset=c size=2 before=a302 value=9b02',
            'object-write ms=117678 object=2a9 offset=c size=2 before=9b02 value=d300',
            'entry ms=132176 room_callback_stage=scanner-call room_callback_id=14 room_callback_event=f',
            'entry ms=132268 room_callback_stage=scanner-return room_callback_result=f6',
            'object-location-set ms=132268 object=d3 location=f6 changed=1',
        ]
        actual = verify_beach_casino_chain(state, lines)
        self.assertEqual(list(actual.values()), [109250, 111158, 117678,
                                                 132176, 132268, 132268])
        with self.assertRaisesRegex(ValueError, 'missing ordered.*pin pickup'):
            verify_beach_casino_chain(state, lines[2:3] + lines[:2] + lines[3:])
        with self.assertRaisesRegex(ValueError, 'intervention'):
            verify_beach_casino_chain(state, lines + ['research-poke16 ms=40000'])
        state['pin_carried'] = False
        with self.assertRaisesRegex(ValueError, 'pin_carried'):
            verify_beach_casino_chain(state, lines)


if __name__ == '__main__':
    unittest.main()

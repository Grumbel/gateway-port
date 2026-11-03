# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-independent verification of controlled room-callback trace comparisons."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from compare_room_callback_traces import compare_traces, parse_poke


def call(ms, ax):
    return [
        f'entry ms={ms-70} pc=15077 cs=148e room_callback_stage=scanner-call '
        'room_callback_id=14 room_callback_event=f',
        f'entry ms={ms-70} pc=14759 cs=1474 room_callback_stage=gate-call '
        'room_callback_id=14 room_callback_event=f '
        'room_callback_target=2c24:14d9 room_callback_section=30',
        f'entry ms={ms} pc=1475e cs=1474 room_callback_stage=gate-return '
        f'room_callback_result={ax:x}',
        f'entry ms={ms} pc=1507c cs=148e ax={ax:x} '
        'room_callback_stage=scanner-return',
    ]


def move(ms, old, new):
    return [f'object-location-set ms={ms} object=d3 location={new:x} '
            'changed=1 return=4319:16e7',
            f'object-write ms={ms} object=d3 pc=447e0 offset=c size=2 '
            f'before={old.to_bytes(2, "little").hex()} '
            f'value={new.to_bytes(2, "little").hex()}']


class RoomCallbackComparisonTests(unittest.TestCase):
    def setUp(self):
        self.poke = (27000, 0x35b24, 0x2a3, 0xd3)
        watch = 'object-watch ms=10000 object=d3 type=7 at=36f9:0 size=20'
        self.base = [watch, *move(25178, 0xf4, 0xf5), *call(28247, 0)]
        self.controlled = [watch, *move(25178, 0xf4, 0xf5),
                           'research-poke16 ms=27000 address=35b24 before=2a3 value=d3',
                           *call(28266, 0xf6), *move(28266, 0xf5, 0xf6)]

    def test_expected_two_branches_and_extra_native_move(self):
        result = compare_traces(self.base, self.controlled, 0x14, 0xf6, self.poke)
        self.assertEqual((result['baseline_ax'], result['controlled_ax']), (0, 0xf6))
        self.assertEqual((result['old_room'], result['new_room']), (0xf5, 0xf6))
        self.assertEqual((result['baseline_movements'], result['controlled_movements']),
                         (1, 2))

    def test_no_unrecorded_poke_or_undocumented_controlled_move(self):
        with self.assertRaisesRegex(ValueError, 'missing or unexpected'):
            compare_traces(self.base, [x for x in self.controlled if not
                                       x.startswith('research-poke16')], 0x14, 0xf6, self.poke)
        with self.assertRaisesRegex(ValueError, 'pre-intervention navigation'):
            compare_traces(self.base, [x.replace('before=f400 value=f500',
                                                'before=f300 value=f500')
                                       for x in self.controlled], 0x14, 0xf6, self.poke)
        with self.assertRaisesRegex(ValueError, 'baseline has a controlled'):
            compare_traces(self.base + self.controlled[3:4], self.controlled,
                           0x14, 0xf6, self.poke)

    def test_bad_native_result_and_missing_verified_player_write_rejected(self):
        with self.assertRaisesRegex(ValueError, 'AX results'):
            compare_traces(self.base, [x.replace('ax=f6', 'ax=f5').replace(
                'room_callback_result=f6', 'room_callback_result=f5')
                for x in self.controlled], 0x14, 0xf6, self.poke)
        without_write = [x for x in self.controlled if not
                         x.startswith('object-write ms=28266')]
        with self.assertRaisesRegex(ValueError, 'corroborated'):
            compare_traces(self.base, without_write, 0x14, 0xf6, self.poke)

    def test_poke_cli_parser(self):
        self.assertEqual(parse_poke('27000:35b24:02a3:00d3'), self.poke)
        for broken in ('27000:35b24:2a3', '27000:35b24:2a3:10000',
                       '27000:a0000:2a3:d3', 'nan:35b24:2a3:d3'):
            with self.assertRaises(ValueError):
                parse_poke(broken)


if __name__ == '__main__':
    unittest.main()

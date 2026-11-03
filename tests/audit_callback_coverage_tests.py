# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic native event/AX correlation, without proprietary DOS assets."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_callback_coverage import audit, format_text, parse_gate_events
from inventory_native_handlers import parse_calls


def gate(ms, event, stage, ax=0, pointer='2c24:14d9', section='30', bp='dc02'):
    result = f'entry ms={ms} pc={"14759" if stage == "gate-call" else "1475e"} '
    result += f'cs=1474 ss=5b46 bp={bp} room_callback_stage={stage}'
    if stage == 'gate-call':
        result += f' room_callback_id=14 room_callback_event={event:x} '
        result += f'room_callback_target={pointer} room_callback_section={section}'
    else:
        result += f' room_callback_result={ax:x}'
    return result


def scanner(ms, stage, ax=0):
    return (f'entry ms={ms} pc={"15077" if stage == "scanner-call" else "1507c"} '
            f'cs=148e ax={ax:x} room_callback_stage={stage} '
            + ('room_callback_id=14 room_callback_event=f' if stage == 'scanner-call'
               else f'room_callback_result={ax:x}'))


class CallbackCoverageTests(unittest.TestCase):
    def setUp(self):
        self.directory = dict(schema_version=1, table_count=44, callbacks=[
            dict(callback_id=20, status='rtlink', pointer_segment=0x2c24,
                 pointer_offset=0x14d9,
                 rtlink_reference=dict(section=48, target_segment=0x4990,
                                       target_offset=0x1a19),
                 native_code=dict(file_offset=0x665f9))])
        self.lines = [
            scanner(100, 'scanner-call'),
            gate(101, 15, 'gate-call'), gate(110, 15, 'gate-return', 0),
            scanner(111, 'scanner-return'),
            gate(120, 1, 'gate-call'), gate(121, 1, 'gate-return', 0xf6),
            gate(130, 1, 'gate-call'), gate(131, 1, 'gate-return', 0xf6),
        ]
        self.calls, self.census = parse_calls([
            'rtlink-census manager_hits=4 validated=4 unrecognized=0',
            'rtlink-call section=48 target=4990:1a19 count=4 first_ms=101 last_ms=132'])

    def test_room_exit_and_object_events_are_separate(self):
        result = audit(self.lines, self.directory, self.calls, self.census)
        summary = result['summary']
        self.assertEqual((summary['callback_gate_calls'], summary['room_scanner_returns'],
                          summary['other_callback_events']), (3, 1, 2))
        item = result['observed_callbacks'][0]
        self.assertEqual((item['rtlink_invocations'], item['verified_gate_calls'],
                          item['other_rtlink_invocations']), (4, 3, 1))
        self.assertEqual(item['event_results'], [dict(event=1, ax=0xf6, count=2),
                                                 dict(event=15, ax=0, count=1)])
        self.assertEqual(result['room_scanner_events'][0]['scanner_return_ms'], 111)
        self.assertIn('1 room exits', format_text(result))

    def test_nested_gate_calls_pair_by_frame_not_just_timestamp(self):
        lines = [gate(10, 1, 'gate-call', bp='100'),
                 gate(11, 15, 'gate-call', bp='f0'),
                 gate(12, 15, 'gate-return', ax=0, bp='f0'),
                 gate(13, 1, 'gate-return', ax=0xf6, bp='100')]
        events = parse_gate_events(lines)
        self.assertEqual([(item['event'], item['ax']) for item in events],
                         [(15, 0), (1, 0xf6)])

    def test_event_f_without_the_scanner_is_not_a_confirmed_exit(self):
        lines = [gate(101, 15, 'gate-call'), gate(110, 15, 'gate-return', 0xf6)]
        result = audit(lines, self.directory, self.calls, self.census)
        self.assertEqual(result['summary']['room_scanner_returns'], 0)
        self.assertEqual(result['summary']['other_callback_events'], 1)
        self.assertEqual(result['observed_callbacks'][0]['room_return_ax'], [])

    def test_far_pointer_annotations_compare_numerically_not_by_string_padding(self):
        lines = [line.replace('2c24:14d9', '2c24:014d9') for line in self.lines]
        self.assertEqual(audit(lines, self.directory, self.calls, self.census)
                         ['summary']['room_scanner_returns'], 1)

    def test_rejects_stale_snapshot_pointer_or_section(self):
        with self.assertRaisesRegex(ValueError, 'trampoline differs'):
            audit([x.replace('2c24:14d9', '2c24:14da') for x in self.lines],
                  self.directory, self.calls, self.census)
        with self.assertRaisesRegex(ValueError, 'trampoline differs'):
            audit([x.replace('room_callback_section=30', 'room_callback_section=31')
                   for x in self.lines], self.directory, self.calls, self.census)

    def test_rejects_missing_gate_return_or_scanner_disagreement(self):
        with self.assertRaisesRegex(ValueError, 'unreturned'):
            audit(self.lines[:-1], self.directory, self.calls, self.census)
        with self.assertRaisesRegex(ValueError, 'gate and scanner return AX disagree'):
            audit([scanner(111, 'scanner-return', ax=0xf6) if x ==
                   scanner(111, 'scanner-return') else x for x in self.lines],
                  self.directory, self.calls, self.census)

    def test_census_underreport_is_rejected(self):
        calls, census = parse_calls([
            'rtlink-census manager_hits=2 validated=2 unrecognized=0',
            'rtlink-call section=48 target=4990:1a19 count=2 first_ms=101 last_ms=132'])
        with self.assertRaisesRegex(ValueError, 'exceed verified RTLink'):
            audit(self.lines, self.directory, calls, census)

    def test_requires_verified_gate_provenance(self):
        with self.assertRaisesRegex(ValueError, 'no byte-verified callback'):
            audit([], self.directory, self.calls, self.census)
        with self.assertRaisesRegex(ValueError, 'lacks byte-verified'):
            parse_gate_events([gate(120, 1, 'gate-call').replace('room_callback_section=30', '')])


if __name__ == '__main__':
    unittest.main()

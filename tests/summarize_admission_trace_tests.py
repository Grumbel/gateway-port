# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic selector/dispatcher correlations, without DOS game assets."""
from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from summarize_admission_trace import collect, parse_entry, summarize  # noqa: E402


class AdmissionTraceTests(unittest.TestCase):
    def test_parser_rejects_non_entries_and_bad_memory_fields(self):
        self.assertIsNone(parse_entry('key ms=18392 value=61'))
        self.assertIsNone(parse_entry('entry ms=abc pc=14768 cs=1476 stack=0,0,46,'))
        self.assertIsNone(parse_entry('entry ms=100 pc=14768'))

    def test_correlates_selector_and_return_without_interpreting_ax(self):
        trace = [
            'entry ms=14000 pc=14768 cs=1476 admission_action=5f stack=1,2,5f,',
            'entry ms=14001 pc=147a2 cs=1476 admission_action=5f admission_return=0',
            'entry ms=18392 pc=14768 cs=1476 admission_action=71 stack=1,2,71,',
            'entry ms=18393 pc=147a2 cs=1476 admission_action=71 admission_return=0',
            'entry ms=18394 pc=147c0 cs=1476 stack=0,0,0,71,',
            'entry ms=25333 pc=14768 cs=1476 admission_action=46 stack=1,2,46,',
            'entry ms=25334 pc=14799 cs=1476 admission_action=46 '
            'admission_selector=8 admission_target=2c24:abcd',
            'entry ms=25335 pc=147a2 cs=1476 admission_action=46 admission_return=1',
            'entry ms=25336 pc=147c0 cs=1476 stack=0,0,0,46,',
        ]
        selectors, dispatches = collect(trace)
        self.assertEqual([(e['action'], e['selector'], e['returned']) for e in selectors],
                         [(0x5f, None, 0), (0x71, None, 0), (0x46, 8, 1)])
        self.assertEqual([(e['action'], e['ms']) for e in dispatches],
                         [(0x71, 18394), (0x46, 25336)])
        commands = ['open drawer', 'look', 'take book', 'inventory',
                    'drop book', 'close drawer', 'look']
        table = summarize(trace, commands)
        self.assertIn('| 18s | open drawer | no callback AX=0x0 | observed |', table)
        self.assertIn('| 25s | take book | selector=0x08 -> 2c24:abcd AX=0x1 | observed |', table)
        self.assertNotIn('14s', table)

    def test_no_final_dispatch_is_not_called_rejection(self):
        lines = [
            'entry ms=25331 pc=14768 cs=1476 admission_action=46',
            'entry ms=25332 pc=14799 cs=1476 admission_selector=8 '
            'admission_target=1234:5678',
            'entry ms=25333 pc=147a2 cs=1476 admission_return=0',
        ]
        table = summarize(lines, ['look', 'open drawer', 'take book', 'look',
                                  'look', 'look', 'inventory'])
        self.assertIn('selector=0x08 -> 1234:5678 AX=0x0 | not observed', table)

    def test_missing_snapshots_and_wrong_command_count_rejected(self):
        with self.assertRaisesRegex(ValueError, 'seven scripted commands'):
            summarize([], ['look'])
        with self.assertRaisesRegex(ValueError, 'no completed selector-stage snapshots'):
            summarize([], ['look'] * 7)


if __name__ == '__main__':
    unittest.main()

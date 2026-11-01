"""Synthetic trace associations for the Gateway shared-action research tool."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from summarize_action_effects import collect_effects, parse_entry


class ActionEffectsTests(unittest.TestCase):
    def test_open_and_close_flag_callsite_evidence(self):
        lines = [
            'entry ms=18392 pc=147c0 cs=1476 ax=2a6 '
            'stack=dc28,4da,1000,71, action_target=2c24:1c59',
            'entry ms=18393 pc=47b82 cs=4661 ax=2a6 stack=701,2c24,dc28,',
            'entry ms=18393 pc=47d44 cs=4661 ax=c stack=124,c,0,dbc2,',
            'entry ms=18394 pc=47d59 cs=4661 ax=2 stack=124,2,0,dbc2,',
            'entry ms=18417 pc=147c5 cs=1476 ax=1 stack=dc28,4da,1000,71,',
            'entry ms=34422 pc=147c0 cs=1476 ax=ae '
            'stack=dc28,4da,1000,1d, action_target=2c24:17f9',
            'entry ms=34422 pc=468ab cs=4661 ax=ae stack=701,2c24,dc28,',
            'entry ms=34443 pc=46a0c cs=4661 ax=c stack=124,c,ae,dbc2,',
            'entry ms=34443 pc=147c5 cs=1476 ax=1 stack=dc28,4da,1000,1d,',
        ]
        records = collect_effects(lines)
        self.assertEqual([(r['action'], r['target'], r['handler'], r['effects'],
                           r['returned'], r['return_ms']) for r in records], [
            (0x71, '2c24:1c59', '4661:1572',
             [(18393, 'set', 0x124, 0xc), (18394, 'set', 0x124, 2)], 1, 18417),
            (0x1d, '2c24:17f9', '4661:029b', [(34443, 'clear', 0x124, 0xc)], 1, 34443),
        ])

    def test_unmatched_callsite_or_wrong_segment_is_not_attributed(self):
        lines = [
            'entry ms=1 pc=47d44 cs=4661 stack=124,c,',
            'entry ms=2 pc=147c0 cs=1476 stack=0,0,0,71,',
            'entry ms=2 pc=46a0c cs=4661 stack=124,c,',
            'entry ms=3 pc=47d44 cs=4662 stack=124,c,',
            'entry ms=3 pc=47b82 cs=4661 stack=0,0,',
            'entry ms=4 pc=47d44 cs=4661 stack=124,2,',
            'entry ms=5 pc=147c5 cs=1476 ax=1 stack=0,0,0,71,',
        ]
        records = collect_effects(lines)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]['handler'], '4661:1572')
        self.assertEqual(records[0]['effects'], [(4, 'set', 0x124, 2)])

    def test_native_service_entries_cover_nested_helpers_without_duplicates(self):
        lines = [
            'entry ms=18 pc=147c0 cs=1476 stack=0,0,0,71,',
            'entry ms=19 pc=47b82 cs=4661 stack=0,0,',
            'entry ms=19 pc=47d44 cs=4661 stack=124,c,',
            'entry ms=19 pc=11950 cs=1157 stack=eb09,4661,124,c,',
            'entry ms=19 pc=47d59 cs=4661 stack=124,2,',
            'entry ms=19 pc=11950 cs=1157 stack=eb1e,4661,124,2,',
            'entry ms=21 pc=147c5 cs=1476 ax=1 stack=0,0,0,71,',
            'entry ms=25 pc=147c0 cs=1476 stack=0,0,0,46,',
            'entry ms=25 pc=49121 cs=4661 stack=0,0,',
            'entry ms=26 pc=11950 cs=1157 stack=beef,1157,123,2,',
            'entry ms=26 pc=11983 cs=1157 stack=beef,1157,123,8,',
            'entry ms=27 pc=11bbb cs=1157 stack=beef,1157,123,d3,',
            'entry ms=28 pc=147c5 cs=1476 ax=1 stack=0,0,0,46,',
            'entry ms=31 pc=147c0 cs=1476 stack=0,0,0,32,',
            'entry ms=31 pc=46be7 cs=4661 stack=0,0,',
            'entry ms=32 pc=11bbb cs=1157 stack=beef,1157,123,ee,',
            'entry ms=33 pc=147c5 cs=1476 ax=1 stack=0,0,0,32,',
            'entry ms=34 pc=11950 cs=1157 stack=beef,1157,123,2,',
        ]
        records = collect_effects(lines)
        self.assertEqual([(r['action'], r['handler'], r['effects']) for r in records], [
            (0x71, '4661:1572', [(19, 'set', 0x124, 0xc), (19, 'set', 0x124, 2)]),
            (0x46, '4661:2b11', [(26, 'set', 0x123, 2),
                                  (26, 'clear', 0x123, 8),
                                  (27, 'location-set', 0x123, 0xd3)]),
            (0x32, '4661:05d7', [(32, 'location-set', 0x123, 0xee)]),
        ])

    def test_wrong_service_segment_and_stack_are_ignored(self):
        lines = [
            'entry ms=1 pc=147c0 cs=1476 stack=0,0,0,53,',
            'entry ms=2 pc=47b04 cs=4661 stack=0,0,',
            'entry ms=3 pc=11950 cs=2157 stack=beef,1157,123,2,',
            'entry ms=4 pc=11950 cs=1157 stack=beef,1157,123,',
            'entry ms=5 pc=147c5 cs=1476 ax=1 stack=0,0,0,53,',
        ]
        records = collect_effects(lines)
        self.assertEqual(records[0]['handler'], '4661:14f4')
        self.assertEqual(records[0]['effects'], [])

    def test_missing_or_malformed_entry(self):
        self.assertIsNone(parse_entry('key ms=42 value=1c0d'))
        self.assertIsNone(parse_entry('entry ms=abc pc=47d44 cs=4661 stack=124,c,'))
        self.assertIsNone(parse_entry('entry ms=1 pc=47d44 cs=4661'))

    def test_command_line_summary_reports_only_traced_effects(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'native.trace'
            trace.write_text('\n'.join([
                'entry ms=18392 pc=147c0 cs=1476 ax=71 stack=dc28,4da,1000,71,',
                'entry ms=18393 pc=47b82 cs=4661 stack=701,2c24,',
                'entry ms=18393 pc=47d44 cs=4661 stack=124,c,0,',
                'entry ms=18417 pc=147c5 cs=1476 ax=1 stack=dc28,4da,1000,71,',
            ]))
            script = Path(__file__).resolve().parents[1] / 'tools' / 'summarize_action_effects.py'
            result = subprocess.run([sys.executable, str(script), str(trace)],
                                    capture_output=True, text=True, check=True)
            self.assertIn('open drawer', result.stdout)
            self.assertIn('set(0x124,0x0c)@18393', result.stdout)
            self.assertNotIn('set(0x124,0x02)', result.stdout)
            self.assertIn('not proof of changed bytes', result.stdout)


if __name__ == '__main__':
    unittest.main()

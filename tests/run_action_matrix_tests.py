# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Batch action-runner unit checks; no executable or proprietary assets needed."""
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_action_matrix import SEQUENCES, main, summarize_trace  # noqa: E402


class ActionMatrixTests(unittest.TestCase):
    def test_sequences_have_seven_distinct_action_slots(self):
        self.assertEqual(len(SEQUENCES), 8)
        for commands in SEQUENCES.values():
            self.assertEqual(len(commands), 7)

    def test_follow_up_sequences_isolate_closed_container_from_room_visibility(self):
        self.assertEqual(SEQUENCES['closed-after-reveal'][2], 'take book')
        self.assertEqual(SEQUENCES['closed-after-reveal'][1], 'close drawer')
        self.assertEqual(SEQUENCES['room-after-close'][3:5], ('drop book', 'take book'))

    def test_selector_trace_flag_reaches_the_native_driver(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'unused'
            result = subprocess.run([sys.executable, str(ROOT / 'tools' / 'run_action_matrix.py'),
                                     '--only', 'closed-after-reveal', '--trace-admission',
                                     '--dry-run', '--output', str(output)],
                                    capture_output=True, text=True, check=True)
            self.assertIn('--trace-admission', result.stdout)
            self.assertFalse(output.exists())

    def test_dry_run_shows_commands_without_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'untouched'
            result = subprocess.run([sys.executable, str(ROOT / 'tools' / 'run_action_matrix.py'),
                                     '--only', 'repeat,open-twice', '--dry-run',
                                     '--output', str(output)], capture_output=True,
                                    text=True, check=True)
            self.assertEqual(len(result.stdout.splitlines()), 2)
            self.assertIn('open drawer,close drawer,open drawer', result.stdout)
            self.assertIn('--trace-state-writes', result.stdout)
            self.assertFalse(output.exists())

    def test_invalid_name_and_existing_output_are_rejected_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = str(ROOT / 'tools' / 'run_action_matrix.py')
            result = subprocess.run([sys.executable, script, '--only', 'missing',
                                     '--output', str(root / 'unused')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((root / 'unused').exists())
            (root / 'existing').mkdir()
            result = subprocess.run([sys.executable, script, '--output',
                                     str(root / 'existing')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('already exists', result.stderr)

    def test_trace_report_uses_only_verified_changes_and_separates_slots(self):
        trace = '\n'.join([
            'entry ms=14456 pc=147c0 cs=1476 stack=0,0,0,5f,',
            'entry ms=14457 pc=11950 cs=1157 stack=abcd,4661,124,7,',
            'object-flag-set ms=14457 object=124 property=7 changed=1',
            'entry ms=14460 pc=147c5 cs=1476 ax=1 stack=0,0,0,5f,',
            'entry ms=18392 pc=147c0 cs=1476 stack=0,0,0,71,',
            'entry ms=18393 pc=11950 cs=1157 stack=abcd,4661,124,c,',
            'object-flag-set ms=18393 object=124 property=c changed=1',
            'entry ms=18394 pc=11950 cs=1157 stack=abcd,4661,124,2,',
            'object-flag-set ms=18394 object=124 property=2 changed=0',
            'entry ms=18417 pc=147c5 cs=1476 ax=1 stack=0,0,0,71,',
            'entry ms=21422 pc=147c0 cs=1476 stack=0,0,0,1d,',
            'entry ms=21443 pc=11983 cs=1157 stack=abcd,4661,124,c,',
            'object-flag-clear ms=21443 object=124 property=c changed=1',
            'entry ms=21444 pc=147c5 cs=1476 ax=1 stack=0,0,0,1d,',
        ])
        summary = summarize_trace(trace, SEQUENCES['repeat'])
        self.assertIn('| 18s | open drawer | 0x1 | set(0x124,0x0c) |', summary)
        self.assertIn('| 21s | close drawer | 0x1 | clear(0x124,0x0c) |', summary)
        self.assertIn('| 25s | open drawer | (no dispatch observed) | — |', summary)
        self.assertNotIn('set(0x124,0x02)', summary)
        self.assertNotIn('set(0x124,0x07)', summary)

    def test_missing_write_annotations_are_not_silent_success(self):
        with self.assertRaisesRegex(ValueError, 'lacks state-write annotations'):
            summarize_trace('entry ms=18000 pc=147c0 cs=1476 stack=0,0,0,71,',
                            SEQUENCES['repeat'])

    def test_batch_creates_report_and_retains_native_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new-batch'

            def replay(command, **_kwargs):
                target = Path(command[command.index('--output') + 1])
                target.mkdir(parents=True, exist_ok=True)
                (target / 'native.trace').write_text('\n'.join([
                    'entry ms=18392 pc=147c0 cs=1476 stack=0,0,0,71,',
                    'entry ms=18393 pc=11950 cs=1157 stack=abcd,4661,124,c,',
                    'object-flag-set ms=18393 object=124 property=c changed=1',
                    'entry ms=18417 pc=147c5 cs=1476 ax=1 stack=0,0,0,71,',
                ]))
                return SimpleNamespace(returncode=0, stdout='native completed\n', stderr='')

            with patch('run_action_matrix.subprocess.run', side_effect=replay) as invoked:
                self.assertEqual(main(['--only', 'repeat', '--output', str(output)]), 0)
            self.assertEqual(invoked.call_count, 1)
            self.assertIn('set(0x124,0x0c)', (output / 'report.md').read_text())
            self.assertIn('native completed', (output / 'repeat' / 'driver.log').read_text())
            self.assertTrue((output / 'repeat' / 'native.trace').exists())

    def test_failed_probe_writes_diagnostics_and_partial_report(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new-batch'
            failure = SimpleNamespace(returncode=2, stdout='', stderr='stale probe\n')
            with patch('run_action_matrix.subprocess.run', return_value=failure):
                self.assertEqual(main(['--only', 'repeat', '--output', str(output)]), 2)
            self.assertIn('stale probe', (output / 'repeat' / 'driver.log').read_text())
            self.assertIn('replay failed', (output / 'report.md').read_text())


if __name__ == '__main__':
    unittest.main()

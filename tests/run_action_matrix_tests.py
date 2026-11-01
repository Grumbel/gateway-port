"""Batch action-runner unit checks; no executable or proprietary assets needed."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_action_matrix import SEQUENCES, summarize_trace  # noqa: E402


class ActionMatrixTests(unittest.TestCase):
    def test_sequences_have_seven_distinct_action_slots(self):
        self.assertEqual(len(SEQUENCES), 6)
        for commands in SEQUENCES.values():
            self.assertEqual(len(commands), 7)

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


if __name__ == '__main__':
    unittest.main()

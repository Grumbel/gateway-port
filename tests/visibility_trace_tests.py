"""Synthetic coverage for the optional resident child-slot trace report."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from summarize_visibility_trace import parse_visibility, summarize

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'summarize_visibility_trace.py'


def entry(ms, site, obj='124', slot='0', restricted='1', value=None):
    names = {'gate-call': '10063', 'gate-return': '10068',
             'child-call': '10075', 'child-return': '1007a'}
    fields = {'gate-return': 'visibility_result', 'child-return': 'visibility_child'}
    text = (f'entry ms={ms} pc={names[site]} cs=1000 visibility_stage={site}'
            f' visibility_object={obj} visibility_slot={slot}'
            f' visibility_restricted={restricted}')
    if site in fields:
        text += f' {fields[site]}={value}'
    return text


class VisibilityTraceTests(unittest.TestCase):
    def test_gate_returns_are_separate_from_child_results_and_grouped_by_phase(self):
        lines = [entry(17000, 'gate-call'), entry(17000, 'gate-return', value='0'),
                 entry(18393, 'gate-call'), entry(18394, 'gate-return', value='1'),
                 entry(18394, 'child-call'), entry(18394, 'child-return', value='123'),
                 entry(21443, 'gate-call'), entry(21444, 'gate-return', value='0'),
                 entry(21445, 'child-call', restricted='0', slot='2'),
                 entry(21445, 'child-return', restricted='0', slot='2', value='0'),
                 entry(31443, 'gate-call', obj='ff'),
                 entry(31443, 'gate-return', obj='ff', value='1')]
        report = summarize(lines, focus=0x124)
        self.assertIn('startup/LOOK', report)
        self.assertIn('open drawer', report)
        self.assertIn('0x123', report)
        self.assertIn('look', report)
        self.assertIn('0x000', report)
        self.assertNotIn('0x0ff', report)
        self.assertIn('gate is only called on restricted traversals', report)
        self.assertEqual(parse_visibility('read ms=2 file=x'), None)

    def test_reject_signature_mismatch_absent_trace_and_invalid_results(self):
        with self.assertRaisesRegex(ValueError, 'code signature mismatch'):
            summarize([entry(18, 'gate-call') + ' visibility_probe=code-mismatch'])
        with self.assertRaisesRegex(ValueError, 'non-Boolean'):
            summarize([entry(18, 'gate-return', value='2')])
        with self.assertRaisesRegex(ValueError, 'no candidate child-slot entries'):
            summarize(['entry ms=1 pc=147c0 cs=1476'])
        with self.assertRaisesRegex(ValueError, 'no child-slot entries for requested'):
            summarize([entry(18, 'gate-call')], focus=0x123)

    def test_cli_rejects_malformed_commands_before_reporting(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = pathlib.Path(directory) / 'native.trace'
            trace.write_text(entry(18392, 'gate-return', value='1') + '\n')
            result = subprocess.run([sys.executable, str(SCRIPT), str(trace),
                                     '--commands', 'look,nope'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('expected 7 comma-separated commands', result.stderr)


if __name__ == '__main__':
    unittest.main()

"""Native service/child-slot time correlation on synthetic instruction traces."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from correlate_visibility_trace import collect, summarize

TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'correlate_visibility_trace.py'


def gate(ms, allowed, child=None):
    lines = [f'entry ms={ms} pc=10068 cs=1000 visibility_stage=gate-return '
             f'visibility_object=124 visibility_slot=0 visibility_restricted=1 '
             f'visibility_result={allowed:x}']
    if child is not None:
        lines.append(f'entry ms={ms} pc=1007a cs=1000 visibility_stage=child-return '
                     f'visibility_object=124 visibility_slot=0 visibility_restricted=1 '
                     f'visibility_child={child:x}')
    return lines


def service(ms, site, object_id, argument):
    return (f'entry ms={ms} pc={site:x} cs=1157 ax=0 '
            f'stack=ffff,4661,{object_id:x},{argument:x},')


class VisibilityCorrelationTests(unittest.TestCase):
    def setUp(self):
        self.lines = (
            gate(18360, 0) + [service(18393, 0x11950, 0x124, 0x0c)] +
            gate(18470, 1, 0x123) + gate(21389, 1, 0x123) +
            [service(21443, 0x11983, 0x124, 0x0c)] + gate(21496, 0) +
            gate(25360, 0) + [service(25393, 0x11950, 0x124, 0x0c)] +
            gate(25469, 1, 0x123) + gate(28300, 1, 0x123) +
            [service(28356, 0x11bbb, 0x123, 0xd3)] + gate(28480, 1, 0) +
            [service(28500, 0x11950, 0x123, 0x02),
             service(28500, 0x11bbb, 0x200, 0xd3),
             'entry ms=28490 pc=10063 cs=1000 '
             'visibility_stage=gate-call visibility_probe=code-mismatch']
        )

    def test_native_timeline_separates_gate_and_child_membership(self):
        samples, services = collect(self.lines, 0x124, 0x123)
        self.assertEqual(len(samples), 8)
        self.assertEqual(len(services), 4)
        report = summarize(self.lines, 0x124, 0x123)
        self.assertIn('18393  set(0x124,0x0c)', report)
        self.assertIn('ms=18360 deny -', report)
        self.assertIn('ms=18470 allow 0x123', report)
        self.assertIn('21443  clear(0x124,0x0c)', report)
        self.assertIn('ms=21496 deny -', report)
        self.assertIn('25393  set(0x124,0x0c)', report)
        self.assertIn('28356  location-set(0x123,0xd3)', report)
        self.assertIn('ms=28300 allow 0x123', report)
        self.assertIn('ms=28480 allow 0x000', report)
        self.assertIn('not verified state deltas', report)

    def test_missing_neighbors_are_explicit_and_validation_is_strict(self):
        report = summarize(self.lines, 0x124, 0x123, max_gap_ms=10)
        self.assertIn('ms     native service', report)
        self.assertIn('  -                         -', report)
        for container, child, gap in [(0, 0x123, 500), (0x124, 0x124, 500),
                                      (0x124, 0x123, 0), (0x124, 0x123, 10001)]:
            with self.subTest(container=container, child=child, gap=gap):
                with self.assertRaises(ValueError):
                    summarize(self.lines, container, child, gap)

    def test_cli_parses_and_rejects_missing_state_hook(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'native.trace'
            path.write_text('\n'.join(self.lines))
            command = [sys.executable, str(TOOL), str(path), '--container', '0x124',
                       '--child', '0x123']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('location-set(0x123,0xd3)', result.stdout)
            path.write_text('\n'.join(gate(100, 1, 0x123)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('no native flag-0x0c or child-location', result.stderr)


if __name__ == '__main__':
    unittest.main()

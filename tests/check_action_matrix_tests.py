"""C++ action replay checker tests independent of game assets and Unicorn."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests' / 'fixtures' / 'gateway-action-matrix.md'


class StoryMatrixCheckerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which('g++') is None:
            raise unittest.SkipTest('g++ is unavailable')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / 'gateway-story-check'
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic',
                        '-I' + str(ROOT / 'src'), str(ROOT / 'src' / 'story_model.cpp'),
                        str(ROOT / 'tools' / 'check_action_matrix.cpp'),
                        '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_report(self, contents):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'report.md'
            file.write_text(contents)
            return subprocess.run([str(self.binary), str(file)], capture_output=True,
                                  text=True)

    def test_all_six_native_scenarios_match(self):
        result = subprocess.run([str(self.binary), str(FIXTURE)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('matches 6 native action-matrix scenarios', result.stdout)

    def test_changed_mutation_fails_with_action_and_slot(self):
        broken = FIXTURE.read_text().replace('set(0x124,0x02)', 'set(0x124,0x23)')
        result = self.run_report(broken)
        self.assertEqual(result.returncode, 1)
        self.assertIn('baseline at 18s (open drawer)', result.stderr)

    def test_dispatch_does_not_equal_mutation(self):
        broken = FIXTURE.read_text().replace('25s | take book | (no dispatch observed)',
                                             '25s | take book | 0x1')
        result = self.run_report(broken)
        self.assertEqual(result.returncode, 1)
        self.assertIn('take-twice at 25s', result.stderr)

    def test_partial_scenario_is_rejected(self):
        beginning = FIXTURE.read_text().split('## repeat')[0]
        result = self.run_report(beginning.replace('| 37s | look | 0x1 | — |', ''))
        self.assertEqual(result.returncode, 1)
        self.assertIn('expected seven command slots', result.stderr)

    def test_unknown_command_is_not_ignored(self):
        broken = FIXTURE.read_text().replace('18s | open drawer', '18s | cast spell', 1)
        result = self.run_report(broken)
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknown scripted first-room command', result.stderr)

    def test_missing_report_is_error(self):
        result = subprocess.run([str(self.binary), '/nonexistent/gateway-matrix.md'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()

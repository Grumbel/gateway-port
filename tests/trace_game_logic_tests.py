"""Replay-driver checks that do not require Unicorn or proprietary game assets."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


DRIVER = Path(__file__).resolve().parents[1] / 'tools' / 'trace_game_logic.py'


class TraceGameLogicTests(unittest.TestCase):
    def test_command_sequence_validation_precedes_probe_and_output_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'should-not-exist'
            result = subprocess.run([sys.executable, str(DRIVER), '--scenario', 'objects',
                                     '--command-sequence', 'one,two', '--native-only',
                                     '--output', str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('expected 7 comma-separated commands', result.stderr)
            self.assertFalse(output.exists())

    def test_custom_sequence_requires_native_only(self):
        result = subprocess.run([
            sys.executable, str(DRIVER), '--scenario', 'objects',
            '--command-sequence', 'look,look,look,look,look,look,look',
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--command-sequence currently requires --native-only', result.stderr)

    def test_old_probe_is_rejected_before_running_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b'old probe: accepts --trace-entry but has no action fields')
            output = root / 'replay'
            result = subprocess.run([sys.executable, str(DRIVER), '--scenario', 'objects',
                                     '--native-only', '--probe', str(probe), '--output',
                                     str(output)], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn('lacks action-dispatch trace support', result.stderr)
            self.assertIn('run gateway-build', result.stderr)
            self.assertFalse(output.exists())

    def test_nix_build_dir_is_probe_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'nix-build'
            build.mkdir()
            (build / 'gateway-probe').write_bytes(b'old probe')
            environment = dict(os.environ, GATEWAY_BUILD_DIR=str(build))
            result = subprocess.run([sys.executable, str(DRIVER), '--scenario', 'drawer',
                                     '--native-only'], env=environment, capture_output=True,
                                    text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn(str(build / 'gateway-probe'), result.stderr)

    def test_state_write_trace_requires_native_replay_and_new_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b' action_code= but no changed field')
            output = root / 'replay'
            base = [sys.executable, str(DRIVER), '--scenario', 'objects',
                    '--trace-state-writes', '--probe', str(probe), '--output', str(output)]
            full = subprocess.run(base, capture_output=True, text=True)
            self.assertEqual(full.returncode, 2)
            self.assertIn('requires --native-only', full.stderr)
            native = subprocess.run(base + ['--native-only'],
                                    capture_output=True, text=True)
            self.assertEqual(native.returncode, 2)
            self.assertIn('lacks state-write delta tracing', native.stderr)
            self.assertFalse(output.exists())

    def test_admission_trace_requires_native_objects_and_updated_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b' action_code= changed= old probe')
            output = root / 'replay'
            base = [sys.executable, str(DRIVER), '--trace-admission',
                    '--probe', str(probe), '--output', str(output)]
            wrong = subprocess.run(base, capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('requires --native-only --scenario objects', wrong.stderr)
            native = subprocess.run(base + ['--scenario', 'objects', '--native-only'],
                                    capture_output=True, text=True)
            self.assertEqual(native.returncode, 2)
            self.assertIn('lacks action-selector trace support', native.stderr)
            self.assertFalse(output.exists())

    def test_visibility_trace_requires_native_objects_and_updated_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b' action_code= changed= admission_target= old probe')
            output = root / 'replay'
            base = [sys.executable, str(DRIVER), '--trace-visibility',
                    '--probe', str(probe), '--output', str(output)]
            wrong = subprocess.run(base, capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('requires --native-only --scenario objects', wrong.stderr)
            native = subprocess.run(base + ['--scenario', 'objects', '--native-only'],
                                    capture_output=True, text=True)
            self.assertEqual(native.returncode, 2)
            self.assertIn('lacks candidate-visibility trace support', native.stderr)
            self.assertFalse(output.exists())

    def test_navigation_entry_hook_requires_native_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b' action_code= old probe')
            output = root / 'replay'
            command = [sys.executable, str(DRIVER), '--trace-navigation',
                       '--probe', str(probe), '--output', str(output)]
            wrong = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('requires --native-only --scenario objects', wrong.stderr)
            wrong_scenario = subprocess.run(command + ['--native-only'],
                                            capture_output=True, text=True)
            self.assertEqual(wrong_scenario.returncode, 2)
            self.assertIn('requires --native-only --scenario objects', wrong_scenario.stderr)
            self.assertFalse(output.exists())

    def test_object_write_watch_rejects_wrong_replay_and_stale_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'gateway-probe'
            probe.write_bytes(b' action_code= changed= admission_target= visibility_stage=')
            output = root / 'replay'
            base = [sys.executable, str(DRIVER), '--scenario', 'objects',
                    '--trace-object-writes', '124', '--probe', str(probe),
                    '--output', str(output)]
            wrong = subprocess.run(base, capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('requires --native-only --scenario objects', wrong.stderr)
            invalid = subprocess.run(base + ['--native-only', '--trace-object-writes', '10000'],
                                     capture_output=True, text=True)
            self.assertEqual(invalid.returncode, 2)
            self.assertIn('nonzero 16-bit hex object ID', invalid.stderr)
            old = subprocess.run(base + ['--native-only'], capture_output=True, text=True)
            self.assertEqual(old.returncode, 2)
            self.assertIn('lacks native object-write tracing', old.stderr)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()

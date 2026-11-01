"""Replay-driver checks that do not require Unicorn or proprietary game assets."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


DRIVER = Path(__file__).resolve().parents[1] / 'tools' / 'trace_game_logic.py'


class TraceGameLogicTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Optional real DOSBox Staging smoke test, without original game assets."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DOSBoxStagingTests(unittest.TestCase):
    def test_headless_dos_command_writes_host_file(self):
        dosbox = os.environ.get("GATEWAY_DOSBOX") or shutil.which("dosbox-staging")
        if not dosbox:
            self.skipTest("set GATEWAY_DOSBOX to a DOSBox Staging executable")

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            dos_drive = root / "drive"
            dos_drive.mkdir()
            env = os.environ.copy()
            env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       XDG_CONFIG_HOME=str(root / "config"))
            process = subprocess.run([
                dosbox, "--noprimaryconf", "--nolocalconf",
                "--set", "core=dynamic", "--set", "cpu_cycles=max",
                "--set", "mouse_capture=nomouse", "--set", "mididevice=none",
                "-c", f'mount c "{dos_drive}"',
                "-c", "c:", "-c", "ver > PROBE.TXT", "--exit",
            ], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
               text=True, timeout=15, check=False)
            self.assertEqual(process.returncode, 0, process.stdout[-2500:])
            probe = dos_drive / "PROBE.TXT"
            self.assertTrue(probe.exists(), process.stdout[-2500:])
            self.assertIn("DOSBox Staging version", probe.read_text())
            self.assertIn("DOS version", probe.read_text())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Compile the same DOS DCL C89 source against a checked guest-host ABI."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent


class DclHostTests(unittest.TestCase):
    def test_mode_13_mode_12_segment_rollover_and_cursor(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("No host C compiler")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged = root / "staged"
            process = subprocess.run(
                ["python3", str(REPO / "tools/msc51.py"), "prepare-launcher",
                 "--output", str(staged)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            # The host compiles original identifier spellings; only the RTL
            # macro values are needed and can be provided directly.
            (root / "RTLIMAC.H").write_text(
                "#define GATE_RTL_VIDEO_MODE_OFFSET 0x0fce\n"
                "#define GATE_RTL_FILE_HANDLE_OFFSET 0x005a\n"
                "#define GATE_RTL_HEADER_OFFSET 0x005c\n")
            (root / "RTLSTATE.H").write_text(
                (REPO / "decomp/launcher/dos/msc51_rtl_state.h").read_text())
            for name in ("GATEMEM.H", "GATEDOS.H", "STDINT.H"):
                shutil.copyfile(staged / name, root / name)
            binary = root / "dcl_check"
            result = subprocess.run(
                [compiler, "-std=c89", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-I", str(root),
                 str(REPO / "decomp/launcher/dos/msc51_picture_dcl.c"),
                 str(REPO / "tests/msc51_picture_dcl_host.c"),
                 "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("Host picture DCL: PASS", run.stdout)

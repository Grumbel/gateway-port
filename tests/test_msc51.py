#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Source-only checks for the optional proprietary DOS compiler adapter."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPO = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("msc51", REPO / "tools/msc51.py")
assert SPEC is not None and SPEC.loader is not None
msc51 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(msc51)


class MSC51Tests(unittest.TestCase):
    def test_stages_only_recovered_source_with_c89_headers(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_probe(output)
            original_random = (REPO / "decomp/launcher/gate_c_runtime_random.c").read_text()
            original_probe = (REPO / "decomp/launcher/dos/intro_probe.c").read_text()
            self.assertEqual((output / "RANDOM.C").read_text(),
                             original_random.replace('#include "gate_rtl_image.h"', '#include "GATE.H"', 1))
            self.assertEqual((output / "PROBE.C").read_text(),
                             original_probe.replace('#include "../gate_exe.h"', '#include "GATE.H"', 1))
            header = (output / "GATE.H").read_text()
            self.assertIn("typedef unsigned long uint32_t;", header)
            self.assertIn("typedef unsigned short uint16_t;", header)
            batch = (output / "BUILD.BAT").read_text()
            self.assertIn("CL /AL /Od PROBE.C RANDOM.C /FeINTROCHK.EXE", batch)
            self.assertIn("INTROCHK.EXE > TEST.LOG", batch)

    def test_installation_is_looked_up_case_insensitively(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for sub in ("bin", "lib", "include"):
                (root / sub).mkdir()
            for name in ("cl.exe", "c1.exe", "c2.exe", "c3.exe", "link.exe"):
                (root / "bin" / name).touch()
            (root / "lib" / "llibcr.lib").touch()
            (root / "include" / "stdio.h").touch()
            self.assertEqual(msc51.validate_compiler(root), root)
            (root / "bin" / "c2.exe").unlink()
            with self.assertRaisesRegex(ValueError, "C2.EXE"):
                msc51.validate_compiler(root)

    def test_only_user_directories_are_mounted(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            commands = msc51.dosbox_commands(root / "compiler", root / "output", True)
            self.assertEqual(commands[0], 'mount c "' + str(root / "compiler") + '" -ro')
            self.assertEqual(commands[1], 'mount d "' + str(root / "output") + '"')
            self.assertEqual(commands[-1], "BUILD.BAT")
            with self.assertRaisesRegex(ValueError, "cannot be mounted"):
                msc51.dosbox_commands(root / 'bad"dir', root / "output", True)

    def test_installer_mounts_original_images_in_supplied_order(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            disk1 = root / "Disk1.img"
            disk2 = root / "Disk2.ima"
            disk1.touch()
            disk2.touch()
            commands = msc51.install_commands(root / "install", [disk1, disk2])
            self.assertEqual(commands, [
                'mount c "' + str(root / "install") + '"',
                'mount a "' + str(disk1) + '" "' + str(disk2) + '" -t floppy',
                "a:", "SETUP.EXE",
            ])
            with self.assertRaisesRegex(ValueError, "Expected a DOS floppy"):
                msc51.install_commands(root / "install", [root / "missing.img"])

    def test_probe_rejects_stale_dos_outputs(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            (compiler / "BIN").mkdir(parents=True)
            (compiler / "LIB").mkdir()
            (compiler / "INCLUDE").mkdir()
            for name in ("CL.EXE", "C1.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            (compiler / "LIB" / "LLIBCR.LIB").touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "output"
            output.mkdir()
            (output / "INTROCHK.EXE").write_bytes(b"stale exe")
            (output / "TEST.LOG").write_text("Intro C runtime: PASS (32 DOS rand steps)")

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run:
                run.return_value.returncode = 0
                status = msc51.main(["probe", "--compiler", str(compiler),
                                     "--output", str(output)])
            self.assertEqual(status, 1)
            self.assertFalse((output / "INTROCHK.EXE").exists())
            self.assertFalse((output / "TEST.LOG").exists())
            self.assertIn('mount c "' + str(compiler) + '" -ro', run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()

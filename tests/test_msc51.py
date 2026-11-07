#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Source-only checks for the optional proprietary DOS compiler adapter."""

import importlib.util
import io
from contextlib import redirect_stderr
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
            self.assertIn("CL /AL /Od PROBE.C RANDOM.C /FeINTROCHK.EXE /link LLIBCR.LIB/NOD", batch)
            self.assertIn("INTROCHK.EXE > TEST.LOG", batch)
            self.assertIn("ECHO Gateway MSC 5.1 batch started > START.LOG", batch)
            self.assertIn("SET TMP=D:\\", batch)
            self.assertIn("IF ERRORLEVEL 1 GOTO BUILD_FAILED", batch)
            self.assertIn("IF NOT EXIST INTROCHK.EXE GOTO BUILD_FAILED", batch)
            self.assertIn(":BUILD_FAILED\nECHO Microsoft CL returned an error > CLFAIL.LOG", batch)
            self.assertIn("IF ERRORLEVEL 1 ECHO DOS probe returned an error > RUNFAIL.LOG", batch)
            # Never launch a partial EXE that LINK left behind on failure.
            self.assertLess(batch.index("IF ERRORLEVEL 1 GOTO BUILD_FAILED"),
                            batch.index("INTROCHK.EXE > TEST.LOG"))
            self.assertTrue(batch.endswith("EXIT\n"))

    def test_installation_is_looked_up_case_insensitively(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for sub in ("bin", "lib", "include"):
                (root / sub).mkdir()
            for name in ("cl.exe", "c1.exe", "c1l.exe", "c2.exe", "c3.exe", "link.exe"):
                (root / "bin" / name).touch()
            (root / "lib" / "llibcr.lib").touch()
            (root / "include" / "stdio.h").touch()
            self.assertEqual(msc51.validate_compiler(root), root)
            (root / "bin" / "c2.exe").unlink()
            with self.assertRaisesRegex(ValueError, "C2.EXE"):
                msc51.validate_compiler(root)

    def test_floppy_image_directory_is_diagnosed(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "Microsoft C 5.1 Optimizing Compiler (5.25)"
            source.mkdir()
            (source / "Disk01.img").touch()
            with self.assertRaisesRegex(ValueError, "contains floppy images, not an installed"):
                msc51.validate_compiler(source)

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
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            (compiler / "LIB" / "LLIBCR.LIB").touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "output"
            output.mkdir()
            (output / "INTROCHK.EXE").write_bytes(b"stale exe")
            (output / "TEST.LOG").write_text("Intro C runtime: PASS (32 DOS rand steps)")
            (output / "start.log").write_text("stale batch start")
            (output / "build.log").write_text("stale compiler output")
            (output / "clfail.log").write_text("stale failure")
            (output / "runfail.log").write_text("stale runtime failure")

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as diagnostics:
                run.return_value.returncode = 0
                status = msc51.main(["probe", "--compiler", str(compiler),
                                     "--output", str(output)])
            self.assertEqual(status, 1)
            self.assertFalse((output / "INTROCHK.EXE").exists())
            self.assertFalse((output / "TEST.LOG").exists())
            self.assertFalse((output / "start.log").exists())
            self.assertFalse((output / "build.log").exists())
            self.assertFalse((output / "clfail.log").exists())
            self.assertFalse((output / "runfail.log").exists())
            self.assertIn("BUILD.BAT did not create START.LOG", diagnostics.getvalue())
            self.assertIn("BUILD.LOG: not created", diagnostics.getvalue())
            self.assertIn('mount c "' + str(compiler) + '" -ro', run.call_args.args[0])

    def test_compiler_error_is_printed_when_exe_missing(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            (output / "START.LOG").write_text("Gateway MSC 5.1 batch started\n")
            (output / "BUILD.LOG").write_text("fatal error C1000: incompatible compiler pass\n")
            (output / "CLFAIL.LOG").write_text("Microsoft CL returned an error\n")
            with redirect_stderr(io.StringIO()) as diagnostics:
                msc51.print_probe_diagnostics(output)
            message = diagnostics.getvalue()
            self.assertIn("fatal error C1000: incompatible compiler pass", message)
            self.assertIn("Microsoft CL returned an error", message)
            self.assertIn("TEST.LOG: not created", message)
            self.assertIn("RUNFAIL.LOG: not created", message)
            self.assertNotIn("did not create START.LOG", message)

    def test_case_insensitive_dos_outputs_are_recognized(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            (compiler / "BIN").mkdir(parents=True)
            (compiler / "LIB").mkdir()
            (compiler / "INCLUDE").mkdir()
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            (compiler / "LIB" / "LLIBCR.LIB").touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "output"

            def create_lowercase_outputs(_command, check):
                self.assertFalse(check)
                (output / "start.log").write_text("Gateway MSC 5.1 batch started")
                (output / "introchk.exe").write_bytes(b"DOS executable")
                (output / "test.log").write_text("Intro C runtime: PASS (32 DOS rand steps)")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=create_lowercase_outputs):
                self.assertEqual(msc51.main(["probe", "--compiler", str(compiler),
                                             "--output", str(output)]), 0)

    def test_failed_dos_run_never_counts_as_a_pass(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            (compiler / "BIN").mkdir(parents=True)
            (compiler / "LIB").mkdir()
            (compiler / "INCLUDE").mkdir()
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            (compiler / "LIB" / "LLIBCR.LIB").touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "output"

            def create_failed_run(_command, check):
                self.assertFalse(check)
                (output / "START.LOG").write_text("Gateway MSC 5.1 batch started")
                (output / "INTROCHK.EXE").write_bytes(b"partial output")
                (output / "TEST.LOG").write_text("Intro C runtime: PASS (32 DOS rand steps)")
                (output / "RUNFAIL.LOG").write_text("DOS probe returned an error")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=create_failed_run), \
                 redirect_stderr(io.StringIO()) as diagnostics:
                self.assertEqual(msc51.main(["probe", "--compiler", str(compiler),
                                             "--output", str(output)]), 1)
            self.assertIn("DOS executable returned a failure status", diagnostics.getvalue())

    def test_rejects_protected_mode_compiler_driver(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "BIN").mkdir()
            (root / "LIB").mkdir()
            (root / "INCLUDE").mkdir()
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (root / "BIN" / name).write_bytes(b"MZ" + bytes(126))
            (root / "LIB" / "LLIBCR.LIB").touch()
            (root / "INCLUDE" / "STDIO.H").touch()

            protected = bytearray(b"MZ" + bytes(126))
            protected[0x3c:0x40] = (0x40).to_bytes(4, "little")
            protected[0x40:0x42] = b"NE"
            (root / "BIN" / "CL.EXE").write_bytes(protected)
            self.assertEqual(msc51.executable_format(protected), "Windows/OS2 NE")
            self.assertEqual(msc51.executable_format(b"MZ" + bytes(126)), "DOS MZ")
            with self.assertRaisesRegex(ValueError, "CL.EXE is a Windows/OS2 NE program"):
                msc51.validate_compiler(root)
            protected[0x40:0x44] = b"PE\0\0"
            self.assertEqual(msc51.executable_format(protected), "Windows PE")


if __name__ == "__main__":
    unittest.main()

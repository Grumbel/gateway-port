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
    def test_stages_all_launcher_modules_as_dos_c89_sources(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            expected = sorted((REPO / "decomp/launcher").glob("*.c"))
            objects = msc51.stage_launcher(output)
            self.assertEqual(len(objects), len(expected))
            self.assertEqual(objects, [f"M{index:02d}.OBJ" for index in range(len(expected))])
            mapping = (output / "SOURCE.MAP").read_text()
            for index, path in enumerate(expected):
                staged = (output / f"M{index:02d}.C").read_text()
                self.assertIn(f"M{index:02d}.C  decomp/launcher/{path.name}", mapping)
                self.assertNotIn("#include <stdint.h>", staged)
                self.assertNotIn("(GateFarPtr){", staged)
                self.assertNotIn('_Noreturn', staged)
            for staged_header in output.glob("H*.H"):
                content = staged_header.read_text()
                self.assertNotIn("enum {", content)
                self.assertNotIn("_Noreturn", content)
                self.assertNotIn("#include <stdint.h>", content)
            aliases = (output / "SYMBOL.MAP").read_text()
            self.assertIn("GATE_DS_STARTUP_ALLOCATED_SEGMENT", aliases)
            self.assertIn("GATE_RESIDENT_MODE_DESCRIPTOR_BYTES", aliases)
            for path in (*output.glob("M*.C"), *output.glob("H*.H")):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers(path.read_text())), path.name)
            clock = (output / "M00.C").read_text()
            self.assertIn("GateFarPtr value;", clock)
            self.assertIn("memset(sample, 0, sizeof sample);", clock)
            self.assertIn("typedef unsigned long uint32_t;", (output / "STDINT.H").read_text())
            batch = (output / "COMPILE.BAT").read_text()
            self.assertEqual(batch.count("CL /AL /Od /c M"), len(expected))
            self.assertEqual(batch.count("IF ERRORLEVEL 1 GOTO BUILD_FAILED"), len(expected))
            self.assertIn("IF NOT EXIST M27.OBJ GOTO BUILD_FAILED", batch)
            self.assertIn("ECHO All reconstructed launcher objects compiled > SUCCESS.LOG", batch)
            self.assertNotIn("/Fe", batch)  # No complete DOS executable yet.
            response = (output / "LINK.RSP").read_text().splitlines()
            self.assertEqual(response[:36],
                             [f"M{index:02d}.OBJ+" for index in range(28)] +
                             ["MEMORY.OBJ+", "DOSFILE.OBJ+", "RTLIMG.OBJ+",
                              "RTLTEXT.OBJ+", "RTLPAL.OBJ+", "RTLGR.OBJ+",
                              "RTLHW.OBJ+", "RTFILE.OBJ"])
            self.assertEqual(response[36:], ["GATEWAY.EXE", "GATEWAY.MAP",
                                             "LLIBCR.LIB LIBH.LIB /NOD", ";"])
            memory_batch = (output / "MEMORY.BAT").read_text()
            self.assertIn("CL /AL /Od /c MEMORY.C", memory_batch)
            self.assertIn("CL /AL /Od MEMCHK.C MEMORY.OBJ", memory_batch)
            self.assertIn("MEMCHK.EXE > MEMTEST.LOG", memory_batch)
            self.assertIn("IF ERRORLEVEL 1 GOTO FAILED", memory_batch)
            self.assertIn("MEMOK.LOG", memory_batch)
            self.assertIn("MEMSIG.LOG", memory_batch)
            self.assertNotIn("MEMOK.LOG >", memory_batch)
            adapter = (output / "MEMORY.C").read_text()
            self.assertIn("FP_SEG(address) = segment", adapter)
            self.assertIn("FP_OFF(address) = offset", adapter)
            self.assertIn("gate_dos_bind_ds", adapter)
            self.assertIn("_dos_allocmem", (output / "MEMCHK.C").read_text())
            file_batch = (output / "DOSFILE.BAT").read_text()
            self.assertIn("CL /AL /Od /c DOSFILE.C", file_batch)
            self.assertIn("CL /AL /Od FILECHK.C DOSFILE.OBJ MEMORY.OBJ", file_batch)
            self.assertIn("FILECHK.EXE > FILTEST.LOG", file_batch)
            self.assertIn("FILSIG.LOG", file_batch)
            self.assertIn("IF ERRORLEVEL 1 GOTO FAILED", file_batch)
            file_adapter = (output / "DOSFILE.C").read_text()
            self.assertIn("intdosx(input, &output, &segments)", file_adapter)
            self.assertIn("output.x.cflag", file_adapter)
            self.assertIn("input.h.ah = 0x42u", file_adapter)
            self.assertIn("remove(filename)", (output / "FILECHK.C").read_text())
            rtl_batch = (output / "RTLST.BAT").read_text()
            self.assertIn("CL /AL /Od /c RTLIMG.C", rtl_batch)
            self.assertIn("CL /AL /Od /c RTLTEXT.C", rtl_batch)
            self.assertIn("CL /AL /Od /c RTLPAL.C", rtl_batch)
            self.assertIn("CL /AL /Od /c RTLGR.C", rtl_batch)
            self.assertIn("RTLCHK.C RTLIMG.OBJ RTLTEXT.OBJ RTLPAL.OBJ RTLGR.OBJ MEMORY.OBJ", rtl_batch)
            self.assertIn("RTLCHK.EXE > RTLTEST.LOG", rtl_batch)
            self.assertIn("RTLSIG.LOG", rtl_batch)
            self.assertIn("RTLOK.LOG", rtl_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", rtl_batch)
            for name in ("RTLIMG.C", "RTLTEXT.C", "RTLPAL.C", "RTLGR.C",
                         "RTLSTATE.H", "RTLLAY.H", "RTLIMAC.H", "RTLCHK.C"):
                staged = (output / name).read_text()
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers(staged)), name)
                self.assertNotIn("(GateFarPtr){", staged)
            self.assertIn("GATE_DS_IMAGE_MODE_SEGMENT", (output / "RTLLAY.H").read_text())
            self.assertIn("gate_rtl_image_mode_segment", (output / "RTLIMG.C").read_text())
            self.assertIn("gate_rtl_text_mode_pitch", (output / "RTLTEXT.C").read_text())
            self.assertIn("gate_rtl_palette_mode", (output / "RTLPAL.C").read_text())
            self.assertIn("gate_rtl_graphics_mode", (output / "RTLGR.C").read_text())
            self.assertIn("mode field remains live", (output / "RTLCHK.C").read_text())
            self.assertIn('#include "RTLIMAC.H"', (output / "RTLCHK.C").read_text())
            self.assertIn("_dos_allocmem", (output / "RTLCHK.C").read_text())
            hw_batch = (output / "RTLHW.BAT").read_text()
            self.assertIn("CL /AL /Od /c RTLHW.C", hw_batch)
            self.assertIn("HWCHK.C RTLHW.OBJ MEMORY.OBJ", hw_batch)
            self.assertIn("HWCHK.EXE > HWTEST.LOG", hw_batch)
            self.assertIn("HWSIG.LOG", hw_batch)
            self.assertIn("HWOK.LOG", hw_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", hw_batch)
            hardware = (output / "RTLHW.C").read_text()
            self.assertIn("(uint8_t)inp(port)", hardware)
            self.assertIn("(void)outp(port, value)", hardware)
            self.assertIn("gate_ds_set_word(GATE_DS_DRAW_CLIP_LEFT, left)", hardware)
            self.assertIn("gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, enabled == 1 ? 1 : 0)", hardware)
            self.assertIn("GATE_DS_DRAW_CLIP_BOTTOM", (output / "HWCHK.C").read_text())
            for name in ("RTLHW.C", "HWCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            runtime_batch = (output / "RTFILE.BAT").read_text()
            self.assertIn("CL /AL /Od /c RTFILE.C", runtime_batch)
            self.assertIn("RTFCHK.C RTFILE.OBJ MEMORY.OBJ", runtime_batch)
            self.assertIn("RTFCHK.EXE > RTFTEST.LOG", runtime_batch)
            self.assertIn("RTFSIG.LOG", runtime_batch)
            self.assertIn("RTFOK.LOG", runtime_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", runtime_batch)
            self.assertIn("access(pathname, 4)", (output / "RTFILE.C").read_text())
            self.assertIn("strncpy padding", (output / "RTFCHK.C").read_text())
            for name in ("RTFILE.C", "RTFCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            link_batch = (output / "LINK.BAT").read_text()
            self.assertIn("C:\\BIN\\LINK.EXE @LINK.RSP > LINK.LOG", link_batch)
            self.assertNotIn("\nLINK @LINK.RSP", link_batch)
            self.assertIn("LNKSTART.LOG", link_batch)
            self.assertNotIn("LINKSTART.LOG", link_batch)  # Nine-character DOS basename!
            self.assertIn("IF ERRORLEVEL 1 GOTO LINK_FAILED", link_batch)
            self.assertIn("IF NOT EXIST GATEWAY.EXE GOTO LINK_FAILED", link_batch)
            self.assertIn(":LINK_FAILED\nECHO Original DOS services", link_batch)

    def test_c89_compatibility_transformations_preserve_large_constants(self):
        staged = msc51.msc51_enums_to_macros("enum { ONE = 1, SPACE = 0xfffff, FLAG = (1ul << 31) };")
        self.assertIn("#define SPACE (0xfffff)", staged)
        self.assertIn("#define FLAG ((1ul << 31))", staged)
        self.assertNotIn("enum", staged)
        transformed, used = msc51.msc51_far_compounds_to_calls(
            "return (GateFarPtr){read_word(value, 2), (uint16_t)(next(3, 4) + 5)};")
        self.assertTrue(used)
        self.assertEqual(transformed,
                         "return gate_msc51_far_pointer(read_word(value, 2), "
                         "(uint16_t)(next(3, 4) + 5));")
        with self.assertRaisesRegex(ValueError, "more than two"):
            msc51.msc51_far_compounds_to_calls("(GateFarPtr){1,2,3}")

    def test_long_identifier_aliases_do_not_collide_or_change_address_comments(self):
        first = "GATE_RESIDENT_MODE_DESCRIPTOR_BASE"
        second = "GATE_RESIDENT_MODE_DESCRIPTOR_BYTES"
        original = (f"/* {first} at GATE.RTL:0x12abc */\n"
                    f"#define {first} (1)\n#define {second} (2)\n"
                    f"const char *name = \"{second}\";\n")
        names = msc51.msc51_short_names([original])
        self.assertNotEqual(names[first], names[second])
        self.assertTrue(all(len(short) <= 31 for short in names.values()))
        rewritten = msc51.msc51_rename_identifiers(original, names)
        self.assertIn(f"/* {first} at GATE.RTL:0x12abc */", rewritten)
        self.assertIn(f'"{second}"', rewritten)
        self.assertIn(f"#define {names[first]} (1)", rewritten)
        self.assertIn(f"#define {names[second]} (2)", rewritten)
        self.assertEqual(msc51.msc51_short_names([original]), names)

    def test_launcher_success_requires_fresh_compiler_objects(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            objects = msc51.stage_launcher(output)
            for name in (*objects, "START.LOG", "SUCCESS.LOG", "BUILDFAIL.LOG",
                         "LINKDONE.LOG", "LINKFAIL.LOG", "GATEWAY.EXE", "GATEWAY.MAP"):
                (output / name.lower()).write_text("stale")

            def simulate_no_compilation(command, check):
                self.assertIn("COMPILE.BAT", command)
                self.assertFalse(check)
                self.assertFalse(any((output / name.lower()).exists() for name in objects))
                self.assertFalse((output / "success.log").exists())
                self.assertFalse((output / "buildfail.log").exists())
                self.assertFalse((output / "linkdone.log").exists())
                self.assertFalse((output / "gateway.exe").exists())
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=simulate_no_compilation), \
                 redirect_stderr(io.StringIO()):
                self.assertEqual(msc51.main(["compile-launcher", "--compiler", str(compiler),
                                             "--output", str(output)]), 1)

            def simulate_success(command, check):
                self.assertIn("COMPILE.BAT", command)
                (output / "start.log").write_text("started")
                (output / "success.log").write_text("all compiled")
                for name in objects:
                    (output / name.lower()).write_bytes(b"object")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=simulate_success):
                self.assertEqual(msc51.main(["compile-launcher", "--compiler", str(compiler),
                                             "--output", str(output)]), 0)

    def test_link_launcher_requires_compiled_objects_and_rejects_stale_exes(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            objects = msc51.stage_launcher(output)
            args = ["link-launcher", "--compiler", str(compiler), "--output", str(output)]

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-launcher first", error.getvalue())

            (output / "start.log").write_text("compiled")
            (output / "success.log").write_text("compiled")
            for name in objects:
                (output / name.lower()).write_bytes(b"compiled object")
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())
            (output / "memory.obj").write_bytes(b"DOS memory adapter")
            (output / "memstart.log").write_text("tested")
            (output / "memok.log").write_text("passed")
            (output / "memsig.log").write_text(msc51.memory_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-dos-files first", error.getvalue())
            (output / "dosfile.obj").write_bytes(b"DOS file adapter")
            (output / "filstart.log").write_text("tested")
            (output / "filok.log").write_text("passed")
            (output / "filsig.log").write_text(msc51.dos_file_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-rtl-state first", error.getvalue())
            (output / "rtlimg.obj").write_bytes(b"original image state")
            (output / "rtltext.obj").write_bytes(b"original text state")
            (output / "rtlpal.obj").write_bytes(b"original palette state")
            (output / "rtlgr.obj").write_bytes(b"original graphics state")
            (output / "rtlstart.log").write_text("tested")
            (output / "rtlok.log").write_text("passed")
            (output / "rtlsig.log").write_text(msc51.rtl_state_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-hardware first", error.getvalue())
            (output / "rtlhw.obj").write_bytes(b"DOS hardware")
            (output / "hwstart.log").write_text("tested")
            (output / "hwok.log").write_text("passed")
            (output / "hwsig.log").write_text(msc51.hardware_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-runtime-files first", error.getvalue())
            (output / "rtfile.obj").write_bytes(b"real DOS runtime file services")
            (output / "rtfstart.log").write_text("tested")
            (output / "rtfok.log").write_text("passed")
            (output / "rtfsig.log").write_text(msc51.runtime_file_source_signature(output))
            for name in ("LINKSTAR.LOG", "LNKSTART.LOG", "LINKDONE.LOG", "LINKFAIL.LOG",
                         "LINK.LOG", "GATEWAY.EXE", "GATEWAY.MAP"):
                (output / name.lower()).write_bytes(b"stale result")

            def simulate_failed_link(command, check):
                self.assertIn("LINK.BAT", command)
                self.assertFalse(check)
                self.assertFalse(any((output / name).exists() for name in
                                     ("gateway.exe", "linkdone.log", "linkfail.log", "link.log",
                                      "linkstar.log", "lnkstart.log")))
                (output / "lnkstart.log").write_text("started")
                (output / "linkfail.log").write_text("unresolved")
                (output / "link.log").write_text("L2029 unresolved external")
                (output / "gateway.exe").write_bytes(b"partial exe")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=simulate_failed_link), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("L2029 unresolved external", error.getvalue())

            def simulate_success(command, check):
                self.assertIn("LINK.BAT", command)
                (output / "lnkstart.log").write_text("started")
                (output / "linkdone.log").write_text("linked")
                (output / "gateway.exe").write_bytes(b"compiled exe")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=simulate_success):
                self.assertEqual(msc51.main(args), 0)

    def test_compile_memory_runs_separately_and_checks_dos_probe_result(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            args = ["compile-memory", "--compiler", str(compiler), "--output", str(output)]

            def failed_probe(command, check):
                self.assertIn("MEMORY.BAT", command)
                self.assertFalse(check)
                (output / "MEMSTART.LOG").write_text("started")
                (output / "MEMORY.OBJ").write_bytes(b"compiled")
                (output / "MEMFAIL.LOG").write_text("incorrect stack segment")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("incorrect stack segment", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("MEMORY.BAT", command)
                self.assertFalse((output / "MEMFAIL.LOG").exists())
                self.assertFalse((output / "MEMORY.OBJ").exists())
                (output / "MEMSTART.LOG").write_text("started")
                (output / "MEMORY.OBJ").write_bytes(b"compiled")
                (output / "MEMOK.LOG").write_text("passed")
                (output / "MEMTEST.LOG").write_text("Memory probe: PASS")
                (output / "MEMSIG.LOG").write_text(msc51.memory_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "MEMORY.C").write_text((output / "MEMORY.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_memory_adapter(output)

    def test_compile_dos_files_requires_memory_and_checks_int21_probe(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            args = ["compile-dos-files", "--compiler", str(compiler), "--output", str(output)]
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())

            (output / "memory.obj").write_bytes(b"DOS memory adapter")
            (output / "memstart.log").write_text("tested")
            (output / "memok.log").write_text("passed")
            (output / "memsig.log").write_text(msc51.memory_source_signature(output))

            def failed_probe(command, check):
                self.assertIn("DOSFILE.BAT", command)
                self.assertFalse(check)
                (output / "FILSTART.LOG").write_text("started")
                (output / "DOSFILE.OBJ").write_bytes(b"compiled")
                (output / "FILFAIL.LOG").write_text("carry flag test failed")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("carry flag test failed", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("DOSFILE.BAT", command)
                self.assertFalse((output / "FILFAIL.LOG").exists())
                self.assertFalse((output / "DOSFILE.OBJ").exists())
                (output / "FILSTART.LOG").write_text("started")
                (output / "DOSFILE.OBJ").write_bytes(b"compiled")
                (output / "FILOK.LOG").write_text("passed")
                (output / "FILTEST.LOG").write_text("DOS file probe: PASS")
                (output / "FILSIG.LOG").write_text(msc51.dos_file_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "DOSFILE.C").write_text((output / "DOSFILE.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_dos_file_adapter(output)

    def test_rtl_state_reuses_native_sources_and_requires_fresh_dos_probe(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            msc51.stage_launcher(output)
            self.assertIn("The original RTLink loader patches segment WORDs",
                          (output / "RTLIMG.C").read_text())
            self.assertIn("The linked RTL text routines read these mode-dependent words",
                          (output / "RTLTEXT.C").read_text())
            args = ["compile-rtl-state", "--compiler", str(compiler), "--output", str(output)]
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())

            (output / "memory.obj").write_bytes(b"memory")
            (output / "memstart.log").write_text("tested")
            (output / "memok.log").write_text("passed")
            (output / "memsig.log").write_text(msc51.memory_source_signature(output))

            def failed_probe(command, check):
                self.assertIn("RTLST.BAT", command)
                self.assertFalse(check)
                (output / "RTLSTART.LOG").write_text("started")
                (output / "RTLIMG.OBJ").write_bytes(b"compiled")
                (output / "RTLTEXT.OBJ").write_bytes(b"compiled")
                (output / "RTLPAL.OBJ").write_bytes(b"compiled")
                (output / "RTLGR.OBJ").write_bytes(b"compiled")
                (output / "RTLFAIL.LOG").write_text("incorrect selector")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("incorrect selector", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("RTLST.BAT", command)
                self.assertFalse((output / "RTLFAIL.LOG").exists())
                self.assertFalse((output / "RTLIMG.OBJ").exists())
                self.assertFalse((output / "RTLTEXT.OBJ").exists())
                self.assertFalse((output / "RTLPAL.OBJ").exists())
                self.assertFalse((output / "RTLGR.OBJ").exists())
                (output / "RTLSTART.LOG").write_text("started")
                (output / "RTLIMG.OBJ").write_bytes(b"compiled")
                (output / "RTLTEXT.OBJ").write_bytes(b"compiled")
                (output / "RTLPAL.OBJ").write_bytes(b"compiled")
                (output / "RTLGR.OBJ").write_bytes(b"compiled")
                (output / "RTLOK.LOG").write_text("passed")
                (output / "RTLTEST.LOG").write_text("RTL state probe: PASS")
                (output / "RTLSIG.LOG").write_text(msc51.rtl_state_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "RTLIMG.C").write_text((output / "RTLIMG.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_rtl_state_adapter(output)

    def test_compile_hardware_checks_clipping_and_input_port_probe(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            args = ["compile-hardware", "--compiler", str(compiler), "--output", str(output)]
            msc51.stage_launcher(output)
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())

            (output / "memory.obj").write_bytes(b"memory")
            (output / "memstart.log").write_text("tested")
            (output / "memok.log").write_text("passed")
            (output / "memsig.log").write_text(msc51.memory_source_signature(output))

            def failed_probe(command, check):
                self.assertIn("RTLHW.BAT", command)
                (output / "HWSTART.LOG").write_text("started")
                (output / "RTLHW.OBJ").write_bytes(b"compiled")
                (output / "HWFAIL.LOG").write_text("clip bottom mismatch")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("clip bottom mismatch", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("RTLHW.BAT", command)
                self.assertFalse((output / "HWFAIL.LOG").exists())
                self.assertFalse((output / "RTLHW.OBJ").exists())
                (output / "HWSTART.LOG").write_text("started")
                (output / "RTLHW.OBJ").write_bytes(b"compiled")
                (output / "HWOK.LOG").write_text("passed")
                (output / "HWTEST.LOG").write_text("Hardware probe: PASS")
                (output / "HWSIG.LOG").write_text(msc51.hardware_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "RTLHW.C").write_text((output / "RTLHW.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_hardware_adapter(output)

    def test_compile_runtime_files_requires_fresh_real_dos_probe(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for directory in ("BIN", "LIB", "INCLUDE"):
                (compiler / directory).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()
            output = root / "launcher"
            msc51.stage_launcher(output)
            args = ["compile-runtime-files", "--compiler", str(compiler),
                    "--output", str(output)]
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())

            (output / "memory.obj").write_bytes(b"memory")
            (output / "memstart.log").write_text("tested")
            (output / "memok.log").write_text("passed")
            (output / "memsig.log").write_text(msc51.memory_source_signature(output))
            for name in ("RTFILE.OBJ", "RTFSTART.LOG", "RTFOK.LOG", "RTFSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("RTFILE.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "RTFILE.OBJ").exists())
                self.assertFalse((output / "RTFOK.LOG").exists())
                (output / "RTFSTART.LOG").write_text("started")
                (output / "RTFILE.OBJ").write_bytes(b"compiled")
                (output / "RTFFAIL.LOG").write_text("resource filename mismatch")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("resource filename mismatch", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("RTFILE.BAT", command)
                self.assertFalse((output / "RTFFAIL.LOG").exists())
                self.assertFalse((output / "RTFILE.OBJ").exists())
                (output / "RTFSTART.LOG").write_text("started")
                (output / "RTFILE.OBJ").write_bytes(b"compiled")
                (output / "RTFOK.LOG").write_text("passed")
                (output / "RTFTEST.LOG").write_text("Runtime file probe: PASS")
                (output / "RTFSIG.LOG").write_text(msc51.runtime_file_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "RTFILE.C").write_text((output / "RTFILE.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_runtime_file_adapter(output)

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
            self.assertIn("CL /AL /Od PROBE.C RANDOM.C /FeINTROCHK.EXE /link LLIBCR.LIB LIBH.LIB/NOD", batch)
            self.assertIn("INTROCHK.EXE > TEST.LOG", batch)
            self.assertIn("ECHO Gateway MSC 5.1 batch started > START.LOG", batch)
            self.assertIn("SET TMP=D:\\", batch)
            self.assertIn("IF ERRORLEVEL 1 GOTO BUILD_FAILED", batch)
            self.assertIn("IF NOT EXIST INTROCHK.EXE GOTO BUILD_FAILED", batch)
            self.assertIn(":BUILD_FAILED\nECHO Microsoft CL returned an error > CLFAIL.LOG", batch)
            self.assertIn("IF ERRORLEVEL 1 GOTO RUN_FAILED\nGOTO DONE", batch)
            self.assertIn(":RUN_FAILED\nECHO DOS probe returned an error > RUNFAIL.LOG", batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", batch)
            # Never launch a partial EXE that LINK left behind on failure.
            self.assertLess(batch.index("IF ERRORLEVEL 1 GOTO BUILD_FAILED"),
                            batch.index("INTROCHK.EXE > TEST.LOG"))
            # The RUNFAIL.LOG redirection must be unreachable on success.
            self.assertLess(batch.index("IF ERRORLEVEL 1 GOTO RUN_FAILED"),
                            batch.index("GOTO DONE\n:BUILD_FAILED"))
            self.assertLess(batch.index(":BUILD_FAILED\n"), batch.index(":RUN_FAILED\n"))
            self.assertTrue(batch.endswith("EXIT\n"))

    def test_installation_is_looked_up_case_insensitively(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for sub in ("bin", "lib", "include"):
                (root / sub).mkdir()
            for name in ("cl.exe", "c1.exe", "c1l.exe", "c2.exe", "c3.exe", "link.exe"):
                (root / "bin" / name).touch()
            (root / "lib" / "llibcr.lib").touch()
            (root / "lib" / "libh.lib").touch()
            (root / "include" / "stdio.h").touch()
            self.assertEqual(msc51.validate_compiler(root), root)
            (root / "lib" / "libh.lib").unlink()
            with self.assertRaisesRegex(ValueError, "LIBH.LIB"):
                msc51.validate_compiler(root)
            (root / "lib" / "libh.lib").touch()
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
            (compiler / "LIB" / "LIBH.LIB").touch()
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
            (compiler / "LIB" / "LIBH.LIB").touch()
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
            (compiler / "LIB" / "LIBH.LIB").touch()
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
            (root / "LIB" / "LIBH.LIB").touch()
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

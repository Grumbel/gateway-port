#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Source-only checks for the optional proprietary DOS compiler adapter."""

import importlib.util
import hashlib
import io
import os
import re
import shutil
import subprocess
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


def fake_runtime_file_obj(include_diagnostic=True):
    """Synthetic Microsoft OMF publics, never a proprietary compiler object."""
    def record(kind, payload):
        size = len(payload) + 1
        header = bytes((kind, size & 255, size >> 8))
        return header + payload + bytes(((-sum(header + payload)) & 255,))

    names = ["_gate_linked_copy_runtime_string",
             "_gate_linked_format_res_cd21e963",
             "_gate_linked_open_runtime_path"]
    if include_diagnostic:
        names.append("_gate_linked_print_intr_b006848b")
    payload = bytearray(b"\x00\x01")  # group 0, segment 1
    for name in names:
        raw = name.encode("ascii")
        payload.extend(bytes((len(raw),)) + raw + b"\x00\x00\x00")
    return (record(0x80, b"\x08RTFILE.C")
            + record(0x90, bytes(payload)) + record(0x8a, b"\x00"))


class MSC51Tests(unittest.TestCase):
    def test_real_mode_entry_batch_is_guarded_and_not_a_false_full_intro_pass(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            batch = (output / "ENTRY.BAT").read_text()
            self.assertIn("D:\\GATEWAY.EXE /CHECK > D:\\ENTRY.LOG", batch)
            self.assertIn("D:\\GATEWAY.EXE /PICTURE > D:\\PICTURE.LOG", batch)
            self.assertEqual(batch.count("IF ERRORLEVEL 1 GOTO FAILED"), 2)
            self.assertIn("ENTRYFAIL.LOG", batch)
            self.assertNotIn("/INTRO", batch)
            link = (output / "LINK.RSP").read_text()
            self.assertIn("MAIN.OBJ+", link)
            self.assertIn("/STACK:16384", link)
            with redirect_stderr(io.StringIO()) as diagnostic:
                with self.assertRaises(SystemExit):
                    msc51.main(["test-dos-entry", "--output", str(output)])
            self.assertIn("requires --original-game-dir", diagnostic.getvalue())

    def test_optional_original_rtl_fixup_words_are_unrelocated_zero(self):
        original = REPO.parent / "gateway-dos" / "GATE.RTL"
        if not original.is_file():
            self.skipTest("user-owned original GATE.RTL is not present")
        rtl = original.read_bytes()
        self.assertEqual(hashlib.sha256(rtl).hexdigest(),
                         "c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566")
        # The first live call after EXE:1309..134b is RTL:c3bd's music
        # initializer. The original source data takes its no-music return.
        self.assertEqual(rtl[0x29 + 0x0662] & 0x06, 0)
        ds_sites = (0x0fb8, 0x0fbc, 0x0fc0, 0x0fc4, 0x0fc8, 0x0fcc,
                    0x153a, 0x153e, 0x1542, 0x1546, 0x154a, 0x154e,
                    0x1552, 0x1556, 0x26d8, 0x2b78, 0x2c10, 0x3246,
                    0x324c, 0x33ae, 0x3540, 0x3544)
        cs_sites = (0x33de, 0x33e2, 0x33e6, 0x33ea, 0x33ee)
        self.assertEqual((len(ds_sites), len(cs_sites)), (22, 5))
        self.assertEqual(len(set(ds_sites + cs_sites)), 27)
        for offset in ds_sites + cs_sites:
            self.assertEqual(rtl[0x29 + offset:0x29 + offset + 2], b"\x00\x00",
                             f"RTL file {0x29 + offset:05x} is no longer zero")
        offset_sites = (
            (0x0fb6, 0x0000, 0x0f92), (0x0fba, 0x0004, 0x0f96),
            (0x0fbe, 0x0008, 0x0f9a), (0x0fc2, 0x000c, 0x0f9e),
            (0x0fc6, 0x0010, 0x0fa2), (0x0fca, 0x0014, 0x0fa6),
            (0x1538, 0x001a, 0x14fa), (0x153c, 0x0020, 0x1500),
            (0x1540, 0x002a, 0x150a), (0x1544, 0x002f, 0x150f),
            (0x1548, 0x0039, 0x1519), (0x154c, 0x003f, 0x151f),
            (0x1550, 0x0049, 0x1529), (0x1554, 0x004e, 0x152e),
            (0x33dc, 0x0000, 0x0c02), (0x33e0, 0x0000, 0x0c02),
            (0x33e4, 0x0000, 0x0c02), (0x33e8, 0x0000, 0x0c02),
            (0x33ec, 0x0000, 0x0c02),
            (0x3244, 0x0000, 0x2c44), (0x324a, 0x0000, 0x2c44),
            (0x33ac, 0x06e4, 0x3328), (0x353e, 0x0000, 0x3530),
            (0x3542, 0x0004, 0x3534),
        )
        self.assertEqual(len(offset_sites), 24)
        for index, (site, source, observed) in enumerate(offset_sites):
            self.assertEqual(int.from_bytes(rtl[0x29 + site:0x2b + site], "little"),
                             source, f"unexpected RTL offset field at {site:04x}")
            if index < 6:
                addend = 0x0f92
            elif index < 14:
                addend = 0x14e0
            elif index < 19:
                addend = 0x0c02
            elif index < 22:
                addend = 0x2c44
            else:
                addend = 0x3530
            self.assertEqual((observed - source) & 0xffff, addend)
        external_tables = (
            ((0x2814, 0x000a), (0x2852, 0x0032), (0x2890, 0x0032),
             (0x28ce, 0x0053), (0x290c, 0x0065), (0x294a, 0x0065),
             (0x2988, 0x0065), (0x29c6, 0x0065)),
            ((0x2a93, 0x006b), (0x2aa7, 0x00d6), (0x2abb, 0x00d6),
             (0x2acf, 0x0133), (0x2ae3, 0x0189), (0x2af7, 0x0189),
             (0x2b0b, 0x0189), (0x2b1f, 0x0189), (0x2b33, 0x01e3),
             (0x2b47, 0x0272), (0x2b5b, 0x0440)),
        )
        self.assertEqual(sum(len(table) for table in external_tables), 19)
        for table in external_tables:
            for site, _ in table:
                self.assertEqual(rtl[0x29 + site:0x29 + site + 4], bytes(4),
                                 f"unexpected RTL external pointer at {site:04x}")

    def test_staged_static_data_copy_c89_host_and_probe_only_link(self):
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for synthetic static EXE data fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            executable = output / "static-data-host-probe"
            build = subprocess.run(
                ["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-include", str(output / "DATNAMES.H"),
                 "-I", str(output), str(output / "DATA.C"), str(output / "SEL.C"),
                 str(REPO / "tests/fixtures/msc51_data/data_host_probe.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            probe = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(probe.returncode, 0, probe.stderr)
            self.assertIn("static EXE/RTL copy, 27 seg + 24 off + 19 external pointers host: PASS", probe.stdout)
            batch = (output / "DATA.BAT").read_text()
            self.assertIn("CL /AL /Od /c DATA.C", batch)
            self.assertIn("DATACHK.C DATA.OBJ SEL.OBJ MEMORY.OBJ DOSFILE.OBJ", batch)
            self.assertIn("DATACHK.EXE > DATTEST.LOG", batch)
            self.assertIn("DATAREAL.C DATA.OBJ SEL.OBJ MEMORY.OBJ DOSFILE.OBJ", batch)
            self.assertIn("IF EXIST E:\\GATE.RTL DATAREAL.EXE > DATREAL.LOG", batch)
            self.assertIn("CL /AL /Od /c BOOT.C", batch)
            self.assertIn("BOOTCHK.C BOOT.OBJ DATA.OBJ SEL.OBJ MEMORY.OBJ DOSFILE.OBJ", batch)
            self.assertIn("IF EXIST E:\\GATE.RTL BOOTCHK.EXE > BOOTREAL.LOG", batch)
            entry_object = msc51.launcher_source_alias(output, "gate_exe_intro_setup.c").replace(".C", ".OBJ")
            self.assertIn("CL /AL /Od /c HEAP.C", batch)
            self.assertIn("CL /AL /Od /c RTLHW.C", batch)
            self.assertIn("CL /AL /Od /c " + entry_object.replace(".OBJ", ".C"), batch)
            mode_object = msc51.launcher_source_alias(output, "gate_rtl_mode.c").replace(".C", ".OBJ")
            self.assertIn("CL /AL /Od /c " + mode_object.replace(".OBJ", ".C"), batch)
            palette_object = msc51.launcher_source_alias(output, "gate_rtl_palette.c").replace(".C", ".OBJ")
            self.assertIn("CL /AL /Od /c " + palette_object.replace(".OBJ", ".C"), batch)
            self.assertIn("CL /AL /Od /c RTLPAL.C", batch)
            self.assertIn("LINK.EXE @BPREP.RSP", batch)
            self.assertIn("IF EXIST E:\\GATE.RTL BOOTPREP.EXE > BPREAL.LOG", batch)
            self.assertIn("CL /AL /Od /c RESSTATE.C >> DATBUILD.LOG", batch)
            self.assertIn("LINK.EXE @BVID.RSP", batch)
            self.assertIn("IF EXIST E:\\GATE.RTL BOOTVGA.EXE > BVIDREAL.LOG", batch)
            self.assertIn(entry_object, (output / "BPREP.RSP").read_text())
            self.assertIn(mode_object, (output / "BPREP.RSP").read_text())
            self.assertIn("RTLHW.OBJ+", (output / "BPREP.RSP").read_text())
            self.assertIn("RESSTATE.OBJ+", (output / "BVID.RSP").read_text())
            self.assertIn("RTLPAL.OBJ+", (output / "BVID.RSP").read_text())
            self.assertIn(entry_object, (output / "BVID.RSP").read_text())
            self.assertIn(mode_object, (output / "BVID.RSP").read_text())
            self.assertIn(palette_object, (output / "BVID.RSP").read_text())
            self.assertNotIn("RESSTATE.OBJ+", (output / "BPREP.RSP").read_text())
            self.assertNotIn("unexpected retrace branch", (output / "BOOTVGA.C").read_text())
            self.assertIn("void gate_rtl_wait_vertical_retrace(void)",
                          (output / palette_object.replace(".OBJ", ".C")).read_text())
            original_initializer = "gate_linked_initialize_intro_services"
            alias = msc51.msc51_short_names([original_initializer]).get(
                original_initializer, original_initializer)
            self.assertIn(alias + "(", (output / "BOOTPREP.C").read_text())
            self.assertIn(alias + "(", (output / "RTLHW.C").read_text())
            self.assertNotIn("BOOTPREP.OBJ", (output / "LINK.RSP").read_text())
            self.assertLessEqual(max(len(line) for line in batch.splitlines()), 127)
            data_aliases = dict(re.findall(
                r"^#define (\w+) (\w+)$", (output / "DATNAMES.H").read_text(), re.MULTILINE))
            def staged_name(original):
                return data_aliases.get(original, original)

            # Microsoft C 5.1 uses only the first 31 identifier characters.
            # Every compiler-visible identifier must be unique after staging;
            # names such as gate_dos_relocate_intro_resident_* otherwise
            # collide as distinct function bodies under C2084.
            for source in ("DATA.C", "DATACHK.C", "DATAREAL.C", "BOOT.H", "BOOTCB.H",
                           "BOOT.C", "BOOTCHK.C", "BOOTPREP.C", "BOOTVGA.C",
                           "RTLHW.C", "RESSTATE.C"):
                ids = msc51.msc51_identifiers((output / source).read_text())
                self.assertTrue(all(len(name) <= 31 for name in ids), source)
                self.assertEqual(len(ids), len({name[:31] for name in ids}), source)
            self.assertGreater(len(data_aliases), 10)
            self.assertIn("gate_dos_load_intro_rtl_data", (output / "DATA.C").read_text())
            self.assertIn("gate_dos_fix_intro_rtl_segs", (output / "DATA.C").read_text())
            self.assertIn("gate_dos_fix_intro_rtl_offs", (output / "DATA.C").read_text())
            self.assertIn("gate_dos_fix_intro_rtl_external", (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_fix_intro_rtl_startup_words"), (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_fix_intro_return_pointer"), (output / "DATA.C").read_text())
            self.assertIn("gate_dos_prepare_intro_data", (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_load_intro_resident_bank"), (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_relocate_intro_resident_self_calls"), (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_relocate_intro_resident_ds_loads"), (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_relocate_intro_exe_call"), (output / "DATA.C").read_text())
            self.assertIn(staged_name("gate_dos_relocate_intro_resident_direct_segments"), (output / "DATA.C").read_text())
            self.assertIn("gate_dos_intro_resident_crc32", (output / "DATA.C").read_text())
            real_probe = (output / "DATAREAL.C").read_text()
            self.assertIn("gate_dos_intro_resident_crc32", real_probe)
            # A previous source check accidentally ran the resident CRC at
            # segment zero, before allocating the bank. The 10 EXE tail
            # bytes must be overlaid before any full-bank fingerprint.
            self.assertLess(real_probe.index(staged_name("gate_dos_overlay_intro_resident_exe_tail") + "(exe_tail_handle"),
                            real_probe.index("if (good && gate_dos_intro_resident_crc32("))
            self.assertLess(real_probe.index("if (good && gate_dos_intro_resident_crc32("),
                            real_probe.index("gate_dos_prepare_intro_data(exe_handle"))
            self.assertIn("gate_dos_intro_data_crc32", (output / "DATA.C").read_text())
            self.assertIn("gate_dos_prepare_intro_data", (output / "DATAREAL.C").read_text())
            self.assertIn("gate_dos_intro_data_crc32", (output / "DATAREAL.C").read_text())
            # The owner probes must supply a byte-validated return context
            # from RTL:4bc8, not a zero-filled non-code placeholder.
            callback_header = (output / "BOOTCB.H").read_text()
            self.assertIn("gate_dos_file_seek(handle, 0u, 0x4bc8ul)", callback_header)
            self.assertIn("gate_dos_file_read(handle,", callback_header)
            for source in ("BOOTCHK.C", "BOOTPREP.C", "BOOTVGA.C"):
                self.assertIn('#include "BOOTCB.H"', (output / source).read_text())
                self.assertIn("gate_probe_load_return_context(", (output / source).read_text())
            self.assertIn("DATA.OBJ", (output / "LINK.RSP").read_text())
            self.assertIn("BOOT.OBJ", (output / "LINK.RSP").read_text())
            self.assertIn("MAIN.OBJ", (output / "LINK.RSP").read_text())
            (output / "STDINT.H").write_text('#include <stdint.h>\n')
            for source in ("DATACHK.C", "DATAREAL.C", "BOOTPREP.C", "BOOTVGA.C"):
                text = (output / source).read_text()
                # MS C 5.1 emits C2073 for an initialized automatic array
                # inside main(), even though host GCC accepts the C89 code.
                self.assertNotRegex(
                    text.split("int main(void)", 1)[1],
                    r"\b(?:char|uint8_t|uint16_t)\s+\w+\s*\[[^]]*\]\s*=",
                )
                self.assertRegex(text, r"static char \w+\[\] = \".*\";")
                syntax = subprocess.run(
                    ["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                     "-include", str(output / "DATNAMES.H"),
                     "-I", str(REPO / "tests/fixtures/msc51_data"),
                     "-I", str(output), "-x", "c", str(output / source)],
                    capture_output=True, text=True)
                self.assertEqual(syntax.returncode, 0, syntax.stderr)
            self.assertNotIn("GATE_STATIC_DATA_REQUIRED_PARAGRAPHS",
                             (output / "DATA.C").read_text())

    def test_dos_intro_bootstrap_owns_resources_and_fails_closed(self):
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for DOS image owner C89 fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            executable = output / "bootstrap-host"
            built = subprocess.run(
                ["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-include", str(output / "DATNAMES.H"),
                 "-I", str(REPO / "tests/fixtures/msc51_data"),
                 "-I", str(output), str(output / "BOOT.C"),
                 str(REPO / "tests/fixtures/msc51_data/bootstrap_host.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("11 allocation/file/CRC outcomes", ran.stdout)
            for source in ("BOOT.C", "BOOTCHK.C"):
                syntax = subprocess.run(
                    ["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror",
                     "-fsyntax-only", "-x", "c",
                     "-include", str(output / "DATNAMES.H"),
                     "-I", str(REPO / "tests/fixtures/msc51_data"),
                     "-I", str(output), str(output / source)],
                    capture_output=True, text=True)
                self.assertEqual(syntax.returncode, 0, syntax.stderr)

    def test_recovered_source_c_intro_prologue_in_boot_image(self):
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for synthetic recovered DOS entry fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            intro_source = msc51.launcher_source_alias(output, "gate_exe_intro_setup.c")
            mode_source = msc51.launcher_source_alias(output, "gate_rtl_mode.c")
            executable = output / "startup-entry-host"
            built = subprocess.run(
                ["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-include", str(output / "DATNAMES.H"),
                 "-I", str(REPO / "tests/fixtures/msc51_data"),
                 "-I", str(output), str(output / "BOOTPREP.C"),
                 str(output / intro_source), str(output / mode_source), str(output / "HEAP.C"),
                 str(REPO / "tests/fixtures/msc51_data/bootstrap_entry_host.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("five recovered writes, original data/CRC, workspace observed",
                          result.stdout)
            self.assertIn("RTL:c3bd no-music return, configuration preserved",
                          result.stdout)
            self.assertIn("DS:0fce=ffff invalid; hardware mode setup still required",
                          result.stdout)
            self.assertIn("RTL:f14d rejected -1 and 5; no BIOS mode entered",
                          result.stdout)
            self.assertIn("RTL:f14d mode 3; 320x200 descriptors; BIOS mode switch simulated",
                          result.stdout)
            self.assertIn("DS:54c2=3592 and DS:54ca=54ce; both segments rebound",
                          result.stdout)
            for invalid in ("TEST_BAD_SELECTOR", "TEST_ALLOC_FAIL",
                            "TEST_MUSIC_ENABLED", "TEST_VIDEO_PREINITIALIZED",
                            "TEST_RESOURCE_BAD_SEGMENT",
                            "TEST_BAD_VIDEO_SELECTOR", "TEST_VIDEO_MUTATED_BY_AUDIO",
                            "TEST_VIDEO_NOT_VGA", "TEST_MODE_BAD_DIMENSIONS",
                            "TEST_CALLBACK_OPEN_FAIL", "TEST_CALLBACK_SEEK_FAIL",
                            "TEST_CALLBACK_READ_FAIL", "TEST_CALLBACK_CLOSE_FAIL"):
                with self.subTest(invalid=invalid):
                    env = os.environ.copy()
                    env[invalid] = "1"
                    rejected = subprocess.run([str(executable)], env=env,
                                              capture_output=True, text=True)
                    self.assertEqual(rejected.returncode, 1, rejected.stderr)
                    self.assertIn("DOS intro C entry prologue: FAIL", rejected.stdout)

    def test_recovered_mode_sets_and_restores_live_vga_in_host_fixture(self):
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for synthetic resident video fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            intro = msc51.launcher_source_alias(output, "gate_exe_intro_setup.c")
            mode = msc51.launcher_source_alias(output, "gate_rtl_mode.c")
            executable = output / "startup-video-host"
            built = subprocess.run(
                ["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-include", str(output / "DATNAMES.H"),
                 "-I", str(REPO / "tests/fixtures/msc51_data"),
                 "-I", str(output), str(output / "BOOTVGA.C"),
                 str(output / intro), str(output / mode), str(output / "HEAP.C"),
                 str(REPO / "tests/fixtures/msc51_data/bootstrap_entry_host.c"),
                 str(REPO / "tests/fixtures/msc51_data/bootstrap_video_host.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            env = os.environ.copy()
            env["TEST_VIDEO_ONLY"] = "1"
            result = subprocess.run([str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("RTL:f14d mode 2; BIOS 12h, 639x479 descriptors, planar pixel 0a",
                          result.stdout)
            self.assertIn("BIOS 12h full clear, rectangle and planar pixel readback",
                          result.stdout)
            self.assertIn("RTL:f14d + resident 2409 mode 13; A000 pixel, clear and rectangle; prior BIOS mode restored",
                          result.stdout)
            for rejected_var in ("TEST_RESOURCE_BAD_SEGMENT", "TEST_VIDEO_BAD_PLANAR_PIXEL",
                                 "TEST_VIDEO_BAD_PLANAR_CLEAR", "TEST_VIDEO_BAD_PLANAR_RECTANGLE",
                                 "TEST_VIDEO_BAD_PIXEL", "TEST_VIDEO_BAD_CLEAR",
                                 "TEST_VIDEO_BAD_RECTANGLE", "TEST_VIDEO_NOT_VGA",
                                 "TEST_VIDEO_FAIL_RESTORE"):
                with self.subTest(rejected_var=rejected_var):
                    rejected_env = env.copy()
                    rejected_env[rejected_var] = "1"
                    rejected = subprocess.run([str(executable)], env=rejected_env,
                                              capture_output=True, text=True)
                    self.assertEqual(rejected.returncode, 1, rejected.stderr)
                    self.assertIn("DOS intro live VGA probe: FAIL", rejected.stdout)

    def test_original_rtl_video_mode_needs_hardware_initialization(self):
        rtl = REPO.parent / "gateway-dos" / "GATE.RTL"
        if not rtl.is_file():
            self.skipTest("user-owned original GATE.RTL is not present")
        # RTL's original data preimage starts at file offset 0x29. Its
        # initial DS:0fce word is an invalid mode, not a usable VGA setup.
        self.assertEqual(rtl.read_bytes()[0x29 + 0x0fce:0x29 + 0x0fd0],
                         b"\xff\xff")
        # These are inclusive bounds and the source's unusual line-grouping
        # convention, not conventional (width,height,pitch) framebuffer sizes.
        data = rtl.read_bytes()
        mode3_tables = ((0x12f8, 319), (0x1302, 199), (0x130c, 40),
                        (0x1316, 25))
        for table, expected in mode3_tables:
            with self.subTest(table=hex(table)):
                offset = 0x29 + table + 2 * 3
                self.assertEqual(int.from_bytes(data[offset:offset + 2], "little"),
                                 expected)

    def test_original_startup_resource_pointers_appear_between_250_and_350ms(self):
        original = REPO / "build/msc51-startup"
        fine = REPO / "build/msc51-startup-fine"
        samples = [(original / "at-0.bin", False),
                   (original / "at-100.bin", False),
                   (fine / "at-250.bin", False),
                   (fine / "at-350.bin", True),
                   (fine / "at-450.bin", True),
                   (original / "at-500.bin", True),
                   (original / "at-1500.bin", True)]
        if not all(path.is_file() for path, _ in samples):
            self.skipTest("original startup RAM captures are not present")
        original_ds = 0x2a7a
        words = {0x54c2: 0x3592, 0x54c4: original_ds,
                 0x54ca: 0x54ce, 0x54cc: original_ds}
        for path, populated in samples:
            data = path.read_bytes()
            for offset, expected in words.items():
                with self.subTest(snapshot=path.name, address=hex(offset)):
                    address = original_ds * 16 + offset
                    observed = int.from_bytes(data[address:address + 2], "little")
                    self.assertEqual(observed, expected if populated else 0)

    def test_original_350ms_startup_is_16_color_mode_12h(self):
        sample = REPO / "build/msc51-startup-fine/at-350.bin"
        if not sample.is_file():
            self.skipTest("original 350ms RAM capture is not present")
        data = sample.read_bytes()
        original_ds = 0x2a7a
        def word(segment, offset):
            address = segment * 16 + offset
            return int.from_bytes(data[address:address + 2], "little")
        self.assertEqual(data[0x449], 0x12)  # BIOS data area, current video mode
        for offset, expected in ((0x0fce, 2), (0x5510, 0x000f),
                                 (0x5512, 0x000f), (0x5514, 0x03da),
                                 (0x5516, 5)):
            with self.subTest(offset=hex(offset)):
                self.assertEqual(word(original_ds, offset), expected)
        self.assertEqual(word(word(original_ds, 0x4d92), 0x003e), 639)
        self.assertEqual(word(word(original_ds, 0x4d94), 0x0042), 479)

    def test_compile_data_requires_verified_dependencies_and_current_guest_log(self):
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
            args = ["compile-data", "--compiler", str(compiler),
                    "--output", str(output)]
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-memory first", error.getvalue())
            for name in ("MEMORY.OBJ", "MEMSTART.LOG", "MEMOK.LOG"):
                (output / name).write_text("tested")
            (output / "MEMSIG.LOG").write_text(msc51.memory_source_signature(output))
            for name in ("SEL.OBJ", "SELCHK.EXE", "SELSTART.LOG", "SELOK.LOG"):
                (output / name).write_text("tested")
            (output / "SELTEST.LOG").write_text(
                "Selector alias probe: PASS (12 data, 33 lower, 160-byte overlap)\n")
            (output / "SELSIG.LOG").write_text(msc51.selector_source_signature(output))
            for name in ("DOSFILE.OBJ", "FILSTART.LOG", "FILOK.LOG"):
                (output / name).write_text("tested")
            (output / "FILSIG.LOG").write_text(msc51.dos_file_source_signature(output))
            for name in ("DATA.OBJ", "DATACHK.EXE", "DATAREAL.EXE", "DATREAL.LOG",
                         "DATTEST.LOG", "DATOK.LOG"):
                (output / name).write_text("stale")

            def completed(command, check):
                self.assertIn("DATA.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "DATACHK.EXE").exists())
                for name in ("DATA.OBJ", "MAIN.OBJ", "DATACHK.EXE", "DATAREAL.EXE",
                             "BOOTPREP.EXE", "DATSTART.LOG", "DATOK.LOG"):
                    (output / name).write_text("current")
                (output / "DATTEST.LOG").write_text(
                    "Static EXE/RTL data probe: PASS (5976 + 13714 bytes; 27 seg + 24 off + 19 external pointers)\n")
                (output / "DATSIG.LOG").write_text(msc51.static_data_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=completed):
                self.assertEqual(msc51.main(args), 0)
            (output / "DATTEST.LOG").write_text("Static EXE/RTL data probe: PASS (old)\n")
            with self.assertRaisesRegex(ValueError, "does not verify"):
                msc51.validate_static_data_probe(output)
            (output / "DATTEST.LOG").write_text(
                "Static EXE/RTL data probe: PASS (5976 + 13714 bytes; 27 seg + 24 off + 19 external pointers)\n")
            with self.assertRaisesRegex(ValueError, "original GATE.EXE/GATE.RTL full-bank and data CRC tests"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "DATREAL.LOG").write_text(
                "Original EXE/RTL data probe: PASS (5976 + 13714 bytes, 27 seg + 24 off + 19 external pointers)\n")
            with self.assertRaisesRegex(ValueError, "full-bank and data CRC tests"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "DATREAL.LOG").write_text(
                "Original EXE/RTL data probe: PASS (5976 + 13714 bytes; 15616-byte normalized resident CRC32 F332BF93)\n")
            with self.assertRaisesRegex(ValueError, "full-bank and data CRC tests"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "DATREAL.LOG").write_text(
                "Original EXE/RTL data probe: PASS (5976 + 13714 bytes; 15616-byte resident CRC32 F332BF93; 19690-byte prepared data CRC32 5A991491)\n")
            with self.assertRaisesRegex(ValueError, "image owner allocation/CRC/cleanup"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BOOTREAL.LOG").write_text("DOS intro image owner probe: PASS (old)\n")
            with self.assertRaisesRegex(ValueError, "image owner allocation/CRC/cleanup"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BOOTREAL.LOG").write_text(
                "DOS intro image owner probe: PASS (two allocated, CRC-checked images; no code executed)\n")
            with self.assertRaisesRegex(ValueError, "source-C intro entry prologue verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text("DOS intro C entry prologue: PASS (old)\n")
            with self.assertRaisesRegex(ValueError, "source-C intro entry prologue verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                "DOS intro C entry prologue: PASS (five recovered writes, original data/CRC, workspace observed; no intro code entered)\n")
            with self.assertRaisesRegex(ValueError, "audio early-return verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                "DOS intro C entry prologue: PASS (five recovered writes, original data/CRC, workspace observed; no intro code entered)\n"
                "DOS intro C audio guard: PASS (RTL:c3bd no-music return, configuration preserved; no IRQ installed)\n")
            with self.assertRaisesRegex(ValueError, "video preflight verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                "DOS intro C entry prologue: PASS (five recovered writes, original data/CRC, workspace observed; no intro code entered)\n"
                "DOS intro C audio guard: PASS (RTL:c3bd no-music return, configuration preserved; no IRQ installed)\n"
                "DOS intro C video preflight: PASS (DS:0fce=ffff invalid; hardware mode setup still required)\n")
            with self.assertRaisesRegex(ValueError, "video rejected-mode guard"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                (output / "BPREAL.LOG").read_text() +
                "DOS intro C video guard: PASS (RTL:f14d rejected -1 and 5; no BIOS mode entered)\n")
            with self.assertRaisesRegex(ValueError, "video layout"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                (output / "BPREAL.LOG").read_text() +
                "DOS intro C video layout: PASS (RTL:f14d mode 3; 320x200 descriptors; BIOS mode switch simulated)\n")
            with self.assertRaisesRegex(ValueError, "resource pointer verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BPREAL.LOG").write_text(
                (output / "BPREAL.LOG").read_text() +
                "DOS intro C resource pointers: PASS (DS:54c2=3592 and DS:54ca=54ce; both segments rebound)\n")
            with self.assertRaisesRegex(ValueError, "resident 16-color VGA verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BVIDREAL.LOG").write_text(
                "DOS intro live VGA probe: PASS (old)\n")
            with self.assertRaisesRegex(ValueError, "resident 16-color VGA verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BVIDREAL.LOG").write_text(
                "DOS intro live VGA probe: PASS (RTL:f14d + resident 2409 mode 13; A000 pixel, clear and rectangle; prior BIOS mode restored)\n")
            with self.assertRaisesRegex(ValueError, "resident 16-color VGA verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BVIDREAL.LOG").write_text(
                (output / "BVIDREAL.LOG").read_text() +
                "DOS intro live 16-color VGA: PASS (RTL:f14d mode 2; BIOS 12h, 639x479 descriptors, planar pixel 0a)\n")
            with self.assertRaisesRegex(ValueError, "planar full-frame drawing verification"):
                msc51.validate_static_data_probe(output, require_original=True)
            (output / "BVIDREAL.LOG").write_text(
                (output / "BVIDREAL.LOG").read_text() +
                "DOS intro live 16-color drawing: PASS (BIOS 12h full clear, rectangle and planar pixel readback)\n")
            msc51.validate_static_data_probe(output, require_original=True)
            (output / "DATA.C").write_text((output / "DATA.C").read_text() + "/* stale */\n")
            with self.assertRaisesRegex(ValueError, "source changed"):
                msc51.validate_static_data_probe(output)

    def test_real_original_file_is_explicitly_pinned_and_readonly(self):
        with tempfile.TemporaryDirectory() as scratch:
            game = Path(scratch) / "original"
            game.mkdir()
            (game / "GATE.EXE").write_bytes(b"MZ\0wrong release")
            with redirect_stderr(io.StringIO()) as diagnostic:
                with self.assertRaises(SystemExit):
                    msc51.main(["compile-data", "--original-game-dir", str(game),
                                "--output", str(Path(scratch) / "launcher")])
            self.assertIn("does not match the pinned original SHA-256",
                          diagnostic.getvalue())
            pinned_exe = REPO.parent / "gateway-dos" / "GATE.EXE"
            if pinned_exe.is_file():
                shutil.copyfile(pinned_exe, game / "GATE.EXE")
                (game / "GATE.RTL").write_bytes(b"untrusted RTL payload")
                with redirect_stderr(io.StringIO()) as rtl_diagnostic:
                    with self.assertRaises(SystemExit):
                        msc51.main(["compile-data", "--original-game-dir", str(game),
                                    "--output", str(Path(scratch) / "launcher")])
                self.assertIn("GATE.RTL does not match the pinned original SHA-256",
                              rtl_diagnostic.getvalue())
            commands = msc51.dosbox_commands(
                Path("/compiler"), Path("/build"), True, "DATA.BAT", game)
            self.assertIn(f'mount e "{game}" -ro', commands)
            self.assertLess(commands.index(f'mount e "{game}" -ro'),
                            commands.index("DATA.BAT"))
            self.assertIn('mount c "/compiler" -ro', commands)

    def test_real_original_file_mount_and_required_result(self):
        game = REPO.parent / "gateway-dos"
        if not (game / "GATE.EXE").is_file():
            self.skipTest("user-owned original GATE.EXE is not present")
        with tempfile.TemporaryDirectory() as scratch:
            args = ["compile-data", "--original-game-dir", str(game),
                    "--output", str(Path(scratch) / "launcher"),
                    "--compiler", str(Path(scratch) / "compiler")]
            with mock.patch.object(msc51, "validate_memory_adapter"), \
                 mock.patch.object(msc51, "validate_selector_probe"), \
                 mock.patch.object(msc51, "validate_dos_file_adapter"), \
                 mock.patch.object(msc51, "validate_compiler", return_value=Path(scratch)), \
                 mock.patch.object(msc51, "validate_static_data_probe") as validate, \
                 mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
                self.assertEqual(msc51.main(args), 0)
                validate.assert_called_once_with(Path(scratch) / "launcher",
                                                 require_original=True)
            command = run.call_args.args[0]
            self.assertIn(f'mount e "{game.resolve()}" -ro', command)
            self.assertIn("DATA.BAT", command)
            self.assertEqual(command[:5], ["/usr/bin/false", "--set", "core=dynamic",
                                           "--set", "cpu_cycles=max"])

    def test_staged_startup_selectors_match_snapshot_layout_and_run_on_host(self):
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for C89 selector alias fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            source = (output / "SEL.C").read_text()
            header = (REPO / "decomp/launcher/gate_memory_layout.h").read_text()
            selector_fields = {
                name: int(value, 16)
                for name, value in re.findall(
                    r"^\s*(GATE_DS_[A-Z0-9_]+_SEGMENT)\s*=\s*(0x[0-9a-fA-F]+)\s*,",
                    header, re.MULTILINE)
                if 0x4d82 <= int(value, 16) <= 0x4e4e
            }
            self.assertEqual(len(selector_fields), 45)
            groups = []
            for name in ("gate_intro_lower_selectors", "gate_intro_data_selectors"):
                match = re.search(r"\b%s\[\]\s*=\s*\{([^}]+)\}" % name, source)
                self.assertIsNotNone(match)
                groups.append({int(value, 16) for value in
                               re.findall(r"0x([0-9a-fA-F]+)u", match.group(1))})
            lower, data = groups
            self.assertEqual((len(lower), len(data)), (33, 12))
            self.assertFalse(lower & data)
            self.assertEqual(lower | data, set(selector_fields.values()))
            self.assertEqual(data, {
                selector_fields[name] for name in (
                    "GATE_DS_FONT_RESOURCE_MODE_SEGMENT",
                    "GATE_DS_IMAGE_MODE_SEGMENT",
                    "GATE_DS_RESOURCE_SIZE_TABLE_SEGMENT",
                    "GATE_DS_IMAGE_TRANSFORM_SEGMENT",
                    "GATE_DS_CLOCK_SAMPLE_STATUS_SEGMENT",
                    "GATE_DS_INTRO_MODE_SEGMENT",
                    "GATE_DS_INTRO_PALETTE_SEGMENT",
                    "GATE_DS_STARTUP_CONFIG_FIRST_SEGMENT",
                    "GATE_DS_STARTUP_CONFIG_SECOND_SEGMENT",
                    "GATE_DS_STARTUP_CONFIG_THIRD_SEGMENT",
                    "GATE_DS_STARTUP_CONFIG_FOURTH_SEGMENT",
                    "GATE_DS_STARTUP_CONFIG_FIFTH_SEGMENT")
            })
            batch = (output / "SELECT.BAT").read_text()
            self.assertIn("CL /AL /Od /c SEL.C", batch)
            self.assertIn("SELCHK.C SEL.OBJ MEMORY.OBJ", batch)
            self.assertIn("SELCHK.EXE > SELTEST.LOG", batch)
            self.assertIn("SEL.OBJ", (output / "LINK.RSP").read_text())
            executable = output / "selector-host-probe"
            build = subprocess.run(
                ["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-I", str(output), str(output / "SEL.C"),
                 str(REPO / "tests/fixtures/msc51_selectors/selector_host_probe.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            probe = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(probe.returncode, 0, probe.stderr)
            self.assertIn("startup selector alias host: PASS", probe.stdout)

    def test_compile_selectors_requires_current_dos_probe(self):
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
            args = ["compile-selectors", "--compiler", str(compiler),
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
            for name in ("SEL.OBJ", "SELCHK.EXE", "SELTEST.LOG", "SELOK.LOG"):
                (output / name).write_text("stale")

            def completed(command, check):
                self.assertIn("SELECT.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "SELCHK.EXE").exists())
                (output / "SEL.OBJ").write_bytes(b"compiled")
                (output / "SELCHK.EXE").write_bytes(b"compiled")
                (output / "SELSTART.LOG").write_text("started")
                (output / "SELOK.LOG").write_text("passed")
                (output / "SELTEST.LOG").write_text(
                    "Selector alias probe: PASS (12 data, 33 lower, 160-byte overlap)\n")
                (output / "SELSIG.LOG").write_text(msc51.selector_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=completed):
                self.assertEqual(msc51.main(args), 0)
            (output / "SELTEST.LOG").write_text("Old probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "did not verify"):
                msc51.validate_selector_probe(output)
            (output / "SELTEST.LOG").write_text(
                "Selector alias probe: PASS (12 data, 33 lower, 160-byte overlap)\n")
            (output / "SEL.C").write_text((output / "SEL.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source changed"):
                msc51.validate_selector_probe(output)

    def test_staged_int1c_handler_restores_vector_on_success_and_timeout(self):
        """Host fake IRQs exercise both paths; DOSBox still has to verify the ABI."""
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for deterministic DOS timer fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            (output / "STDINT.H").write_text('#include <stdint.h>\n')
            fixtures = REPO / "tests/fixtures/msc51_timer"
            syntax = subprocess.run(["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror",
                                     "-fsyntax-only", "-I", str(fixtures),
                                     "-I", str(output), "-x", "c",
                                     str(output / "TMRCHK.C")],
                                    capture_output=True, text=True)
            self.assertEqual(syntax.returncode, 0, syntax.stderr)
            executable = output / "timer-host-probe"
            build = subprocess.run(["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(fixtures), "-I", str(output),
                                    "-x", "c", str(output / "TIMER.C"),
                                    str(fixtures / "timer_host_probe.c"),
                                    "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("callback/timeout/PIT/clock restoration: PASS", result.stdout)

    def test_staged_clock_calendar_and_dst_on_host(self):
        """Execute staged calendar logic with deterministic DOS interrupt replies.

        This is a host regression only; Microsoft C 5.1's own CLOCK.BAT
        remains the mandatory verification before linking the launcher.
        """
        if not shutil.which("gcc"):
            self.skipTest("gcc needed for deterministic DOS clock fixture")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            # The real DOS typedef is 'unsigned long' (32-bit on MSC 5.1).
            # On 64-bit Linux use stdint's exact widths for this host probe.
            (output / "STDINT.H").write_text('#include <stdint.h>\n')
            fixtures = REPO / "tests/fixtures/msc51_clock"
            check = subprocess.run(["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra",
                                    "-Werror", "-fsyntax-only", "-I", str(fixtures),
                                    "-I", str(output), str(output / "CLKCHK.C")],
                                   capture_output=True, text=True)
            self.assertEqual(check.returncode, 0, check.stderr)
            shortened = {original: alias for alias, original in
                         (line.split() for line in
                          (output / "SYMBOL.MAP").read_text().splitlines()[1:])}
            (output / "CLKHOST.C").write_text(msc51.msc51_rename_identifiers(
                (fixtures / "clock_host_probe.c").read_text(), shortened))
            executable = output / "clock-host-probe"
            build = subprocess.run(["gcc", "-x", "c", "-std=c89", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(fixtures), "-I", str(output),
                                    str(output / "CLOCK.C"), str(output / "CLKHOST.C"),
                                    "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("calendar/DST/timezone: PASS", result.stdout)

    def test_dos_resource_file_abi_uses_stack_slot_and_cpu_divide_fault(self):
        """Source-level host assembly check, not a Microsoft C 5.1 verification."""
        if not shutil.which("gcc") or not shutil.which("nm"):
            self.skipTest("gcc and nm needed to inspect staged DOS C89 symbols")
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            mapping = (output / "SOURCE.MAP").read_text().splitlines()
            staged_file = next(line.split()[0] for line in mapping
                               if line.endswith("gate_rtl_file.c"))
            obj = output / "FILEABI.OBJ"
            subprocess.run(["gcc", "-x", "c", "-std=c89", "-O0", "-c",
                            str(output / staged_file), "-o", str(obj)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            undefined = subprocess.run(["nm", "-u", str(obj)], check=True,
                                       capture_output=True, text=True).stdout
            self.assertNotIn("gate_rtl_file_stack_word", undefined)
            self.assertNotIn("gate_rtl_file_divide_fault", undefined)
            self.assertIn("gate_linked_dos_file_read", undefined)

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
            rtl_file_source = expected.index(REPO / "decomp/launcher/gate_rtl_file.c")
            rtl_file = (output / f"M{rtl_file_source:02d}.C").read_text()
            self.assertIn("#define GATE_MSC51_ORIGINAL_FILE_ABI 1", rtl_file)
            self.assertIn("#ifdef GATE_MSC51_ORIGINAL_FILE_ABI", rtl_file)
            self.assertIn("#ifndef GATE_MSC51_ORIGINAL_FILE_ABI", rtl_file)
            self.assertEqual(rtl_file.count("uint16_t value;"), 2)
            self.assertIn("typedef unsigned long uint32_t;", (output / "STDINT.H").read_text())
            batch = (output / "COMPILE.BAT").read_text()
            self.assertEqual(batch.count("CL /AL /Od /c M"), len(expected))
            self.assertEqual(batch.count("IF ERRORLEVEL 1 GOTO BUILD_FAILED"), len(expected))
            self.assertIn("IF NOT EXIST M27.OBJ GOTO BUILD_FAILED", batch)
            self.assertIn("ECHO All reconstructed launcher objects compiled > SUCCESS.LOG", batch)

            self.assertNotIn("/Fe", batch)  # No complete DOS executable yet.
            response = (output / "LINK.RSP").read_text().splitlines()
            self.assertEqual(response[:49],
                             [f"M{index:02d}.OBJ+" for index in range(28)] +
                             ["MEMORY.OBJ+", "DOSFILE.OBJ+", "RTLIMG.OBJ+",
                              "RTLTEXT.OBJ+", "RTLPAL.OBJ+", "RTLGR.OBJ+",
                              "RTLHW.OBJ+", "RTFILE.OBJ+", "RESSTATE.OBJ+",
                              "HEAP.OBJ+", "CTRL.OBJ+", "BREAK.OBJ+", "BREXIT.OBJ+",
                              "INP.OBJ+", "CLOCK.OBJ+", "CRTENV.OBJ+",
                              "PICTDCL.OBJ+", "MAIN.OBJ+", "BOOT.OBJ+",
                              "DATA.OBJ+", "SEL.OBJ"])
            self.assertEqual(response[49:], ["GATEWAY.EXE", "GATEWAY.MAP",
                                             "LLIBCR.LIB LIBH.LIB /NOD /STACK:16384", ";"])
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
            self.assertIn("void gate_linked_initialize_c306ca35(void)", hardware)
            self.assertIn("GATE_STARTUP_MUSIC_TRI_2e58f2f2", hardware)
            self.assertIn("gate_ds_word(GATE_DS_MUSIC_STARTUP_CONFIG) == 0u", hardware)
            self.assertIn("abort();", hardware)
            self.assertIn("music disabled preserves device configuration",
                          (output / "HWCHK.C").read_text())
            self.assertIn("music unconfigured preserves trigger flags",
                          (output / "HWCHK.C").read_text())
            self.assertIn("GATE_DS_DRAW_CLIP_BOTTOM", (output / "HWCHK.C").read_text())
            self.assertIn("gate_dos_bind_rtl_video_ds", hardware)
            self.assertIn("gate_far_word(rtl_video_segment(), 0x030eu)", hardware)
            self.assertIn("gate_far_word(rtl_video_segment(), 0x0004u)", hardware)
            self.assertIn("gate_far_word(rtl_video_segment(), 0x0006u)", hardware)
            self.assertIn("gate_far_word(rtl_video_segment(), 0x001au)", hardware)
            self.assertIn("void gate_rtl_video_command(uint16_t function", hardware)
            self.assertIn("int86(0x10, &in, &out)", hardware)
            self.assertIn("text cursor column", (output / "HWCHK.C").read_text())
            self.assertIn("RTL block cursor write", (output / "HWCHK.C").read_text())
            self.assertIn("gate_rtl_mode_query_adapter", hardware)
            self.assertIn("gate_rtl_mode_svga_variant", hardware)
            self.assertIn("VGA color monitor subtype", (output / "HWCHK.C").read_text())
            self.assertIn("SVGA variant range", (output / "HWCHK.C").read_text())
            self.assertIn("Hardware Ctrl-Break silent audio: PASS", (output / "HWCHK.C").read_text())
            self.assertIn("void gate_linked_shutdown_audio(void)", (output / "RTLHW.C").read_text())
            original_hardware = (REPO / "decomp/launcher/dos/msc51_hardware.c").read_text()
            self.assertIn("gate_linked_transform_picture_palette_rgb", original_hardware)
            self.assertIn("gate_linked_transform_picture_palette_indexed", original_hardware)
            self.assertIn("RGB/palette conversion round trip", (output / "HWCHK.C").read_text())
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
            self.assertIn("gate_linked_print_intro_diagnostic", (output / "SYMBOL.MAP").read_text())
            self.assertIn("putchar((int)ch)", (output / "RTFILE.C").read_text())
            self.assertIn("100% literal", (output / "RTFCHK.C").read_text())
            self.assertIn("strncpy padding", (output / "RTFCHK.C").read_text())
            picture_batch = (output / "PICTDCL.BAT").read_text()
            self.assertIn("CL /AL /Od /c PICTDCL.C", picture_batch)
            self.assertIn("PICCHK.C PICTDCL.OBJ DOSFILE.OBJ RTLIMG.OBJ MEMORY.OBJ", picture_batch)
            self.assertIn("PICCHK.EXE > PICTEST.LOG", picture_batch)
            self.assertIn("PICSIG.LOG", picture_batch)
            self.assertIn("mode-12 decode", (output / "PICCHK.C").read_text())
            self.assertIn("GATE_RTL_FILE_HANDLE_OFFSET", (output / "RTLIMAC.H").read_text())
            self.assertIn("GATE_RTL_HEADER_OFFSET", (output / "RTLIMAC.H").read_text())
            for name in ("PICTDCL.C", "PICCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            for name in ("RTFILE.C", "RTFCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            resident_batch = (output / "RESST.BAT").read_text()
            self.assertIn("CL /AL /Od /c RESSTATE.C", resident_batch)
            self.assertIn("RESCHK.C RESSTATE.OBJ HEAP.OBJ MEMORY.OBJ", resident_batch)
            self.assertIn("RESCHK.EXE > RESTEST.LOG", resident_batch)
            self.assertIn("RESSIG.LOG", resident_batch)
            self.assertIn("RESOK.LOG", resident_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", resident_batch)
            self.assertIn("gate_ds_set_word(GATE_DS_DRAW_COLOR, color)",
                          (output / "RESSTATE.C").read_text())
            self.assertIn("int86(0x10, &in, &out)", (output / "RESSTATE.C").read_text())
            self.assertIn("fill_vga_planes", (output / "RESSTATE.C").read_text())
            self.assertIn("clear rejects mismatched descriptor", (output / "RESCHK.C").read_text())
            self.assertIn("void gate_rtl_plot_pixel(uint16_t color", (output / "RESSTATE.C").read_text())
            self.assertIn("in.h.ah = 0x0du", (output / "RESSTATE.C").read_text())
            self.assertIn("in.h.ah = 0x0cu", (output / "RESSTATE.C").read_text())
            self.assertIn("void gate_rtl_resident_line_to(uint16_t x", (output / "RESSTATE.C").read_text())
            self.assertIn("void gate_rtl_background_rect(uint16_t style", (output / "RESSTATE.C").read_text())
            self.assertIn("mode-13 pixel XOR", (output / "RESCHK.C").read_text())
            self.assertIn("planar VGA pixel", (output / "RESCHK.C").read_text())
            self.assertIn("gate_linked_blit_vfx_picture", (output / "RESCHK.C").read_text())
            self.assertIn("mode-13 VFX pixel copy", (output / "RESCHK.C").read_text())
            self.assertIn("mode-12 VFX four-plane pixel", (output / "RESCHK.C").read_text())
            self.assertIn("int86(0x33, &in, &out)", (output / "RESSTATE.C").read_text())
            self.assertIn("VFX save state accepted", (output / "RESCHK.C").read_text())
            self.assertIn("Ctrl-Break active VFX state cleared", (output / "RESCHK.C").read_text())
            self.assertIn("Ctrl-Break first DOS MCB released", (output / "RESCHK.C").read_text())
            self.assertIn("Ctrl-Break failed image preserved", (output / "RESCHK.C").read_text())
            self.assertIn("void gate_linked_vfx_shutdown(void)",
                          (output / "RESSTATE.C").read_text())
            self.assertIn("GATE_DS_VFX_STATE_FLAGS", (output / "RTLLAY.H").read_text())
            for name in ("RESSTATE.C", "RESCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            heap_batch = (output / "HEAP.BAT").read_text()
            self.assertIn("CL /AL /Od /c HEAP.C", heap_batch)
            self.assertIn("HEAPCHK.C HEAP.OBJ MEMORY.OBJ", heap_batch)
            self.assertIn("HEAPCHK.EXE > HEAPTST.LOG", heap_batch)
            self.assertIn("HEAPSIG.LOG", heap_batch)
            self.assertIn("HEAPOK.LOG", heap_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", heap_batch)
            self.assertIn("_dos_allocmem", (output / "HEAP.C").read_text())
            self.assertIn("_dos_freemem", (output / "HEAP.C").read_text())
            self.assertIn("0x10010ul", (output / "HEAPCHK.C").read_text())
            self.assertIn("gate_linked_allocate_i_c0beca92", (output / "HEAP.C").read_text())
            self.assertIn("startup paragraph base", (output / "HEAPCHK.C").read_text())
            for name in ("HEAP.C", "HEAPCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            input_batch = (output / "INPUT.BAT").read_text()
            self.assertIn("CL /AL /Od /c INP.C", input_batch)
            self.assertIn("INPCHK.C INP.OBJ MEMORY.OBJ", input_batch)
            self.assertIn("INPCHK.EXE > INPTEST.LOG", input_batch)
            self.assertIn("INPSIG.LOG", input_batch)
            self.assertIn("INPOK.LOG", input_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", input_batch)
            self.assertIn("#include <bios.h>", (output / "INP.C").read_text())
            self.assertIn("_bios_keybrd(_KEYBRD_READY)", (output / "INP.C").read_text())
            self.assertIn("_bios_keybrd(_KEYBRD_READ)", (output / "INP.C").read_text())
            self.assertNotIn("output.x.flags", (output / "INP.C").read_text())
            self.assertIn("int86(0x33, &input, &output)", (output / "INP.C").read_text())
            self.assertIn("unget slot reset", (output / "INPCHK.C").read_text())
            for name in ("INP.C", "INPCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            clock_batch = (output / "CLOCK.BAT").read_text()
            self.assertIn("CL /AL /Od /c CLOCK.C", clock_batch)
            self.assertIn("CLKCHK.C CLOCK.OBJ MEMORY.OBJ", clock_batch)
            self.assertIn("CLKCHK.EXE > CLKTEST.LOG", clock_batch)
            self.assertIn("CL /AL /Od /c TIMER.C", clock_batch)
            self.assertIn("TMRCHK.C TIMER.OBJ", clock_batch)
            self.assertIn("TMRCHK.EXE > TMRTEST.LOG", clock_batch)
            self.assertIn("gate_dos_probe_pit_calibration", (output / "TMRCHK.C").read_text())
            self.assertIn("CLKSIG.LOG", clock_batch)
            self.assertIn("CLKOK.LOG", clock_batch)
            self.assertNotIn("IF ERRORLEVEL 1 ECHO", clock_batch)
            self.assertIn("input.h.ah = 0x2cu", (output / "CLOCK.C").read_text())
            self.assertIn("void gate_linked_read_runti_94f6de12(GateFarPtr destination)",
                          (output / "CLOCK.C").read_text())
            self.assertIn("input.h.ah = 0x2au", (output / "CLOCK.C").read_text())
            self.assertIn("gate_rtl_graphics_delay(20ul)",
                          (output / "CLKCHK.C").read_text())
            clock_probe = (output / "CLKCHK.C").read_text()
            self.assertIn("GateFarPtr destination;", clock_probe)
            self.assertIn("destination = gate_stack_pointer(sample);", clock_probe)
            self.assertNotIn("GateFarPtr destination = gate_stack_pointer(sample)", clock_probe)
            for name in ("CLOCK.C", "CLKCHK.C", "TIMER.C", "TMRCHK.C"):
                self.assertTrue(all(len(identifier) <= 31 for identifier in
                                    msc51.msc51_identifiers((output / name).read_text())), name)
            crt_batch = (output / "CRT.BAT").read_text()
            self.assertIn("CL /AL /Od /c CRTENV.C", crt_batch)
            self.assertIn("CRTCHK.C CRTENV.OBJ MEMORY.OBJ", crt_batch)
            self.assertIn("CRTCHK.EXE > CRTTEST.LOG", crt_batch)
            self.assertIn("CRTSIG.LOG", crt_batch)
            self.assertIn("CRTOK.LOG", crt_batch)
            self.assertIn("FP_SEG(value)", (output / "CRTENV.C").read_text())
            self.assertIn("FP_SEG(digits) = number.segment", (output / "CRTENV.C").read_text())
            self.assertIn("FP_OFF(digits) = number.offset", (output / "CRTENV.C").read_text())
            self.assertNotIn("MK_FP(", (output / "CRTENV.C").read_text())
            self.assertIn("atol(digits)", (output / "CRTENV.C").read_text())
            for name in ("CRTENV.C", "CRTCHK.C"):
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

    def test_font_probe_links_actual_recovered_module_and_rejects_stale_results(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            font_obj = msc51.launcher_source_alias(output, "gate_rtl_font.c").replace(".C", ".OBJ")
            batch = (output / "FONT.BAT").read_text()
            self.assertIn(f"FONTCHK.C {font_obj} HEAP.OBJ MEMORY.OBJ", batch)
            self.assertIn("/c M13.C", batch)  # Always compile the current recovered source.
            self.assertIn("gate_rtl_clear_font_cache()", (output / "FONTCHK.C").read_text())
            with self.assertRaisesRegex(ValueError, "not passed"):
                msc51.validate_font_probe(output)
            for name in ("FONTCHK.EXE", "FNTSTART.LOG", "FNTOK.LOG"):
                (output / name).write_text("built\n")
            (output / "FNTTEST.LOG").write_text("Font cache DOS probe: PASS\n")
            (output / "FNTSIG.LOG").write_text(msc51.font_source_signature(output))
            msc51.validate_font_probe(output)
            (output / "M13.C").write_text((output / "M13.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source changed"):
                msc51.validate_font_probe(output)
            msc51.remove_font_outputs(output)
            with self.assertRaisesRegex(ValueError, "not passed"):
                msc51.validate_font_probe(output)

    def test_stream_probe_links_recovered_file_wrapper_and_rejects_stale_results(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_launcher(output)
            wrapper = msc51.launcher_source_alias(output, "gate_rtl_file.c")
            stream_batch = (output / "STREAM.BAT").read_text()
            self.assertIn(f"CL /AL /Od /c {wrapper}", stream_batch)
            self.assertIn("CL /AL /Od /c STRBRDG.C", stream_batch)
            self.assertIn(f"STRCHK.C {wrapper.replace('.C', '.OBJ')} "
                          "STRBRDG.OBJ RTFILE.OBJ DOSFILE.OBJ MEMORY.OBJ", stream_batch)
            self.assertIn("STRCHK.EXE > STRTEST.LOG", stream_batch)
            self.assertIn("CL /AL /Od /c CTRL.C", stream_batch)
            self.assertIn("C:\\BIN\\LINK.EXE @CTRL.RSP", stream_batch)
            self.assertIn("CTRLCHK.EXE > CTRLTEST.LOG", stream_batch)
            self.assertIn("CL /AL /Od /c BREAK.C", stream_batch)
            self.assertIn("CL /AL /Od /c BREXIT.C", stream_batch)
            self.assertIn("C:\\BIN\\LINK.EXE @BRK.RSP", stream_batch)
            self.assertIn("BRKCHK.EXE > BRKTEST.LOG", stream_batch)
            self.assertIn("IF ERRORLEVEL 2 GOTO FAILED", stream_batch)
            self.assertIn("IF NOT ERRORLEVEL 1 GOTO FAILED", stream_batch)
            self.assertIn("BREAK.OBJ+", (output / "LINK.RSP").read_text())
            self.assertIn("M13.OBJ+", (output / "BRK.RSP").read_text())
            self.assertIn("gate_linked_handle_control_key", (output / "BREAK.C").read_text())
            self.assertIn("CTRL.OBJ+", (output / "LINK.RSP").read_text())
            self.assertIn("RESSTATE.OBJ+", (output / "CTRL.RSP").read_text())
            self.assertIn("gate_rtl_control_teardown", (output / "CTRL.C").read_text())
            self.assertIn("Original Ctrl-Break fifth cleanup DOS probe: PASS",
                          (output / "CTRLCHK.C").read_text())
            self.assertIn("GATE_DS_CONTROL_STREAM_AUX",
                          (output / "STRCHK.C").read_text())
            with self.assertRaisesRegex(ValueError, "not passed"):
                msc51.validate_stream_probe(output)
            for name in ("STRCHK.EXE", "STRSTART.LOG", "STROK.LOG"):
                (output / name).write_text("built\n")
            for name in ("CTRL.OBJ", "CTRLCHK.EXE", "BREAK.OBJ", "BREXIT.OBJ",
                         "BRKCHK.EXE"):
                (output / name).write_text("built\n")
            (output / "CTRLTEST.LOG").write_text(
                "Original Ctrl-Break fifth cleanup DOS probe: PASS\n")
            (output / "BRKTEST.LOG").write_text(
                "Ctrl-Break outer six-call DOS probe: entered\n")
            (output / "STRTEST.LOG").write_text("Original stream quiet-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "no-write flush"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                "Original stream no-write flush DOS probe: PASS\n"
                "Original stream quiet-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "descriptor-table"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                "Original stream no-write flush DOS probe: PASS\n"
                "Original stream descriptor-table DOS probe: PASS\n"
                "Original stream quiet-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "CRT binary descriptor"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                "Original stream no-write flush DOS probe: PASS\n"
                "Original stream descriptor-table DOS probe: PASS\n"
                "Original CRT binary descriptor DOS probe: PASS\n"
                "Original stream quiet-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "CRT text no-LF"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                "Original stream no-write flush DOS probe: PASS\n"
                "Original stream descriptor-table DOS probe: PASS\n"
                "Original CRT binary descriptor DOS probe: PASS\n"
                "Original CRT text no-LF DOS probe: PASS\n"
                "Original stream quiet-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "CRT text LF high-stack"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                (output / "STRTEST.LOG").read_text() +
                "Original CRT text LF high-stack DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "CRT text LF low-stack"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                (output / "STRTEST.LOG").read_text() +
                "Original CRT text LF low-stack DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "active stream flush-close"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                (output / "STRTEST.LOG").read_text() +
                "Original active stream flush-close DOS probe: PASS\n")
            with self.assertRaisesRegex(ValueError, "owned CRT stream buffer"):
                msc51.validate_stream_probe(output)
            (output / "STRTEST.LOG").write_text(
                (output / "STRTEST.LOG").read_text() +
                "Original owned CRT stream buffer DOS probe: PASS\n")
            (output / "CTRLTEST.LOG").write_text("not passed\n")
            with self.assertRaisesRegex(ValueError, "fifth cleanup"):
                msc51.validate_stream_probe(output)
            (output / "CTRLTEST.LOG").write_text(
                "Original Ctrl-Break fifth cleanup DOS probe: PASS\n")
            (output / "BRKTEST.LOG").write_text("no entry\n")
            with self.assertRaisesRegex(ValueError, "outer six-call"):
                msc51.validate_stream_probe(output)
            (output / "BRKTEST.LOG").write_text(
                "Ctrl-Break outer six-call DOS probe: entered\n")
            (output / "STRSIG.LOG").write_text(msc51.stream_source_signature(output))
            msc51.validate_stream_probe(output)
            (output / wrapper).write_text((output / wrapper).read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source changed"):
                msc51.validate_stream_probe(output)
            msc51.remove_stream_outputs(output)
            with self.assertRaisesRegex(ValueError, "not passed"):
                msc51.validate_stream_probe(output)

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
            (output / "hwtest.log").write_text(
                "Hardware Ctrl-Break silent audio: PASS\n"
                "Hardware original DOS vector setter: PASS")
            (output / "hwsig.log").write_text(msc51.hardware_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-runtime-files first", error.getvalue())
            (output / "rtfile.obj").write_bytes(fake_runtime_file_obj())
            (output / "rtfstart.log").write_text("tested")
            (output / "rtfok.log").write_text("passed")
            (output / "rtfsig.log").write_text(msc51.runtime_file_source_signature(output))
            (output / "rtfile.obj").write_bytes(fake_runtime_file_obj(False))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("RTFILE.OBJ lacks public _gate_linked_print_intr_b006848b",
                          error.getvalue())
            (output / "rtfile.obj").write_bytes(fake_runtime_file_obj())
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-picture-dcl first", error.getvalue())
            (output / "pictdcl.obj").write_bytes(b"real picture decoder")
            (output / "picstart.log").write_text("tested")
            (output / "picok.log").write_text("passed")
            (output / "picsig.log").write_text(msc51.picture_dcl_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-resident-state first", error.getvalue())
            (output / "resstate.obj").write_bytes(b"real resident state")
            (output / "resstart.log").write_text("tested")
            (output / "resok.log").write_text("passed")
            (output / "ressig.log").write_text(msc51.resident_state_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-heap first", error.getvalue())
            (output / "heap.obj").write_bytes(b"DOS heap")
            (output / "heapstrt.log").write_text("tested")
            (output / "heapok.log").write_text("passed")
            (output / "heapsig.log").write_text(msc51.heap_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("gateway-msc51 compile-stream", error.getvalue())
            for name in ("strchk.exe", "strstart.log", "strok.log", "ctrl.obj",
                         "ctrlchk.exe", "break.obj", "brexit.obj", "brkchk.exe"):
                (output / name).write_text("built\n")
            witnesses = (
                "Original stream quiet-close DOS probe: PASS",
                "Original stream no-write flush DOS probe: PASS",
                "Original stream descriptor-table DOS probe: PASS",
                "Original CRT binary descriptor DOS probe: PASS",
                "Original CRT text no-LF DOS probe: PASS",
                "Original CRT text LF high-stack DOS probe: PASS",
                "Original CRT text LF low-stack DOS probe: PASS",
                "Original active stream flush-close DOS probe: PASS",
                "Original owned CRT stream buffer DOS probe: PASS",
            )
            (output / "strtest.log").write_text("\n".join(witnesses))
            (output / "ctrltest.log").write_text(
                "Original Ctrl-Break fifth cleanup DOS probe: PASS\n")
            (output / "brktest.log").write_text(
                "Ctrl-Break outer six-call DOS probe: entered\n")
            (output / "strsig.log").write_text(msc51.stream_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-input first", error.getvalue())
            (output / "inp.obj").write_bytes(b"BIOS input")
            (output / "inpstart.log").write_text("tested")
            (output / "inpok.log").write_text("passed")
            (output / "inpsig.log").write_text(msc51.input_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-clock first", error.getvalue())
            (output / "clock.obj").write_bytes(b"DOS clock")
            (output / "timer.obj").write_bytes(b"INT 1Ch check")
            (output / "tmrchk.exe").write_bytes(b"INT 1Ch probe")
            (output / "tmrtest.log").write_text(
                "INT 1Ch probe: PASS ticks=2, prior vector restored\n"
                "PIT 60Hz probe: PASS ticks=8 loops_per_tick=3654, "
                "prior vector restored, PIT reset to FFFFh, "
                "DOS date/time restored\n")
            (output / "clkstart.log").write_text("tested")
            (output / "clkok.log").write_text("passed")
            (output / "clksig.log").write_text(msc51.clock_source_signature(output))
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("msc51-compile-crt first", error.getvalue())
            (output / "crtenv.obj").write_bytes(b"Microsoft CRT")
            (output / "crtstart.log").write_text("tested")
            (output / "crtok.log").write_text("passed")
            (output / "crtsig.log").write_text(msc51.crt_source_signature(output))
            # The linked source-C entry now consumes the CRC-checked data
            # image; a stale or missing MAIN/DATA/BOOT/SEL cannot link.
            for name in ("main.obj", "data.obj", "boot.obj", "sel.obj",
                         "datareal.exe", "datachk.exe", "bootprep.exe",
                         "datstart.log", "datok.log"):
                (output / name).write_bytes(b"DOS startup")
            (output / "dattest.log").write_text(
                "Static EXE/RTL data probe: PASS (5976 + 13714 bytes; 27 seg + 24 off + 19 external pointers)\n")
            (output / "datsig.log").write_text(msc51.static_data_source_signature(output))
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

            # A shared RTL layout update can invalidate multiple probes.
            # All requested rebuilds must appear in one rejection, without
            # invoking LINK or treating its previous result as fresh.
            (output / "rtlsig.log").write_text("stale RTL layout")
            (output / "picsig.log").write_text("stale picture layout")
            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit):
                    msc51.main(args)
                run.assert_not_called()
            self.assertIn("gateway-msc51 compile-rtl-state", error.getvalue())
            self.assertIn("gateway-msc51 compile-picture-dcl", error.getvalue())
            self.assertIn("source has changed since its test", error.getvalue())
            self.assertNotIn("msc51-compile-input:", error.getvalue())

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
                (output / "HWTEST.LOG").write_text(
                    "Hardware Ctrl-Break silent audio: PASS\n"
                    "Hardware original DOS vector setter: PASS\nHardware probe: PASS")
                (output / "HWSIG.LOG").write_text(msc51.hardware_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "HWTEST.LOG").write_text("Hardware probe: PASS")
            with self.assertRaisesRegex(ValueError, "silent-audio shutdown"):
                msc51.validate_hardware_adapter(output)
            (output / "HWTEST.LOG").write_text("Hardware Ctrl-Break silent audio: PASS")
            with self.assertRaisesRegex(ValueError, "interrupt-vector setter"):
                msc51.validate_hardware_adapter(output)
            (output / "HWTEST.LOG").write_text(
                "Hardware Ctrl-Break silent audio: PASS\n"
                "Hardware original DOS vector setter: PASS")

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
                (output / "RTFILE.OBJ").write_bytes(fake_runtime_file_obj())
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
                (output / "RTFILE.OBJ").write_bytes(fake_runtime_file_obj())
                (output / "RTFOK.LOG").write_text("passed")
                (output / "RTFTEST.LOG").write_text("Runtime file probe: PASS")
                (output / "RTFSIG.LOG").write_text(msc51.runtime_file_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "RTFILE.OBJ").write_bytes(fake_runtime_file_obj(False))
            with self.assertRaisesRegex(ValueError,
                                        "RTFILE.OBJ lacks public _gate_linked_print_intr_b006848b"):
                msc51.validate_runtime_file_adapter(output)
            (output / "RTFILE.OBJ").write_bytes(fake_runtime_file_obj())
            (output / "RTFILE.C").write_text((output / "RTFILE.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_runtime_file_adapter(output)

    def test_compile_picture_dcl_requires_fresh_dos_file_and_rtl_probes(self):
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
            args = ["compile-picture-dcl", "--compiler", str(compiler),
                    "--output", str(output)]
            (output / "memory.obj").write_bytes(b"memory")
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
            (output / "dosfile.obj").write_bytes(b"dos files")
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
            for name in ("rtlimg", "rtltext", "rtlpal", "rtlgr"):
                (output / f"{name}.obj").write_bytes(b"state")
            (output / "rtlstart.log").write_text("tested")
            (output / "rtlok.log").write_text("passed")
            (output / "rtlsig.log").write_text(msc51.rtl_state_source_signature(output))

            def failed_probe(command, check):
                self.assertIn("PICTDCL.BAT", command)
                self.assertFalse(check)
                (output / "PICSTART.LOG").write_text("started")
                (output / "PICTDCL.OBJ").write_bytes(b"compiled")
                (output / "PICFAIL.LOG").write_text("DCL probe bad")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("DCL probe bad", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("PICTDCL.BAT", command)
                self.assertFalse((output / "PICFAIL.LOG").exists())
                self.assertFalse((output / "PICTDCL.OBJ").exists())
                (output / "PICSTART.LOG").write_text("started")
                (output / "PICTDCL.OBJ").write_bytes(b"compiled")
                (output / "PICOK.LOG").write_text("passed")
                (output / "PICTEST.LOG").write_text("DCL probe: PASS")
                (output / "PICSIG.LOG").write_text(msc51.picture_dcl_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)
            (output / "PICTDCL.C").write_text((output / "PICTDCL.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_picture_dcl_adapter(output)

    def test_compile_resident_state_guards_fresh_bios_and_memory_probe(self):
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
            args = ["compile-resident-state", "--compiler", str(compiler),
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
            for name in ("RESSTATE.OBJ", "RESSTART.LOG", "RESOK.LOG", "RESSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("RESST.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "RESSTATE.OBJ").exists())
                self.assertFalse((output / "RESOK.LOG").exists())
                (output / "RESSTART.LOG").write_text("started")
                (output / "RESSTATE.OBJ").write_bytes(b"compiled")
                (output / "RESFAIL.LOG").write_text("BIOS video mismatch")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("BIOS video mismatch", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("RESST.BAT", command)
                self.assertFalse((output / "RESFAIL.LOG").exists())
                self.assertFalse((output / "RESSTATE.OBJ").exists())
                (output / "RESSTART.LOG").write_text("started")
                (output / "RESSTATE.OBJ").write_bytes(b"compiled")
                (output / "RESOK.LOG").write_text("passed")
                (output / "RESTEST.LOG").write_text("Resident state probe: PASS")
                (output / "RESSIG.LOG").write_text(msc51.resident_state_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "RESSTATE.C").write_text((output / "RESSTATE.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_resident_state_adapter(output)

    def test_compile_heap_guards_real_memory_and_large_allocation_probe(self):
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
            args = ["compile-heap", "--compiler", str(compiler), "--output", str(output)]
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
            for name in ("HEAP.OBJ", "HEAPSTRT.LOG", "HEAPOK.LOG", "HEAPSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("HEAP.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "HEAP.OBJ").exists())
                (output / "HEAPSTRT.LOG").write_text("started")
                (output / "HEAP.OBJ").write_bytes(b"compiled")
                (output / "HEAPFAIL.LOG").write_text("VFX 64KiB wrap mismatch")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("64KiB wrap mismatch", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("HEAP.BAT", command)
                self.assertFalse((output / "HEAPFAIL.LOG").exists())
                self.assertFalse((output / "HEAP.OBJ").exists())
                (output / "HEAPSTRT.LOG").write_text("started")
                (output / "HEAP.OBJ").write_bytes(b"compiled")
                (output / "HEAPOK.LOG").write_text("passed")
                (output / "HEAPTST.LOG").write_text("Heap probe: PASS")
                (output / "HEAPSIG.LOG").write_text(msc51.heap_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "HEAP.C").write_text((output / "HEAP.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_heap_adapter(output)

    def test_compile_input_guards_real_dos_keyboard_probe(self):
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
            args = ["compile-input", "--compiler", str(compiler), "--output", str(output)]
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
            for name in ("INP.OBJ", "INPSTART.LOG", "INPOK.LOG", "INPSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("INPUT.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "INP.OBJ").exists())
                (output / "INPSTART.LOG").write_text("started")
                (output / "INP.OBJ").write_bytes(b"compiled")
                (output / "INPFAIL.LOG").write_text("unget key mismatch")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("unget key mismatch", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("INPUT.BAT", command)
                self.assertFalse((output / "INPFAIL.LOG").exists())
                self.assertFalse((output / "INP.OBJ").exists())
                (output / "INPSTART.LOG").write_text("started")
                (output / "INP.OBJ").write_bytes(b"compiled")
                (output / "INPOK.LOG").write_text("passed")
                (output / "INPTEST.LOG").write_text("Input probe: PASS")
                (output / "INPSIG.LOG").write_text(msc51.input_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)

            (output / "INP.C").write_text((output / "INP.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_input_adapter(output)

    def test_compile_clock_requires_fresh_dos_clock_probe(self):
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
            args = ["compile-clock", "--compiler", str(compiler), "--output", str(output)]
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
            for name in ("CLOCK.OBJ", "TIMER.OBJ", "TMRCHK.EXE", "TMRTEST.LOG",
                         "CLKSTART.LOG", "CLKOK.LOG", "CLKSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("CLOCK.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "CLOCK.OBJ").exists())
                self.assertFalse((output / "TIMER.OBJ").exists())
                (output / "CLKSTART.LOG").write_text("started")
                (output / "CLOCK.OBJ").write_bytes(b"compiled")
                (output / "CLKFAIL.LOG").write_text("DOS clock did not advance")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("clock did not advance", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("CLOCK.BAT", command)
                self.assertFalse((output / "CLKFAIL.LOG").exists())
                self.assertFalse((output / "CLOCK.OBJ").exists())
                (output / "CLKSTART.LOG").write_text("started")
                (output / "CLOCK.OBJ").write_bytes(b"compiled")
                (output / "TIMER.OBJ").write_bytes(b"compiled")
                (output / "TMRCHK.EXE").write_bytes(b"compiled")
                (output / "TMRTEST.LOG").write_text(
                    "INT 1Ch probe: PASS ticks=2, prior vector restored\n"
                    "PIT 60Hz probe: PASS ticks=8 loops_per_tick=3654, "
                    "prior vector restored, PIT reset to FFFFh, "
                    "DOS date/time restored\n")
                (output / "CLKOK.LOG").write_text("passed")
                (output / "CLKTEST.LOG").write_text("Clock probe: PASS")
                (output / "CLKSIG.LOG").write_text(msc51.clock_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)
            (output / "TMRTEST.LOG").write_text(
                "INT 1Ch probe: PASS ticks=2, prior vector restored\n"
                "PIT 60Hz probe: PASS ticks=8 loops_per_tick=3654, "
                "prior vector restored, PIT reset to FFFFh\n")
            with self.assertRaisesRegex(ValueError, "DOS date/time restoration probe"):
                msc51.validate_clock_adapter(output)
            (output / "CLOCK.C").write_text((output / "CLOCK.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_clock_adapter(output)

    def test_compile_crt_requires_verified_original_dos_library(self):
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
            args = ["compile-crt", "--compiler", str(compiler), "--output", str(output)]
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
            for name in ("CRTENV.OBJ", "CRTSTART.LOG", "CRTOK.LOG", "CRTSIG.LOG"):
                (output / name).write_text("stale")

            def failed_probe(command, check):
                self.assertIn("CRT.BAT", command)
                self.assertFalse(check)
                self.assertFalse((output / "CRTENV.OBJ").exists())
                (output / "CRTSTART.LOG").write_text("started")
                (output / "CRTENV.OBJ").write_bytes(b"compiled")
                (output / "CRTFAIL.LOG").write_text("getenv failed")
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=failed_probe), \
                 redirect_stderr(io.StringIO()) as error:
                self.assertEqual(msc51.main(args), 1)
            self.assertIn("getenv failed", error.getvalue())

            def successful_probe(command, check):
                self.assertIn("CRT.BAT", command)
                self.assertFalse((output / "CRTFAIL.LOG").exists())
                self.assertFalse((output / "CRTENV.OBJ").exists())
                (output / "CRTSTART.LOG").write_text("started")
                (output / "CRTENV.OBJ").write_bytes(b"compiled")
                (output / "CRTOK.LOG").write_text("passed")
                (output / "CRTTEST.LOG").write_text("CRT probe: PASS")
                (output / "CRTSIG.LOG").write_text(msc51.crt_source_signature(output))
                return mock.Mock(returncode=0)

            with mock.patch.object(msc51.shutil, "which", return_value="/usr/bin/false"), \
                 mock.patch.object(msc51.subprocess, "run", side_effect=successful_probe):
                self.assertEqual(msc51.main(args), 0)
            (output / "CRTENV.C").write_text((output / "CRTENV.C").read_text() + "/* changed */\n")
            with self.assertRaisesRegex(ValueError, "source has changed"):
                msc51.validate_crt_adapter(output)

    def test_stages_only_recovered_source_with_c89_headers(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch)
            msc51.stage_probe(output)
            original_random = (REPO / "decomp/launcher/gate_c_runtime_random.c").read_text()
            original_probe = (REPO / "decomp/launcher/dos/intro_probe.c").read_text()
            original_constants = (REPO / "decomp/launcher/gate_c_runtime_constants.h").read_text()
            original_layout = (REPO / "decomp/launcher/gate_memory_layout.h").read_text()
            self.assertEqual((output / "RANDOM.C").read_text(),
                             original_random.replace('#include "gate_rtl_image.h"', '#include "GATE.H"', 1)
                                            .replace('#include "gate_c_runtime_constants.h"', '#include "CONST.H"', 1))
            self.assertEqual((output / "CONST.H").read_text(),
                             msc51.msc51_probe_constants(original_constants, (
                                 "GATE_CRT_RANDOM_MULTIPLIER", "GATE_CRT_RANDOM_INCREMENT",
                                 "GATE_CRT_RANDOM_RESULT_MASK", "GATE_CRT_WORD_BITS")))
            self.assertIn("#define GATE_CRT_RANDOM_MULTIPLIER (0x000343fd)",
                          (output / "CONST.H").read_text())
            self.assertEqual((output / "PROBE.C").read_text(),
                             original_probe.replace('#include "../gate_exe.h"', '#include "GATE.H"', 1))
            header = (output / "GATE.H").read_text()
            self.assertIn(msc51.msc51_probe_constants(original_layout, (
                "GATE_DS_RANDOM_STATE_LOW", "GATE_DS_RANDOM_STATE_HIGH")), header)
            with self.assertRaisesRegex(ValueError, "Missing original rand constant"):
                msc51.msc51_probe_constants(original_constants, ("NO_SUCH_RANDOM_CONSTANT",))
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

    def test_msc51_dosbox_compiler_cpu_speed_and_compatibility_overrides(self):
        self.assertEqual(msc51.dosbox_performance_args(),
                         ["--set", "core=dynamic", "--set", "cpu_cycles=max"])
        self.assertEqual(msc51.dosbox_performance_args("25000", "normal"),
                         ["--set", "core=normal", "--set", "cpu_cycles=25000"])
        self.assertEqual(msc51.dosbox_performance_args("auto", "auto"),
                         ["--set", "core=auto", "--set", "cpu_cycles=3000"])
        self.assertEqual(msc51.dosbox_performance_args("default", "default"), [])
        for invalid in ("", "0", "49", "2000001", "overclock", "2.5", "max --exit",
                        "٥٠", "0100000000000000000000000000000000000000000"):
            with self.subTest(cycles=invalid):
                with self.assertRaisesRegex(ValueError, "--dosbox-cycles"):
                    msc51.dosbox_performance_args(invalid, "dynamic")
        with self.assertRaisesRegex(ValueError, "--dosbox-core"):
            msc51.dosbox_performance_args("max", "hyper")

    def test_msc51_headless_dosbox_invocation(self):
        self.assertEqual(msc51.dosbox_headless_args(), [
            "--noprimaryconf", "--nolocalconf",
            "--set", "mouse_capture=nomouse", "--set", "mididevice=none",
            "--set", "output=texture", "--set", "texture_renderer=software",
        ])
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            compiler = root / "compiler"
            for subdir in ("BIN", "LIB", "INCLUDE"):
                (compiler / subdir).mkdir(parents=True)
            for name in ("CL.EXE", "C1.EXE", "C1L.EXE", "C2.EXE", "C3.EXE", "LINK.EXE"):
                (compiler / "BIN" / name).touch()
            for name in ("LLIBCR.LIB", "LIBH.LIB"):
                (compiler / "LIB" / name).touch()
            (compiler / "INCLUDE" / "STDIO.H").touch()

            with mock.patch.object(msc51.shutil, "which", return_value="/dosbox"), \
                 mock.patch.object(msc51.subprocess, "run") as run, \
                 redirect_stderr(io.StringIO()):
                run.return_value.returncode = 17
                status = msc51.main(["probe", "--compiler", str(compiler),
                                     "--output", str(root / "output"), "--headless"])
            self.assertEqual(status, 17)
            command = run.call_args.args[0]
            self.assertEqual(command[0], "/dosbox")
            self.assertIn("--noprimaryconf", command)
            self.assertIn("--nolocalconf", command)
            self.assertIn("mouse_capture=nomouse", command)
            self.assertIn("output=texture", command)
            self.assertIn("texture_renderer=software", command)
            self.assertIn("cpu_cycles=max", command)
            self.assertEqual(run.call_args.kwargs["env"]["SDL_VIDEODRIVER"], "dummy")
            self.assertEqual(run.call_args.kwargs["env"]["SDL_AUDIODRIVER"], "dummy")
            self.assertIn("--exit", command)

        with self.assertRaises(SystemExit):
            msc51.main(["shell", "--headless"])

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

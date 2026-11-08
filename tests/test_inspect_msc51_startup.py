#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic startup captures; proprietary DOS executable not required."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "inspect_msc51_startup", REPO / "tools/inspect_msc51_startup.py")
assert SPEC is not None and SPEC.loader is not None
startup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(startup)


def put_word(data, offset, value):
    data[offset:offset + 2] = value.to_bytes(2, "little")


def fixture():
    exe = bytearray(0x620)
    exe[:2] = b"MZ"
    for off, value in ((2, 0x20), (4, 4), (6, 0), (8, 2),
                       (0x0e, 0x2229), (0x10, 0x400), (0x14, 0x71),
                       (0x16, 0x2c), (0x18, 0x1c)):
        put_word(exe, off, value)
    image = bytes((i * 17 + 61) % 251 for i in range(0x600))
    exe[0x20:] = image

    # An actual bank has relocation operand differences. Use distinct source
    # bytes in this test to distinguish validated bank from zero-filled RAM.
    rtl = bytearray(0x10000)
    rtl[0x56de:0x56de + 0x3d00] = bytes(
        (i * 23 + i // 17 + 79) % 251 for i in range(0x3d00))
    return bytes(exe), bytes(rtl)


def regs(cs, ip, ds, ss, sp):
    return dict(CS=cs, IP=ip, DS=ds, ES=ds, SS=ss, SP=sp)


class StartupSnapshotTests(unittest.TestCase):
    def test_phase_transitions_and_source_identity(self):
        exe, rtl = fixture()
        initial = bytearray(startup.RAM_SIZE)
        initial[0x10000:0x10600] = exe[0x20:]
        bank = rtl[0x56de:0x56de + 0x3d00]
        loaded = bytearray(initial)
        loaded[0x12a50:0x12a50 + len(bank)] = bank
        # A linker may change relocation operands without replacing the bank.
        loaded[0x12a50 + 0x140:0x12a50 + 0x144] = b"\xcc" * 4
        executing = bytearray(loaded)
        executing[0x2a7a0 + 0x65c:0x2a7a0 + 0x65e] = b"\x7f\x00"
        executing[0x2a7a0 + 0x65e:0x2a7a0 + 0x660] = b"\x01\x00"
        captures = [
            (0, regs(0x102c, 0x71, 0x0ff0, 0x3229, 0x400), bytes(initial)),
            (100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4), bytes(loaded)),
            (500, regs(0x12a5, 0x3570, 0x2a7a, 0x2a7a, 0x7a1a), bytes(executing)),
        ]
        report = startup.analyze(exe, rtl, captures)
        self.assertEqual(report["original_exe"]["image_load_segment"], 0x1000)
        self.assertEqual(report["original_exe"]["relocation_count"], 0)
        self.assertEqual(report["observed_runtime_data_segment"], 0x2a7a)
        start, load, run = report["snapshots"]
        self.assertTrue(start["initial_mz_entry"])
        self.assertTrue(start["initial_mz_stack"])
        self.assertEqual(start["exe_image_matching_bytes"], 0x600)
        self.assertFalse(start["resident_bank_verified"])
        self.assertEqual(start["instruction_source"],
                         {"kind": "exe-exact-16", "file_offset": 0x351})
        self.assertTrue(load["resident_bank_verified"])
        self.assertEqual(load["resident_bank_matching_bytes"], 0x3d00 - 4)
        self.assertEqual(len(load["resident_bank_witness_offsets"]), 3)
        self.assertEqual(load["instruction_source"]["kind"], "unmapped")
        self.assertTrue(run["data_stack_same_segment"])
        self.assertEqual(run["instruction_source"],
                         {"kind": "rtl-exact-16", "file_offset": 0x8c4e})
        self.assertEqual(run["selected_ds_words"]["065e"], 1)
        self.assertEqual(start["selected_runtime_data_words"]["065e"], 0)
        self.assertEqual(run["selected_runtime_data_words"]["065e"], 1)
        self.assertEqual(run["changed_memory_bytes"], 2)

    def test_capture_rejects_incomplete_and_duplicate_data(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            with self.assertRaisesRegex(ValueError, "baseline"):
                startup.snapshots_from(folder)
            (folder / "at-0.regs").write_text(
                "CS=102c\nIP=71\nDS=ff0\nES=ff0\nSS=3229\nSP=400\n")
            with self.assertRaisesRegex(ValueError, "RAM missing"):
                startup.snapshots_from(folder)
            (folder / "at-0.bin").write_bytes(b"truncated")
            with self.assertRaisesRegex(ValueError, "expected 1 MiB"):
                startup.snapshots_from(folder)
            (folder / "at-0.bin").write_bytes(bytes(startup.RAM_SIZE))
            self.assertEqual(len(startup.snapshots_from(folder)), 1)
            (folder / "at-0.regs").write_text("CS=102c\nCS=102c\n")
            with self.assertRaisesRegex(ValueError, "duplicate register"):
                startup.snapshots_from(folder)

    def test_mz_rejects_wrong_declared_length_and_invalid_fixups(self):
        exe, _ = fixture()
        self.assertEqual(startup.mz_image(exe)["image_bytes"], 0x600)
        bad = bytearray(exe)
        put_word(bad, 2, 1)
        with self.assertRaisesRegex(ValueError, "declared file length"):
            startup.mz_image(bad)
        bad = bytearray(exe)
        put_word(bad, 6, 2)
        with self.assertRaisesRegex(ValueError, "relocation directory"):
            startup.mz_image(bad)

    def test_bank_requires_independent_witnesses(self):
        exe, rtl = fixture()
        ram = bytearray(startup.RAM_SIZE)
        ram[0x10000:0x10600] = exe[0x20:]
        ram[0x12a50:0x16750] = rtl[0x56de:0x93de]
        first = 0x12a50 + 0x8298 - 0x56de
        # A large aggregate overlap by itself must not verify the bank.
        ram[first:first + 64] = bytes(64)
        capture = [(0, regs(0x102c, 0x71, 0xff0, 0x3229, 0x400), bytes(ram))]
        report = startup.analyze(exe, rtl, capture)
        self.assertGreater(report["snapshots"][0]["resident_bank_matching_bytes"], 15000)
        self.assertFalse(report["snapshots"][0]["resident_bank_verified"])
        self.assertIsNone(report["observed_runtime_data_segment"])


if __name__ == "__main__":
    unittest.main()

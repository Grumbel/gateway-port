# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Validate the bounded 02a5 resident CALL FAR relocation cluster."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import msc51  # noqa: E402


def manifest():
    source = (ROOT / "decomp/launcher/dos/msc51_startup_static_data.c").read_text()
    match = re.search(
        r"static const GateResidentSelfCall gate_resident_self_calls\[\] = \{(.*?)\n\};",
        source, re.DOTALL)
    if match is None:
        raise AssertionError("resident self-call relocation table missing")
    return [(int(site, 16), int(target, 16)) for site, target in
            re.findall(r"\{0x([0-9a-f]+)u, 0x([0-9a-f]+)u\}", match.group(1))]


def ds_load_manifest():
    source = (ROOT / "decomp/launcher/dos/msc51_startup_static_data.c").read_text()
    match = re.search(r"static const uint16_t gate_resident_ds_loads\[\] = "
                      r"\{(.*?)\};", source, re.DOTALL)
    if match is None:
        raise AssertionError("resident DS-load relocation table missing")
    return [int(value, 16) for value in re.findall(r"0x([0-9a-f]+)u", match.group(1))]


def direct_segment_manifest(name):
    source = (ROOT / "decomp/launcher/dos/msc51_startup_static_data.c").read_text()
    match = re.search(r"static const uint16_t " + name +
                      r"\[\] = \{(.*?)\};", source, re.DOTALL)
    if match is None:
        raise AssertionError(name + " manifest missing")
    return [int(value, 16) for value in re.findall(r"0x([0-9a-f]+)u", match.group(1))]


class ResidentSelfCallTests(unittest.TestCase):
    def test_early_data_words_against_original_ram_captures(self):
        rtl_path = ROOT.parent / "gateway-dos/GATE.RTL"
        captures = [ROOT / "build/msc51-startup/at-100.bin",
                    ROOT / "build/msc51-startup-fine/at-125.bin",
                    ROOT / "build/msc51-startup-fine/at-175.bin",
                    ROOT / "build/msc51-startup-fine/at-250.bin",
                    ROOT / "build/msc51-startup/at-500.bin"]
        if not rtl_path.exists() or not all(p.exists() for p in captures):
            self.skipTest("Original RTL and five DOS RAM captures unavailable")
        source = rtl_path.read_bytes()
        self.assertEqual(source[0x29 + 0x518], 0)
        self.assertEqual(source[0x29 + 0x547:0x29 + 0x557], bytes(16))
        expected = bytes.fromhex("e000ef000b020b0203020b01fe001d01")
        for path in captures:
            ram = path.read_bytes()
            base = 0x2a7a0
            self.assertEqual(ram[base + 0x518], 0x15, path.name)
            self.assertEqual(ram[base + 0x547:base + 0x557], expected, path.name)

    def test_synthetic_dos_adapter_and_fail_closed_preflight(self):
        if not shutil.which("gcc"):
            self.skipTest("GCC needed for the C89 host relocator test")
        self.assertEqual(len(manifest()), 98)
        self.assertEqual(len(ds_load_manifest()), 7)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            msc51.stage_launcher(out)
            executable = out / "resident-self-calls-host"
            compiled = subprocess.run(
                ["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror",
                 "-I", str(out),
                 str(ROOT / "tests/fixtures/msc51_data/resident_self_calls_host.c"),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("98 self-calls, 7 DS loads, EXE call and 7 direct segments: PASS",
                          ran.stdout)

    def test_original_source_and_four_stable_live_captures(self):
        rtl_path = ROOT.parent / "gateway-dos/GATE.RTL"
        captures = [ROOT / f"build/msc51-startup-fine/at-{ms}.bin"
                    for ms in (125, 175, 250)]
        captures.append(ROOT / "build/msc51-startup/at-500.bin")
        if not rtl_path.exists() or not all(p.exists() for p in captures):
            self.skipTest("Original RTL and genuine early DOS RAM captures unavailable")
        rtl = rtl_path.read_bytes()[0x56de:0x93de]
        live = [p.read_bytes()[0x12a50:0x12a50 + 0x3d00] for p in captures]
        pairs = manifest()
        self.assertEqual(len(pairs), 98)
        self.assertEqual([site for site, _ in pairs], sorted(set(site for site, _ in pairs)))
        for site, target in pairs:
            self.assertEqual(rtl[site:site + 5], b"\x9a" + bytes(4), hex(site))
            for snapshot in live:
                self.assertEqual(snapshot[site], 0x9a, hex(site))
                self.assertEqual(int.from_bytes(snapshot[site + 1:site + 3], "little"),
                                 target, hex(site))
                self.assertEqual(snapshot[site + 3:site + 5], b"\xa5\x12", hex(site))
        for site in ds_load_manifest():
            self.assertEqual(rtl[site - 1:site + 4], b"\xb8\x00\x00\x8e\xd8")
            for snapshot in live:
                self.assertEqual(snapshot[site - 1:site + 4],
                                 b"\xb8\x7a\x2a\x8e\xd8", hex(site))
        self.assertEqual(rtl[0x0a4d:0x0a52], b"\x9a" + bytes(4))
        for snapshot in live:
            self.assertEqual(snapshot[0x0a4d:0x0a52],
                             b"\x9a\x30\x00\x00\x10")
        cmp_sites = direct_segment_manifest("gate_resident_cmp_ds")
        dx_sites = direct_segment_manifest("gate_resident_mov_dx_ds")
        self.assertEqual(len(cmp_sites), 3)
        self.assertEqual(len(dx_sites), 2)
        for site in cmp_sites:
            self.assertEqual(rtl[site - 3:site + 2], b"\x81\x7e\x0a\x00\x00")
            for snapshot in live:
                self.assertEqual(snapshot[site - 3:site + 2],
                                 b"\x81\x7e\x0a\x7a\x2a", hex(site))
        for site in dx_sites:
            self.assertEqual(rtl[site - 4], 0xb8)
            self.assertEqual(rtl[site - 1:site + 2], b"\xba\x00\x00")
            for snapshot in live:
                self.assertEqual(snapshot[site - 1:site + 2], b"\xba\x7a\x2a")
        self.assertEqual(rtl[0x09b8:0x09bb], b"\xbf\x00\x00")
        self.assertEqual(rtl[0x0197:0x019e], b"\x26\xc7\x06\x72\x00\x00\x00")
        for snapshot in live:
            self.assertEqual(snapshot[0x09b8:0x09bb], b"\xbf\x7a\x2a")
            self.assertEqual(snapshot[0x0197:0x019e],
                             b"\x26\xc7\x06\x72\x00\xa5\x12")


if __name__ == "__main__":
    unittest.main()

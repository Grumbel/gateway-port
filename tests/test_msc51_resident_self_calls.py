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


class ResidentSelfCallTests(unittest.TestCase):
    def test_synthetic_dos_adapter_and_fail_closed_preflight(self):
        if not shutil.which("gcc"):
            self.skipTest("GCC needed for the C89 host relocator test")
        self.assertEqual(len(manifest()), 98)
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
            self.assertIn("98 resident self CALL FAR relocations: PASS", ran.stdout)

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


if __name__ == "__main__":
    unittest.main()

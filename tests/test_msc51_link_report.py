#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Source-only tests for parsing the Microsoft LINK 3.65 L2029 inventory."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, REPO / f"tools/{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msc51 = load_tool("msc51")
reporter = load_tool("msc51_link_report")


class LinkReportTests(unittest.TestCase):
    def test_report_restores_aliases_original_addresses_and_guest_references(self):
        with tempfile.TemporaryDirectory() as scratch:
            stage = Path(scratch)
            msc51.stage_launcher(stage)
            aliases = reporter.read_mapping(stage / "SYMBOL.MAP")
            shortened = next(key for key, value in aliases.items()
                             if value == "gate_linked_format_resource_filename")
            # Synthetic subset of the reported LINK 3.65 transcript. No
            # original executable or game data is required to run this test.
            log = f"""Microsoft (R) Overlay Linker  Version 3.65

LINK : error L2029: Unresolved externals:

_gate_far_byte in file(s):
 M27.OBJ(M27.C) M08.OBJ(M08.C)
_gate_linked_intro_mouse_flags in file(s):
 M04.OBJ(M04.C)
_{shortened} in file(s):
 M11.OBJ(M11.C)
_main in file(s):
 C:\\LIB\\LLIBCR.LIB(dos\\crt0.asm)
_gate_rtl_video_columns in file(s):
 M26.OBJ(M26.C)
_gate_rtl_string_length in file(s):
 M27.OBJ(M27.C)

There were 108 errors detected
"""
            report = reporter.build_report(log, stage, REPO / "decomp/launcher",
                                           REPO / "decomp/launcher/symbol-names.tsv")
            self.assertEqual(report["reported_link_errors"], 108)
            self.assertEqual(report["unresolved_symbols"], 6)
            self.assertEqual(report["categories"], {
                "crt_entry": 1, "guest_memory": 1,
                "linked_original_services": 2, "rtl_state_and_hardware": 2,
            })
            found = {item["name"]: item for item in report["externals"]}
            self.assertEqual(found["gate_linked_format_resource_filename"]["provenance"], {
                "kind": "observed_linked_far", "address": "12a5:3366",
                "identifier": "gate_far_12a5_3366"})
            self.assertEqual(found["gate_linked_intro_mouse_flags"]["provenance"]["address"],
                             "0e48:07fe")
            self.assertEqual(found["gate_rtl_string_length"]["provenance"], {
                "kind": "original_file_offset", "address": "rtl:0x8298",
                "identifier": "gate_rtl_8298"})
            sources = reporter.read_mapping(stage / "SOURCE.MAP")
            self.assertEqual(found["gate_far_byte"]["referenced_from"],
                             sorted([sources["M27.C"], sources["M08.C"]]))
            self.assertIn("decomp/launcher/gate_rtl_video.h",
                          found["gate_rtl_video_columns"]["declared_in"])
            self.assertEqual(found["gate_rtl_video_columns"]["rtl_ds_offsets"], ["DS:0x0004"])
            self.assertEqual(found["main"]["references"],
                             [r"C:\LIB\LLIBCR.LIB(dos\crt0.asm)"])
            self.assertNotIn("provenance", found["main"])

            (stage / "LINK.LOG").write_text(log)
            json_path = stage / "inventory.json"
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(reporter.main(["--log", str(stage / "LINK.LOG"),
                                                "--json", str(json_path), "--details"]), 0)
            self.assertIn("108 reported errors", output.getvalue())
            self.assertIn("observed_linked_far: 12a5:3366", output.getvalue())
            self.assertEqual(json.loads(json_path.read_text()), report)

    def test_merge_duplicate_symbol_blocks_and_detect_empty_logs(self):
        text = """LINK : error L2029: Unresolved externals:
_gate_ds_word in file(s):
 M00.OBJ(M00.C)
_gate_ds_word in file(s):
 M01.OBJ(M01.C) M00.OBJ(M00.C)
There were 1 errors detected
"""
        externals, count = reporter.parse_link_log(text)
        self.assertEqual(count, 1)
        self.assertEqual(externals, [{"linker_symbol": "_gate_ds_word",
                                      "references": ["M00.OBJ(M00.C)", "M01.OBJ(M01.C)"]}])
        self.assertEqual(reporter.parse_link_log("Linker banner only"), ([], None))


if __name__ == "__main__":
    unittest.main()

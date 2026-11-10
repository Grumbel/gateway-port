#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""OMF source-vs-original matcher tests without proprietary files or compiler."""

import sys
import tempfile
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import msc51_code_compare as comparison  # noqa: E402


def record(kind, payload):
    header = bytes((kind, (len(payload) + 1) & 255,
                    (len(payload) + 1) >> 8))
    return header + payload + bytes(((-sum(header + payload)) & 255,))


def omf_object(symbol, code, fixup=None):
    name = symbol.encode("ascii")
    records = [
        record(0x80, b"\x06TEST.C"),
        record(0x96, b"\x05_TEXT\x04CODE\x00"),
        record(0x98, b"\x28" + len(code).to_bytes(2, "little") + b"\x01\x02\x03"),
        record(0x90, b"\x00\x01" + bytes((len(name),)) + name + b"\x00\x00\x00"),
        record(0xa0, b"\x01\x00\x00" + code),
    ]
    if fixup is not None:
        records.append(record(0x9c, fixup))
    records.append(record(0x8a, b"\x00"))
    return b"".join(records)


class CodeComparisonTests(unittest.TestCase):
    def test_parse_and_stitch_with_relocation_mask(self):
        code = bytes(range(32))
        obj = omf_object("_gate_exe_fade_in_picture", code,
                         b"\xc4\x04\x44\x01")
        module = comparison.parse_object(obj)
        self.assertEqual(module.name, "TEST.C")
        routines = comparison.object_routines(obj)
        content, mask = routines["_gate_exe_fade_in_picture"]
        self.assertEqual(content, code)
        self.assertEqual(mask[:6], b"\x01\x01\x01\x01\x00\x00")
        self.assertEqual(mask[6:], bytes([1] * 26))

    def test_identical_relocated_and_different_classifications(self):
        routine = comparison.Routine("GATE.EXE", 3, 35, "gate_exe_fade_in_picture")
        original = b"XXX" + bytes(range(32))
        code = bytes(range(32))
        fixed = bytes([1] * 32)
        exact = comparison.compare(routine, original, code, fixed)
        self.assertEqual(exact["status"], "exact_object_bytes")
        self.assertEqual(exact["matching_fixed_prefix_bytes"], 32)
        self.assertEqual(exact["compiled_public_span_bytes"], 32)

        other = bytearray(code)
        other[4:6] = b"\xff\xff"
        mask = bytearray(fixed)
        mask[4:6] = b"\x00\x00"
        reloc = comparison.compare(routine, original, other, mask)
        self.assertEqual(reloc["status"], "relocation_aware_match")
        self.assertEqual(reloc["relocation_masked_bytes"], 2)
        self.assertEqual(reloc["matching_fixed_prefix_bytes"], 32)

        other[9] ^= 1
        different = comparison.compare(routine, original, other, mask)
        self.assertEqual(different["status"], "different")
        self.assertEqual(different["matching_fixed_prefix_bytes"], 9)
        self.assertEqual(different["equal_fixed_bytes"], 29)
        size_mismatch = comparison.compare(routine, original, code + b"\x90", fixed + b"\x01")
        self.assertEqual(size_mismatch["status"], "different")

    def test_all_masked_bytes_cannot_prove_code_matches(self):
        routine = comparison.Routine("GATE.RTL", 0, 32, "example")
        result = comparison.compare(routine, bytes(range(32)), bytes(32), bytes(32))
        self.assertEqual(result["status"], "different")
        self.assertEqual(result["fixed_compared_bytes"], 0)

    def test_reject_untrusted_or_malformed_omf(self):
        valid = omf_object("_sample", bytes(range(32)))
        for item in (b"raw", valid[:-1], valid + b"bad",
                     valid[:10] + b"bad" + valid[13:]):
            with self.subTest(item=item[:12]), self.assertRaises(ValueError):
                comparison.parse_object(item)

    def test_collect_uses_linker_inputs_and_short_name_map(self):
        routine = comparison.Routine("GATE.EXE", 0, 32, "gate_exe_fade_in_picture")
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / "LINK.RSP").write_text("M04.OBJ+\nMAIN.OBJ\n")
            (stage / "SYMBOL.MAP").write_text(
                "gate_exe_fade_1234 gate_exe_fade_in_picture\n")
            (stage / "M04.OBJ").write_bytes(omf_object(
                "_gate_exe_fade_1234", bytes(range(32))))
            (stage / "MAIN.OBJ").write_bytes(omf_object(
                "_main", bytes([0x90] * 32)))
            # Not in LINK.RSP: must not introduce duplicate _main or a
            # bogus competing definition of the function under test.
            (stage / "OTHER.OBJ").write_bytes(omf_object(
                "_gate_exe_fade_1234", bytes([0xcc] * 32)))
            report = comparison.collect(stage, {"GATE.EXE": bytes(range(32))},
                                        (routine,))
            self.assertEqual(report[0]["status"], "exact_object_bytes")
            self.assertEqual(report[0]["object"], "M04.OBJ")


if __name__ == "__main__":
    unittest.main()

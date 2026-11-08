#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Exercise real OMF record grammar without shipping proprietary objects."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("msc51_object_symbols",
                                           ROOT / "tools/msc51_object_symbols.py")
assert SPEC is not None and SPEC.loader is not None
objects = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(objects)


def record(kind: int, payload: bytes, checksum: bool = True) -> bytes:
    length = len(payload) + 1
    header = bytes((kind, length & 0xff, length >> 8))
    tail = (-sum(header + payload)) & 0xff if checksum else 0
    return header + payload + bytes((tail,))


def omf_object(name: str, publics: tuple[str, ...] = (),
               public_kind: int = 0x90) -> bytes:
    out = [record(0x80, bytes((len(name),)) + name.encode("ascii"))]
    if publics:
        payload = bytearray(b"\x00\x01")  # group=0, segment=1
        for i, public in enumerate(publics):
            raw = public.encode("ascii")
            payload.extend((len(raw),))
            payload.extend(raw)
            payload.extend((i * 3).to_bytes(4 if public_kind == 0x91 else 2,
                                           "little"))
            payload.extend(b"\x00")  # type index
        out.append(record(public_kind, bytes(payload)))
    out.append(record(0x8a, b"\x00"))
    return b"".join(out)


class OMFSymbolTests(unittest.TestCase):
    def test_relocated_externals_must_be_real_pubdefs(self):
        diagnostic = "_gate_linked_print_intr_b006848b"
        self.assertEqual(len(diagnostic), 32)
        fixture = omf_object("RTFILE.C", (diagnostic,
                                         "_gate_linked_format_res_cd21e963"))
        self.assertIn(diagnostic, objects.public_names(fixture))
        self.assertEqual(objects.public_names(omf_object("RTFILE.C")), set())
        self.assertEqual(objects.public_names(
            omf_object("RTFILE.C", ("_key",), public_kind=0x91)), {"_key"})
        self.assertEqual(objects.public_names(fixture + bytes(8)),
                         objects.public_names(fixture))
        self.assertEqual(objects.public_names(fixture + omf_object("EXTRA.C", ("_foo",))),
                         {diagnostic, "_gate_linked_format_res_cd21e963", "_foo"})

    def test_reject_truncated_corrupt_and_false_pubdef_like_bytes(self):
        valid = omf_object("X.C", ("_fn",))
        damaged = bytearray(valid)
        damaged[-6] ^= 0x10  # inside PUBDEF/MODEND tail
        for data in (valid[:-1], bytes(damaged), valid + b"\x11",
                     record(0x90, b"\x00\x01\x03_fn\x00\x00\x00"),
                     omf_object("X.C")[:-4],
                     omf_object("X.C") + record(0x90, b"\x00\x01")):
            with self.subTest(data=data[-20:]):
                with self.assertRaises(ValueError):
                    objects.public_names(data)

    def test_stage_audit_distinguishes_missing_from_exported(self):
        diagnostic = "_gate_linked_print_intr_b006848b"
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / "LINK.RSP").write_text(
                "M07.OBJ+\nRTFILE.OBJ+\nINP.OBJ\nGATEWAY.EXE\nGATEWAY.MAP\n;\n")
            (stage / "M07.OBJ").write_bytes(omf_object("M07.C"))
            (stage / "RTFILE.OBJ").write_bytes(omf_object("RTFILE.C", (diagnostic,)))
            (stage / "INP.OBJ").write_bytes(omf_object("INP.C", ("_key",)))
            log = ("_main in file(s):\n LLIBCR.LIB(crt0.asm)\n"
                   "_gate_linked_handle_control_key in file(s):\n M07.OBJ(M07.C)\n"
                   + diagnostic + " in file(s):\n M07.OBJ(M07.C)\n")
            report = objects.audit_stage(stage, ["_key"], log)
            self.assertEqual(report["objects_in_response"], 3)
            self.assertEqual(report["objects_read"], 3)
            by_symbol = {item["symbol"]: item for item in report["symbols"]}
            self.assertEqual(by_symbol["_key"]["status"], "exported")
            self.assertEqual(by_symbol[diagnostic]["status"],
                             "reported_unresolved_but_exported")
            self.assertEqual(by_symbol[diagnostic]["providers"], ["RTFILE.OBJ"])
            self.assertEqual(by_symbol["_gate_linked_handle_control_key"]["status"],
                             "no_export_in_staged_objects")
            self.assertEqual(by_symbol["_main"]["status"],
                             "no_export_in_staged_objects")
            (stage / "INP.OBJ").unlink()
            report = objects.audit_stage(stage, [], log)
            self.assertEqual(report["objects_read"], 2)
            self.assertEqual(report["missing_objects"], ["INP.OBJ"])
            (stage / "INP.OBJ").write_bytes(b"not OMF")
            report = objects.audit_stage(stage, [], log)
            self.assertIn("INP.OBJ", report["malformed_objects"])

    def test_source_has_matching_dos_alias_but_does_not_prove_obj_export(self):
        # This proves the generated source contract. PUBDEF must be checked
        # separately against the actual MSC object, not guessed from text.
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            import msc51
            with tempfile.TemporaryDirectory() as directory:
                stage = Path(directory)
                msc51.stage_launcher(stage)
                symbol = "gate_linked_print_intr_b006848b"
                self.assertIn("void " + symbol + "(", (stage / "RTFILE.C").read_text())
                self.assertIn("void " + symbol + "(", (stage / "RTFCHK.C").read_text())
                self.assertIn("RTFILE.OBJ+", (stage / "LINK.RSP").read_text())
        finally:
            sys.path.remove(str(ROOT / "tools"))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Test CRT-code matching without distributing or requiring Microsoft binaries."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("msc51_compare", REPO / "tools/msc51_compare.py")
assert SPEC is not None and SPEC.loader is not None
msc = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = msc
SPEC.loader.exec_module(msc)


class MSC51CompareTests(unittest.TestCase):
    def test_omf_module_and_exact_public_symbol_attribution(self):
        def record(kind, payload):
            header = bytes((kind, (len(payload) + 1) & 255,
                            (len(payload) + 1) >> 8))
            return header + payload + bytes(((-sum(header + payload)) & 255,))

        page_size = 32
        dictionary_offset = 512
        header = (bytes((0xf0, page_size - 3, 0)) +
                  dictionary_offset.to_bytes(4, "little") + b"\x01\x00\x00")
        lib = bytearray(header.ljust(page_size, b"\0"))
        module_one = record(0x82, b"\x06RAND.C")
        # PUBDEF: group 0, segment 1, _rand at segment offset 0x0120.
        public = record(0x90, b"\x00\x01\x05_rand\x20\x01\x00")
        signature = b"\x55\x8b\xec\x2b\x76\x01\x5d\xcb" + bytes(range(12))
        ledata = record(0xa0, b"\x01\x20\x01" + signature)
        module_bytes = module_one + public + ledata + record(0x8a, b"\x00")
        lib.extend(module_bytes)
        lib.extend(bytes((-len(lib)) % page_size))
        second = record(0x80, b"\x08STRAND.C") + record(0x8a, b"\x00")
        second_start = len(lib)
        lib.extend(second)
        lib.extend(bytes((-len(lib)) % page_size))
        lib.extend(bytes((0xf1,)))
        lib.extend(bytes(dictionary_offset - len(lib)))
        lib.extend(bytes(512))  # dictionary block

        modules = msc.parse_omf_library(bytes(lib))
        self.assertEqual([m.name for m in modules], ["RAND.C", "STRAND.C"])
        self.assertEqual(modules[1].start, second_start)
        offset = page_size + len(module_one) + len(public) + 3 + 3
        self.assertEqual(lib[offset:offset + len(signature)], signature)
        self.assertEqual(msc.omf_match_details(bytes(lib), modules, offset, len(signature)), {
            "module": "RAND.C",
            "module_offset": "0x00020",
            "segment_index": 1,
            "segment_offset": "0x0120",
            "public_symbol": "_rand",
        })
        self.assertIsNone(msc.omf_match_details(bytes(lib), modules,
                                                offset - 1, len(signature)))
        self.assertIsNone(msc.omf_match_details(bytes(lib), modules,
                                                len(lib) - 1, len(signature)))
        candidates = msc.scan_exact_crt_prefixes(
            b"padding" + signature + b"end", bytes(lib), modules)
        self.assertEqual(candidates, [{
            "symbol": "_rand",
            "module": "RAND.C",
            "rtl_offset": "0x00007",
            "segment_index": 1,
            "segment_offset": "0x0120",
            "exact_prefix_bytes": len(signature),
        }])
        self.assertEqual(msc.scan_exact_crt_prefixes(
            signature + b"padding" + signature, bytes(lib), modules), [])
        self.assertEqual(msc.scan_exact_crt_prefixes(
            signature[:15], bytes(lib), modules), [])

        # A second public claiming the same opening bytes makes attribution
        # ambiguous even when there is only one match in the original RTL.
        second_module = msc.OMFModule("COPY.C", modules[0].start,
                                      modules[0].end, modules[0].records)
        self.assertEqual(msc.scan_exact_crt_prefixes(
            b"padding" + signature, bytes(lib), [modules[0], second_module]), [])

    def test_omf_library_rejects_invalid_header_and_module(self):
        with self.assertRaisesRegex(ValueError, "Not an OMF library"):
            msc.parse_omf_library(b"other bytes")
        malformed = bytearray(1024)
        malformed[0:3] = b"\xf0\x1d\x00"  # 32-byte page
        malformed[3:7] = (512).to_bytes(4, "little")
        with self.assertRaisesRegex(ValueError, "Missing OMF module header"):
            msc.parse_omf_library(bytes(malformed))
        malformed[32:35] = b"\x82\xff\xff"  # overruns dictionary
        with self.assertRaisesRegex(ValueError, "Truncated OMF record"):
            msc.parse_omf_library(bytes(malformed))

    def test_only_mutable_operands_are_ignored(self):
        mask = msc.build_mask(msc.SIGNATURES[0])
        self.assertEqual(sum(mask), 13)
        synthetic = bytes(range(len(mask)))
        relocated = bytearray(synthetic)
        for index, fixed in enumerate(mask):
            if not fixed:
                relocated[index] ^= 0x7f
        library = b"padding" + relocated + b"other"
        self.assertEqual(msc.search_code(library, synthetic, mask), [7])
        relocated[3] ^= 1
        self.assertEqual(msc.search_code(b"padding" + relocated, synthetic, mask), [])

    def test_masks_are_valid_for_all_linked_crt_functions(self):
        for signature in msc.SIGNATURES:
            mask = msc.build_mask(signature)
            self.assertGreater(sum(mask), signature.size // 2)
            self.assertGreater(signature.offset, 0)
        with self.assertRaisesRegex(ValueError, "nonempty"):
            msc.search_code(b"", b"", b"")

    def test_case_insensitive_library_lookup(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "lib").mkdir()
            (root / "lib" / "llibcr.lib").touch()
            self.assertEqual(msc.resolve_library(root), root / "lib" / "llibcr.lib")

    def test_disk_image_directory_is_not_mistaken_for_installation(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "Disk 1.img").touch()
            with self.assertRaisesRegex(ValueError, "contains floppy images"):
                msc.resolve_library(root)

    def test_pinned_original_rtl_self_match_when_present(self):
        original = REPO.parent / "gateway-dos/GATE.RTL"
        if not original.is_file():
            self.skipTest("original Gateway DOS assets not provided")
        payload = original.read_bytes()
        result = msc.compare_bytes(payload, payload)
        self.assertEqual([match["routine"] for match in result], ["srand", "rand"])
        for match in result:
            self.assertIn(match["rtl_offset"], match["library_offsets"])
        with self.assertRaisesRegex(ValueError, "pinned original"):
            msc.compare_bytes(payload[:-1], payload)


if __name__ == "__main__":
    unittest.main()

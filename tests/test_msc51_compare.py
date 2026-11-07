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

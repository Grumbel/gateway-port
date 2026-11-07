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
    @staticmethod
    def fixture_with_fixups(fixup_records, code_size=160):
        """Minimal OMF library with one CODE public and FIXUPP records."""
        def record(kind, payload):
            header = bytes((kind, (len(payload) + 1) & 255,
                            (len(payload) + 1) >> 8))
            return header + payload + bytes(((-sum(header + payload)) & 255,))

        code = bytes((i * 37 + 11) & 255 for i in range(code_size))
        records = [
            record(0x82, b"\x06TEST.C"),
            record(0x96, b"\x05_TEXT\x04CODE\x00"),
            record(0x98, b"\x28" + code_size.to_bytes(2, "little") + b"\x01\x02\x03"),
            record(0x90, b"\x00\x01\x03_fn\x00\x00\x00"),
            record(0xa0, b"\x01\x00\x00" + code),
            *(record(*item) if isinstance(item, tuple) else record(0x9c, item)
              for item in fixup_records),
            record(0x8a, b"\x00"),
        ]
        page_size = 32
        dictionary = 512
        header = (bytes((0xf0, page_size - 3, 0)) +
                  dictionary.to_bytes(4, "little") + b"\x01\x00\x00")
        library = bytearray(header.ljust(page_size, b"\0"))
        for item in records:
            library.extend(item)
        library.extend(bytes((-len(library)) % page_size))
        library.extend(b"\xf1")
        library.extend(bytes(dictionary - len(library)))
        library.extend(bytes(512))
        data = bytes(library)
        modules = msc.parse_omf_library(data)
        return data, modules, code

    def test_omf_fixupp_relocation_fields_and_extended_prefix(self):
        # Offset 4: explicit frame F4, explicit target T0 (index 1), no
        # displacement. Offset 90: FAR pointer with FRAME/TARGET threads.
        payload = b"\xc4\x04\x44\x01\x00\x01\x51\xcc\x5a\x9c"
        library, modules, code = self.fixture_with_fixups([payload])
        rtl_code = bytearray(code)
        for offset in (4, 5, 90, 91, 92, 93):
            rtl_code[offset] ^= 0xff
        original = b"padding" + rtl_code + b"trailing"
        self.assertEqual(msc.scan_exact_crt_prefixes(original, library, modules), [])
        hits = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["rtl_offset"], "0x00007")
        self.assertEqual(hits[0]["symbol"], "_fn")
        self.assertEqual(hits[0]["exact_prefix_bytes"], 4)
        self.assertEqual(hits[0]["relocation_aware_bytes"], 160)
        self.assertEqual(hits[0]["masked_relocation_bytes"], 6)
        self.assertEqual(hits[0]["fixed_compared_bytes"], 154)

        # Both copies match the masked first 16 bytes, but a fixed byte at
        # offset 20 disambiguates the full 160-byte entry.
        almost = bytearray(rtl_code)
        almost[20] ^= 0x01
        extra = msc.scan_exact_crt_prefixes(
            original + b"separator" + almost, library, modules,
            relocations=True)
        self.assertEqual(len(extra), 1)
        self.assertEqual(extra[0]["rtl_offset"], "0x00007")

        # Two complete masked matches remain ambiguous despite the extra
        # comparison length; arbitrary original addresses are never chosen.
        self.assertEqual(msc.scan_exact_crt_prefixes(
            original + b"separator" + rtl_code, library, modules,
            relocations=True), [])

        # One unmasked byte beyond the previous 64-byte limit stops the match.
        rtl_code[110] ^= 1
        shorter = msc.scan_exact_crt_prefixes(
            b"padding" + rtl_code, library, modules, relocations=True)
        self.assertEqual(shorter[0]["relocation_aware_bytes"], 110)

    def test_omf_fixupp_continuations_and_fail_closed(self):
        first = b"\xc4\x04\x44\x01"
        second = b"\x00\x01\x51\xcc\x5a\x9c"
        library, modules, code = self.fixture_with_fixups([first, second])
        rtl_code = bytearray(code)
        for offset in (4, 5, 90, 91, 92, 93):
            rtl_code[offset] ^= 0x7f
        self.assertEqual(msc.scan_exact_crt_prefixes(
            b"padding" + rtl_code, library, modules,
            relocations=True)[0]["relocation_aware_bytes"], 160)

        # Unsupported location type 6, out-of-bounds field, truncated frame
        # index, or truncated target displacement rejects the entire LEDATA.
        invalid = (
            b"\xd8\x04\x44\x01",
            b"\xc4\x9f\x44\x01",  # 2 bytes starting at offset 159
            b"\xc4\x04\x00",       # frame F0/target T0 lack indices
            b"\xc4\x04\x40\x01",  # target displacement missing
        )
        for payload in invalid:
            with self.subTest(payload=payload):
                bad_lib, bad_modules, _ = self.fixture_with_fixups([payload])
                self.assertEqual(msc.scan_exact_crt_prefixes(
                    b"padding" + code, bad_lib, bad_modules,
                    relocations=True), [])

    def test_omf_fixupp_field_widths(self):
        self.assertEqual(msc.FIXUP_WIDTHS, {
            0: 1, 1: 2, 2: 2, 3: 4, 4: 1, 5: 2,
            9: 4, 11: 6, 13: 4,
        })
        # FIXUPP32 uses a four-byte displacement. Parsing it must leave
        # precisely the 32-bit fixup location masked, not its target datum.
        fixupp32 = (0x9d, b"\xe4\x64\x40\x01\xef\xbe\xad\xde")
        library, modules, code = self.fixture_with_fixups([fixupp32])
        rtl_code = bytearray(code)
        for offset in range(100, 104):
            rtl_code[offset] ^= 0xfe
        hits = msc.scan_exact_crt_prefixes(
            b"padding" + rtl_code, library, modules, relocations=True)
        self.assertEqual(hits[0]["relocation_aware_bytes"], 160)
        self.assertEqual(hits[0]["masked_relocation_bytes"], 4)

    def test_omf_exact_only_explanations(self):
        fixup = b"\xc4\x04\x44\x01"  # 2-byte fixup at offset 4
        library, modules, code = self.fixture_with_fixups([fixup])
        relocated_copy = bytearray(code)
        relocated_copy[4] ^= 1
        original = b"padding" + code + b"gap" + relocated_copy
        baseline = msc.scan_exact_crt_prefixes(original, library, modules)
        relocated = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(len(baseline), 1)
        self.assertEqual(relocated, [])
        difference = msc.compare_public_scans(baseline, relocated)
        reasons = msc.explain_exact_only(
            original, library, modules, difference["exact_only"])
        self.assertEqual(reasons[0]["status"], "ambiguous_best_fixed_match")
        self.assertEqual(reasons[0]["masked_opening_matches"], 2)
        self.assertEqual(reasons[0]["best_fixed_bytes"], 158)

        # The default exact scan still matches the bytes even if OMF's
        # fixup record is unsupported, but the masked comparison fails closed.
        broken_lib, broken_modules, _ = self.fixture_with_fixups(
            [b"\xd8\x04\x44\x01"])
        explanation = msc.explain_exact_only(
            b"padding" + code, broken_lib, broken_modules, [{
                "rtl_offset": "0x00007", "module": "TEST.C", "symbol": "_fn",
            }])
        self.assertEqual(explanation[0]["status"],
                         "invalid_or_unsupported_fixups")

        # Thirty two-byte fixups through byte 59 leave only four fixed bytes
        # inside the maximum 64-byte opening. Do not search by four bytes.
        dense_fixups = b"".join(
            b"\xc4" + bytes((2 * index,)) + b"\x44\x01"
            for index in range(30))
        dense_lib, dense_modules, _ = self.fixture_with_fixups([dense_fixups])
        dense_reason = msc.explain_exact_only(
            b"padding" + code, dense_lib, dense_modules, [{
                "rtl_offset": "0x00007", "module": "TEST.C", "symbol": "_fn",
            }])
        self.assertEqual(dense_reason[0]["status"],
                         "too_few_fixed_opening_bytes")
        self.assertEqual(dense_reason[0]["fixed_opening_bytes"], 4)
        self.assertEqual(dense_reason[0]["opening_bytes"], 64)

    def test_omf_adaptive_opening_recovers_heavily_relocated_prefix(self):
        # Eight relocated bytes among the first 16 leave eight fixed. Four
        # additional fixed bytes extend the anchor to 20 without relaxing the
        # requirement for 12 fixed bytes in a unique anchored comparison.
        fixups = b"".join(
            b"\xc4" + bytes((2 * index,)) + b"\x44\x01"
            for index in range(4))
        library, modules, code = self.fixture_with_fixups([fixups], code_size=20)
        rtl_code = bytearray(code)
        for index in range(8):
            rtl_code[index] ^= 0x43
        original = b"prefix" + rtl_code
        self.assertEqual(msc.scan_exact_crt_prefixes(original, library, modules), [])
        hits = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["opening_bytes"], 20)
        self.assertEqual(hits[0]["relocation_aware_bytes"], len(code))
        self.assertEqual(hits[0]["fixed_compared_bytes"], len(code) - 8)

    def test_public_evidence_catalogue_and_nonoverlapping_gap_summary(self):
        def entry(offset, symbol, size, kind="code", relocation=False):
            hit = {
                "rtl_offset": f"0x{offset:05x}", "module": "CRT.ASM",
                "symbol": symbol, "segment_class": "CODE" if kind == "code" else "DATA",
                "segment_kind": kind, "exact_prefix_bytes": size,
            }
            if relocation:
                hit.update({
                    "relocation_aware_bytes": size, "fixed_compared_bytes": size - 2,
                    "masked_relocation_bytes": 2, "opening_bytes": 16,
                })
            return hit

        exact = [entry(0x10, "_both", 16), entry(0x30, "_data", 20, "other")]
        relocated = [entry(0x10, "_both", 20, relocation=True),
                     entry(0x40, "_new", 16, relocation=True)]
        catalogue = msc.public_evidence_catalogue(exact, relocated)
        self.assertEqual(len(catalogue), 3)
        self.assertEqual([item["evidence"] for item in catalogue],
                         ["both", "exact_only", "relocation_only"])
        self.assertEqual(catalogue[0]["candidate_span_bytes"], 20)
        self.assertEqual(catalogue[0]["fixed_compared_bytes"], 18)
        self.assertEqual(msc.summarize_candidate_spans(catalogue), {
            "code_class_candidates": 2,
            "code_class_candidate_extent": ["0x00010", "0x00050"],
            "candidate_spanned_bytes": 36,
            "unmatched_gap_count": 1,
            "unmatched_gap_bytes": 28,
            "largest_unmatched_gaps": [{
                "start": "0x00024", "end": "0x00040", "bytes": 28,
                "preceding_publics": ["CRT.ASM:_both"],
                "following_publics": ["CRT.ASM:_new"],
            }],
        })
        self.assertEqual(msc.summarize_candidate_spans([]), {
            "code_class_candidates": 0, "candidate_spanned_bytes": 0,
            "unmatched_gap_count": 0, "unmatched_gap_bytes": 0,
            "largest_unmatched_gaps": [],
        })

        # Overlapping fingerprint windows must be counted once, not
        # mistaken for extra covered original-code bytes.
        overlap = msc.public_evidence_catalogue(
            [entry(0x10, "_one", 32), entry(0x20, "_two", 32)], [])
        summary = msc.summarize_candidate_spans(overlap)
        self.assertEqual(summary["candidate_spanned_bytes"], 48)
        self.assertEqual(summary["unmatched_gap_count"], 0)

    def test_omf_stitches_only_contiguous_valid_ledata(self):
        def record(kind, payload):
            header = bytes((kind, (len(payload) + 1) & 255,
                            (len(payload) + 1) >> 8))
            return header + payload + bytes(((-sum(header + payload)) & 255,))

        # Deliberately longer than the former 512-byte single-record ceiling.
        code = (b"MSC51STITCHMARK!" +
                bytes((i * 37 + 11) & 255 for i in range(634)))

        def library_and_module(second_offset=24, second_fixup=b"\xc4\x2e\x44\x01",
                               extra_public=False, lidata=False, data_class=False):
            raw_records = [
                (0x82, b"\x06TEST.C"),
                (0x96, b"\x05_TEXT\x04DATA\x00" if data_class else
                 b"\x05_TEXT\x04CODE\x00"),
                (0x98, b"\x28\x78\x00\x01\x02\x03"),
                (0x90, b"\x00\x01\x03_fn\x00\x00\x00"),
                (0xa0, b"\x01\x00\x00" + code[:24]),
                (0x9c, b"\xc4\x04\x44\x01"),
                (0xa0, b"\x01" + second_offset.to_bytes(2, "little") + code[24:]),
                (0x9c, second_fixup),
            ]
            if extra_public:
                raw_records.append((0x90, b"\x00\x01\x05_next\x3c\x00\x00"))
            if lidata:
                raw_records.append((0xa2, b"\x01\x00\x00\x00"))
            raw_records.append((0x8a, b"\x00"))
            raw = bytearray()
            records = []
            for kind, payload in raw_records:
                start = len(raw)
                raw.extend(record(kind, payload))
                records.append(msc.OMFRecord(kind, start, len(raw)))
            return bytes(raw), [msc.OMFModule(
                "TEST.C", 0, len(raw), tuple(records))]

        rtl_code = bytearray(code)
        for index in (4, 5, 70, 71):
            rtl_code[index] ^= 0xa5
        original = b"padding" + rtl_code

        library, modules = library_and_module()
        hits = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["relocation_aware_bytes"], len(code))
        self.assertEqual(hits[0]["masked_relocation_bytes"], 4)
        self.assertEqual(hits[0]["ledata_records_compared"], 2)

        # Segment gaps/overlaps and a malformed second FIXUPP stop the
        # comparison at the end of the trusted first fragment.
        for second_offset, second_fixup, expected in (
                (25, b"\xc4\x2e\x44\x01", 24),
                (23, b"\xc4\x2e\x44\x01", 23),
                (24, b"\xd8\x2e\x44\x01", 24)):
            with self.subTest(second_offset=second_offset,
                              second_fixup=second_fixup):
                library, modules = library_and_module(second_offset, second_fixup)
                hits = msc.scan_exact_crt_prefixes(
                    original, library, modules, relocations=True)
                self.assertEqual(hits[0]["relocation_aware_bytes"], expected)
                self.assertEqual(hits[0]["ledata_records_compared"], 1)

        library, modules = library_and_module(extra_public=True)
        hits = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(next(hit for hit in hits if hit["symbol"] == "_fn")
                         ["relocation_aware_bytes"], 60)
        library, modules = library_and_module(lidata=True)
        self.assertEqual(msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True), [])

        # Avoid promoting repetitive DATA tables to false long matches:
        # their relocation-aware fingerprints stay inside the first record.
        library, modules = library_and_module(data_class=True)
        hits = msc.scan_exact_crt_prefixes(
            original, library, modules, relocations=True)
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["segment_class"], "DATA")
        self.assertEqual(hits[0]["relocation_aware_bytes"], 24)
        self.assertEqual(hits[0]["ledata_records_compared"], 1)

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
        names = record(0x96, b"\x05_TEXT\x04CODE\x05_DATA\x04DATA\x00")
        # SEGDEF 1: _TEXT, CODE; SEGDEF 2: _DATA, DATA.
        code_segment = record(0x98, b"\x28\x40\x01\x01\x02\x05")
        data_segment = record(0x98, b"\x28\x40\x01\x03\x04\x05")
        # PUBDEF: group 0, segment 1, _rand at segment offset 0x0120.
        public = record(0x90, b"\x00\x01\x05_rand\x20\x01\x00")
        signature = b"\x55\x8b\xec\x2b\x76\x01\x5d\xcb" + bytes(range(12))
        ledata = record(0xa0, b"\x01\x20\x01" + signature)
        data_public = record(0x90, b"\x00\x02\x06_month\x00\x00\x00")
        data_signature = b"const-month-data-info"
        data_ledata = record(0xa0, b"\x02\x00\x00" + data_signature)
        module_bytes = (module_one + names + code_segment + data_segment + public
                        + ledata + data_public + data_ledata + record(0x8a, b"\x00"))
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
        self.assertEqual(msc.omf_segments(bytes(lib), modules[0]), {
            1: {"name": "_TEXT", "class": "CODE"},
            2: {"name": "_DATA", "class": "DATA"},
        })
        self.assertEqual(msc.segment_kind({"class": "CODE"}), "code")
        self.assertEqual(msc.segment_kind({"class": "DATA"}), "other")
        self.assertEqual(msc.segment_kind({}), "unknown")
        offset = (page_size + len(module_one) + len(names) + len(code_segment)
                  + len(data_segment) + len(public) + 3 + 3)
        self.assertEqual(lib[offset:offset + len(signature)], signature)
        self.assertEqual(msc.omf_match_details(bytes(lib), modules, offset, len(signature)), {
            "module": "RAND.C",
            "module_offset": "0x00020",
            "segment_index": 1,
            "segment_offset": "0x0120",
            "segment_name": "_TEXT",
            "segment_class": "CODE",
            "public_symbol": "_rand",
        })
        self.assertIsNone(msc.omf_match_details(bytes(lib), modules,
                                                offset - 1, len(signature)))
        self.assertIsNone(msc.omf_match_details(bytes(lib), modules,
                                                len(lib) - 1, len(signature)))
        data_start = offset + len(signature) + 1 + len(data_public) + 6
        self.assertEqual(msc.omf_match_details(
            bytes(lib), modules, data_start, len(data_signature)), {
                "module": "RAND.C",
                "module_offset": "0x00020",
                "segment_index": 2,
                "segment_offset": "0x0000",
                "segment_name": "_DATA",
                "segment_class": "DATA",
                "public_symbol": "_month",
            })
        candidates = msc.scan_exact_crt_prefixes(
            b"padding" + signature + b"end", bytes(lib), modules)
        self.assertEqual(candidates, [{
            "symbol": "_rand",
            "module": "RAND.C",
            "rtl_offset": "0x00007",
            "segment_index": 1,
            "segment_offset": "0x0120",
            "segment_name": "_TEXT",
            "segment_class": "CODE",
            "segment_kind": "code",
            "exact_prefix_bytes": len(signature),
        }])
        both = msc.scan_exact_crt_prefixes(
            b"padding" + signature + b"end" + data_signature, bytes(lib), modules)
        self.assertEqual([entry["segment_kind"] for entry in both], ["code", "other"])
        self.assertEqual(both[1]["symbol"], "_month")
        self.assertEqual(msc.summarize_public_prefixes(both), {
            "total": 2,
            "segment_classes": {"CODE": 1, "DATA": 1},
            "code_class_entry_span": ["0x00007", "0x00007"],
        })
        self.assertEqual(msc.summarize_public_prefixes([]), {
            "total": 0, "segment_classes": {},
        })
        self.assertEqual(msc.compare_public_scans(candidates, both), {
            "exact_candidates": 1,
            "relocation_aware_candidates": 2,
            "exact_only": [],
            "relocation_only": [{
                "rtl_offset": both[1]["rtl_offset"],
                "module": "RAND.C",
                "symbol": "_month",
            }],
        })
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

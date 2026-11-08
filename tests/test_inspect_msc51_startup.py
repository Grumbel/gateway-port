#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic startup captures; proprietary DOS executable not required."""

import importlib.util
from pathlib import Path
import re
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
    def test_rtl_remainder_known_sites_match_dos_initializer_tables(self):
        source = (REPO / "decomp/launcher/dos/msc51_startup_static_data.c").read_text()
        for identifier, expected in (
                ("gate_rtl_ds_seg_fixups", startup.KNOWN_RTL_DS_SEG_WORDS),
                ("gate_rtl_cs_seg_fixups", startup.KNOWN_RTL_CS_SEG_WORDS)):
            match = re.search(r"static const uint16_t " + identifier +
                              r"\[\] = \{([^}]+)\};", source)
            self.assertIsNotNone(match, identifier)
            observed = tuple(int(word, 16) for word in
                             re.findall(r"0x([0-9a-fA-F]+)u", match.group(1)))
            self.assertEqual(observed, expected)
        offsets_match = re.search(
            r"static const GateIntroOffsetFixup gate_rtl_offset_fixups\[\] = "
            r"\{(.*?)\};", source, re.DOTALL)
        self.assertIsNotNone(offsets_match)
        triples = re.findall(
            r"\{0x([0-9a-fA-F]+)u, 0x([0-9a-fA-F]+)u, "
            r"0x([0-9a-fA-F]+)u\}", offsets_match.group(1))
        observed = tuple((int(site, 16), int(original, 16),
                          (int(original, 16) + int(addend, 16)) & 0xffff)
                         for site, original, addend in triples)
        self.assertEqual(observed, startup.KNOWN_RTL_OFFSET_FIXUPS)
        for name, entries in startup.KNOWN_RTL_EXTERNAL_POINTERS:
            match = re.search(
                r"static const GateIntroExternalPtr gate_rtl_" + name +
                r"\[\] = \{(.*?)\};", source, re.DOTALL)
            self.assertIsNotNone(match, name)
            pairs = re.findall(r"\{0x([0-9a-fA-F]+)u, 0x([0-9a-fA-F]+)u\}",
                               match.group(1))
            self.assertEqual(tuple((int(site, 16), int(off, 16))
                                   for site, off in pairs), entries)

    def test_rtl_remainder_excludes_only_confirmed_word_sites(self):
        length = startup.RTL_DATA_IMAGE_END
        rtl = bytearray(b"\x55" * (0x29 + length))
        source = memoryview(rtl)[0x29:]
        for offset in (startup.KNOWN_RTL_DS_SEG_WORDS +
                       startup.KNOWN_RTL_CS_SEG_WORDS):
            put_word(source, offset, 0)
        for offset, old, _ in startup.KNOWN_RTL_OFFSET_FIXUPS:
            put_word(source, offset, old)
        for _, entries in startup.KNOWN_RTL_EXTERNAL_POINTERS:
            for offset, _ in entries:
                source[offset:offset + 4] = bytes(4)
        source[0x110] = 0
        put_word(source, 0x200, 0x1111)
        put_word(source, 0x210, 0x1111)
        first = bytearray(source)
        for offset in startup.KNOWN_RTL_DS_SEG_WORDS:
            put_word(first, offset, 0x2a7a)
        for offset in startup.KNOWN_RTL_CS_SEG_WORDS:
            put_word(first, offset, 0x12a5)
        for offset, _, observed in startup.KNOWN_RTL_OFFSET_FIXUPS:
            put_word(first, offset, observed)
        for index, (_, entries) in enumerate(startup.KNOWN_RTL_EXTERNAL_POINTERS):
            for offset, observed_offset in entries:
                put_word(first, offset, observed_offset)
                put_word(first, offset + 2, (0x252c, 0x25d7)[index])
        first[0x100:0x103] = bytes(3)
        first[0x110] = 0x77
        put_word(first, 0x200, 0x2222)
        put_word(first, 0x210, 0x2222)
        late = bytearray(first)
        late[0x100] = 0x12
        late[0x300] = 0  # Write not present in the first-prepared mismatch.
        put_word(late, 0x2c10, 0)  # Original game also later mutates this site.
        samples = [(100, bytes(first)), (125, bytes(first)), (500, bytes(late))]
        remainder = startup.rtl_preimage_remainder(bytes(rtl), samples, 0x2a7a, 0x1000)
        self.assertEqual(remainder["initial_changed_bytes"], 165)
        self.assertEqual(remainder["verified_segment_bytes"], 54)
        self.assertEqual(remainder["verified_offset_bytes"], 38)
        self.assertEqual(remainder["verified_external_pointers"], 19)
        self.assertEqual(remainder["verified_external_bytes"], 65)
        self.assertEqual(remainder["verified_total_bytes"], 157)
        self.assertEqual([entry["observed_segment"]
                          for entry in remainder["observed_external_tables"]],
                         [0x252c, 0x25d7])
        self.assertEqual(remainder["remaining_changed_bytes"], 8)
        self.assertEqual((remainder["source_zero_to_nonzero"],
                          remainder["source_nonzero_to_zero"],
                          remainder["changed_nonzero_bytes"]), (1, 3, 4))
        self.assertEqual(remainder["cluster_count"], 4)
        clusters = {item["runtime_ds_offset"]: item for item in remainder["clusters"]}
        self.assertEqual(clusters[0x100]["length"], 3)
        self.assertEqual(clusters[0x100]["changed_bytes"], 3)
        self.assertEqual(clusters[0x100]["changed_bytes_mutated_in_later_samples"], 1)
        self.assertEqual(clusters[0x110]["source_hex"], "00")
        self.assertEqual(clusters[0x110]["observed_hex"], "77")
        self.assertTrue(any(item["source_word"] == 0x1111 and
                            item["observed_word"] == 0x2222 and
                            item["runtime_ds_offsets"] == [0x200, 0x210]
                            for item in remainder["repeated_word_transitions"]))
        damaged = bytearray(first)
        put_word(damaged, 0x0fb8, 0x2a79)
        with self.assertRaisesRegex(ValueError, "known source-zero RTL segment site"):
            startup.rtl_preimage_remainder(
                bytes(rtl), [(100, bytes(damaged)), (500, bytes(late))],
                0x2a7a, 0x1000)
        raw_damaged = bytearray(rtl)
        put_word(raw_damaged, 0x29 + 0x0fb8, 1)
        with self.assertRaisesRegex(ValueError, "known source-zero RTL segment site"):
            startup.rtl_preimage_remainder(bytes(raw_damaged), samples, 0x2a7a, 0x1000)
        damaged = bytearray(first)
        put_word(damaged, 0x1538, 0x14f9)
        with self.assertRaisesRegex(ValueError, "known RTL offset fixup"):
            startup.rtl_preimage_remainder(
                bytes(rtl), [(100, bytes(damaged)), (500, bytes(late))],
                0x2a7a, 0x1000)
        damaged = bytearray(first)
        put_word(damaged, 0x2ae3 + 2, 0x25d6)
        with self.assertRaisesRegex(ValueError, "known RTL external_b pointer"):
            startup.rtl_preimage_remainder(
                bytes(rtl), [(100, bytes(damaged)), (500, bytes(late))],
                0x2a7a, 0x1000)
        raw_damaged = bytearray(rtl)
        put_word(raw_damaged, 0x29 + 0x2814, 1)
        with self.assertRaisesRegex(ValueError, "known RTL external_a pointer"):
            startup.rtl_preimage_remainder(bytes(raw_damaged), samples,
                                           0x2a7a, 0x1000)

    def test_preimage_fixups_restrict_hypotheses_and_track_later_writes(self):
        length = startup.RTL_DATA_IMAGE_END
        rtl = bytearray(b"\x55" * (0x29 + length))
        source = memoryview(rtl)[0x29:]
        for offset, initial in ((0x100, 0), (0x107, 0), (0x200, 7),
                                (0x260, 0x1234), (0x300, 0x1234)):
            put_word(source, offset, initial)
        first = bytearray(source)
        for offset, observed in ((0x100, 0x2a7a), (0x107, 0x2a70),
                                 (0x200, 0x12ac), (0x260, 0x2234),
                                 (0x300, 0xabcd)):
            put_word(first, offset, observed)
        during = bytearray(first)
        during[0x107] ^= 1
        late = bytearray(first)
        late[0x200] ^= 1
        late[0x310] ^= 1  # A later write absent from the first mismatches.
        report = startup.rtl_preimage_fixup_candidates(
            bytes(rtl), [(100, bytes(first)), (125, bytes(during)),
                         (500, bytes(late))], 0x2a7a, 0x1000)
        self.assertEqual(report["changed_bytes"], 9)
        self.assertEqual(report["changed_bytes_stable_through_250ms"], 8)
        self.assertEqual(report["changed_bytes_mutated_by_500ms_or_later"], 1)
        self.assertEqual(report["all_bytes_mutated_by_500ms_or_later"], 2)
        candidates = {item["runtime_ds_offset"]: item
                      for item in report["candidate_words"]}
        self.assertEqual(set(candidates), {0x100, 0x107, 0x200, 0x260})
        self.assertIn("runtime_ds", candidates[0x100]["target_segments"])
        self.assertIn("lower_ds", candidates[0x107]["target_segments"])
        self.assertFalse(candidates[0x107]["stable_through_250ms"])
        self.assertIn("resident_cs", candidates[0x200]["delta_segments"])
        self.assertTrue(candidates[0x200]["changed_by_500ms_or_later"])
        self.assertIn("image_load", candidates[0x260]["delta_segments"])
        self.assertFalse(candidates[0x260]["changed_by_500ms_or_later"])
        self.assertEqual(candidates[0x107]["rtl_file_offset"], 0x130)
        # Early loader DS is deliberately different from the eventual DS.
        ram_first = bytearray(startup.RAM_SIZE)
        ram_late = bytearray(startup.RAM_SIZE)
        start = 0x2a7a * 16
        ram_first[start:start + length] = first
        ram_late[start:start + length] = late
        wrapped = startup.runtime_data_fixups(
            bytes(rtl), [
                (100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4),
                 bytes(ram_first)),
                (500, regs(0x12a5, 0x3570, 0x2a7a, 0x2a7a, 0x7a1a),
                 bytes(ram_late)),
            ], 0x2a7a, 0x1000)
        self.assertEqual(wrapped["changed_bytes"], 9)
        self.assertEqual(wrapped["first_prepared_ms"], 100)
        with self.assertRaisesRegex(ValueError, "100..250ms"):
            startup.rtl_preimage_fixup_candidates(
                bytes(rtl), [(500, bytes(late))], 0x2a7a, 0x1000)
        with self.assertRaisesRegex(ValueError, "complete"):
            startup.rtl_preimage_fixup_candidates(
                bytes(rtl), [(100, b"tiny")], 0x2a7a, 0x1000)
        with self.assertRaisesRegex(ValueError, "before original DS"):
            startup.rtl_preimage_fixup_candidates(
                bytes(rtl), [(100, bytes(first))], None, 0x1000)

    def test_data_layout_recovers_exact_exe_and_rtl_runs_with_zero_prefix(self):
        exe = bytearray(0x400)
        exe[:2] = b"MZ"
        put_word(exe, 4, 2)
        put_word(exe, 8, 2)
        put_word(exe, 0x18, 0x1c)
        exe_payload = bytes(21) + bytes(
            (i * 29 + (i * i // 13)) % 256 for i in range(200))
        rtl_payload = bytes((i * 23 + (i * i // 17)) % 256 for i in range(190))
        exe[0x180:0x180 + len(exe_payload)] = exe_payload
        rtl = bytearray(0x500)
        rtl[0x100:0x100 + len(rtl_payload)] = rtl_payload
        data = bytearray(b"\xa6" * 0x10000)
        data[0x300:0x300 + len(exe_payload)] = exe_payload
        data[0x2400:0x2400 + len(rtl_payload)] = rtl_payload
        data[0x1100:0x1260] = bytes(0x160)

        index = startup.source_window_index(bytes(exe), bytes(rtl))
        matches = startup.exact_source_copies(data, index)
        self.assertIn({"source": "exe", "source_file_offset": 0x180,
                       "runtime_ds_offset": 0x300, "length": len(exe_payload)},
                      matches)
        self.assertIn({"source": "rtl", "source_file_offset": 0x100,
                       "runtime_ds_offset": 0x2400, "length": len(rtl_payload)},
                      matches)
        self.assertFalse(any(match["runtime_ds_offset"] == 0x1100
                             for match in matches))
        ambiguous = bytearray(exe)
        ambiguous[0x250:0x250 + len(rtl_payload)] = rtl_payload
        # Two equally long source copies cannot be attributed to either
        # file with confidence. Suppress the ambiguous source claim.
        dup_matches = startup.exact_source_copies(
            data, startup.source_window_index(bytes(ambiguous), bytes(rtl)))
        self.assertFalse(any(match["runtime_ds_offset"] == 0x2400
                             for match in dup_matches))

        ram = bytearray(b"\xa6" * startup.RAM_SIZE)
        segment = 0x2000
        start = segment * 16
        ram[start:start + 0x10000] = data
        settled = bytearray(ram)
        settled[start + 0x1300:start + 0x1350] = bytes(0x50)
        captures = [
            (100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4), bytes(ram)),
            (500, regs(0x12a5, 0x3570, segment, segment, 0x7a1a), bytes(settled)),
        ]
        layout = startup.runtime_data_layout(bytes(exe), bytes(rtl), captures, segment)
        self.assertEqual(layout["runtime_data_segment"], segment)
        self.assertEqual(len(layout["snapshots"]), 2)
        self.assertIsNone(layout["snapshots"][0]["rtl_preimage"])
        self.assertEqual(layout["stable_zero_regions_from_500ms"],
                         [{"runtime_ds_offset": 0x1100, "length": 0x160}])
        self.assertEqual(layout["nonzero_at_first_prepared_to_zero_by_500ms"], 0x50)
        self.assertEqual(layout["largest_cleared_regions_by_500ms"],
                         [{"runtime_ds_offset": 0x1300, "length": 0x50}])
        with self.assertRaisesRegex(ValueError, "before original DS"):
            startup.runtime_data_layout(bytes(exe), bytes(rtl), captures, None)

    def test_rtl_preimage_shift_and_transient_loader_segment_alias(self):
        rtl = bytearray((i * 29 + i // 19) % 251 for i in range(0x4000))
        length = startup.RTL_DATA_IMAGE_END
        image = bytearray(rtl[0x29:0x29 + length])
        source_zero = next(i for i, byte in enumerate(image) if byte == 0)
        source_nonzero = next(i for i, byte in enumerate(image) if byte != 0)
        image[source_zero] = 0x5a
        image[source_nonzero] = 0
        third = next(i for i, byte in enumerate(image)
                     if byte not in (0, 0x77, 0x5a))
        image[third] = 0x77
        comparison = startup.rtl_preimage_comparison(bytes(rtl), image)
        self.assertEqual(comparison["length"], length)
        self.assertEqual(comparison["equal_bytes"], length - 3)
        self.assertEqual(comparison["source_zero_to_nonzero"], 1)
        self.assertEqual(comparison["source_nonzero_to_zero"], 1)
        self.assertEqual(comparison["changed_nonzero_bytes"], 1)

        exe, _ = fixture()
        ram = bytearray(b"\xb4" * startup.RAM_SIZE)
        segment = 0x2a7a
        ram[segment * 16:segment * 16 + length] = image
        captures = [(100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4), bytes(ram)),
                    (500, regs(0x12a5, 0x3570, segment, segment, 0x7a1a), bytes(ram))]
        layout = startup.runtime_data_layout(exe, bytes(rtl), captures, segment)
        self.assertEqual(layout["snapshots"][0]["loader_ds_alias_offset"], 0xa100)
        self.assertIsNone(layout["snapshots"][1]["loader_ds_alias_offset"])
        self.assertEqual(layout["snapshots"][0]["rtl_preimage"]["equal_bytes"],
                         length - 3)

    def test_static_exe_data_is_exact_and_present_before_resident_handoff(self):
        base_exe, rtl = fixture()
        exe = bytearray(0x6000)
        exe[:len(base_exe)] = base_exe
        put_word(exe, 2, 0)
        put_word(exe, 4, len(exe) // 512)
        file_offset, ds_offset, length = startup.STATIC_EXE_DATA
        exe[file_offset:file_offset + length] = bytes(
            (i * 29 + (i // 13) * 7 + 17) % 251 for i in range(length))
        before = bytearray(startup.RAM_SIZE)
        before[0x10000:0x10000 + len(exe) - 0x20] = exe[0x20:]
        loaded = bytearray(before)
        runtime_offset = 0x2a7a0 + ds_offset
        loaded[runtime_offset:runtime_offset + length] = exe[
            file_offset:file_offset + length]
        captures = [
            (0, regs(0x102c, 0x71, 0x0ff0, 0x3229, 0x400), bytes(before)),
            (100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4), bytes(loaded)),
            (500, regs(0x12a5, 0x3570, 0x2a7a, 0x2a7a, 0x7a1a), bytes(loaded)),
        ]
        report = startup.analyze(bytes(exe), rtl, captures)
        self.assertEqual(report["static_exe_data"], {
            "original_exe_offset": file_offset,
            "runtime_ds_offset": ds_offset,
            "length": length,
        })
        self.assertEqual(report["first_complete_static_exe_data_ms"], 100)
        self.assertLess(report["snapshots"][0]["static_exe_data_matching_bytes"], 100)
        self.assertEqual([item["static_exe_data_matching_bytes"]
                          for item in report["snapshots"][1:]], [length, length])
        damaged = bytearray(loaded)
        damaged[runtime_offset + 100] ^= 1
        captures[1] = (100, captures[1][1], bytes(damaged))
        report = startup.analyze(bytes(exe), rtl, captures)
        self.assertEqual(report["first_complete_static_exe_data_ms"], 500)
        self.assertEqual(report["snapshots"][1]["static_exe_data_matching_bytes"],
                         length - 1)

    def test_selector_windows_are_overlapping_and_initialized_before_handoff(self):
        exe, rtl = fixture()
        selectors = startup.intro_segment_selectors(
            (REPO / "decomp/launcher/gate_memory_layout.h").read_text())
        self.assertEqual(len(selectors), 45)
        same_ds = {
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
            "GATE_DS_STARTUP_CONFIG_FIFTH_SEGMENT",
        }
        initial = bytearray(startup.RAM_SIZE)
        initial[0x10000:0x10600] = exe[0x20:]
        prepared = bytearray(initial)
        prepared[0x12a50:0x16750] = rtl[0x56de:0x93de]
        for name, off in selectors.items():
            put_word(prepared, 0x2a7a0 + off,
                     0x2a7a if name in same_ds else 0x2a70)
        self.assertEqual([len(startup.segment_selector_groups(prepared, 0x2a7a,
                                                              selectors)[group])
                          for group in ("data", "data_minus_000a", "other")],
                         [12, 33, 0])

        captures = [
            (0, regs(0x102c, 0x71, 0xff0, 0x3229, 0x400), bytes(initial)),
            (100, regs(0x3259, 0x103c, 0x348a, 0x3229, 0x3c4), bytes(prepared)),
            (500, regs(0x12a5, 0x3570, 0x2a7a, 0x2a7a, 0x7a1a), bytes(prepared)),
        ]
        report = startup.analyze(exe, rtl, captures, selectors)
        self.assertEqual(report["first_complete_segment_selectors"]["milliseconds"],
                         100)
        self.assertEqual(set(report["first_complete_segment_selectors"]["data"]),
                         same_ds)
        self.assertEqual(report["snapshots"][0]["runtime_segment_selector_counts"],
                         {"data": 0, "data_minus_000a": 0, "other": 45})
        self.assertEqual(report["snapshots"][1]["runtime_segment_selector_counts"],
                         {"data": 12, "data_minus_000a": 33, "other": 0})

        # One corrupt selector makes the 100ms table incomplete, even if
        # the resident code bank has already been independently validated.
        bad = bytearray(prepared)
        put_word(bad, 0x2a7a0 + selectors["GATE_DS_INTRO_PALETTE_SEGMENT"],
                 0x2a71)
        captures[1] = (100, captures[1][1], bytes(bad))
        report = startup.analyze(exe, rtl, captures, selectors)
        self.assertEqual(report["first_complete_segment_selectors"]["milliseconds"],
                         500)
        self.assertEqual(report["snapshots"][1]["runtime_segment_selector_counts"],
                         {"data": 11, "data_minus_000a": 33, "other": 1})

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

    def test_capture_combines_disjoint_time_ranges(self):
        text = "CS=102c\nIP=71\nDS=ff0\nES=ff0\nSS=3229\nSP=400\n"
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for directory, timestamp in ((first, 0), (second, 125)):
                folder = Path(directory)
                (folder / ("at-%d.regs" % timestamp)).write_text(text)
                (folder / ("at-%d.bin" % timestamp)).write_bytes(
                    bytes(startup.RAM_SIZE))
            captures = startup.snapshots_from_directories([Path(second), Path(first)])
            self.assertEqual([time for time, _, _ in captures], [0, 125])
            with self.assertRaisesRegex(ValueError, "duplicate snapshot times"):
                startup.snapshots_from_directories([Path(first), Path(first)])

    def test_calibration_relocations_mask_only_documented_words(self):
        source = bytearray(0x10000)
        original = 0x5806
        length = 58
        src = bytearray((i * 17 + 3) % 251 for i in range(length))
        for pos in (14, 29, 50):
            put_word(src, pos, 0)
        source[original:original + length] = src
        ram = bytearray(startup.RAM_SIZE)
        offset = 0x12a50 + original - 0x56de
        ram[offset:offset + length] = src
        for pos, value in ((14, 0x5c8), (29, 0x5c8), (50, 0x5ba)):
            put_word(ram, offset + pos, value)
        entry = startup.relocated_timing_entry(
            bytes(source), bytes(ram), original, length,
            (14, 29, 50), 0x12a50, 0x56de)
        self.assertTrue(entry["matched"])
        self.assertEqual(entry["matched_nonrelocated_bytes"], 52)
        self.assertEqual(entry["relocated_words"], [0x5c8, 0x5c8, 0x5ba])
        ram[offset + 35] ^= 1
        self.assertFalse(startup.relocated_timing_entry(
            bytes(source), bytes(ram), original, length,
            (14, 29, 50), 0x12a50, 0x56de)["matched"])

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

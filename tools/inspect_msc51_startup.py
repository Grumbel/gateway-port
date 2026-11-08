#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare original GATE.EXE startup RAM captures with the EXE and RTL images.

The probe's at-N.regs/at-N.bin pairs are guest observations. This report
identifies *when* the original resident bank becomes visible; it does not
derive or implement the Microsoft C launcher `_main` from a loader snapshot.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

from prepare_intro_ghidra import RTL_BANKS, validate_bank


RAM_SIZE = 0x100000
RESIDENT_SEGMENT = 0x02a5
DATA_WORDS = (0x064e, 0x065c, 0x065e, 0x0660, 0x0662)
# The original EXE's trailing initialized-data block is copied without
# translation into the runtime DS. Both ends are confirmed by immediately
# adjacent non-matching bytes in every settled capture, not by text content.
STATIC_EXE_DATA = (0x41e6, 0x3592, 0x1758)
# Both FAR- and interrupt-returning routines lie in the independently
# validated 02a5 resident bank. The original bytes have zero relocation
# operands at these precisely disassembled instruction positions.
CALIBRATION_ENTRY = (0x5806, 58, (14, 29, 50))
TIMING_SETUP_ENTRY = (0x5840, 86, (12, 19, 27, 31, 39, 43, 51, 58))
TIMING_RESTORE_ENTRY = (0x5896, 76, (11, 18, 33, 50, 54, 62, 66))
TICK_CALLBACK_ENTRY = (0x58e2, 28, (10, 16))
DATA_MATCH_SEED = 16
DATA_MATCH_MINIMUM = 64
# Multiple independent original-copy runs share this offset delta. It is
# not yet evidence that *all* intervening RTL bytes survive relocation.
RTL_DATA_IMAGE_FILE_OFFSET = 0x29
RTL_DATA_IMAGE_END = STATIC_EXE_DATA[1]


def source_window_index(exe, rtl, seed=DATA_MATCH_SEED):
    """Index nontrivial file-byte windows, retaining source file offsets.

    Common patterns (especially BSS-like zero runs) are excluded. A match
    therefore means an *exact* source-image copy, not merely an all-zero
    region or a relocated code entry with differing segment operands.
    """
    header = mz_image(exe)
    source_regions = (("exe", header["header_bytes"],
                       exe[header["header_bytes"]:]), ("rtl", 0, rtl))
    index = {}
    for kind, file_base, data in source_regions:
        for position in range(len(data) - seed + 1):
            key = data[position:position + seed]
            if len(set(key)) < 4:
                continue
            if key not in index:
                index[key] = []
            if index[key] is not None:
                index[key].append((kind, file_base + position, data, position))
                if len(index[key]) > 4:
                    index[key] = None  # ambiguous repeated file pattern
    return index


def exact_source_copies(destination, index, seed=DATA_MATCH_SEED,
                        minimum=DATA_MATCH_MINIMUM):
    """Find maximal source-image runs, resolving overlapping candidates.

    This routine deliberately rejects two-way ambiguous equal-length runs;
    it must never infer an original file offset from repeated filler.
    """
    candidates = []
    for dest in range(len(destination) - seed + 1):
        matches = index.get(bytes(destination[dest:dest + seed]))
        if not matches:
            continue
        for kind, source_offset, source, pos in matches:
            if any(candidate["source"] == kind
                   and candidate["runtime_ds_offset"] <= dest
                   < candidate["runtime_ds_offset"] + candidate["length"]
                   and candidate["source_file_offset"] + dest -
                   candidate["runtime_ds_offset"] == source_offset
                   for candidate in candidates):
                continue
            # The first usable seed may occur *inside* the copy if its
            # beginning contains low-entropy bytes. Recover the true start.
            back = 0
            while (dest > back and pos > back
                   and destination[dest - back - 1] == source[pos - back - 1]):
                back += 1
            start = dest - back
            source_start = pos - back
            size = seed + back
            while (start + size < len(destination) and source_start + size < len(source)
                   and destination[start + size] == source[source_start + size]):
                size += 1
            if size >= minimum:
                candidates.append({"source": kind,
                                   "source_file_offset": source_offset - back,
                                   "runtime_ds_offset": start, "length": size})
    candidates.sort(key=lambda item: (-item["length"], item["runtime_ds_offset"],
                                      item["source"], item["source_file_offset"]))
    origins = {}
    for candidate in candidates:
        key = candidate["runtime_ds_offset"], candidate["length"]
        origins.setdefault(key, set()).add((candidate["source"],
                                            candidate["source_file_offset"]))
    chosen = []
    occupied = bytearray(len(destination))
    for candidate in candidates:
        start = candidate["runtime_ds_offset"]
        end = start + candidate["length"]
        if len(origins[(start, candidate["length"])]) != 1 or any(occupied[start:end]):
            continue
        occupied[start:end] = b"\x01" * candidate["length"]
        chosen.append(candidate)
    return chosen


def zero_regions(data, minimum=128):
    """Return large exact zero ranges; these are *candidates*, not proven BSS."""
    spans = []
    position = 0
    while position < len(data):
        if data[position] != 0:
            position += 1
            continue
        end = position + 1
        while end < len(data) and data[end] == 0:
            end += 1
        if end - position >= minimum:
            spans.append({"runtime_ds_offset": position, "length": end - position})
        position = end
    return sorted(spans, key=lambda span: (-span["length"],
                                            span["runtime_ds_offset"]))


def rtl_preimage_comparison(rtl, data):
    """Compare the proposed leading RTL data image, including mismatches.

    Report mismatch classes, not actual proprietary source/guest bytes. This
    is a diagnostic baseline for identifying relocations, NOT a validated
    instruction to copy the whole range into a new DOS executable.
    """
    size = RTL_DATA_IMAGE_END
    file_base = RTL_DATA_IMAGE_FILE_OFFSET
    if len(rtl) < file_base + size or len(data) < size:
        return None
    source = rtl[file_base:file_base + size]
    live = data[:size]
    equal = sum(a == b for a, b in zip(source, live))
    from_zero = sum(a == 0 and b != 0 for a, b in zip(source, live))
    to_zero = sum(a != 0 and b == 0 for a, b in zip(source, live))
    changed_nonzero = size - equal - from_zero - to_zero
    mask = bytes(0 if a != b else 1 for a, b in zip(source, live))
    return {"rtl_file_offset": file_base,
            "runtime_ds_offset": 0,
            "length": size,
            "equal_bytes": equal,
            "source_zero_to_nonzero": from_zero,
            "source_nonzero_to_zero": to_zero,
            "changed_nonzero_bytes": changed_nonzero,
            "largest_changed_regions": zero_regions(mask, minimum=16)[:12]}


def runtime_data_layout(exe, rtl, captures, segment, *, minimum=DATA_MATCH_MINIMUM):
    """Report EXE/RTL exact matches and zero spans in the *eventual* DS.

    Even while the loader has a different DS, the window is sampled at the
    later observed runtime segment, never at a guessed transient register.
    """
    if segment is None:
        raise ValueError("cannot inspect runtime data before original DS is observed")
    base = segment * 16
    if base + 0x10000 > RAM_SIZE:
        raise ValueError("runtime data segment outside conventional RAM")
    index = source_window_index(exe, rtl)
    snapshots = []
    for milliseconds, registers, ram in captures:
        data = ram[base:base + 0x10000]
        copies = exact_source_copies(data, index, minimum=minimum)
        # Earlier DS values can point inside the eventual 64KiB DS window.
        # This is a *physical alias*, so a copy there may be loader scratch
        # rather than intended data for the post-handoff program.
        difference = registers["DS"] - segment
        loader_alias = (difference * 16 if 0 < difference < 0x1000 else None)
        for entry in copies:
            entry["overlaps_loader_ds"] = (loader_alias is not None and
                entry["runtime_ds_offset"] + entry["length"] > loader_alias)
        snapshots.append({"milliseconds": milliseconds,
                          "loader_ds_alias_offset": loader_alias,
                          "source_copies": copies,
                          "rtl_preimage": rtl_preimage_comparison(rtl, data),
                          "zero_bytes": data.count(0),
                          "largest_zero_regions": zero_regions(data)[:10]})
    # Any zero across all settled snapshots is only a potential uninitialized
    # or deliberately cleared region, never evidence of C runtime BSS alone.
    settled = [ram[base:base + 0x10000] for milliseconds, _, ram in captures
               if milliseconds >= 500]
    zero_all = (bytes(1 if all(sample[i] == 0 for sample in settled) else 0
                      for i in range(0x10000)) if settled else b"")
    stable_zero_spans = (zero_regions(bytes(0 if flag else 1 for flag in zero_all))
                         if settled else [])
    first_prepared = next((ram[base:base + 0x10000] for time, _, ram in captures
                           if time >= 100), None)
    cleared = (bytes(0 if old != 0 and new == 0 else 1
                     for old, new in zip(first_prepared, settled[0]))
               if first_prepared is not None and settled else b"")
    return {"runtime_data_segment": segment,
            "minimum_source_copy_bytes": minimum,
            "snapshots": snapshots,
            "nonzero_at_first_prepared_to_zero_by_500ms": (
                len(cleared) - cleared.count(1) if cleared else None),
            "largest_cleared_regions_by_500ms": zero_regions(cleared, minimum=64)[:20],
            "stable_zero_regions_from_500ms": stable_zero_spans[:20]}


def intro_segment_selectors(header):
    """Return the resident FAR selector words named in our C layout.

    These are relocated DS words, not independent allocation handles. The
    original startup's two segment windows overlap by ten paragraphs; only
    the observed runtime values can establish which alias a selector uses.
    """
    selectors = {}
    for match in re.finditer(
            r"^\s*(GATE_DS_[A-Z0-9_]+_SEGMENT)\s*=\s*(0x[0-9a-fA-F]+)\s*,",
            header, re.MULTILINE):
        name, offset = match.group(1), int(match.group(2), 16)
        if 0x4d82 <= offset <= 0x4e4e:
            if name in selectors or offset in selectors.values():
                raise ValueError("duplicate intro segment selector %s" % name)
            selectors[name] = offset
    if not selectors:
        raise ValueError("no intro segment selectors in layout header")
    return selectors


def segment_selector_groups(memory, segment, selectors):
    """Classify the original two observed overlapping real-mode windows."""
    start = segment * 16
    if start + 0x10000 > len(memory):
        raise ValueError("runtime segment outside snapshot")
    groups = {"data": [], "data_minus_000a": [], "other": []}
    for name, offset in selectors.items():
        value = word(memory, start + offset)
        if value == segment:
            groups["data"].append(name)
        elif value == ((segment - 10) & 0xffff):
            groups["data_minus_000a"].append(name)
        else:
            groups["other"].append(name)
    return groups


def word(data, offset):
    return int.from_bytes(data[offset:offset + 2], "little")


def mz_image(exe):
    if len(exe) < 0x1c or exe[:2] != b"MZ":
        raise ValueError("expected a DOS MZ executable")
    header_size = word(exe, 8) * 16
    count = word(exe, 6)
    relocation_offset = word(exe, 0x18)
    if (header_size < 0x1c or header_size >= len(exe)
            or relocation_offset + count * 4 > header_size):
        raise ValueError("invalid DOS MZ header or relocation directory")
    image_size = (word(exe, 4) - 1) * 512 + (word(exe, 2) or 512)
    if image_size != len(exe):
        raise ValueError("MZ declared file length differs from supplied EXE")
    return {
        "header_bytes": header_size,
        "image_bytes": len(exe) - header_size,
        "relocation_count": count,
        "relative_cs": word(exe, 0x16),
        "initial_ip": word(exe, 0x14),
        "relative_ss": word(exe, 0x0e),
        "initial_sp": word(exe, 0x10),
    }


def parse_regs(path):
    result = {}
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        key, sep, value = line.partition("=")
        if sep != "=" or key not in {"CS", "IP", "DS", "ES", "SS", "SP"}:
            continue
        if key in result:
            raise ValueError("duplicate register %s in %s" % (key, path))
        result[key] = int(value.strip(), 16)
        if not 0 <= result[key] <= 0xffff:
            raise ValueError("invalid register %s in %s" % (key, path))
    if set(result) != {"CS", "IP", "DS", "ES", "SS", "SP"}:
        raise ValueError("missing registers in %s" % path)
    return result


def snapshots_from_directories(directories):
    entries = []
    for directory in directories:
        for path in directory.glob("at-*.regs"):
            token = path.stem.removeprefix("at-")
            if not token.isdecimal():
                raise ValueError("invalid snapshot time in %s" % path)
            time = int(token)
            image = path.with_suffix(".bin")
            if not image.is_file():
                raise ValueError("snapshot RAM missing for %s" % path)
            memory = image.read_bytes()
            if len(memory) != RAM_SIZE:
                raise ValueError("snapshot %s has %d bytes, expected 1 MiB" %
                                 (image, len(memory)))
            entries.append((time, parse_regs(path), memory))
    entries.sort(key=lambda entry: entry[0])
    if not entries or entries[0][0] != 0:
        raise ValueError("need an at-0.regs/at-0.bin baseline")
    if len({time for time, _, _ in entries}) != len(entries):
        raise ValueError("duplicate snapshot times")
    return entries


def snapshots_from(directory):
    """Retain the original one-directory API for callers and tests."""
    return snapshots_from_directories([directory])


def relocated_timing_entry(original, ram, original_offset, length,
                           relocated_word_offsets, resident_start,
                           original_bank_offset):
    """Verify a complete routine, masking only known 16-bit relocations."""
    relative = original_offset - original_bank_offset
    if relative < 0 or original_offset + length > len(original):
        raise ValueError("timing entry outside source RTL")
    live_start = resident_start + relative
    if live_start < 0 or live_start + length > len(ram):
        raise ValueError("timing entry outside snapshot")
    source = original[original_offset:original_offset + length]
    live = ram[live_start:live_start + length]
    masked = {i for start in relocated_word_offsets for i in (start, start + 1)}
    if (any(start + 2 > length or source[start:start + 2] != b"\0\0"
            for start in relocated_word_offsets)
            or any(source[i] != live[i] for i in range(length) if i not in masked)):
        return {"matched": False}
    return {
        "matched": True,
        "original_rtl_offset": original_offset,
        "length": length,
        "matched_nonrelocated_bytes": length - len(masked),
        "relocated_words": [word(live, offset) for offset in relocated_word_offsets],
    }


def analyze(exe, rtl, captures, selector_fields=None):
    if selector_fields is None:
        source = Path(__file__).resolve().parent.parent / "decomp/launcher/gate_memory_layout.h"
        selector_fields = intro_segment_selectors(source.read_text())
    header = mz_image(exe)
    relative = header["relative_cs"]
    image_load = (captures[0][1]["CS"] - relative) & 0xffff
    image_start = image_load * 16
    resident_bank = next(bank for bank in RTL_BANKS
                         if bank["segment"] == RESIDENT_SEGMENT)
    resident_start = (image_load + RESIDENT_SEGMENT) * 16
    original_start = resident_bank["file_base"]
    bank_size = resident_bank["size"]
    if (image_start + header["image_bytes"] > RAM_SIZE
            or resident_start + bank_size > RAM_SIZE
            or original_start + bank_size > len(rtl)):
        raise ValueError("EXE or RTL bank outside supplied RAM/file")
    original_bank = rtl[original_start:original_start + bank_size]
    original_image = exe[header["header_bytes"]:]
    static_file_offset, static_ds_offset, static_length = STATIC_EXE_DATA
    # Older synthetic unit fixtures intentionally have much smaller EXEs.
    # Do not claim the block is present when their source cannot contain it.
    static_exe_data = (exe[static_file_offset:static_file_offset + static_length]
                       if static_file_offset + static_length <= len(exe) else None)
    # Derive the eventual data segment only from observed registers when
    # execution has reached the resident bank with the normal shared DS/SS.
    # Sampling this *same* segment at earlier times reveals initialization
    # without pretending that the loader's earlier DS points to it.
    settled_segments = {
        regs["DS"] for _, regs, _ in captures
        if (regs["CS"] == ((image_load + RESIDENT_SEGMENT) & 0xffff)
            and regs["DS"] == regs["SS"] == regs["ES"])
    }
    runtime_data_segment = (next(iter(settled_segments))
                            if len(settled_segments) == 1 else None)
    result = {
        "original_exe": {
            "sha256": hashlib.sha256(exe).hexdigest(),
            "image_load_segment": image_load,
            "image_physical_start": image_start,
            **header,
        },
        "original_rtl_sha256": hashlib.sha256(rtl).hexdigest(),
        "resident_bank": {
            "relative_segment": RESIDENT_SEGMENT,
            "original_rtl_offset": original_start,
            "length": bank_size,
        },
        "observed_runtime_data_segment": runtime_data_segment,
        "intro_segment_selector_offsets": selector_fields,
        "first_complete_segment_selectors": None,
        "static_exe_data": {"original_exe_offset": static_file_offset,
                            "runtime_ds_offset": static_ds_offset,
                            "length": static_length},
        "first_complete_static_exe_data_ms": None,
        "snapshots": [],
    }
    prev_ram = None
    for time, regs, ram in captures:
        image = ram[image_start:image_start + len(original_image)]
        bank = ram[resident_start:resident_start + bank_size]
        image_matches = sum(x == y for x, y in zip(image, original_image))
        bank_matches = sum(x == y for x, y in zip(bank, original_bank))
        try:
            _, _, witnesses = validate_bank(rtl, ram, resident_bank, image_load)
            verified = True
        except ValueError:
            witnesses = []
            verified = False

        calibration = {"matched": False}
        timing_setup = {"matched": False}
        timing_restore = {"matched": False}
        tick_callback = {"matched": False}
        if verified:
            calibration = relocated_timing_entry(
                rtl, ram, *CALIBRATION_ENTRY, resident_start, original_start)
            timing_setup = relocated_timing_entry(
                rtl, ram, *TIMING_SETUP_ENTRY, resident_start, original_start)
            timing_restore = relocated_timing_entry(
                rtl, ram, *TIMING_RESTORE_ENTRY, resident_start, original_start)
            tick_callback = relocated_timing_entry(
                rtl, ram, *TICK_CALLBACK_ENTRY, resident_start, original_start)
            if (calibration["matched"]
                    and calibration["relocated_words"][0] != calibration["relocated_words"][1]):
                calibration = {"matched": False}
            if (tick_callback["matched"] and runtime_data_segment is not None
                    and tick_callback["relocated_words"][0] != runtime_data_segment):
                tick_callback = {"matched": False}
            if (calibration["matched"] and tick_callback["matched"]
                    and calibration["relocated_words"][0] != tick_callback["relocated_words"][1]):
                tick_callback = {"matched": False}
            if timing_setup["matched"]:
                operands = timing_setup["relocated_words"]
                # The last two absolute fixups are the installed INT 1Ch
                # callback, not arbitrary values in a source byte window.
                if operands[6:] != [TICK_CALLBACK_ENTRY[0] - original_start,
                                     image_load + RESIDENT_SEGMENT]:
                    timing_setup = {"matched": False}
            if timing_setup["matched"] and timing_restore["matched"]:
                setup = timing_setup["relocated_words"]
                restore = timing_restore["relocated_words"]
                if ([setup[i] for i in range(6)] !=
                        [restore[i] for i in (0, 1, 3, 4, 5, 6)]):
                    timing_restore = {"matched": False}

        physical_ip = ((regs["CS"] << 4) + regs["IP"]) & 0xfffff
        entry = {"kind": "unmapped"}
        if physical_ip + 16 <= RAM_SIZE:
            if (image_start <= physical_ip <= image_start + len(image) - 16
                    and ram[physical_ip:physical_ip + 16] == original_image[
                        physical_ip - image_start:physical_ip - image_start + 16]):
                entry = {"kind": "exe-exact-16",
                         "file_offset": header["header_bytes"] + physical_ip - image_start}
            elif (verified and resident_start <= physical_ip <= resident_start + bank_size - 16
                  and ram[physical_ip:physical_ip + 16] == original_bank[
                      physical_ip - resident_start:physical_ip - resident_start + 16]):
                entry = {"kind": "rtl-exact-16",
                         "file_offset": original_start + physical_ip - resident_start}
            elif calibration["matched"] and (resident_start + CALIBRATION_ENTRY[0] - original_start
                    <= physical_ip < resident_start + CALIBRATION_ENTRY[0] - original_start + CALIBRATION_ENTRY[1]):
                entry = {"kind": "rtl-relocated-calibration",
                         "file_offset": original_start + physical_ip - resident_start,
                         "function_offset": CALIBRATION_ENTRY[0]}

        data = ram[(regs["DS"] << 4):(regs["DS"] << 4) + 0x10000]
        selected_words = ({"%04x" % offset: word(data, offset)
                           for offset in DATA_WORDS}
                          if len(data) == 0x10000 else {})
        runtime_data = (ram[runtime_data_segment * 16:
                            runtime_data_segment * 16 + 0x10000]
                        if runtime_data_segment is not None else b"")
        runtime_words = ({"%04x" % offset: word(runtime_data, offset)
                          for offset in DATA_WORDS}
                         if len(runtime_data) == 0x10000 else {})
        static_match = (sum(a == b for a, b in zip(
            runtime_data[static_ds_offset:static_ds_offset + static_length],
            static_exe_data)) if (len(runtime_data) == 0x10000
                                  and static_exe_data is not None) else None)
        if (static_match == static_length
                and result["first_complete_static_exe_data_ms"] is None):
            result["first_complete_static_exe_data_ms"] = time
        selector_groups = (segment_selector_groups(ram, runtime_data_segment,
                                                   selector_fields)
                           if runtime_data_segment is not None else None)
        if (selector_groups is not None and not selector_groups["other"]
                and result["first_complete_segment_selectors"] is None):
            result["first_complete_segment_selectors"] = {
                "milliseconds": time,
                "data": selector_groups["data"],
                "data_minus_000a": selector_groups["data_minus_000a"],
            }
        timing_words = ({"%04x" % offset: word(runtime_data, offset)
                         for offset in (0x05ba, 0x05bc, 0x05be)}
                        if len(runtime_data) == 0x10000 else {})
        timing_flag = (runtime_data[0x05c8]
                       if len(runtime_data) == 0x10000 else None)
        int1c = {"offset": word(ram, 0x70), "segment": word(ram, 0x72)}
        callback_expected = (image_load + RESIDENT_SEGMENT,
                             TICK_CALLBACK_ENTRY[0] - original_start)
        callback_installed = (tick_callback["matched"]
                              and (int1c["segment"], int1c["offset"]) == callback_expected)
        changed = (None if prev_ram is None else
                   sum(a != b for a, b in zip(prev_ram, ram)))
        result["snapshots"].append({
            "milliseconds": time,
            "registers": regs,
            "physical_ip": physical_ip,
            "instruction_source": entry,
            "initial_mz_entry": (regs["CS"] == ((image_load + relative) & 0xffff)
                                 and regs["IP"] == header["initial_ip"]),
            "initial_mz_stack": (regs["SS"] == ((image_load + header["relative_ss"]) & 0xffff)
                                 and regs["SP"] == header["initial_sp"]),
            "exe_image_matching_bytes": image_matches,
            "resident_bank_matching_bytes": bank_matches,
            "resident_bank_verified": verified,
            "resident_bank_witness_offsets": witnesses,
            "timing_calibration_entry": calibration,
            "timing_setup_entry": timing_setup,
            "timing_restore_entry": timing_restore,
            "timing_tick_callback_entry": tick_callback,
            "int1c_vector": int1c,
            "timing_tick_callback_installed": callback_installed,
            "timing_runtime_words": timing_words,
            "timing_runtime_flag_byte": timing_flag,
            "data_stack_same_segment": regs["DS"] == regs["SS"],
            "selected_ds_words": selected_words,
            "selected_runtime_data_words": runtime_words,
            "static_exe_data_matching_bytes": static_match,
            "runtime_segment_selector_counts": (
                {key: len(names) for key, names in selector_groups.items()}
                if selector_groups is not None else None),
            "changed_memory_bytes": changed,
        })
        prev_ram = ram
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--rtl", required=True, type=Path)
    parser.add_argument("--snapshot-dir", required=True, type=Path,
                        action="append", help="repeat for additional time ranges")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--runtime-data-layout", action="store_true",
                        help="find exact original-file copies and zero spans in the eventual 64KiB DS")
    args = parser.parse_args()
    try:
        exe = args.exe.read_bytes()
        rtl = args.rtl.read_bytes()
        captures = snapshots_from_directories(args.snapshot_dir)
        report = analyze(exe, rtl, captures)
        if args.runtime_data_layout:
            report["runtime_data_layout"] = runtime_data_layout(
                exe, rtl, captures, report["observed_runtime_data_segment"])
    except (ValueError, OSError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        exe = report["original_exe"]
        bank = report["resident_bank"]
        print("MZ image load=%04x, entry=%04x:%04x, relocations=%d" % (
            exe["image_load_segment"], exe["relative_cs"],
            exe["initial_ip"], exe["relocation_count"]))
        print("RTL bank relative=%04x, file offset=%05x, size=%d" % (
            bank["relative_segment"], bank["original_rtl_offset"], bank["length"]))
        for item in report["snapshots"]:
            regs = item["registers"]
            print("%4dms %04x:%04x DS=%04x SS:SP=%04x:%04x "
                  "EXE=%d/%d RTL=%d/%d verified=%s IP=%s changed=%s" % (
                      item["milliseconds"], regs["CS"], regs["IP"],
                      regs["DS"], regs["SS"], regs["SP"],
                      item["exe_image_matching_bytes"], exe["image_bytes"],
                      item["resident_bank_matching_bytes"], bank["length"],
                      item["resident_bank_verified"],
                      item["instruction_source"]["kind"],
                      item["changed_memory_bytes"]))
            if item["timing_calibration_entry"]["matched"]:
                print("       timing calibration: original rtl:5806 verified; "
                      "INT 1Ch callback installed=%s; count=%s flag=%s" % (
                          item["timing_tick_callback_installed"],
                          item["timing_runtime_words"].get("05ba"),
                          item["timing_runtime_flag_byte"]))
        layout = report["first_complete_segment_selectors"]
        if layout is not None:
            print("Segment selectors complete by %dms: %d same-DS, "
                  "%d DS-minus-10-paragraphs (160 bytes); "
                  "the windows overlap" % (
                      layout["milliseconds"], len(layout["data"]),
                      len(layout["data_minus_000a"])))
        complete = report["first_complete_static_exe_data_ms"]
        if complete is not None:
            static = report["static_exe_data"]
            print("EXE static data complete by %dms: exe:%05x +%d bytes "
                  "at runtime DS:%04x (exact original bytes)" % (
                      complete, static["original_exe_offset"], static["length"],
                      static["runtime_ds_offset"]))
        if args.runtime_data_layout:
            layout = report["runtime_data_layout"]
            print("Eventual DS=%04x; exact source copies >=%d bytes "
                  "(zero/filler windows excluded)" % (
                      layout["runtime_data_segment"],
                      layout["minimum_source_copy_bytes"]))
            for sample in layout["snapshots"]:
                if sample["milliseconds"] < 100:
                    continue
                print(" %4dms: %d exact source copies, %d zero bytes" % (
                    sample["milliseconds"], len(sample["source_copies"]),
                    sample["zero_bytes"]))
                alias = sample["loader_ds_alias_offset"]
                if alias is not None:
                    print("       current loader DS physically aliases eventual DS:%04x" % alias)
                for match in sample["source_copies"][:12]:
                    print("     %-3s:%05x -> DS:%04x +%d bytes%s" % (
                        match["source"], match["source_file_offset"],
                        match["runtime_ds_offset"], match["length"],
                        " (overlaps loader DS)" if match["overlaps_loader_ds"] else ""))
                preimage = sample["rtl_preimage"]
                if preimage is not None:
                    print("       RTL +0029 preimage DS:0000..3591: %d/%d exact, "
                          "%d zero->nonzero, %d nonzero->zero, %d changed nonzero" % (
                              preimage["equal_bytes"], preimage["length"],
                              preimage["source_zero_to_nonzero"],
                              preimage["source_nonzero_to_zero"],
                              preimage["changed_nonzero_bytes"]))
                    if sample["milliseconds"] in (100, 500, 1500):
                        for span in preimage["largest_changed_regions"][:8]:
                            print("         mismatch DS:%04x +%d bytes" % (
                                span["runtime_ds_offset"], span["length"]))
            cleared = layout["nonzero_at_first_prepared_to_zero_by_500ms"]
            if cleared is not None:
                print("%d bytes nonzero at first >=100ms sample were zero by "
                      "first >=500ms sample (cannot label them BSS)" % cleared)
                for span in layout["largest_cleared_regions_by_500ms"][:12]:
                    print("     DS:%04x +%d bytes cleared" % (
                        span["runtime_ds_offset"], span["length"]))
            zero_spans = layout["stable_zero_regions_from_500ms"]
            print("Stable zero candidates across >=500ms samples: %d large spans" %
                  len(zero_spans))
            for span in zero_spans[:12]:
                print("     DS:%04x +%d zero bytes (not proven BSS)" % (
                    span["runtime_ds_offset"], span["length"]))


if __name__ == "__main__":
    main()

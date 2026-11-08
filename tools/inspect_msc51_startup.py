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
    args = parser.parse_args()
    try:
        report = analyze(args.exe.read_bytes(), args.rtl.read_bytes(),
                         snapshots_from_directories(args.snapshot_dir))
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


if __name__ == "__main__":
    main()

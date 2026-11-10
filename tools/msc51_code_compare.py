#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare compiled Microsoft C 5.1 OMF routines with bounded original code.

The *object* is not the final linked image: FIXUPP bytes are relocatable and
cannot be called exact even if the remaining instruction bytes agree. A byte
comparison is evidence about code generation, not proof of behavioral parity.
Only original file offsets with independently established end boundaries are
included. No proprietary bytes are emitted in reports.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from msc51_compare import (OMFModule, OMFRecord, omf_segments, segment_kind,
                           stitched_public_code)
from msc51_object_symbols import linked_objects


PINNED = {
    "GATE.EXE": "767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b",
    "GATE.RTL": "c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566",
}


@dataclass(frozen=True)
class Routine:
    file: str
    start: int
    end: int  # exclusive original file offset, not a linked segment address
    symbol: str


# EXE ranges are a contiguous group recovered from the actual disassembly.
# RTL bounds exclude any unclaimed alignment bytes between procedures.
ROUTINES = (
    Routine("GATE.EXE", 0x0e37, 0x0f18, "gate_exe_handle_intro_input"),
    Routine("GATE.EXE", 0x0f18, 0x0f61, "gate_exe_wait_interruptible"),
    Routine("GATE.EXE", 0x0f61, 0x103a, "gate_exe_fade_in_picture"),
    Routine("GATE.EXE", 0x103a, 0x117e, "gate_exe_measure_caption"),
    Routine("GATE.EXE", 0x117e, 0x1302, "gate_exe_show_caption"),
    Routine("GATE.EXE", 0x1302, 0x1a14, "gate_exe_play_intro"),
    Routine("GATE.EXE", 0x1a14, 0x1c11, "gate_exe_play_title_interlude"),
    Routine("GATE.EXE", 0x1c11, 0x1c6c, "gate_exe_reveal_red_bar"),
    Routine("GATE.EXE", 0x1c6c, 0x1cab, "gate_exe_reveal_credit_color"),
    Routine("GATE.RTL", 0x1266f, 0x12772, "gate_rtl_prepare_picture_display"),
    Routine("GATE.RTL", 0x12773, 0x12822, "gate_rtl_fade_palette_in"),
    Routine("GATE.RTL", 0x12823, 0x12892, "gate_rtl_clear_palette_buffer"),
    Routine("GATE.RTL", 0x12893, 0x12996, "gate_rtl_fade_palette_out"),
    Routine("GATE.RTL", 0x12b19, 0x12b67, "gate_rtl_show_picture"),
    Routine("GATE.RTL", 0x12ed7, 0x12f2b, "gate_rtl_draw_picture_record"),
    Routine("GATE.RTL", 0x12f2b, 0x12f6b, "gate_rtl_draw_picture_record_sequence"),
    Routine("GATE.RTL", 0xf473, 0xf49b, "gate_rtl_wait_vertical_retrace"),
    Routine("GATE.RTL", 0xf61b, 0xf827, "gate_rtl_upload_vga_palette"),
)


def parse_object(data: bytes) -> OMFModule:
    """Parse one strict THEADR..MODEND module, with record checksums."""
    cursor = 0
    records = []
    name = ""
    while cursor < len(data):
        if cursor + 3 > len(data):
            raise ValueError("truncated OMF record header")
        kind = data[cursor]
        length = int.from_bytes(data[cursor + 1:cursor + 3], "little")
        end = cursor + 3 + length
        if length < 1 or end > len(data):
            raise ValueError("truncated OMF record")
        if data[end - 1] and sum(data[cursor:end]) & 0xff:
            raise ValueError("invalid OMF checksum")
        if not records:
            if kind not in (0x80, 0x82):
                raise ValueError("OMF object does not begin with THEADR")
            count = data[cursor + 3]
            if cursor + 4 + count > end - 1:
                raise ValueError("truncated THEADR name")
            name = data[cursor + 4:cursor + 4 + count].decode("ascii", errors="replace")
        elif kind in (0x80, 0x82):
            raise ValueError("nested OMF module")
        records.append(OMFRecord(kind, cursor, end))
        cursor = end
        if kind in (0x8a, 0x8b):
            if any(data[cursor:]):
                raise ValueError("nonzero bytes after OMF MODEND")
            return OMFModule(name, 0, cursor, tuple(records))
    raise ValueError("OMF module lacks MODEND")


def object_routines(data: bytes) -> dict[str, tuple[bytes, bytes]]:
    module = parse_object(data)
    segments = omf_segments(data, module)
    found: dict[str, tuple[bytes, bytes]] = {}
    for symbol, segment, _offset, code, fixed, _ in stitched_public_code(
            data, module, min_size=1, max_size=16384):
        if segment_kind(segments.get(segment, {})) != "code":
            continue
        if symbol in found:
            raise ValueError(f"duplicate CODE public: {symbol}")
        found[symbol] = code, fixed
    return found


def compare(routine: Routine, original: bytes, code: bytes,
            fixed: bytes) -> dict:
    """Compare aligned byte positions; never infer equivalence from a score."""
    if not (0 <= routine.start < routine.end <= len(original)):
        raise ValueError("original function is out of bounds")
    if len(code) != len(fixed):
        raise ValueError("invalid compiled relocation mask")
    target = original[routine.start:routine.end]
    examined = min(len(code), len(target))
    fixed_count = sum(fixed[:examined])
    equal_fixed = sum(bool(fixed[i]) and code[i] == target[i]
                      for i in range(examined))
    equal_raw = sum(code[i] == target[i] for i in range(examined))
    prefix = 0
    while prefix < examined and (not fixed[prefix] or code[prefix] == target[prefix]):
        prefix += 1
    # An OMF PUBDEF gives a *start*, not an end: the next public may follow
    # unnamed compiler helpers. It is a conservative candidate span, not a
    # measured compiled function size. A full match is conclusive only when
    # the entire span agrees with a separately proven original boundary.
    complete = (len(code) == len(target) and fixed_count == equal_fixed and
                fixed_count >= 12)
    raw_complete = complete and equal_raw == len(target)
    status = ("exact_object_bytes" if raw_complete else
              "relocation_aware_match" if complete else "different")
    return {
        "symbol": routine.symbol,
        "original": routine.file,
        "file_start": f"0x{routine.start:05x}",
        "file_end": f"0x{routine.end:05x}",
        "original_bytes": len(target),
        "compiled_public_span_bytes": len(code),
        "compared_bytes": examined,
        "fixed_compared_bytes": fixed_count,
        "equal_fixed_bytes": equal_fixed,
        "equal_raw_bytes": equal_raw,
        "relocation_masked_bytes": examined - fixed_count,
        "matching_fixed_prefix_bytes": prefix,
        "compiled_span_boundary": "next_public_or_known_code_extent",
        "status": status,
        "interpretation": ("unlinked OMF; span may include private functions; "
                           "behavioral equivalence not tested"),
    }


def collect(stage: Path, originals: dict[str, bytes],
            routines: tuple[Routine, ...] = ROUTINES) -> list[dict]:
    code = {}
    aliases = {}
    symbol_map = stage / "SYMBOL.MAP"
    if symbol_map.is_file():
        for line in symbol_map.read_text().splitlines():
            columns = line.split()
            if len(columns) == 2:
                aliases[columns[0]] = columns[1]
    # Diagnostic .OBJ files in this directory also export _main and other
    # probe-only publics; they are not part of the rebuilt launcher.
    response = (stage / "LINK.RSP").read_text(errors="replace")
    for filename in linked_objects(response):
        path = stage / filename
        if not path.is_file():
            raise ValueError(f"missing linked object: {path}")
        for symbol, pair in object_routines(path.read_bytes()).items():
            # Microsoft C adds one ABI leading underscore to C globals.
            name = symbol[1:] if symbol.startswith("_") else symbol
            name = aliases.get(name, name)
            if name in code:
                raise ValueError(f"public {symbol} exported by multiple objects")
            code[name] = (path.name, *pair)
    results = []
    for routine in routines:
        if routine.symbol not in code:
            results.append({"symbol": routine.symbol, "original": routine.file,
                            "file_start": f"0x{routine.start:05x}",
                            "status": "missing_compiled_public"})
            continue
        filename, bytes_, mask = code[routine.symbol]
        result = compare(routine, originals[routine.file], bytes_, mask)
        result["object"] = filename
        results.append(result)
    return results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, default=Path("build/msc51-all"),
                        help="directory with same-build Microsoft C 5.1 .OBJ files")
    parser.add_argument("--original-game-dir", type=Path, required=True,
                        help="read-only original GATE.EXE and GATE.RTL")
    parser.add_argument("--json", type=Path, help="write metadata-only JSON report")
    parser.add_argument("--require-exact", action="append", default=[],
                        metavar="SYMBOL", help="fail if this symbol does not byte-match")
    args = parser.parse_args(argv)
    originals = {}
    for filename, digest in PINNED.items():
        original = (args.original_game_dir / filename).read_bytes()
        if hashlib.sha256(original).hexdigest() != digest:
            parser.error(f"original {filename} does not match pinned SHA-256")
        originals[filename] = original
    try:
        report = collect(args.stage, originals)
    except ValueError as error:
        parser.error(str(error))
    for row in report:
        if row["status"] == "missing_compiled_public":
            print(f"{row['symbol']}: missing compiled public")
        else:
            print(f"{row['symbol']} ({row['object']}): {row['status']}, "
                  f"public span {row['compiled_public_span_bytes']}/"
                  f"{row['original_bytes']} original bytes, "
                  f"{row['equal_fixed_bytes']}/{row['fixed_compared_bytes']} "
                  f"fixed bytes agree, prefix={row['matching_fixed_prefix_bytes']}")
    counts = {status: sum(row["status"] == status for row in report)
              for status in ("exact_object_bytes", "relocation_aware_match",
                             "different", "missing_compiled_public")}
    print("comparison summary: " + ", ".join(f"{key}={value}" for key, value in counts.items()))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"compiler": "Microsoft C 5.1 /AL /Od",
                                        "counts": counts, "routines": report}, indent=2) + "\n")
    failures = {row["symbol"] for row in report
                if row["status"] != "exact_object_bytes"}
    return int(bool(failures.intersection(args.require_exact) or
                    set(args.require_exact) - {row["symbol"] for row in report}))


if __name__ == "__main__":
    raise SystemExit(main())

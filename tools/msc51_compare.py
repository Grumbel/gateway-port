#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare the original Gateway CRT routines against a local Microsoft C library.

Searches the raw OMF library for linked procedure code while masking mutable
address operands. A positive match identifies an instruction-level library
fingerprint; a miss cannot exclude code split across OMF data records.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import sys


GATE_RTL_SHA256 = "c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566"


@dataclass(frozen=True)
class Signature:
    name: str
    offset: int
    size: int
    mutable: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class OMFRecord:
    kind: int
    start: int
    end: int


@dataclass(frozen=True)
class OMFModule:
    name: str
    start: int
    end: int
    records: tuple[OMFRecord, ...]


# Original file offsets, not runtime segment:offset addresses. The mutable
# operands are data addresses or the destination of a FAR call. We never copy
# original executable bytes into the source repository.
SIGNATURES = (
    Signature("srand", 0x91D8, 17, ((7, 9), (11, 13))),
    Signature("rand", 0x91EA, 40, ((10, 12), (14, 16), (17, 21), (28, 30), (32, 34))),
)


def build_mask(signature: Signature) -> bytes:
    mask = bytearray([1] * signature.size)
    for start, end in signature.mutable:
        if not (0 <= start < end <= signature.size):
            raise ValueError(f"Invalid mutable range for {signature.name}")
        mask[start:end] = b"\0" * (end - start)
    return bytes(mask)


def search_code(haystack: bytes, needle: bytes, mask: bytes) -> list[int]:
    """Return raw file offsets, ignoring only the explicitly mutable bytes."""
    if len(needle) != len(mask) or not needle or not any(mask):
        raise ValueError("A nonempty pattern and nonzero byte mask are required")
    anchor = next(i for i, value in enumerate(mask) if value)
    positions = []
    cursor = 0
    while True:
        candidate = haystack.find(needle[anchor:anchor + 1], cursor)
        if candidate == -1:
            return positions
        start = candidate - anchor
        cursor = candidate + 1
        if start < 0 or start + len(needle) > len(haystack):
            continue
        if all(not fixed or haystack[start + i] == needle[i]
               for i, fixed in enumerate(mask)):
            positions.append(start)


def compare_bytes(original: bytes, library: bytes) -> list[dict]:
    if hashlib.sha256(original).hexdigest() != GATE_RTL_SHA256:
        raise ValueError("GATE.RTL does not match the pinned original SHA-256")
    matches = []
    for signature in SIGNATURES:
        end = signature.offset + signature.size
        if end > len(original):
            raise ValueError("Original GATE.RTL is truncated")
        mask = build_mask(signature)
        locations = search_code(library, original[signature.offset:end], mask)
        matches.append({
            "routine": signature.name,
            "rtl_offset": f"0x{signature.offset:05x}",
            "bytes": signature.size,
            "fixed_bytes": sum(mask),
            "library_offsets": [f"0x{index:05x}" for index in locations],
        })
    return matches


def parse_omf_library(data: bytes) -> list[OMFModule]:
    """Read MS-DOS OMF library module boundaries; never scan the dictionary.

    The F0 header determines page alignment and the start of the library
    dictionary. Each aligned THEADR/LHEADR module ends at MODEND (8A/8B).
    This deliberately does not attempt to relocate or decompile object code.
    """
    if len(data) < 16 or data[0] != 0xf0:
        raise ValueError("Not an OMF library (expected F0 header)")
    page_size = int.from_bytes(data[1:3], "little") + 3
    dictionary = int.from_bytes(data[3:7], "little")
    if not (16 <= page_size <= 32768 and page_size & (page_size - 1) == 0):
        raise ValueError(f"Invalid OMF library page size: {page_size}")
    if not (page_size <= dictionary <= len(data) and dictionary % 512 == 0):
        raise ValueError(f"Invalid OMF dictionary offset: {dictionary}")

    def record_at(pos: int) -> OMFRecord:
        if pos + 3 > dictionary:
            raise ValueError(f"Truncated OMF record header at 0x{pos:x}")
        size = int.from_bytes(data[pos + 1:pos + 3], "little")
        end = pos + 3 + size
        if not size or end > dictionary:
            raise ValueError(f"Truncated OMF record at 0x{pos:x}")
        return OMFRecord(data[pos], pos, end)

    modules = []
    pos = page_size
    while pos < dictionary:
        if data[pos] == 0xf1:  # dictionary marker, not an object module
            break
        if data[pos] not in (0x80, 0x82):
            raise ValueError(f"Missing OMF module header at 0x{pos:x}")
        first = record_at(pos)
        name_len = data[pos + 3]
        if pos + 4 + name_len > first.end - 1:
            raise ValueError(f"Truncated OMF module name at 0x{pos:x}")
        name = data[pos + 4:pos + 4 + name_len].decode("ascii", errors="replace")
        records = []
        cursor = pos
        while True:
            entry = record_at(cursor)
            records.append(entry)
            cursor = entry.end
            if entry.kind in (0x8a, 0x8b):
                break
            if cursor >= dictionary:
                raise ValueError(f"Unterminated OMF module at 0x{pos:x}")
        modules.append(OMFModule(name, pos, cursor, tuple(records)))
        next_page = ((cursor + page_size - 1) // page_size) * page_size
        if any(data[cursor:next_page]):
            raise ValueError(f"Nonzero OMF module padding at 0x{cursor:x}")
        pos = next_page
    return modules


def omf_index(data: bytes, pos: int, end: int) -> tuple[int, int]:
    """Return (variable-width OMF index, next byte position)."""
    if pos >= end:
        raise ValueError("Missing OMF index")
    first = data[pos]
    if first & 0x80:
        if pos + 1 >= end:
            raise ValueError("Truncated two-byte OMF index")
        return ((first & 0x7f) << 8) | data[pos + 1], pos + 2
    return first, pos + 1


def omf_publics(data: bytes, module: OMFModule) -> list[tuple[int, int, str]]:
    """Extract explicit PUBDEFs; do not guess a symbol from neighboring code."""
    symbols = []
    for record in module.records:
        if record.kind not in (0x90, 0x91):
            continue
        pos, end = record.start + 3, record.end - 1
        try:
            _, pos = omf_index(data, pos, end)  # group index
            segment, pos = omf_index(data, pos, end)
            if segment == 0:
                pos += 2  # explicit frame
            while pos < end:
                length = data[pos]
                pos += 1
                if pos + length > end:
                    raise ValueError("Truncated OMF public name")
                name = data[pos:pos + length].decode("ascii", errors="replace")
                pos += length
                width = 4 if record.kind == 0x91 else 2
                if pos + width > end:
                    raise ValueError("Truncated OMF public symbol")
                offset = int.from_bytes(data[pos:pos + width], "little")
                pos += width
                _, pos = omf_index(data, pos, end)  # type index
                symbols.append((segment, offset, name))
        except (ValueError, IndexError):
            # PUBDEF is supplemental diagnostics; a vendor extension must not
            # turn a valid direct-code match into a false negative.
            continue
    return symbols


def omf_segments(data: bytes, module: OMFModule) -> dict[int, dict[str, str]]:
    """Resolve SEGDEF segment/class names using module-local LNAMES indexes.

    The segment index is the one-based ordinal of SEGDEF records, not the
    LNAMES index. Unknown or malformed optional metadata is left unlabelled.
    """
    names = [""]
    segments = {}
    for record in module.records:
        pos, end = record.start + 3, record.end - 1
        try:
            if record.kind == 0x96:  # LNAMES
                while pos < end:
                    length = data[pos]
                    pos += 1
                    if pos + length > end:
                        raise ValueError("Truncated OMF LNAMES string")
                    names.append(data[pos:pos + length].decode("ascii", errors="replace"))
                    pos += length
            elif record.kind in (0x98, 0x99):  # SEGDEF / SEGDEF32
                index = len(segments) + 1
                if pos >= end:
                    raise ValueError("Truncated OMF SEGDEF attributes")
                attributes = data[pos]
                pos += 1
                if attributes >> 5 == 0:  # Absolute: frame and offset follow
                    pos += 3
                pos += 4 if record.kind == 0x99 else 2  # segment length
                segment_name, pos = omf_index(data, pos, end)
                class_name, pos = omf_index(data, pos, end)
                _, pos = omf_index(data, pos, end)  # overlay name
                if pos != end or segment_name >= len(names) or class_name >= len(names):
                    raise ValueError("Invalid OMF SEGDEF names")
                segments[index] = {"name": names[segment_name], "class": names[class_name]}
        except (ValueError, IndexError):
            # Keep indexes stable even if this particular SEGDEF is malformed.
            if record.kind in (0x98, 0x99):
                segments[len(segments) + 1] = {}
    return segments


def segment_kind(info: dict[str, str]) -> str:
    """Separate CODE-class entries from other classes without guessing content."""
    class_name = info.get("class", "").upper()
    if not class_name:
        return "unknown"
    if class_name == "CODE" or class_name.endswith("_CODE"):
        return "code"
    return "other"


def omf_match_details(data: bytes, modules: list[OMFModule],
                      position: int, size: int) -> dict | None:
    """Attribute a contiguous signature only if it lies inside LEDATA bytes."""
    for module in modules:
        if not (module.start <= position and position + size <= module.end):
            continue
        for record in module.records:
            if record.kind not in (0xa0, 0xa1):
                continue
            end = record.end - 1  # OMF checksum is not part of segment data
            try:
                segment, payload = omf_index(data, record.start + 3, end)
                width = 4 if record.kind == 0xa1 else 2
                if payload + width > end:
                    continue
                segment_offset = int.from_bytes(data[payload:payload + width], "little")
                payload += width
            except ValueError:
                continue
            if payload <= position and position + size <= end:
                code_offset = segment_offset + position - payload
                names = [name for seg, off, name in omf_publics(data, module)
                         if seg == segment and off == code_offset]
                detail = {
                    "module": module.name,
                    "module_offset": f"0x{module.start:05x}",
                    "segment_index": segment,
                    "segment_offset": f"0x{code_offset:04x}",
                }
                segment_info = omf_segments(data, module).get(segment, {})
                if segment_info:
                    detail["segment_name"] = segment_info["name"]
                    detail["segment_class"] = segment_info["class"]
                if names:
                    detail["public_symbol"] = names[0]
                return detail
    return None


def public_code_prefixes(data: bytes, module: OMFModule,
                         min_size: int = 16, max_size: int = 64):
    """Yield contiguous LEDATA bytes at PUBDEF entries and their record offsets.

    Do not run across another public entry or an OMF record boundary. The
    caller decides whether to use exact comparisons or FIXUPP relocation masks.
    """
    publics = omf_publics(data, module)
    for segment, offset, symbol in publics:
        following = [other for seg, other, _ in publics
                     if seg == segment and other > offset]
        upper = min(following) if following else offset + max_size
        for record in module.records:
            if record.kind not in (0xa0, 0xa1):
                continue
            limit = record.end - 1
            try:
                record_segment, cursor = omf_index(data, record.start + 3, limit)
                width = 4 if record.kind == 0xa1 else 2
                if cursor + width > limit:
                    continue
                record_offset = int.from_bytes(data[cursor:cursor + width], "little")
            except ValueError:
                continue
            cursor += width
            if record_segment != segment or not record_offset <= offset < record_offset + limit - cursor:
                continue
            start = cursor + offset - record_offset
            end = min(limit, start + max_size, start + upper - offset)
            if end - start >= min_size:
                yield symbol, segment, offset, data[start:end], record.start, start - cursor
            break


FIXUP_WIDTHS = {0: 1, 1: 2, 2: 2, 3: 4, 4: 1, 5: 2,
                9: 4, 11: 6, 13: 4}
MAX_MASKED_OPENING = 64


def parse_omf_fixupp(data: bytes, record: OMFRecord,
                     data_length: int | None) -> list[tuple[int, int]]:
    """Decode FIXUPP locations; return (data-byte offset, field width).

    Consume FRAME/TARGET threads and explicit target/frame indices to locate
    the *next* subrecord correctly. The target value is immaterial because we
    only mask fields that the linker is authorised to modify. Unsupported
    forms fail closed rather than inventing a mask.
    """
    pos, end = record.start + 3, record.end - 1
    fields = []

    def index(cursor: int) -> int:
        _, cursor = omf_index(data, cursor, end)
        return cursor

    while pos < end:
        first = data[pos]
        pos += 1
        if not first & 0x80:  # THREAD subrecord
            if first & 0x20:
                raise ValueError("Reserved OMF THREAD flag")
            method = (first >> 2) & 7
            if method > 5 or (not first & 0x40 and method > 2):
                raise ValueError("Unsupported OMF THREAD method")
            if method <= 2:
                pos = index(pos)
            continue
        if pos + 2 > end:
            raise ValueError("Truncated OMF FIXUP location and FIX Data")
        data_offset = ((first & 3) << 8) | data[pos]
        pos += 1
        location = (first >> 2) & 15
        width = FIXUP_WIDTHS.get(location)
        if width is None:
            raise ValueError(f"Unsupported OMF FIXUP location type {location}")
        fixdata = data[pos]
        pos += 1
        if not fixdata & 0x80:  # Explicit FRAME datum (when required)
            frame_method = (fixdata >> 4) & 7
            if frame_method not in (0, 1, 2, 4, 5):
                raise ValueError(f"Unsupported OMF FRAME method {frame_method}")
            if frame_method <= 2:
                pos = index(pos)
        if not fixdata & 0x08:  # Explicit TARGET datum (when required)
            target_method = (4 if fixdata & 0x04 else 0) | (fixdata & 3)
            if target_method not in (0, 1, 2, 4, 5, 6):
                raise ValueError(f"Unsupported OMF TARGET method {target_method}")
            pos = index(pos)
        if not fixdata & 0x04:  # Target displacement
            pos += 4 if record.kind == 0x9d else 2
        if pos > end:
            raise ValueError("Truncated OMF FIXUP displacement")
        if data_length is not None:
            if data_offset + width > data_length:
                raise ValueError("OMF FIXUP lies outside LEDATA payload")
            fields.append((data_offset, width))
    return fields


def omf_relocation_masks(data: bytes, module: OMFModule) -> dict[int, bytes | None]:
    """Make LEDATA masks from immediately following FIXUPP records.

    True/1 means an exact byte must match. False/0 means the linker could
    change that byte. A malformed or unsupported fixup makes the *entire*
    associated LEDATA unsuitable for relocation-aware comparison.
    """
    masks: dict[int, bytes | None] = {}
    current_start = None
    fixed = None
    for record in module.records:
        if record.kind in (0xa0, 0xa1):
            end = record.end - 1
            try:
                _, pos = omf_index(data, record.start + 3, end)
                width = 4 if record.kind == 0xa1 else 2
                if pos + width > end:
                    raise ValueError("Truncated OMF LEDATA offset")
                pos += width
                fixed = bytearray([1] * (end - pos))
                masks[record.start] = bytes(fixed)
            except ValueError:
                fixed = None
                masks[record.start] = None
            current_start = record.start
        elif record.kind in (0x9c, 0x9d):
            try:
                locations = parse_omf_fixupp(
                    data, record, len(fixed) if fixed is not None else None)
                if fixed is not None:
                    for offset, width in locations:
                        fixed[offset:offset + width] = bytes(width)
                    masks[current_start] = bytes(fixed)
            except ValueError:
                if current_start is not None:
                    masks[current_start] = None
                fixed = None
        else:
            current_start = None
            fixed = None
    return masks


def choose_relocated_prefix(original: bytes, code: bytes, mask: bytes,
                            min_size: int = 16) -> dict:
    """Resolve a masked public to one RTL offset or explain why we cannot.

    Only *fixed* agreeing bytes break ties. Return counts and file offsets,
    never executable bytes, so diagnostics are safe to export as JSON.
    """
    if len(mask) != len(code):
        return {"status": "invalid_or_unsupported_fixups"}
    if len(code) < min_size:
        return {"status": "too_short_for_opening", "available_bytes": len(code)}
    opening = min_size
    fixed_opening = sum(mask[:opening])
    # FIXUPP may relocate much of the beginning of a procedure. Extend the
    # masked anchor until it has the same 12 fixed bytes required previously;
    # never weaken the threshold or scan across a public/LEDATA boundary.
    while (fixed_opening < 12 and opening < len(code)
           and opening < MAX_MASKED_OPENING):
        fixed_opening += mask[opening]
        opening += 1
    if fixed_opening < 12:
        return {"status": "too_few_fixed_opening_bytes",
                "fixed_opening_bytes": fixed_opening,
                "opening_bytes": opening}
    positions = search_code(original, code[:opening], mask[:opening])
    if not positions:
        return {"status": "no_masked_opening_match",
                "fixed_opening_bytes": fixed_opening,
                "opening_bytes": opening}
    scores = []
    for rtl_offset in positions:
        matched = opening
        while matched < len(code) and rtl_offset + matched < len(original):
            if mask[matched] and original[rtl_offset + matched] != code[matched]:
                break
            matched += 1
        scores.append((sum(mask[:matched]), rtl_offset, matched))
    best_fixed = max(fixed for fixed, _, _ in scores)
    best = [(location, length) for fixed, location, length in scores
            if fixed == best_fixed]
    if len(best) != 1:
        return {"status": "ambiguous_best_fixed_match",
                "masked_opening_matches": len(positions),
                "opening_bytes": opening,
                "best_fixed_bytes": best_fixed,
                "tied_locations": [f"0x{location:05x}" for location, _ in best[:8]],
                "ties_truncated": len(best) > 8}
    rtl_offset, matched = best[0]
    return {"status": "unique", "rtl_offset": rtl_offset,
            "matched_bytes": matched, "fixed_compared_bytes": best_fixed,
            "opening_bytes": opening,
            "masked_opening_matches": len(positions)}


def scan_exact_crt_prefixes(original: bytes, library: bytes,
                            modules: list[OMFModule], min_size: int = 16,
                            relocations: bool = False) -> list[dict]:
    """Find unambiguous OMF public prefixes in the original RTL.

    Relocation-aware mode masks only FIXUPP locations and lengthens the
    comparison to 512 bytes per LEDATA/public boundary, but still requires
    at least 12 fixed bytes in an opening of 16–64 bytes. If several RTL locations
    match the masked opening, select a location only when the additional
    *fixed* bytes establish a unique longest match.
    Neither mode establishes that a public is a callable function.
    """
    hits = []
    for module in modules:
        segments = omf_segments(library, module)
        masks = omf_relocation_masks(library, module) if relocations else {}
        for symbol, segment, offset, code, record_start, data_offset in public_code_prefixes(
                library, module, min_size=min_size,
                max_size=512 if relocations else 64):
            code_mask = (masks.get(record_start) or b"")[data_offset:data_offset + len(code)]
            if relocations and len(code_mask) != len(code):
                continue
            if relocations:
                chosen = choose_relocated_prefix(original, code, code_mask, min_size)
                if chosen["status"] != "unique":
                    continue
                rtl_offset, matched = chosen["rtl_offset"], chosen["matched_bytes"]
            else:
                rtl_offset = original.find(code[:min_size])
                if rtl_offset < 0 or original.find(code[:min_size], rtl_offset + 1) >= 0:
                    continue
                matched = min_size
                while matched < len(code) and rtl_offset + matched < len(original):
                    if original[rtl_offset + matched] != code[matched]:
                        break
                    matched += 1
            info = segments.get(segment, {})
            hits.append({
                "symbol": symbol,
                "module": module.name,
                "rtl_offset": f"0x{rtl_offset:05x}",
                "segment_index": segment,
                "segment_offset": f"0x{offset:04x}",
                "segment_name": info.get("name", ""),
                "segment_class": info.get("class", ""),
                "segment_kind": segment_kind(info),
                "exact_prefix_bytes": matched,
            })
            if relocations:
                hits[-1]["relocation_aware_bytes"] = matched
                hits[-1]["masked_relocation_bytes"] = matched - sum(code_mask[:matched])
                hits[-1]["fixed_compared_bytes"] = sum(code_mask[:matched])
                hits[-1]["opening_bytes"] = chosen["opening_bytes"]
                # Retain the legacy exact-prefix metric, independent of the
                # masked comparison, for easy side-by-side interpretation.
                exact = 0
                while exact < matched and original[rtl_offset + exact] == code[exact]:
                    exact += 1
                hits[-1]["exact_prefix_bytes"] = exact
    # Two library publics with the same opening instructions do not justify
    # choosing either name, even if those bytes occur only once in GATE.RTL.
    counts = {}
    for hit in hits:
        counts[hit["rtl_offset"]] = counts.get(hit["rtl_offset"], 0) + 1
    return sorted((hit for hit in hits if counts[hit["rtl_offset"]] == 1),
                  key=lambda hit: (int(hit["rtl_offset"], 16), hit["symbol"]))


def summarize_public_prefixes(hits: list[dict]) -> dict:
    """Summarize original segment classes, not inferred function identities."""
    classes: dict[str, int] = {}
    code_locations = []
    for hit in hits:
        cls = hit["segment_class"] or "(unknown)"
        classes[cls] = classes.get(cls, 0) + 1
        if hit["segment_kind"] == "code":
            code_locations.append(int(hit["rtl_offset"], 16))
    result = {"total": len(hits), "segment_classes": dict(sorted(classes.items()))}
    if code_locations:
        result["code_class_entry_span"] = [
            f"0x{min(code_locations):05x}", f"0x{max(code_locations):05x}"]
    return result


def compare_public_scans(exact: list[dict], relocated: list[dict]) -> dict:
    """Report changes in attributed symbol/RTL pairs without merging evidence."""
    def key(hit: dict) -> tuple[str, str, str]:
        return hit["rtl_offset"], hit["module"], hit["symbol"]

    exact_keys = {key(hit) for hit in exact}
    relocated_keys = {key(hit) for hit in relocated}
    return {
        "exact_candidates": len(exact),
        "relocation_aware_candidates": len(relocated),
        "exact_only": [dict(rtl_offset=rtl, module=module, symbol=symbol)
                       for rtl, module, symbol in sorted(exact_keys - relocated_keys)],
        "relocation_only": [dict(rtl_offset=rtl, module=module, symbol=symbol)
                            for rtl, module, symbol in sorted(relocated_keys - exact_keys)],
    }


def explain_exact_only(original: bytes, library: bytes,
                       modules: list[OMFModule], exact_only: list[dict]) -> list[dict]:
    """Give each lost exact match an actionable OMF/matcher rejection reason."""
    explanations = []
    for hit in exact_only:
        sources = []
        for module in modules:
            if module.name != hit["module"]:
                continue
            masks = omf_relocation_masks(library, module)
            for symbol, segment, offset, code, record_start, data_offset in public_code_prefixes(
                    library, module, max_size=512):
                if symbol != hit["symbol"]:
                    continue
                mask = masks.get(record_start)
                code_mask = (mask or b"")[data_offset:data_offset + len(code)]
                sources.append(choose_relocated_prefix(original, code, code_mask))
        result = {"rtl_offset": hit["rtl_offset"], "module": hit["module"],
                  "symbol": hit["symbol"]}
        if len(sources) != 1:
            result["status"] = "source_public_unavailable_or_ambiguous"
            result["source_public_count"] = len(sources)
        else:
            chosen = sources[0]
            result.update({key: value for key, value in chosen.items()
                           if key != "rtl_offset"})
            if chosen["status"] == "unique":
                selected = f'0x{chosen["rtl_offset"]:05x}'
                if selected != hit["rtl_offset"]:
                    result["status"] = "unique_match_at_other_rtl_offset"
                    result["selected_rtl_offset"] = selected
                else:
                    result["status"] = "overlapping_public_at_same_rtl_offset"
        explanations.append(result)
    return explanations


def resolve_library(compiler_root: Path) -> Path:
    for candidate in (compiler_root / "LIB", compiler_root / "lib", compiler_root):
        if candidate.is_dir():
            for file in candidate.iterdir():
                if file.name.upper() == "LLIBCR.LIB":
                    return file
    if compiler_root.is_dir() and any(
        entry.is_file() and entry.suffix.lower() in (".img", ".ima", ".dsk", ".vfd")
        for entry in compiler_root.rglob("*")
    ):
        raise ValueError(
            f"{compiler_root} contains floppy images, not an installed compiler. "
            "Use 'nix run .#msc51-unpack -- IMAGE-DIR OUTPUT-DIR' or "
            "the default pinned flake compiler."
        )
    raise ValueError(
        f"No LLIBCR.LIB found in {compiler_root}; expected an installed compiler "
        "with LIB/LLIBCR.LIB (not disk images)"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, default=Path("../gateway-dos/GATE.RTL"))
    parser.add_argument("--library", type=Path,
                        help="Microsoft C 5.1 LLIBCR.LIB (or another version for comparison)")
    parser.add_argument("--compiler", type=Path,
                        default=os.environ.get("GATEWAY_MSC51_DIR"),
                        help="compiler installation, used to find LLIBCR.LIB")
    parser.add_argument("--json", type=Path, help="optional JSON report path")
    parser.add_argument("--omf-details", action="store_true",
                        help="identify OMF object modules/LEDATA/PUBDEF behind direct matches")
    parser.add_argument("--scan-public-prefixes", action="store_true",
                        help="find unique 16-byte exact CRT public prefixes in GATE.RTL (candidates only)")
    parser.add_argument("--scan-relocations", action="store_true",
                        help="scan OMF public prefixes with FIXUPP relocations masked, "
                             "up to 512 bytes per LEDATA record (candidates only)")
    args = parser.parse_args(argv)
    if args.library is None and args.compiler is None:
        parser.error("provide --library FILE or GATEWAY_MSC51_DIR/--compiler DIR")
    try:
        library = args.library if args.library is not None else resolve_library(args.compiler)
        original = args.original.read_bytes()
        library_bytes = library.read_bytes()
        matches = compare_bytes(original, library_bytes)
        if args.omf_details or args.scan_public_prefixes or args.scan_relocations:
            modules = parse_omf_library(library_bytes)
        if args.omf_details:
            for entry in matches:
                entry["omf_matches"] = [omf_match_details(
                    library_bytes, modules, int(position, 16), entry["bytes"])
                    for position in entry["library_offsets"]]
        public_candidates = (
            scan_exact_crt_prefixes(original, library_bytes, modules,
                                    relocations=args.scan_relocations)
            if args.scan_public_prefixes or args.scan_relocations else None)
        exact_baseline = (scan_exact_crt_prefixes(original, library_bytes, modules)
                          if args.scan_relocations else None)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    for entry in matches:
        found = ", ".join(entry["library_offsets"]) or "no direct byte match"
        print(f'{entry["routine"]} GATE.RTL:{entry["rtl_offset"]} '
              f'({entry["fixed_bytes"]}/{entry["bytes"]} fixed bytes): {found}')
        for details in entry.get("omf_matches", ()):
            if details is not None:
                symbol = f' symbol={details["public_symbol"]}' if "public_symbol" in details else ""
                segment_class = (f' class={details["segment_class"]}'
                                 if "segment_class" in details else "")
                print(f'  module={details["module"]} at {details["module_offset"]}, '
                      f'segment #{details["segment_index"]}:{details["segment_offset"]}'
                      f'{segment_class}{symbol}')
            else:
                print("  hit is not entirely within a single OMF LEDATA record")
    if public_candidates is not None:
        summary = summarize_public_prefixes(public_candidates)
        counts = {kind: sum(hit["segment_kind"] == kind for hit in public_candidates)
                  for kind in ("code", "other", "unknown")}
        mode = "relocation-aware" if args.scan_relocations else "exact"
        print(f"Unique {mode} CRT public-prefix candidates (at least 16 bytes): "
              f'{len(public_candidates)} (CODE class: {counts["code"]}, '
              f'other classes: {counts["other"]}, unknown: {counts["unknown"]})')
        for entry in public_candidates:
            length = (f'{entry["relocation_aware_bytes"]} compared bytes, '
                      f'{entry["masked_relocation_bytes"]} relocation bytes masked, '
                      f'{entry["exact_prefix_bytes"]} exact leading bytes, '
                      f'{entry["opening_bytes"]}-byte opening'
                      if args.scan_relocations else
                      f'{entry["exact_prefix_bytes"]} exact leading bytes')
            print(f'  GATE.RTL:{entry["rtl_offset"]} {entry["module"]}:{entry["symbol"]} '
                  f'#{entry["segment_index"]}:{entry["segment_offset"]} '
                  f'class={entry["segment_class"] or "?"} '
                  f'({length})')
        if "code_class_entry_span" in summary:
            first, last = summary["code_class_entry_span"]
            print(f"CODE-class entry span: GATE.RTL:{first}..{last} "
                  "(entry points only; gaps unverified)")
        print("OMF class CODE may contain data (e.g. string literals), "
              "not necessarily callable functions.")
        print("Candidates need separate code/boundary validation; absent matches prove nothing.")
        if args.scan_relocations:
            print("FIXUPP masking is restricted to each LEDATA/public boundary; "
                  "malformed or unsupported fixups are excluded.")
            differences = compare_public_scans(exact_baseline, public_candidates)
            explanations = explain_exact_only(
                original, library_bytes, modules, differences["exact_only"])
            print(f'Exact-prefix baseline: {differences["exact_candidates"]} candidates; '
                  f'{len(differences["exact_only"])} exact-only, '
                  f'{len(differences["relocation_only"])} relocation-only.')
            for name, label in (("exact_only", "Exact-only"),
                                ("relocation_only", "Relocation-only")):
                if differences[name]:
                    print(f'{label} entries: ' + ", ".join(
                        f'{hit["module"]}:{hit["symbol"]}@{hit["rtl_offset"]}'
                        for hit in differences[name]))
            for item in explanations:
                print(f'  Exact-only reason {item["module"]}:{item["symbol"]} '
                      f'at {item["rtl_offset"]}: {item["status"]}'
                      + (f' ({item["fixed_opening_bytes"]} fixed opening bytes)'
                         if "fixed_opening_bytes" in item else "")
                      + (f' ({item["masked_opening_matches"]} possible openings, '
                         f'{item["best_fixed_bytes"]} best fixed bytes)'
                         if "best_fixed_bytes" in item else ""))
    print(f"Compared against {library} ({len(library_bytes)} bytes)")
    print("A match supports identical CRT code; a miss is inconclusive for OMF modules.")
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        report = {
            "original_sha256": GATE_RTL_SHA256,
            "library_sha256": hashlib.sha256(library_bytes).hexdigest(),
            "routines": matches,
        }
        if public_candidates is not None:
            report["public_prefix_candidates"] = public_candidates
            report["public_prefix_summary"] = summary
            report["public_prefix_scan_mode"] = (
                "omf-fixupp" if args.scan_relocations else "exact")
            if exact_baseline is not None:
                report["public_prefix_exact_baseline"] = exact_baseline
                report["public_prefix_scan_comparison"] = differences
                report["public_prefix_exact_only_explanations"] = explanations
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Saved {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

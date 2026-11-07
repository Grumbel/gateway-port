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
                if names:
                    detail["public_symbol"] = names[0]
                return detail
    return None


def public_code_prefixes(data: bytes, module: OMFModule,
                         min_size: int = 16, max_size: int = 64):
    """Yield contiguous LEDATA prefixes at PUBDEF entries, with no relocation masking.

    Do not run across another public entry or an OMF record boundary. Exact
    prefixes avoid guessing which bytes a linker might have relocated.
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
                yield symbol, segment, offset, data[start:end]
            break


def scan_exact_crt_prefixes(original: bytes, library: bytes,
                            modules: list[OMFModule], min_size: int = 16) -> list[dict]:
    """Find unambiguous exact first bytes of OMF publics in original RTL.

    This is an exploratory *candidate* list: exact bytes alone cannot prove a
    Gateway file location is code, or that a PUBDEF is a function rather than
    a variable. Relocated prefixes simply do not match (false negatives).
    """
    hits = []
    for module in modules:
        for symbol, segment, offset, code in public_code_prefixes(
                library, module, min_size=min_size):
            prefix = code[:min_size]
            rtl_offset = original.find(prefix)
            if rtl_offset < 0 or original.find(prefix, rtl_offset + 1) >= 0:
                continue
            matched = min_size
            while (matched < len(code) and rtl_offset + matched < len(original)
                   and original[rtl_offset + matched] == code[matched]):
                matched += 1
            hits.append({
                "symbol": symbol,
                "module": module.name,
                "rtl_offset": f"0x{rtl_offset:05x}",
                "segment_index": segment,
                "segment_offset": f"0x{offset:04x}",
                "exact_prefix_bytes": matched,
            })
    # Two library publics with the same opening instructions do not justify
    # choosing either name, even if those bytes occur only once in GATE.RTL.
    counts = {}
    for hit in hits:
        counts[hit["rtl_offset"]] = counts.get(hit["rtl_offset"], 0) + 1
    return sorted((hit for hit in hits if counts[hit["rtl_offset"]] == 1),
                  key=lambda hit: (int(hit["rtl_offset"], 16), hit["symbol"]))


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
    args = parser.parse_args(argv)
    if args.library is None and args.compiler is None:
        parser.error("provide --library FILE or GATEWAY_MSC51_DIR/--compiler DIR")
    try:
        library = args.library if args.library is not None else resolve_library(args.compiler)
        original = args.original.read_bytes()
        library_bytes = library.read_bytes()
        matches = compare_bytes(original, library_bytes)
        if args.omf_details or args.scan_public_prefixes:
            modules = parse_omf_library(library_bytes)
        if args.omf_details:
            for entry in matches:
                entry["omf_matches"] = [omf_match_details(
                    library_bytes, modules, int(position, 16), entry["bytes"])
                    for position in entry["library_offsets"]]
        public_candidates = (scan_exact_crt_prefixes(original, library_bytes, modules)
                             if args.scan_public_prefixes else None)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    for entry in matches:
        found = ", ".join(entry["library_offsets"]) or "no direct byte match"
        print(f'{entry["routine"]} GATE.RTL:{entry["rtl_offset"]} '
              f'({entry["fixed_bytes"]}/{entry["bytes"]} fixed bytes): {found}')
        for details in entry.get("omf_matches", ()):
            if details is not None:
                symbol = f' symbol={details["public_symbol"]}' if "public_symbol" in details else ""
                print(f'  module={details["module"]} at {details["module_offset"]}, '
                      f'segment #{details["segment_index"]}:{details["segment_offset"]}{symbol}')
            else:
                print("  hit is not entirely within a single OMF LEDATA record")
    if public_candidates is not None:
        print(f"Unique exact CRT public-prefix candidates (at least 16 bytes): {len(public_candidates)}")
        for entry in public_candidates:
            print(f'  GATE.RTL:{entry["rtl_offset"]} {entry["module"]}:{entry["symbol"]} '
                  f'#{entry["segment_index"]}:{entry["segment_offset"]} '
                  f'({entry["exact_prefix_bytes"]} exact leading bytes)')
        print("Candidates need separate code/boundary validation; absent matches prove nothing.")
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
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Saved {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Audit actual Microsoft C 5.1 OMF PUBDEF exports against LINK.L2029.

No compiler, DOS emulator, proprietary game data or OMF libraries are needed.
The input objects and LINK.LOG must come from the same staged DOS build.
Source declarations alone cannot prove a linker-visible symbol exists.
"""

import argparse
from collections import defaultdict
import difflib
import json
from pathlib import Path
import re


OBJECT_NAME = re.compile(r"\b([A-Z][A-Z0-9_]*)\.OBJ\b", re.IGNORECASE)
LINK_UNRESOLVED = re.compile(r"^\s*([_A-Za-z][_A-Za-z0-9@$?]*) in file\(s\):\s*$", re.MULTILINE)


def omf_index(data: bytes, pos: int, end: int) -> tuple[int, int]:
    if pos >= end:
        raise ValueError("truncated OMF index")
    value = data[pos]
    if value < 0x80:
        return value, pos + 1
    if pos + 1 >= end:
        raise ValueError("truncated extended OMF index")
    return ((value & 0x7f) << 8) | data[pos + 1], pos + 2


def public_names(data: bytes) -> set[str]:
    """Parse the THEADR..MODEND records, not arbitrary PUBDEF-like bytes.

    The checksum may be zero (OMF's unchecked-record sentinel); nonzero
    checksums are validated. A truncated/malformed record rejects the object
    instead of silently undercounting its exported symbols.
    """
    found = set()
    pos = 0
    modules = 0
    inside = False
    while pos < len(data):
        if data[pos] == 0 and not any(data[pos:]):  # optional final padding
            break
        if pos + 3 > len(data):
            raise ValueError(f"truncated OMF record header at 0x{pos:x}")
        kind = data[pos]
        length = int.from_bytes(data[pos + 1:pos + 3], "little")
        end = pos + 3 + length
        if length < 1 or end > len(data):
            raise ValueError(f"truncated OMF record at 0x{pos:x}")
        if data[end - 1] != 0 and sum(data[pos:end]) & 0xff:
            raise ValueError(f"incorrect OMF checksum at 0x{pos:x}")
        if not inside:
            if kind not in (0x80, 0x82):
                raise ValueError(f"OMF module must start with THEADR at 0x{pos:x}")
            modules += 1
            inside = True
        elif kind in (0x80, 0x82):
            raise ValueError(f"nested OMF module at 0x{pos:x}")
        if kind in (0x90, 0x91):
            cursor, payload_end = pos + 3, end - 1
            _, cursor = omf_index(data, cursor, payload_end)
            segment, cursor = omf_index(data, cursor, payload_end)
            if segment == 0:
                if cursor + 2 > payload_end:
                    raise ValueError("truncated absolute OMF public frame")
                cursor += 2
            while cursor < payload_end:
                size = data[cursor]
                cursor += 1
                if size == 0 or cursor + size > payload_end:
                    raise ValueError("truncated OMF PUBDEF name")
                name = data[cursor:cursor + size].decode("ascii")
                cursor += size
                width = 4 if kind == 0x91 else 2
                if cursor + width > payload_end:
                    raise ValueError("truncated OMF PUBDEF address")
                cursor += width
                _, cursor = omf_index(data, cursor, payload_end)
                found.add(name)
        if kind in (0x8a, 0x8b):
            inside = False
        pos = end
    if inside or modules == 0:
        raise ValueError("missing OMF module terminator")
    return found


def linked_objects(response: str) -> list[str]:
    names = []
    for match in OBJECT_NAME.finditer(response):
        name = match.group(1).upper() + ".OBJ"
        if name not in names:
            names.append(name)
    return names


def audit_stage(stage: Path, symbols: list[str], link_log: str = "",
                inspect_objects: list[str] | None = None) -> dict:
    response = (stage / "LINK.RSP").read_text(errors="replace")
    inputs = linked_objects(response)
    providers: dict[str, list[str]] = defaultdict(list)
    object_publics = {}
    missing = []
    malformed = {}
    for name in inputs:
        path = stage / name
        if not path.is_file():
            missing.append(name)
            continue
        try:
            publics = public_names(path.read_bytes())
        except (ValueError, UnicodeDecodeError) as error:
            malformed[name] = str(error)
            continue
        object_publics[name] = sorted(publics)
        for name_export in publics:
            providers[name_export].append(name)
    unresolved = list(dict.fromkeys(LINK_UNRESOLVED.findall(link_log)))
    requested = list(dict.fromkeys([*symbols, *unresolved]))
    entries = []
    for symbol in requested:
        matches = sorted(providers.get(symbol, []))
        close = []
        if not matches:
            # A mismatched compiler name, including a truncation of a long
            # C identifier or differing case, is more informative than a
            # bare "not exported" result. Always preserve the actual case.
            candidate_names = list(providers)
            ranked = difflib.get_close_matches(symbol.lower(),
                         [name.lower() for name in candidate_names], n=8, cutoff=0.72)
            close = sorted((name for name in candidate_names
                            if name.lower() in ranked),
                           key=lambda name: (-difflib.SequenceMatcher(
                               None, symbol.lower(), name.lower()).ratio(), name))[:5]
        entries.append({
            "symbol": symbol,
            "reported_unresolved": symbol in unresolved,
            "providers": matches,
            "similar_exports": [{"name": name, "providers": sorted(providers[name])}
                                for name in close],
            "status": ("reported_unresolved_but_exported" if matches and symbol in unresolved
                       else "exported" if matches else "no_export_in_staged_objects"),
        })
    requested_objects = [name.upper() for name in (inspect_objects or [])]
    unknown_objects = sorted(set(requested_objects) - set(inputs))
    return {"objects_in_response": len(inputs),
            "objects_read": len(inputs) - len(missing) - len(malformed),
            "missing_objects": missing, "malformed_objects": malformed,
            "symbols": entries,
            "object_exports": {name: object_publics[name]
                               for name in requested_objects if name in object_publics},
            "unknown_objects": unknown_objects}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, default=Path("build/msc51-launcher"))
    parser.add_argument("--symbol", action="append", default=[],
                        help="exact OMF name, including the C leading underscore")
    parser.add_argument("--object", action="append", default=[],
                        help="show every PUBDEF export from this LINK.RSP object (repeatable)")
    parser.add_argument("--log", type=Path, help="LINK.LOG to cross-check (default: stage/LINK.LOG if present)")
    parser.add_argument("--json", type=Path, help="optional report output")
    args = parser.parse_args(argv)
    if not (args.stage / "LINK.RSP").is_file():
        parser.error(f"LINK.RSP not found in {args.stage}")
    log = args.log or args.stage / "LINK.LOG"
    if args.log and not log.is_file():
        parser.error(f"LINK.LOG not found: {log}")
    result = audit_stage(args.stage, args.symbol,
                         log.read_text(errors="replace") if log.is_file() else "",
                         inspect_objects=args.object)
    print(f"OMF: {result['objects_read']}/{result['objects_in_response']} "
          "linker input objects inspected")
    for entry in result["symbols"]:
        source = ", ".join(entry["providers"]) or "no PUBDEF found"
        print(f"  {entry['symbol']}: {entry['status']} ({source})")
        for candidate in entry["similar_exports"]:
            print(f"    similar: {candidate['name']} ({', '.join(candidate['providers'])})")
    for name, publics in result["object_exports"].items():
        print(f"  {name}: {len(publics)} PUBDEF exports")
        for symbol in publics:
            print(f"    {symbol}")
    for name in result["unknown_objects"]:
        print(f"  not in LINK.RSP: {name}")
    for name in result["missing_objects"]:
        print(f"  missing object: {name}")
    for name, error in result["malformed_objects"].items():
        print(f"  malformed object: {name}: {error}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + "\n")
    return 1 if (result["missing_objects"] or result["malformed_objects"]
                 or result["unknown_objects"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())

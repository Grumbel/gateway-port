#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Inventory original Microsoft LINK 3.65 unresolved symbols without relinking.

LINK.LOG comes from the user's private DOSBox run, not from checked-in game
assets. The generated mapping files restore the reconstructed C identifiers;
the historical symbol table distinguishes file offsets from observed FAR
addresses in one particular runtime layout.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


REPO = Path(__file__).resolve().parent.parent
EXTERNAL = re.compile(r"^([_A-Za-z][_A-Za-z0-9@$?]*) in file\(s\):$")
REFERENCE = re.compile(r"\S+\.(?:OBJ|LIB)\([^\s)]+\)", re.IGNORECASE)
ERROR_COUNT = re.compile(r"There were (\d+) errors detected", re.IGNORECASE)


def parse_link_log(content: str) -> tuple[list[dict], int | None]:
    """Read the multi-line L2029 symbol/reference list from Microsoft LINK."""
    found = {}
    current = None
    for line in content.splitlines():
        match = EXTERNAL.fullmatch(line.strip())
        if match:
            name = match.group(1)
            current = found.setdefault(name, {"linker_symbol": name, "references": []})
        elif current is not None:
            for reference in REFERENCE.findall(line):
                if reference not in current["references"]:
                    current["references"].append(reference)
    errors = ERROR_COUNT.search(content)
    return list(found.values()), int(errors.group(1)) if errors else None


def read_mapping(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    result = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split(None, 1)
        if len(fields) == 2:
            result[fields[0]] = fields[1].strip()
    return result


def read_provenance(path: Path) -> dict[str, dict]:
    """Attach only provenanced original offsets or explicitly layout-bound FARs."""
    if not path.is_file():
        return {}
    result = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 2:
            continue
        identifier, name = fields
        if match := re.fullmatch(r"gate_(exe|rtl)_([0-9a-f]+)", identifier):
            result[name] = {"kind": "original_file_offset", "address":
                            f"{match.group(1)}:0x{match.group(2)}", "identifier": identifier}
        elif match := re.fullmatch(r"gate_far_([0-9a-f]{4})_([0-9a-f]{4})", identifier):
            result[name] = {"kind": "observed_linked_far", "address":
                            f"{match.group(1)}:{match.group(2)}", "identifier": identifier}
    return result


def category(name: str) -> str:
    if name == "main":
        return "crt_entry"
    if name.startswith(("gate_ds_", "gate_far_")) or name == "gate_stack_pointer":
        return "guest_memory"
    if name.startswith("gate_dos_file_"):
        return "dos_file_services"
    if name.startswith("gate_linked_"):
        return "linked_original_services"
    if name.startswith("gate_rtl_"):
        return "rtl_state_and_hardware"
    return "other"


def build_report(log: str, stage: Path, headers: Path,
                 historical_names: Path) -> dict:
    aliases = read_mapping(stage / "SYMBOL.MAP")
    sources = read_mapping(stage / "SOURCE.MAP")
    locations = read_provenance(historical_names)
    declarations = {path.name: path.read_text() for path in sorted(headers.glob("*.h"))}
    externals, errors = parse_link_log(log)
    for entry in externals:
        # One leading underscore is the Microsoft C 5.1 decoration; do not
        # mistakenly strip an underscore that belongs to the original name.
        compiled = entry["linker_symbol"][1:] if entry["linker_symbol"].startswith("_") else entry["linker_symbol"]
        original = aliases.get(compiled, compiled)
        entry["name"] = original
        entry["category"] = category(original)
        if original in locations:
            entry["provenance"] = locations[original]
        entry["declared_in"] = [f"decomp/launcher/{name}" for name, content in declarations.items()
                                if re.search(r"\b" + re.escape(original) + r"\s*\(", content)]
        # Some unresolved RTL accessors have no code-file-offset identity:
        # their declaration instead documents a DS-relative data location.
        # Keep that separate from the original image offsets above.
        ds_offsets = set()
        for content in declarations.values():
            pattern = (r"(?m)^[^/\n]*\b" + re.escape(original) +
                       r"\s*\([^\n;]*\)[^\n;]*;[^\n]*/\*\s*(?:RTL\s+)?DS:0x([0-9a-fA-F]+)")
            for match in re.finditer(pattern, content):
                ds_offsets.add(f"DS:0x{match.group(1).lower()}")
        if ds_offsets:
            entry["rtl_ds_offsets"] = sorted(ds_offsets)
        # Resolve exactly the DOS 8.3 translation-unit aliases, preserving
        # unrelated compiler library references in `references` verbatim.
        referenced = set()
        for reference in entry["references"]:
            match = re.match(r"^(M\d\d)\.OBJ\(", reference, re.IGNORECASE)
            if match:
                source = sources.get(match.group(1).upper() + ".C")
                if source:
                    referenced.add(source)
        entry["referenced_from"] = sorted(referenced)
    externals.sort(key=lambda item: (item["category"], item["name"]))
    return {
        "reported_link_errors": errors,
        "unresolved_symbols": len(externals),
        "categories": dict(sorted(Counter(item["category"] for item in externals).items())),
        "externals": externals,
    }


def display(report: dict, details: bool) -> None:
    errors = report["reported_link_errors"]
    print(f"Microsoft LINK: {report['unresolved_symbols']} unresolved symbols"
          + (f" ({errors} reported errors)" if errors is not None else ""))
    for name, count in report["categories"].items():
        print(f"  {name:27} {count:3d}")
    if details:
        for item in report["externals"]:
            source = ", ".join(item["referenced_from"]) or ", ".join(item["references"])
            provenance = item.get("provenance")
            address = f" [{provenance['kind']}: {provenance['address']}]" if provenance else ""
            if item.get("rtl_ds_offsets"):
                address += f" [rtl_ds_relative: {', '.join(item['rtl_ds_offsets'])}]"
            compiled = item["linker_symbol"][1:] if item["linker_symbol"].startswith("_") else item["linker_symbol"]
            alias = f" (LINK: {item['linker_symbol']})" if compiled != item["name"] else ""
            print(f"  {item['name']}{alias}{address}\n    <- {source}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=Path("build/msc51-launcher/LINK.LOG"))
    parser.add_argument("--json", type=Path, help="optional structured report output")
    parser.add_argument("--details", action="store_true", help="list every external and its address provenance")
    args = parser.parse_args(argv)
    if not args.log.is_file():
        parser.error(f"Microsoft LINK log not found: {args.log}")
    content = args.log.read_text(errors="replace")
    report = build_report(content, args.log.parent, REPO / "decomp/launcher",
                          REPO / "decomp/launcher/symbol-names.tsv")
    if not report["externals"]:
        parser.error("No unresolved-symbol blocks found; inspect LINK.LOG directly")
    display(report, args.details)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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


def resolve_library(compiler_root: Path) -> Path:
    for candidate in (compiler_root / "LIB", compiler_root / "lib", compiler_root):
        if candidate.is_dir():
            for file in candidate.iterdir():
                if file.name.upper() == "LLIBCR.LIB":
                    return file
    raise ValueError(f"No LLIBCR.LIB found in {compiler_root}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, default=Path("../gateway-dos/GATE.RTL"))
    parser.add_argument("--library", type=Path,
                        help="Microsoft C 5.1 LLIBCR.LIB (or another version for comparison)")
    parser.add_argument("--compiler", type=Path,
                        default=os.environ.get("GATEWAY_MSC51_DIR"),
                        help="compiler installation, used to find LLIBCR.LIB")
    parser.add_argument("--json", type=Path, help="optional JSON report path")
    args = parser.parse_args(argv)
    if args.library is None and args.compiler is None:
        parser.error("provide --library FILE or GATEWAY_MSC51_DIR/--compiler DIR")
    try:
        library = args.library if args.library is not None else resolve_library(args.compiler)
        original = args.original.read_bytes()
        library_bytes = library.read_bytes()
        matches = compare_bytes(original, library_bytes)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    for entry in matches:
        found = ", ".join(entry["library_offsets"]) or "no direct byte match"
        print(f'{entry["routine"]} GATE.RTL:{entry["rtl_offset"]} '
              f'({entry["fixed_bytes"]}/{entry["bytes"]} fixed bytes): {found}')
    print(f"Compared against {library} ({len(library_bytes)} bytes)")
    print("A match supports identical CRT code; a miss is inconclusive for OMF modules.")
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            "original_sha256": GATE_RTL_SHA256,
            "library_sha256": hashlib.sha256(library_bytes).hexdigest(),
            "routines": matches,
        }, indent=2) + "\n")
        print(f"Saved {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

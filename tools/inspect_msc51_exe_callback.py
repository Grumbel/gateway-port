#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Identify the overwritten original-EXE callback in early DOS snapshots.

The first resident RTL bank calls the live address 1000:0030. At initial MZ
load this address is *data*, but the original loader copies code from
GATE.EXE:0bca here before the resident callback is called. Only the bounded
0bca..0c52 prefix is considered; the rest of this function is not recovered.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


SOURCE_OFFSET = 0x0BCA
PREFIX_SIZE = 0x89  # Until the branch target at GATE.EXE:0c53.
LIVE_SEGMENT = 0x1000  # Observed DOSBox load; not a portable linker address.
LIVE_OFFSET = 0x0030
# Instruction starts, not raw byte differences. All 11 are CALL FAR imm16:imm16.
FAR_CALLS = (0x06, 0x10, 0x21, 0x29, 0x2E, 0x4B,
             0x5C, 0x68, 0x71, 0x79, 0x81)


@dataclass(frozen=True)
class CallbackMatch:
    invariant_equal: int
    invariant_total: int
    relocated_calls: int
    original_calls: int
    other_calls: int

    @property
    def settled(self) -> bool:
        return (self.invariant_equal == self.invariant_total
                and self.relocated_calls == len(FAR_CALLS))


def extract_source(image: bytes) -> bytes:
    if image[:2] != b"MZ" or len(image) < SOURCE_OFFSET + PREFIX_SIZE:
        raise ValueError("not a full original MZ image")
    source = image[SOURCE_OFFSET:SOURCE_OFFSET + PREFIX_SIZE]
    if source[:3] != b"\x55\x8b\xec":
        raise ValueError("original callback entry prologue does not match")
    for start in FAR_CALLS:
        if source[start] != 0x9A:
            raise ValueError(f"missing CALL FAR at file {SOURCE_OFFSET + start:04x}")
    return source


def compare_callback(source: bytes, live: bytes, segment_delta: int) -> CallbackMatch:
    if len(source) != PREFIX_SIZE or len(live) != PREFIX_SIZE:
        raise ValueError("incomplete callback prefix")
    mutable = set()
    relocated = original = other = 0
    for start in FAR_CALLS:
        if source[start] != 0x9A:
            raise ValueError(f"invalid source FAR call at +{start:02x}")
        mutable.update((start + 3, start + 4))
        before = int.from_bytes(source[start + 3:start + 5], "little")
        after = int.from_bytes(live[start + 3:start + 5], "little")
        if after == ((before + segment_delta) & 0xFFFF):
            relocated += 1
        elif after == before:
            original += 1
        else:
            other += 1
    invariant = sum(source[index] == byte for index, byte in enumerate(live)
                    if index not in mutable)
    return CallbackMatch(invariant, PREFIX_SIZE - len(mutable),
                         relocated, original, other)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--snapshot-dir", required=True, type=Path)
    parser.add_argument("--times", default="0,10,100,500,1500")
    args = parser.parse_args()
    try:
        source = extract_source(args.exe.read_bytes())
        for value in args.times.split(","):
            millis = int(value)
            memory = (args.snapshot_dir / f"at-{millis}.bin").read_bytes()
            physical = (LIVE_SEGMENT << 4) + LIVE_OFFSET
            if len(memory) < physical + PREFIX_SIZE:
                raise ValueError(f"{millis}ms snapshot is incomplete")
            match = compare_callback(source,
                                     memory[physical:physical + PREFIX_SIZE],
                                     LIVE_SEGMENT)
            print(f"{millis:5d}ms original EXE:{SOURCE_OFFSET:04x} -> "
                  f"{LIVE_SEGMENT:04x}:{LIVE_OFFSET:04x}: "
                  f"invariant={match.invariant_equal}/{match.invariant_total}, "
                  f"relocated={match.relocated_calls}/{len(FAR_CALLS)}, "
                  f"unrelocated={match.original_calls}, "
                  f"other={match.other_calls}, settled={match.settled}")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

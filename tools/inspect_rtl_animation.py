#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Resolve Your Quarters' four linked graphics calls to original RTL code.

Each OVL call operand is checked against the original source, then its guest
entry bytes are matched to GATE.EXE/GATE.RTL. No original executable data or
guest RAM is embedded in, or written by, this read-only inspector.
"""

import argparse
import json
from pathlib import Path
import struct

from rank_engine_services import resolve_linked_origins


# Stable file offsets of the far CALL opcodes in GATEMAIN.OVL, section 38.
# Descriptions summarize separately inspected 8086 code in GATE.RTL.
ROOM_CALLS = (
    ('create', 0x51538, 0x1485, 0x1788,
     'allocate a sequence and return its slot handle'),
    ('initialize', 0x51543, 0x1485, 0x18da,
     'initialize active sequence frame indices and due times'),
    ('clear', 0x5155a, 0x1485, 0x1928,
     'clear existing sequence drawings and reset timing'),
    ('release_all', 0x5155f, 0x1485, 0x1bac,
     'release all active sequence buffers and zero their count'),
)


def inspect_animation_calls(ovl, exe, rtl, ram, load_segment=0x1000):
    """Return linked-to-file matches, rejecting changed or unresolved calls."""
    if not 0 <= load_segment <= 0xffff:
        raise ValueError('load segment must be a 16-bit unsigned value')
    targets = set()
    for name, source_offset, segment, offset, _note in ROOM_CALLS:
        if source_offset + 5 > len(ovl):
            raise ValueError(f'{name}: original OVL CALL site is out of bounds')
        opcode, target_offset, target_segment = struct.unpack_from(
            '<BHH', ovl, source_offset)
        if (opcode, target_segment, target_offset) != (0x9a, segment, offset):
            raise ValueError(f'{name}: original OVL linked far CALL changed')
        targets.add((segment, offset))
    mappings = resolve_linked_origins(targets, ram, exe, rtl, load_segment)
    results = []
    for name, source_offset, segment, offset, note in ROOM_CALLS:
        mapped = mappings.get((segment, offset))
        if mapped is None:
            raise ValueError(f'{name}: could not uniquely match the guest entry')
        if mapped['file'] != 'rtl':
            raise ValueError(f'{name}: matched {mapped["file"]}, not GATE.RTL')
        results.append(dict(name=name, caller_file='ovl',
                            caller_offset=source_offset,
                            linked_segment=segment, linked_offset=offset,
                            original_file=mapped['file'],
                            original_offset=mapped['offset'],
                            matched_bytes=mapped['matched_bytes'],
                            segment_support=mapped['segment_support'],
                            purpose=note))
    # The four services are parts of one resident RTL segment. This check
    # catches false matches in a different copy of the same byte signature.
    origins = {item['original_offset'] - item['linked_offset']
               for item in results}
    if len(origins) != 1:
        raise ValueError('far calls did not resolve to one consistent RTL origin')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ovl', type=Path, required=True,
                        help='original GATEMAIN.OVL')
    parser.add_argument('--exe', type=Path, required=True,
                        help='original GATE.EXE')
    parser.add_argument('--rtl', type=Path, required=True,
                        help='original GATE.RTL')
    parser.add_argument('--ram', type=Path, required=True,
                        help='guest RAM dump containing the loaded resident RTL code')
    parser.add_argument('--load-segment', type=lambda value: int(value, 0),
                        default=0x1000)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        rows = inspect_animation_calls(args.ovl.read_bytes(), args.exe.read_bytes(),
                                       args.rtl.read_bytes(), args.ram.read_bytes(),
                                       args.load_segment)
    except ValueError as exc:
        parser.error(str(exc))
    if args.json:
        print(json.dumps({'services': rows}, indent=2))
    else:
        for row in rows:
            print(f'{row["name"]:<12} ovl:0x{row["caller_offset"]:06x} '
                  f'{row["linked_segment"]:04x}:{row["linked_offset"]:04x} '
                  f'=> {row["original_file"]}:0x{row["original_offset"]:05x} '
                  f'({row["matched_bytes"]}-byte match, '
                  f'{row["segment_support"]} supporting entries)')
            print(f'  {row["purpose"]}')
        print('These are original-source identities, not a verified host port.')


if __name__ == '__main__':
    main()

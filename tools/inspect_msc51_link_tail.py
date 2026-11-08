#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Match the final Gateway launcher service entries against live DOS code.

The segment values are original *load-relative* linker operands, not fixed
physical addresses. The snapshot must contain the relocated GATE.EXE startup
image; a later capture may have overwritten one or more service banks.
"""

import argparse
import json
from pathlib import Path
import sys

from rank_engine_services import resolve_linked_origins


TAIL_ENTRIES = (
    ('music_init', 0x0a46, 0x01ec, 'rtl', 0xc3bd),
    ('control_key', 0x123f, 0x0004, None, None),
    ('intro_diagnostic', 0x1258, 0x172e, None, None),
)


def inspect(exe, rtl, ram, load_segment=0x1000):
    if len(ram) != (1 << 20):
        raise ValueError(f'expected a 1-MiB conventional RAM capture, got {len(ram)} bytes')
    if not exe.startswith(b'MZ'):
        raise ValueError('GATE.EXE must start with an MZ header')
    targets = {(segment, offset) for _, segment, offset, _, _ in TAIL_ENTRIES}
    matches = resolve_linked_origins(targets, ram, exe, rtl, load_segment)
    rows = []
    for name, segment, offset, source_file, source_offset in TAIL_ENTRIES:
        physical = ((load_segment + segment) << 4) + offset
        candidate = matches.get((segment, offset))
        row = {
            'name': name,
            'load_relative_target': {'segment': segment, 'offset': offset},
            'physical_entry': physical,
            'live_entry_hex': (ram[physical:physical + 32].hex()
                               if physical < len(ram) else None),
            'live_source_match': candidate,
        }
        if source_file is not None:
            image = {'rtl': rtl, 'exe': exe}[source_file]
            source = image[source_offset:source_offset + 16]
            row['independent_original_entry'] = {
                'file': source_file,
                'offset': source_offset,
                'matches_live_16_bytes': (physical + 16 <= len(ram) and
                                          ram[physical:physical + 16] == source),
            }
        if name == 'music_init':
            # Original GATE.RTL:c3c5/c3cf: the two direct DS operands are
            # relocated in the original 22-second entry capture from
            # 004a/0048 to linked DS:0662/0660. Comparing the original 16
            # bytes verbatim would incorrectly reject this genuine entry.
            row['relocated_music_guard'] = (
                rtl[0xc3c5:0xc3ca] == bytes.fromhex('f6064a0006') and
                rtl[0xc3cf:0xc3d4] == bytes.fromhex('833e480000') and
                ram[physical + 8:physical + 13] == bytes.fromhex('f606620606') and
                ram[physical + 18:physical + 23] == bytes.fromhex('833e600600'))
        rows.append(row)
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--rtl', required=True, type=Path)
    parser.add_argument('--ram', required=True, type=Path)
    parser.add_argument('--load-segment', type=lambda value: int(value, 0),
                        default=0x1000)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        if not args.ram.exists():
            raise ValueError(f'guest RAM snapshot not found: {args.ram}\n'
                             'Generate build/vfx-replay/checkpoint-book_taken.bin '
                             'using the original opening walkthrough and --capture-labels.')
        rows = inspect(args.exe.read_bytes(), args.rtl.read_bytes(),
                       args.ram.read_bytes(), args.load_segment)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.json:
        print(json.dumps({'services': rows}, indent=2))
    else:
        for row in rows:
            match = row['live_source_match']
            identity = (f"{match['file']}:{match['offset']:05x}"
                        if match is not None else 'unmapped/overwritten')
            print(f"{row['name']}: live {row['physical_entry']:05x} -> {identity}")
    return 0


if __name__ == '__main__':
    sys.exit(main())

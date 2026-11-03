#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Annotate a bounded 16-bit Gateway overlay disassembly with story text IDs.

The on-disk image mixes code, tables and relocation operands. This is a
research view, not a recursive disassembler or a C decompiler.
"""
import argparse
import pathlib
import re
import struct
import subprocess

from gateway_strings import GatewayStrings


INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+\s*(.*?)\s*$')
MOV_AX = re.compile(r'^mov\s+ax,0x([0-9a-f]+)$')
MOV_DX = re.compile(r'^mov\s+dx,0x([0-9a-f]+)$')
PUSH_DX = re.compile(r'^push\s+dx$')
PUSH_AX = re.compile(r'^push\s+ax$')
CALL_FAR = re.compile(r'^call\s+0x[0-9a-f]+:0x[0-9a-f]+$')


def text_id(ax, dx):
    """Return (segment, string) only for the validated f000:offset form."""
    if dx != 0xf000:
        return None
    return ax >> 10, ax & 0x3ff


def annotate(lines, strings=None):
    """Identify contiguous immediate-load/push/call text references.

    Do not attribute a token to a call when a branch, register write, or other
    instruction intervenes. Some matches can be non-text far-pointer calls;
    callers must confirm the call target against the known formatter.
    """
    instructions = []
    for index, line in enumerate(lines):
        match = INSTRUCTION.match(line)
        if match:
            instructions.append((index, match.group(2)))
    labels = {}
    for i in range(len(instructions) - 4):
        window = instructions[i:i + 5]
        ax = MOV_AX.fullmatch(window[0][1])
        dx = MOV_DX.fullmatch(window[1][1])
        if not (ax and dx and PUSH_DX.fullmatch(window[2][1])
                and PUSH_AX.fullmatch(window[3][1]) and CALL_FAR.fullmatch(window[4][1])):
            continue
        token = text_id(int(ax.group(1), 16), int(dx.group(1), 16))
        if token is not None:
            label = f'  ; possible story text segment={token[0]} string={token[1]}'
            if strings is not None:
                try:
                    value = strings.get(*token)
                except (IndexError, ValueError):
                    value = '<invalid text reference>'
                label += ' ' + repr(value[:160]) + ('...' if len(value) > 160 else '')
            labels[window[4][0]] = label
    return [line + labels.get(index, '') for index, line in enumerate(lines)]


def event_targets(data, section_base, table_offset, count):
    """Decode known near-offset jump tables; values are relative to section base."""
    if section_base < 0 or table_offset < 0 or count < 1 or count > 1024:
        raise ValueError('invalid event table parameters')
    if table_offset + count * 2 > len(data):
        raise ValueError('event table extends past file')
    return [(event + 1, section_base + struct.unpack_from('<H', data, table_offset + event * 2)[0])
            for event in range(count)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('overlay', type=pathlib.Path)
    parser.add_argument('--start', required=True, type=lambda s: int(s, 0), help='file offset')
    parser.add_argument('--size', type=lambda s: int(s, 0), default=0x100, help='byte count')
    parser.add_argument('--strings', type=pathlib.Path, help='GATESTR.DAT (Gateway only)')
    parser.add_argument('--section-base', type=lambda s: int(s, 0), help='file offset of section code')
    parser.add_argument('--event-table', type=lambda s: int(s, 0), help='file offset of near jump table')
    parser.add_argument('--events', type=int, help='number of event entries')
    args = parser.parse_args()
    if args.start < 0 or args.size <= 0 or args.start + args.size > args.overlay.stat().st_size:
        parser.error('requested range is outside the overlay')
    command = ['objdump', '-D', '-b', 'binary', '-m', 'i8086', '-M', 'intel',
               f'--start-address={args.start}', f'--stop-address={args.start + args.size}',
               str(args.overlay)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    strings = GatewayStrings(args.strings.read_bytes()) if args.strings else None
    print('\n'.join(annotate(result.stdout.splitlines(), strings)))
    if any(value is not None for value in (args.section_base, args.event_table, args.events)):
        if any(value is None for value in (args.section_base, args.event_table, args.events)):
            parser.error('--section-base, --event-table and --events must be used together')
        print('\nEvent jump-table entries (not executable instructions):')
        try:
            targets = event_targets(args.overlay.read_bytes(), args.section_base,
                                    args.event_table, args.events)
        except ValueError as error:
            parser.error(str(error))
        for event, target in targets:
            print(f'  event {event:2} -> file 0x{target:05x}')
    print('\nNote: linear sweep can decode embedded data as instructions; far-call targets'
          ' and relocation must be confirmed in a live overlay before translating to C.')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Resolve native kind-4 room-exit callback IDs to RTLink overlay handlers.

GATEMAIN.EXE's room scanner passes (exit.argument, 0x0f) to resident
1474:0002 (original EXE file 0x592a). That function accepts callback IDs 0..43, indexing
four-byte far pointers at [word DS:8da6]:4a22. The pointers lead to ten-byte
RTLink trampolines; only a separately verified replay proves a callback's
return value or any resulting movement.
"""
import argparse
import json
from pathlib import Path
import struct

from inspect_object_state import inspect_objects
from inspect_room_exits import inspect_exits
from rtlink_trampoline import decode_trampoline, map_reference, sections_by_id


CALLBACK_COUNT = 44
CALLBACK_SEGMENT_POINTER = 0x8da6
CALLBACK_TABLE_OFFSET = 0x4a22


def inspect_callbacks(memory, ds, world, manifest=None):
    """Read callback IDs referenced by supplied kind-4 room exits."""
    limit = min(len(memory), 0xa0000)

    def word(segment, offset):
        if not 0 <= offset <= 0xfffe or segment * 16 + offset + 2 > limit:
            raise ValueError('room callback pointer outside guest memory')
        return struct.unpack_from('<H', memory, segment * 16 + offset)[0]

    if not 0 < ds <= 0xffff:
        raise ValueError('invalid Gateway data segment')
    table_segment = word(ds, CALLBACK_SEGMENT_POINTER)
    if not table_segment:
        raise ValueError('room callback table pointer is null')
    uses = {}
    for room in world['rooms']:
        for route in room['exits']:
            if route['kind'] == 4:
                uses.setdefault(route['argument'], []).append(dict(
                    room=room['id'], room_name=room.get('name', ''),
                    direction_code=route['direction_code'], exit_index=route['index']))

    sections = sections_by_id(manifest) if manifest is not None else None
    callbacks = []
    for index in sorted(uses):
        item = dict(callback_id=index, uses=uses[index])
        if not 0 <= index < CALLBACK_COUNT:
            item['status'] = 'outside native dispatch table; returns AX=0'
        else:
            offset = CALLBACK_TABLE_OFFSET + 4 * index
            ip = word(table_segment, offset)
            cs = word(table_segment, offset + 2)
            item.update(pointer_segment=cs, pointer_offset=ip)
            reference = decode_trampoline(memory, cs, ip)
            if reference is None:
                item['status'] = 'not a verified RTLink trampoline'
            else:
                item['rtlink_reference'] = reference
                item['status'] = 'rtlink'
                if sections is not None:
                    item['native_code'] = map_reference(reference, sections)
        callbacks.append(item)
    return dict(schema_version=1, table_segment=table_segment,
                table_offset=CALLBACK_TABLE_OFFSET, table_count=CALLBACK_COUNT,
                exit_count=sum(len(item['uses']) for item in callbacks),
                callbacks=callbacks,
                note='Static handler mapping from one native snapshot; '
                     'not evidence every exit is reachable or returns a room.')


def format_text(report):
    output = [f"Native room callbacks at {report['table_segment']:04x}:"
              f"{report['table_offset']:04x} ({report['table_count']} entries)",
              f"{len(report['callbacks'])} distinct IDs used by "
              f"{report['exit_count']} kind-4 room-exit records"]
    for item in report['callbacks']:
        line = f"  0x{item['callback_id']:02x} ({len(item['uses'])} exits)"
        if 'pointer_segment' in item:
            line += f" trampoline={item['pointer_segment']:04x}:" \
                    f"{item['pointer_offset']:04x}"
        if 'rtlink_reference' in item:
            ref = item['rtlink_reference']
            line += f" section={ref['section']} target={ref['target_segment']:04x}:" \
                    f"{ref['target_offset']:04x}"
        if 'native_code' in item:
            code = item['native_code']
            line += f" file={code['filename']}:0x{code['file_offset']:x}"
        if item['status'] != 'rtlink':
            line += f" [{item['status']}]"
        output.append(line)
        for use in item['uses']:
            output.append(f"      0x{use['room']:03x} {use['room_name']}: "
                          f"direction=0x{use['direction_code']:02x} "
                          f"exit_index={use['exit_index']}")
    output.append(report['note'])
    return '\n'.join(output) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path, help='guest dump prefix, without .bin/.regs')
    parser.add_argument('--names', type=Path, help='optional original OBJECT.DAT')
    parser.add_argument('--overlay-map', type=Path,
                        help='gateway-overlay JSON for original GATEMAIN.OVL file offsets')
    parser.add_argument('--room', action='append', type=lambda value: int(value, 0),
                        help='restrict to room ID (repeatable); default all rooms')
    parser.add_argument('--format', choices=['text', 'json'], default='text')
    args = parser.parse_args()
    try:
        registers = dict(line.split('=', 1) for line in
                         args.snapshot.with_suffix('.regs').read_text().splitlines())
        memory = args.snapshot.with_suffix('.bin').read_bytes()
        names = None
        if args.names:
            raw = args.names.read_bytes()
            if len(raw) < 2 or struct.unpack_from('<H', raw)[0] != len(raw) - 2:
                raise ValueError('invalid OBJECT.DAT size header')
            names = raw[2:]
        ds = int(registers['DS'], 16)
        objects = inspect_objects(memory, ds, names)
        world = inspect_exits(memory, ds, objects, args.room)
        manifest = json.loads(args.overlay_map.read_text()) if args.overlay_map else None
        report = inspect_callbacks(memory, ds, world, manifest)
        print(json.dumps(report, indent=2) if args.format == 'json' else
              format_text(report), end='\n' if args.format == 'json' else '')
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

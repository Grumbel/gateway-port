#!/usr/bin/env python3
"""Read Gateway's native type-1 room exit records from a guest memory dump.

Original GATEMAIN.EXE file 0x60cf–0x6381 (runtime 148e:05f6) reads room
+0x16 (byte count) and +0x18 (one-based start), then scans 4-byte entries
at [word DS:8dcc]:417e. Each entry is direction byte, kind byte, argument
word. Kind 1 calls the normal room mover with the argument as destination.
Kinds 2–5 have additional, incompletely decoded behavior. This reports a
snapshot of raw exit data, not proof that any particular exit is traversable.
"""
import argparse
import json
from pathlib import Path
import struct

from inspect_object_state import inspect_objects


EXIT_TABLE_POINTER = 0x8dcc
EXIT_TABLE_OFFSET = 0x417e
ENTRY_SIZE = 4
ROOM_RECORD_MIN_SIZE = 0x1a
KIND_MEANING = {
    1: 'direct-room',
    2: 'conditional',
    3: 'message',
    4: 'callback',
    5: 'extended',
}


def inspect_exits(memory, ds, objects, room_ids=None):
    """Decode exits without inventing cardinal meanings for direction bytes."""
    if not 0 < ds <= 0xffff:
        raise ValueError('invalid native data segment')
    if ds * 16 + EXIT_TABLE_POINTER + 2 > min(len(memory), 0xa0000):
        raise ValueError('exit-table pointer outside conventional memory')
    table_segment = struct.unpack_from('<H', memory, ds * 16 + EXIT_TABLE_POINTER)[0]
    if not table_segment:
        raise ValueError('no native exit-table segment at DS:8dcc')
    by_id = {entry['object']: entry for entry in objects}
    if len(by_id) != len(objects):
        raise ValueError('duplicate object IDs in native snapshot')
    if room_ids is not None:
        missing = set(room_ids) - set(by_id)
        if missing:
            raise ValueError(f'unknown room ID 0x{min(missing):03x}')
        wrong_type = [room for room in room_ids if by_id[room]['type'] != 1]
        if wrong_type:
            raise ValueError(f'object 0x{wrong_type[0]:03x} is not a type-1 room')
    rooms = []
    for entry in objects:
        if entry['type'] != 1 or (room_ids is not None and entry['object'] not in room_ids):
            continue
        record = bytes.fromhex(entry['raw_state'])
        if len(record) < ROOM_RECORD_MIN_SIZE:
            raise ValueError(f'room 0x{entry["object"]:03x}: record too short for exits')
        count = record[0x16]
        first = struct.unpack_from('<H', record, 0x18)[0]
        exits = []
        if first:
            end = EXIT_TABLE_OFFSET + ENTRY_SIZE * (first - 1 + count)
            if end > 0x10000:
                raise ValueError(f'room 0x{entry["object"]:03x}: exit table wraps segment')
            for index in range(first - 1, first - 1 + count):
                offset = EXIT_TABLE_OFFSET + ENTRY_SIZE * index
                linear = table_segment * 16 + offset
                if linear + ENTRY_SIZE > min(len(memory), 0xa0000):
                    raise ValueError(f'room 0x{entry["object"]:03x}: '
                                     'exit entry outside conventional memory')
                direction, kind, argument = struct.unpack_from('<BBH', memory, linear)
                row = dict(index=index, direction_code=direction, kind=kind,
                           kind_meaning=KIND_MEANING.get(kind, 'unknown'),
                           argument=argument)
                if kind == 1 and argument in by_id and by_id[argument]['type'] == 1:
                    row['direct_room'] = argument
                    row['direct_room_name'] = by_id[argument].get('name', '')
                exits.append(row)
        rooms.append(dict(id=entry['object'], name=entry.get('name', ''),
                          count=count, first=first, active=bool(first), exits=exits))
    return dict(schema_version=1,
                meaning='native room exit snapshot; direction codes and conditional '
                        'traversability not yet verified against typed commands',
                table_segment=table_segment, table_offset=EXIT_TABLE_OFFSET,
                rooms=rooms)


def format_text(world):
    lines = [f'Native room exit table at {world["table_segment"]:04x}:'
             f'{world["table_offset"]:04x} (four-byte records)',
             'Direction codes are raw bytes; only kind 1 directly identifies '
             'a destination room.']
    for room in world['rooms']:
        lines.append(f'0x{room["id"]:03x} {room["name"]}: '
                     f'count={room["count"]} first={room["first"]}')
        if room['count'] and not room['active']:
            lines.append('  (first index is zero; native dispatcher skips these exits)')
        for route in room['exits']:
            target = (f' -> 0x{route["direct_room"]:03x} '
                      f'{route["direct_room_name"]}' if 'direct_room' in route else '')
            lines.append(f'  [{route["index"]:3}] direction=0x{route["direction_code"]:02x} '
                         f'kind={route["kind"]} ({route["kind_meaning"]}) '
                         f'argument=0x{route["argument"]:03x}{target}')
    lines.append('Exit entries are raw native data, not verified reachable '
                 'edges or a reconstructed parser command mapping.')
    return '\n'.join(lines) + '\n'


def format_dot(world):
    """Draw only direct-room records; retain unknown direction bytes."""
    def quote(value):
        return ('"' + str(value).replace('\\', '\\\\').replace('"', '\\"')
                .replace('\n', '\\n') + '"')

    edges = [(room, route) for room in world['rooms'] for route in room['exits']
             if 'direct_room' in route]
    node_names = {}
    for room, route in edges:
        node_names[room['id']] = room['name']
        node_names[route['direct_room']] = route['direct_room_name']
    output = ['digraph gateway_native_exits {',
              '  // Raw type-1 exits only; direction codes are not decoded.',
              '  rankdir=LR;']
    for node_id, name in sorted(node_names.items()):
        output.append(f'  n{node_id:03x} [label={quote(f"0x{node_id:03x} {name}")}];')
    for room, route in edges:
        output.append(f'  n{room["id"]:03x} -> n{route["direct_room"]:03x} '
                      f'[label="0x{route["direction_code"]:02x}"];')
    output.extend(['}', ''])
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path,
                        help='guest dump prefix (native.bin and native.regs)')
    parser.add_argument('--names', type=Path, help='optional Gateway OBJECT.DAT')
    parser.add_argument('--room', action='append', type=lambda value: int(value, 0),
                        help='show only one room ID (repeatable; e.g. 0x0ee)')
    parser.add_argument('--format', choices=('text', 'json', 'dot'), default='text')
    args = parser.parse_args()
    try:
        regs = dict(line.split('=', 1) for line in
                    args.snapshot.with_suffix('.regs').read_text().splitlines())
        memory = args.snapshot.with_suffix('.bin').read_bytes()
        names = None
        if args.names:
            file = args.names.read_bytes()
            if len(file) < 2 or struct.unpack_from('<H', file)[0] != len(file) - 2:
                raise ValueError('invalid OBJECT.DAT size header')
            names = file[2:]
        ds = int(regs['DS'], 16)
        records = inspect_objects(memory, ds, names)
        world = inspect_exits(memory, ds, records, args.room)
        if args.format == 'json':
            print(json.dumps(world, indent=2))
        elif args.format == 'dot':
            print(format_dot(world), end='')
        else:
            print(format_text(world), end='')
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

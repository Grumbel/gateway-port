#!/usr/bin/env python3
"""Read Gateway's native type-1 room exit records from a guest memory dump.

Original GATEMAIN.EXE file 0x60cf–0x6381 (runtime 148e:05f6) reads room
+0x16 (byte count) and +0x18 (one-based start), then scans 4-byte entries
at [word DS:8dcc]:417e. Each entry is direction byte, kind byte, argument
word. Kind 1 calls the normal room mover with the argument as destination.
Kind 2 indexes another four-byte table at [word DS:8dd2]:4996 with a
destination room and flag-object ID; it checks object flag 0x0c and takes
a further path for flag 0x10 (sets 0x0c and then moves). Kinds 3–5 remain
incompletely decoded. This
reports a snapshot of raw exit data, not proof that an exit is traversable.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

from inspect_object_state import inspect_objects


EXIT_TABLE_POINTER = 0x8dcc
EXIT_TABLE_OFFSET = 0x417e
CONDITIONAL_TABLE_POINTER = 0x8dd2
CONDITIONAL_TABLE_OFFSET = 0x4996
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

    def pointer(offset, label):
        if ds * 16 + offset + 2 > min(len(memory), 0xa0000):
            raise ValueError(f'{label} pointer outside conventional memory')
        segment = struct.unpack_from('<H', memory, ds * 16 + offset)[0]
        if not segment:
            raise ValueError(f'no native {label} segment at DS:{offset:04x}')
        return segment

    def table_entry(segment, offset, label, layout='<BBH'):
        if offset + ENTRY_SIZE > 0x10000:
            raise ValueError(f'{label} wraps segment')
        linear = segment * 16 + offset
        if linear + ENTRY_SIZE > min(len(memory), 0xa0000):
            raise ValueError(f'{label} outside conventional memory')
        return struct.unpack_from(layout, memory, linear)

    table_segment = pointer(EXIT_TABLE_POINTER, 'exit-table')
    conditional_segment = None
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
            for index in range(first - 1, first - 1 + count):
                offset = EXIT_TABLE_OFFSET + ENTRY_SIZE * index
                direction, kind, argument = table_entry(
                    table_segment, offset,
                    f'room 0x{entry["object"]:03x}: exit table entry')
                row = dict(index=index, direction_code=direction, kind=kind,
                           kind_meaning=KIND_MEANING.get(kind, 'unknown'),
                           argument=argument)
                if kind == 1 and argument in by_id and by_id[argument]['type'] == 1:
                    row['direct_room'] = argument
                    row['direct_room_name'] = by_id[argument].get('name', '')
                if kind == 2:
                    if conditional_segment is None:
                        conditional_segment = pointer(CONDITIONAL_TABLE_POINTER,
                                                      'conditional-exit-table')
                    destination, condition = table_entry(
                        conditional_segment, CONDITIONAL_TABLE_OFFSET + 4 * argument,
                        f'room 0x{entry["object"]:03x}: conditional entry', '<HH')
                    row['conditional_destination'] = destination
                    row['condition_object'] = condition
                    if condition in by_id:
                        row['condition_name'] = by_id[condition].get('name', '')
                    if destination in by_id and by_id[destination]['type'] == 1:
                        row['conditional_room'] = destination
                        row['conditional_room_name'] = by_id[destination].get('name', '')
                exits.append(row)
        rooms.append(dict(id=entry['object'], name=entry.get('name', ''),
                          count=count, first=first, active=bool(first), exits=exits))
    return dict(schema_version=1,
                meaning='native room exit snapshot; 0x03 east and 0x07 west '
                        'observed in repeated first-room transitions; other '
                        'directions and conditional traversability remain unverified',
                table_segment=table_segment, table_offset=EXIT_TABLE_OFFSET,
                conditional_table_segment=conditional_segment,
                conditional_table_offset=CONDITIONAL_TABLE_OFFSET,
                rooms=rooms)


def format_text(world):
    lines = [f'Native room exit table at {world["table_segment"]:04x}:'
             f'{world["table_offset"]:04x} (four-byte records)',
             'Direction codes are raw bytes (0x03 EAST, 0x07 WEST confirmed '
             'in the first-room replay). Kind 2 additionally indexes a '
             'conditional destination and flag-object record.']
    for room in world['rooms']:
        lines.append(f'0x{room["id"]:03x} {room["name"]}: '
                     f'count={room["count"]} first={room["first"]}')
        if room['count'] and not room['active']:
            lines.append('  (first index is zero; native dispatcher skips these exits)')
        for route in room['exits']:
            target = (f' -> 0x{route["direct_room"]:03x} '
                      f'{route["direct_room_name"]}' if 'direct_room' in route else '')
            if route['kind'] == 2:
                target += (f' conditional_destination=0x{route["conditional_destination"]:03x}'
                           f' condition_object=0x{route["condition_object"]:03x}'
                           f' {route.get("condition_name", "")}'
                           ' (flag 0x0c moves; flag 0x10 sets 0x0c then moves)')
            lines.append(f'  [{route["index"]:3}] direction=0x{route["direction_code"]:02x} '
                         f'kind={route["kind"]} ({route["kind_meaning"]}) '
                         f'argument=0x{route["argument"]:03x}{target}')
    lines.append('Exit entries are native snapshot data, not verified reachable '
                 'edges. Conditional destinations need their object flags checked.')
    return '\n'.join(lines) + '\n'


def format_dot(world, include_conditional=False):
    """Draw direct records, optionally dashed conditional destination candidates."""
    def quote(value):
        return ('"' + str(value).replace('\\', '\\\\').replace('"', '\\"')
                .replace('\n', '\\n') + '"')

    edges = [(room, route, route.get('direct_room', route.get('conditional_room')),
              route.get('direct_room_name', route.get('conditional_room_name', '')))
             for room in world['rooms'] for route in room['exits']
             if 'direct_room' in route or
             (include_conditional and 'conditional_room' in route)]
    node_names = {}
    for room, route, destination, name in edges:
        node_names[room['id']] = room['name']
        node_names[destination] = name
    output = ['digraph gateway_native_exits {',
              '  // Kind 1: solid; optional kind 2: dashed and flag-gated.',
              '  // 0x03 EAST and 0x07 WEST observed for the first-room pair.',
              '  rankdir=LR;']
    for node_id, name in sorted(node_names.items()):
        output.append(f'  n{node_id:03x} [label={quote(f"0x{node_id:03x} {name}")}];')
    for room, route, destination, _ in edges:
        options = f'label="0x{route["direction_code"]:02x}"'
        if route['kind'] == 2:
            options += (f', style=dashed, tooltip="object '
                        f'0x{route["condition_object"]:03x}: flags 0x0c/0x10"')
        output.append(f'  n{room["id"]:03x} -> n{destination:03x} [{options}];')
    output.extend(['}', ''])
    return '\n'.join(output)


def format_stats(world):
    """Summarize decoded entry kinds without treating candidates as legal moves."""
    rooms = world['rooms']
    exits = [route for room in rooms for route in room['exits']]
    kinds = Counter(route['kind'] for route in exits)
    direct = [route for route in exits if 'direct_room' in route]
    conditional = [route for route in exits if 'conditional_room' in route]
    connected = {room['id'] for room in rooms for route in room['exits']
                 if 'direct_room' in route or 'conditional_room' in route}
    connected |= {route['direct_room'] for route in direct}
    connected |= {route['conditional_room'] for route in conditional}
    output = [f'Native type-1 rooms: {len(rooms)} '
              f'({sum(bool(room["active"]) for room in rooms)} with indexed exits)',
              f'Decoded exit entries: {len(exits)}',
              'Exit kinds: ' + ', '.join(f'{kind}={count}' for kind, count in
                                      sorted(kinds.items())) if kinds else 'Exit kinds: none',
              f'Kind-1 resolved room targets: {len(direct)} of {kinds[1]}',
              f'Kind-2 resolved room targets: {len(conditional)} of {kinds[2]}',
              f'Rooms represented by candidate kind-1/2 edges: {len(connected)}',
              'These counts describe the native snapshot, not presently usable exits.']
    return '\n'.join(output) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path,
                        help='guest dump prefix (native.bin and native.regs)')
    parser.add_argument('--names', type=Path, help='optional Gateway OBJECT.DAT')
    parser.add_argument('--room', action='append', type=lambda value: int(value, 0),
                        help='show only one room ID (repeatable; e.g. 0x0ee)')
    parser.add_argument('--format', choices=('text', 'json', 'dot', 'stats'), default='text')
    parser.add_argument('--include-conditional', action='store_true',
                        help='include kind-2 candidate destinations as dashed DOT edges')
    args = parser.parse_args()
    try:
        if args.include_conditional and args.format != 'dot':
            parser.error('--include-conditional requires --format dot')
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
            print(format_dot(world, include_conditional=args.include_conditional), end='')
        elif args.format == 'stats':
            print(format_stats(world), end='')
        else:
            print(format_text(world), end='')
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Inspect authentic 1992 Gateway GATE###.SAV files without DOS execution.

The 734 fixed object kinds and type-specific record sizes were independently
recovered from original native memory. No game resource bytes are embedded.
"""
import argparse
from dataclasses import dataclass
import json
import pathlib
import struct

from inspect_object_state import (HANDLER_FIELDS, LOCATION_FIELDS, SIBLING_FIELDS,
                                  child_links, inspect_objects, relation_links)


# One object kind (1..8) per object ID, beginning at ID 1. This *structural*
# mapping is required because the save stores bare record bytes, without type
# tags, separators, or an object count. Verified against native object tables.
OBJECT_KINDS = (
    '1111111111222555511228888888555611117782222222222222222888888888'
    '5555511111187777777777777778888888888888888855555555555555222222'
    '2222222388851111111111111111778822222222222228888888888888288888'
    '8888855555555555557222244485444435483311111111111111111111111177'
    '7777777777777777722222222222222222222222222222222222222222222222'
    '2222222222222222223333333333333355555555588888888888888888888888'
    '8888882888888888888288888882888288888888886666661228886881111111'
    '1111111111178888822222222228888822228888288882222888222222222282'
    '8222222888888822822222222222222882826665555588311111222228882882'
    '2222222228886682581111116822228222222252885211111111111776222222'
    '2222222222288551288888888112222222788888888881822222888888718222'
    '888888888718182788888855333338'
)
OBJECT_RECORD_SIZES = {1: 26, 2: 36, 3: 12, 4: 20,
                       5: 22, 6: 24, 7: 32, 8: 24}
OBJECT_RELATION_COUNTS = {1: 0, 2: 1, 3: 0, 4: 0,
                          5: 0, 6: 2, 7: 1, 8: 1}
OBJECT_CHILD_COUNTS = {1: 1, 2: 3, 3: 0, 4: 2,
                       5: 2, 6: 2, 7: 3, 8: 0}
DESCRIPTION_BYTES = 34              # 33 space-padded ASCII bytes plus NUL
OBJECT_BYTES = 20650                # sum of all 734 type-specific records
GLOBAL_STATE_BYTES = 1071          # includes current/previous room and turns
EVENT_COUNT_BYTES = 2
EVENT_BYTES = 4                      # u16 event ID, u16 remaining turns
EVENTS_OFFSET = DESCRIPTION_BYTES + OBJECT_BYTES + GLOBAL_STATE_BYTES + EVENT_COUNT_BYTES

if len(OBJECT_KINDS) != 734 or sum(OBJECT_RECORD_SIZES[int(t)] for t in OBJECT_KINDS) != OBJECT_BYTES:
    raise RuntimeError('incorrect recovered Gateway object kind map')


@dataclass(frozen=True)
class SaveObject:
    object_id: int
    kind: int
    file_offset: int
    raw: bytes
    name_token: int
    handler: int
    location: int | None
    relation_links: tuple[int, ...]
    child_links: tuple[int, ...]
    child_slot: int | None
    next_sibling: int | None


@dataclass(frozen=True)
class TimedEvent:
    event_id: int
    remaining_turns: int


@dataclass(frozen=True)
class GatewaySave:
    description: str
    objects: tuple[SaveObject, ...]
    global_state: bytes
    current_room: int
    previous_room: int
    turn_counter: int
    events: tuple[TimedEvent, ...]
    file_bytes: int

    def summary(self, include_objects=False):
        result = dict(description=self.description, file_bytes=self.file_bytes,
                      object_count=len(self.objects), object_bytes=OBJECT_BYTES,
                      global_state_bytes=GLOBAL_STATE_BYTES,
                      current_room=self.current_room, previous_room=self.previous_room,
                      turn_counter=self.turn_counter, event_count=len(self.events),
                      events=[dict(event_id=e.event_id, remaining_turns=e.remaining_turns)
                              for e in self.events])
        if include_objects:
            result['objects'] = [dict(object_id=o.object_id, kind=o.kind,
                                      file_offset=o.file_offset, size=len(o.raw),
                                      name_token=o.name_token, handler=o.handler,
                                      location=o.location,
                                      relation_links=o.relation_links,
                                      child_links=o.child_links,
                                      child_slot=o.child_slot,
                                      next_sibling=o.next_sibling)
                                 for o in self.objects]
        return result


def compare_saved_state(left, right):
    """Classify differences using the independently verified save layout.

    Preserve file offsets and byte values for undecoded globals, while
    keeping original object text and serialized records out of the report.
    The +4 turn-counter interpretation remains provisional.
    """
    objects = [a.object_id for a, b in zip(left.objects, right.objects)
               if a.kind != b.kind or a.raw != b.raw]
    globals_changed = [(offset, x, y)
                       for offset, (x, y) in enumerate(zip(left.global_state,
                                                           right.global_state)) if x != y]
    event_changes = []
    for index in range(max(len(left.events), len(right.events))):
        a = left.events[index] if index < len(left.events) else None
        b = right.events[index] if index < len(right.events) else None
        if a != b:
            event_changes.append(dict(slot=index,
                                      left=[a.event_id, a.remaining_turns] if a else None,
                                      right=[b.event_id, b.remaining_turns] if b else None))
    delta = right.turn_counter - left.turn_counter
    same_event_ids = ([event.event_id for event in left.events] ==
                      [event.event_id for event in right.events])
    consistent_turn_shift = (bool(delta) and bool(left.events) and not objects and
                             left.description == right.description and same_event_ids and
                             all(a.remaining_turns - b.remaining_turns == delta
                                 for a, b in zip(left.events, right.events)) and
                             all(offset in (4, 5) for offset, _, _ in globals_changed))
    return dict(file_bytes=[left.file_bytes, right.file_bytes],
                description_equal=left.description == right.description,
                object_records_changed=len(objects), object_ids=objects,
                global_bytes_changed=len(globals_changed),
                global_byte_changes=[dict(offset=offset,
                                          file_offset=DESCRIPTION_BYTES + OBJECT_BYTES + offset,
                                          left=f'{a:02x}', right=f'{b:02x}')
                                     for offset, a, b in globals_changed],
                current_room=[left.current_room, right.current_room],
                previous_room=[left.previous_room, right.previous_room],
                turn_counter=[left.turn_counter, right.turn_counter],
                event_counts=[len(left.events), len(right.events)],
                event_changes=event_changes,
                turn_shift_consistent=consistent_turn_shift)


def parse_gateway_save(data):
    if len(data) < EVENTS_OFFSET:
        raise ValueError(f'truncated Gateway save: {len(data)} < {EVENTS_OFFSET} bytes')
    if data[33] != 0:
        raise ValueError('Gateway save description lacks the 34th-byte NUL terminator')
    padded = data[:33]
    if any(not 0x20 <= ch <= 0x7e for ch in padded):
        raise ValueError('Gateway save description must contain printable ASCII')
    description = padded.rstrip(b' ').decode('ascii')
    if not description:
        raise ValueError('empty Gateway save description')

    event_count = struct.unpack_from('<H', data, EVENTS_OFFSET - EVENT_COUNT_BYTES)[0]
    expected = EVENTS_OFFSET + EVENT_BYTES * event_count
    if len(data) != expected:
        raise ValueError(f'Gateway save has {len(data)} bytes, expected {expected} '
                         f'from {event_count} scheduled events')

    offset = DESCRIPTION_BYTES
    objects = []
    for object_id, digit in enumerate(OBJECT_KINDS, 1):
        kind = int(digit)
        size = OBJECT_RECORD_SIZES[kind]
        raw = data[offset:offset + size]
        handler = struct.unpack_from('<H', raw, HANDLER_FIELDS[kind - 1])[0]
        if handler > 696:
            raise ValueError(f'object {object_id}: invalid native handler index {handler}')
        loc = (struct.unpack_from('<H', raw, LOCATION_FIELDS[kind])[0]
               if kind in LOCATION_FIELDS else None)
        child_slot = next_sibling = None
        if kind in SIBLING_FIELDS:
            slot_offset, next_offset = SIBLING_FIELDS[kind]
            child_slot = raw[slot_offset]
            next_sibling = struct.unpack_from('<H', raw, next_offset)[0]
        objects.append(SaveObject(object_id, kind, offset, raw,
                                  struct.unpack_from('<H', raw)[0], handler, loc,
                                  tuple(relation_links(kind, OBJECT_RELATION_COUNTS[kind], raw)),
                                  tuple(child_links(kind, OBJECT_CHILD_COUNTS[kind], raw)),
                                  child_slot, next_sibling))
        offset += size
    if offset != DESCRIPTION_BYTES + OBJECT_BYTES:
        raise RuntimeError('object record offsets disagree with recovered layout')
    global_state = data[offset:offset + GLOBAL_STATE_BYTES]
    current_room, previous_room = struct.unpack_from('<HH', global_state)
    # Unlike the two room references, these two bytes increase as a big-endian
    # count: 01 13 -> 01 14 exactly as extant event countdowns drop by one.
    # Its internal implementation may instead store a pair of byte counters;
    # the 16-bit wrap convention has not yet been observed.
    turn_counter = int.from_bytes(global_state[4:6], 'big')
    if not 0 < current_room <= 734 or previous_room > 734:
        raise ValueError('invalid current/previous Gateway room ID')
    events = tuple(TimedEvent(*struct.unpack_from('<HH', data, EVENTS_OFFSET + i * EVENT_BYTES))
                   for i in range(event_count))
    return GatewaySave(description, tuple(objects), global_state,
                       current_room, previous_room, turn_counter, events, len(data))


def compare_snapshot(saved, prefix):
    """Compare every serialized object with the original native RAM image."""
    regs = dict(line.split('=', 1) for line in prefix.with_suffix('.regs').read_text().splitlines())
    memory = prefix.with_suffix('.bin').read_bytes()
    native = None
    for register in ('DS', 'ES'):
        if register not in regs:
            continue
        try:
            candidate = inspect_objects(memory, int(regs[register], 16))
        except ValueError:
            continue
        if len(candidate) == len(saved.objects):
            native = candidate
            break
    if native is None:
        raise ValueError('no matching native object directory at saved DS/ES')
    return [obj.object_id for obj, live in zip(saved.objects, native)
            if obj.kind != live['type'] or obj.raw.hex() != live['raw_state']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('save', type=pathlib.Path, help='original GATE###.SAV file')
    parser.add_argument('--objects', action='store_true', help='include all 734 object descriptors')
    parser.add_argument('--compare-snapshot', type=pathlib.Path, metavar='PREFIX',
                        help='compare serialized records with a native .bin/.regs capture')
    parser.add_argument('--compare', type=pathlib.Path, metavar='OTHER_SAVE',
                        help='classify differences against a second original Gateway save')
    args = parser.parse_args()
    try:
        saved = parse_gateway_save(args.save.read_bytes())
        result = saved.summary(args.objects)
        if args.compare:
            other = parse_gateway_save(args.compare.read_bytes())
            result = dict(left=result, right=other.summary(args.objects),
                          difference=compare_saved_state(saved, other))
        if args.compare_snapshot:
            mismatch = compare_snapshot(saved, args.compare_snapshot)
            result['native_record_mismatches'] = mismatch
            if mismatch:
                raise ValueError(f'{len(mismatch)} objects differ from native memory: {mismatch[:16]}')
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Inspect the Beach-success and membership-pin chain in a *native* snapshot.

The offsets here are GATEMAIN.EXE DS pointer offsets observed in the original
section-50 glass handler and section-51 return routine. They are not inferred
from Ghidra's synthetic overlay address spaces. No state is modified.
"""

import argparse
import json
from pathlib import Path
import re
import struct

from inspect_object_state import inspect_objects


LIMIT = 0xa0000
DISPATCH_DS = re.compile(r'^entry .*\bpc=147a6 cs=1476 ds=([0-9a-f]+)\b')


def snapshot_objects(memory, register_ds, trace_lines=()):
    """Recover Gateway DS from observed dispatcher calls if the CPU stopped in a far service.

    A bounded emulator stop can capture DS belonging to the RTLink manager
    instead of Gateway. Only the original action dispatcher's *recorded*
    segment is an acceptable fallback; never probe memory for plausible DS.
    """
    try:
        return register_ds, inspect_objects(memory, register_ds)
    except ValueError:
        candidates = {int(match.group(1), 16) for line in trace_lines
                      if (match := DISPATCH_DS.match(line))}
        if len(candidates) != 1:
            raise ValueError('final DS is not Gateway state and no unique native '
                             'dispatcher DS was observed')
        ds = next(iter(candidates))
        if ds == register_ds:
            raise ValueError('native Gateway object snapshot is invalid at dispatcher DS')
        return ds, inspect_objects(memory, ds)


def status_at(memory, ds, objects):
    """Return original state words, never treating an absent object as empty."""
    if not 0 < ds <= 0xffff or len(memory) < LIMIT:
        raise ValueError('native conventional-memory image or DS is invalid')

    def word(address):
        if address < 0 or address + 2 > LIMIT:
            raise ValueError('native DS pointer or word outside conventional memory')
        return struct.unpack_from('<H', memory, address)[0]

    def through(segment_offset, data_offset, size):
        pointer = word(ds * 16 + segment_offset)
        location = pointer * 16 + data_offset
        if not pointer or location + size > LIMIT:
            raise ValueError('unavailable native Beach/VR state segment pointer')
        return location

    by_id = {item['object']: item for item in objects}
    if len(by_id) != len(objects):
        raise ValueError('duplicate object IDs in native world')

    def owner(object_id):
        record = by_id.get(object_id)
        if record is None or len(record['relation_links']) != 1:
            raise ValueError('object 0x%x has no unique native location' % object_id)
        return record['relation_links'][0]

    def enclosing_room(location):
        """Follow immediate native ownership links, preserving couch/chair locations."""
        visited = set()
        while location not in visited:
            visited.add(location)
            record = by_id.get(location)
            if record is None:
                return None
            if record.get('type') == 1:
                return location
            if len(record['relation_links']) != 1:
                return None
            location = record['relation_links'][0]
        return None

    beach_flag = memory[through(0x9644, 0x78fa, 1)]
    player_location = owner(0xd3)
    return dict(
        player_location=player_location,
        player_enclosing_room=enclosing_room(player_location),
        technician_location=owner(0x2a3),
        pin_location=owner(0x2a9),
        glass_location=owner(0x2b4),
        beach_success_byte=beach_flag,
        beach_success_byte_return_view=memory[through(0x969c, 0x78fa, 1)],
        drink_refresh_counter=word(through(0x962a, 0x78f6, 2)),
        pin_on_terminal_floor=owner(0x2a9) == 0x29b,
        pin_carried=owner(0x2a9) == 0xd3,
    )


def event_millis(line):
    field = next((item[3:] for item in line.split() if item.startswith('ms=')), None)
    if field is None or not field.isdecimal():
        raise ValueError('malformed native trace event timestamp')
    return int(field)


def verify_beach_casino_chain(state, lines):
    """Assert the *ordered* original guest events, including the room-exit callback.

    An end-state match alone cannot tell normal gameplay from a state poke or
    an unrelated binary branch. Require the observed writes and native event
    0x0f callback result in chronological order. The physical success-byte
    watch must have been explicitly enabled during the replay.
    """
    if any(line.startswith('research-poke16 ') for line in lines):
        raise ValueError('controlled guest-memory intervention in native replay')
    expected = {
        'beach_success_byte': 1,
        'beach_success_byte_return_view': 1,
        'drink_refresh_counter': 5,
        'pin_carried': True,
        'player_location': 0x0f6,
        'player_enclosing_room': 0x0f6,
    }
    for field, value in expected.items():
        if state[field] != value:
            raise ValueError('expected native %s=%s, observed %s' %
                             (field, value, state[field]))

    milestones = (
        ('Beach flag write', ('byte-write ', 'pc=4a908', 'before=0 value=1')),
        ('technician pin award', ('object-write ', 'object=2a9',
                                  'offset=c size=2 before=a302 value=9b02')),
        ('pin pickup', ('object-write ', 'object=2a9',
                        'offset=c size=2 before=9b02 value=d300')),
        ('Casino room-exit call', ('room_callback_stage=scanner-call',
                                   'room_callback_id=14 room_callback_event=f')),
        ('Casino callback admits pin holder', ('room_callback_stage=scanner-return',
                                               'room_callback_result=f6')),
        ('native Pedroza Lounge movement', ('object-location-set ', 'object=d3',
                                            'location=f6', 'changed=1')),
    )
    index = 0
    result = {}
    for label, terms in milestones:
        while index < len(lines) and not all(term in lines[index] for term in terms):
            index += 1
        if index >= len(lines):
            raise ValueError('missing ordered native evidence: ' + label)
        result[label] = event_millis(lines[index])
        index += 1
    return result


def inspect_snapshot(prefix, after_ms=18000):
    regs = dict(line.split('=', 1) for line in prefix.with_suffix('.regs').read_text().splitlines()
                if '=' in line)
    register_ds = int(regs['DS'], 16)
    memory = prefix.with_suffix('.bin').read_bytes()
    trace_path = prefix.with_suffix('.trace')
    lines = trace_path.read_text().splitlines() if trace_path.exists() else []
    ds, objects = snapshot_objects(memory, register_ds, lines)
    result = status_at(memory, ds, objects)
    result['native_data_segment'] = ds
    result['final_register_ds'] = register_ds
    if lines:
        result['observed_byte_writes'] = [line for line in lines
                                          if line.startswith('byte-write ')]
        # A physical address can contain unrelated startup data *before* the
        # native Gateway DS is initialized. Keep the complete audit trail,
        # but distinguish writes after the first scripted gameplay command.
        result['post_startup_byte_writes'] = [line for line in result['observed_byte_writes']
                                              if event_millis(line) >= after_ms]
        result['observed_pin_record_writes'] = [line for line in lines
                                                if line.startswith('object-write ') and
                                                ' object=2a9 ' in line]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshots', nargs='+', type=Path, help='native dump prefixes (no .bin)')
    parser.add_argument('--format', choices=('text', 'json'), default='text')
    parser.add_argument('--after-ms', type=int, default=18000,
                        help='post-startup cutoff for guest physical byte writes (default: 18000)')
    parser.add_argument('--expect-beach-casino', action='store_true',
                        help='verify the unmodified-game Beach reward, pin pickup and Casino EAST chain')
    args = parser.parse_args()
    if args.after_ms < 0:
        parser.error('--after-ms must be nonnegative')
    try:
        states = {str(prefix): inspect_snapshot(prefix, args.after_ms)
                  for prefix in args.snapshots}
        verified = ({str(prefix): verify_beach_casino_chain(
            states[str(prefix)], prefix.with_suffix('.trace').read_text().splitlines())
            for prefix in args.snapshots} if args.expect_beach_casino else {})
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(1, 'inspect_beach_pin: %s\n' % error)
    if args.format == 'json':
        if verified:
            for prefix, milestones in verified.items():
                states[prefix]['verified_native_chain_ms'] = milestones
        print(json.dumps(states, indent=2))
    else:
        for prefix, state in states.items():
            print('%s: player-location=0x%03x enclosing-room=%s technician=0x%03x pin=0x%03x '
                  'glass=0x%03x Beach-byte=%d return-view=%d refresh-count=%d'
                  % (prefix, state['player_location'],
                     ('0x%03x' % state['player_enclosing_room'])
                     if state['player_enclosing_room'] is not None else 'unknown',
                     state['technician_location'],
                     state['pin_location'], state['glass_location'],
                     state['beach_success_byte'], state['beach_success_byte_return_view'],
                     state['drink_refresh_counter']))
            print('  pin on VR Terminal floor: %s; carried by player: %s' %
                  (state['pin_on_terminal_floor'], state['pin_carried']))
            for field in ('observed_byte_writes', 'post_startup_byte_writes',
                          'observed_pin_record_writes'):
                if field in state:
                    print('  %s: %d' % (field, len(state[field])))
            print('  Physical byte-watch hits before %d ms may be unrelated startup data. '
                  'No write count proves a negative unless the watch was enabled.' %
                  args.after_ms)
            if prefix in verified:
                print('  VERIFIED original Beach → pin pickup → Casino → Pedroza Lounge: ' +
                      ', '.join('%s=%d ms' % (label, ms)
                                for label, ms in verified[prefix].items()))


if __name__ == '__main__':
    main()

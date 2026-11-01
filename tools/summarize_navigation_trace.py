#!/usr/bin/env python3
"""Correlate scripted Gateway room moves with native player-location writes.

The player record is native object 0x0d3 (type 7, location at +0x0c).
Service hooks identify verified native deltas; object-write hooks capture
attempted memory writes *before execution*. A room-label association follows
scripted timing slots and is not proof of parser acceptance or a legal exit.
"""
import argparse
from bisect import bisect_right
from pathlib import Path
import json

from story_scenarios import OBJECT_SLOTS, parse_sequence


PLAYER = 0x0d3
LOCATION_OFFSET = 0x0c


def fields(line):
    return dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)


def location_events(lines, player=PLAYER):
    """Combine native service results with matching pre-execution writes."""
    services = []
    attempts = []
    watching = False
    for line in lines:
        try:
            if line.startswith('object-watch '):
                info = fields(line)
                if int(info['object'], 16) == player:
                    if int(info['type'], 16) != 7:
                        raise ValueError('player watch is not native type 7')
                    watching = True
            elif line.startswith('object-location-set '):
                info = fields(line)
                if int(info['object'], 16) != player:
                    continue
                change = info.get('changed')
                if change not in ('0', '1', 'unsupported'):
                    continue
                services.append(dict(ms=int(info['ms']), new=int(info['location'], 16),
                                     changed=change, caller=info.get('return', '?')))
            elif line.startswith('object-write '):
                info = fields(line)
                if int(info['object'], 16) != player:
                    continue
                offset, size = int(info['offset'], 16), int(info['size'], 16)
                if offset != LOCATION_OFFSET or size != 2:
                    continue
                before, value = bytes.fromhex(info['before']), bytes.fromhex(info['value'])
                if len(before) != 2 or len(value) != 2:
                    raise ValueError('invalid player location word write')
                attempts.append(dict(ms=int(info['ms']),
                                     old=int.from_bytes(before, 'little'),
                                     new=int.from_bytes(value, 'little'),
                                     pc=info.get('pc', '?')))
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f'invalid native player trace ({error}): {line}') from error

    if attempts and not watching:
        raise ValueError('player location write without verified object-watch entry')

    # The service begins before its write. Match by new value and a bounded
    # guest-time interval; this is not a complete reconstruction of writes.
    consumed = set()
    events = []
    for service in services:
        candidate = next((i for i, attempt in enumerate(attempts)
                          if i not in consumed and 0 <= attempt['ms'] - service['ms'] <= 2
                          and attempt['new'] == service['new']), None)
        write = attempts[candidate] if candidate is not None else None
        if candidate is not None:
            consumed.add(candidate)
        if service['changed'] == '0':
            if write and write['old'] != write['new']:
                raise ValueError('native unchanged service disagrees with watched write')
            continue
        if service['changed'] == '1' and write and write['old'] == write['new']:
            raise ValueError('native changed service disagrees with watched write')
        events.append(dict(ms=service['ms'], old=write['old'] if write else None,
                           new=service['new'], evidence=('verified service' if
                           service['changed'] == '1' else 'unsupported service'),
                           caller=service['caller'], pc=write['pc'] if write else '?'))
    for i, attempt in enumerate(attempts):
        if i not in consumed and attempt['old'] != attempt['new']:
            events.append(dict(ms=attempt['ms'], old=attempt['old'], new=attempt['new'],
                               evidence='pre-write attempt', caller='?', pc=attempt['pc']))
    return sorted(events, key=lambda event: event['ms'])


def room_names(world):
    """Collect only type-1 IDs; duplicated names are allowed."""
    names = {}

    def visit(node):
        if node['native_type'] == 1:
            names[node['id']] = node.get('name', '')
        for slot in node.get('child_slots', []):
            for child in slot.get('objects', []):
                visit(child)

    for root in world['roots']:
        visit(root)
    return names


def summarize(events, names=None, commands=None):
    names = names or {}

    def display(object_id):
        if object_id is None:
            return '?'
        return f'0x{object_id:03x}' + (f' {names[object_id]}' if object_id in names else '')

    lines = ['ms     recent input       player location before -> after'
             '                        evidence / native caller / writer PC']
    for event in events:
        slot = bisect_right(OBJECT_SLOTS, event['ms']) - 1
        label = (commands[slot] if commands and slot >= 0 else
                 f'slot {slot + 1}' if slot >= 0 else 'startup')
        lines.append(f'{event["ms"]:6} {label:<18} '
                     f'{display(event["old"])} -> {display(event["new"])} '
                     f'[{event["evidence"]}; return={event["caller"]}; pc={event["pc"]}]')
    if not events:
        lines.append('(no nonredundant player-location service or watched word write)')
    lines.append('Input is a timed association, not proven command handling. '
                 'Only verified service deltas establish executed native writes; '
                 'pre-write attempts may not complete.')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='native.trace from trace_game_logic.py')
    parser.add_argument('--world', type=Path, help='export_story_world.py JSON for room names')
    parser.add_argument('--commands', help='the seven scheduled comma-separated commands')
    args = parser.parse_args()
    try:
        commands = parse_sequence(args.commands) if args.commands else None
        names = room_names(json.loads(args.world.read_text())) if args.world else {}
        events = location_events(args.trace.read_text().splitlines())
        print(summarize(events, names, commands), end='')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

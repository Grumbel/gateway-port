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
from summarize_object_writes import (HEAD_INSERT_CALL, HEAD_UNLINK_CALL,
                                     child_setter_calls, map_child_setter_return)


PLAYER = 0x0d3
LOCATION_OFFSET = 0x0c
# Original five-byte direct far CALL to the native location setter 0157:064b.
# RTLink relocates the segment word on load; compare against unrelocated OVL.
LOCATION_SETTER_CALL = bytes.fromhex('9a4b065701')
# Section-4 object relocation helper starts at file 0xc830, running at
# 4319:1650 in the observed default layout. Its far return is redirected
# through RTLink manager 2c24:0701, which is not a game-script callsite.
MOVE_HELPER_PC = 0x447e0
MOVE_HELPER_CS = 0x4319
RTLINK_RETURN_IP = 0x0701
RTLINK_RETURN_CS = 0x2c24
# Original GATEMAIN.EXE file 0x60cf: type-1 room exit-table scanner.
# The resident caller at 148e:0c1d pushes AX=direction, then CS before
# CALL 148e:05f6, producing an effective far-return frame on entry.
ROOM_DISPATCH_PC = 0x14ed6
ROOM_DISPATCH_CS = 0x148e
ROOM_DISPATCH_RETURN_IP = 0x0c28
ROOM_DISPATCH_RETURN_CS = 0x148e


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


def map_location_setter_return(return_address, manifest, overlay):
    """Find a byte-verified direct far CALL in the original RTLink overlay.

    Multiple sections can occupy the same guest runtime segment. Only a
    matching five-byte far-call target identifies the original source site;
    indirect calls and unmatched addresses are deliberately unresolved.
    """
    cs_text, ip_text = return_address.split(':')
    linear = int(cs_text, 16) * 16 + int(ip_text, 16)
    matches = []
    for section in manifest['sections']:
        if not section['filename'].upper().endswith('GATEMAIN.OVL'):
            continue
        offset = linear - section['runtime_segment'] * 16
        if not 5 <= offset <= section['file_bytes']:
            continue
        file_return = section['image_file_offset'] + offset
        if overlay[file_return - 5:file_return] == LOCATION_SETTER_CALL:
            matches.append((section['id'], file_return - 5))
    if len(matches) > 1:
        raise ValueError('location setter callsite is ambiguous across RTLink sections')
    return matches[0] if matches else None


def summarize(events, names=None, commands=None, manifest=None, overlay=None):
    if (manifest is None) != (overlay is None):
        raise ValueError('RTLink manifest and original overlay must be supplied together')
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
        detail = (f'{event["evidence"]}; return={event["caller"]}; pc={event["pc"]}')
        if manifest is not None and event['caller'] != '?':
            location = map_location_setter_return(event['caller'], manifest, overlay)
            detail += (f'; original=section={location[0]} '
                       f'GATEMAIN.OVL:0x{location[1]:x}' if location else
                       '; original=unresolved (no matching direct call)')
        lines.append(f'{event["ms"]:6} {label:<18} '
                     f'{display(event["old"])} -> {display(event["new"])} '
                     f'[{detail}]')
    if not events:
        lines.append('(no nonredundant player-location service or watched word write)')
    lines.append('Input is a timed association, not proven command handling. '
                 'Only verified service deltas establish executed native writes; '
                 'pre-write attempts may not complete.')
    return '\n'.join(lines) + '\n'


def summarize_room_heads(events, lines, manifest, overlay, player=PLAYER):
    """Show original-callsite-verified head writes near player location changes.

    The setter is intercepted on entry, before its actual write. Absence of
    an old-head update is consistent with a *middle-sibling* unlink; it does
    not establish that a sibling write occurred or that a move succeeded.
    """
    calls = child_setter_calls(lines)
    classified = [(call, map_child_setter_return(call['return_address'], manifest, overlay))
                  for call in calls]
    output = ['', 'Indexed room child-head setters near player moves '
              '(original direct CALL sites verified):',
              'ms     old-room head replacement             new-room head insertion',
              '------  ------------------------------------  ------------------------------------']
    if not calls:
        output.append('(no indexed-child setter entry hooks in trace; rerun with '
                      '--trace-object-writes d3)')
    for event in events:
        if event['evidence'] != 'verified service' or event['old'] is None:
            continue
        old = [call for call, site in classified
               if site == HEAD_UNLINK_CALL and call['object'] == event['old']
               and call['slot'] == 0 and abs(call['ms'] - event['ms']) <= 1]
        new = [call for call, site in classified
               if site == HEAD_INSERT_CALL and call['object'] == event['new']
               and call['slot'] == 0 and call['value'] == player
               and abs(call['ms'] - event['ms']) <= 1]

        def result(candidates, parent, value_label):
            if len(candidates) > 1:
                return f'0x{parent:03x}: ambiguous ({len(candidates)} setters)'
            if not candidates:
                return f'0x{parent:03x}: not observed'
            call = candidates[0]
            return f'0x{parent:03x}: {value_label(call)} @{call["ms"]}'

        before = result(old, event['old'], lambda call: f'0x{call["value"]:03x}')
        after = result(new, event['new'], lambda call: f'0x{call["value"]:03x}')
        output.append(f'{event["ms"]:6}  {before:<36}  {after}')
    output.append('Head-setter entry observations are pre-write attempts, not verified '
                  'head deltas. A missing old-head setter can mean a middle-sibling '
                  'unlink; it does not prove one. Unrelated setters and other '
                  'RTLink sections are excluded by object ID and original callsite.')
    return '\n'.join(output) + '\n'


def move_helper_calls(lines, player=PLAYER):
    """Decode only player moves with the observed RTLink-managed far ABI.

    The first two words of a hooked entry are the manager's substituted
    return IP/CS. Subsequent words are the actual object, destination and
    slot arguments. The BP frame chain is *not* a direct-call stack trace.
    """
    calls = []
    for line in lines:
        if not line.startswith('entry '):
            continue
        info = fields(line)
        try:
            if int(info['pc'], 16) != MOVE_HELPER_PC or \
                    int(info['cs'], 16) != MOVE_HELPER_CS:
                continue
            stack = [int(word, 16) for word in info['stack'].split(',') if word]
            if len(stack) < 5 or stack[:2] != [RTLINK_RETURN_IP, RTLINK_RETURN_CS] or \
                    stack[2] != player:
                continue
            frames = []
            for frame in info.get('frames', '').split(','):
                if not frame:
                    continue
                parts = [int(word, 16) for word in frame.split(':')]
                if len(parts) != 4:
                    raise ValueError('invalid saved BP frame')
                frames.append(f'{parts[3]:04x}:{parts[2]:04x}')
            calls.append(dict(ms=int(info['ms']), object=stack[2],
                              destination=stack[3], slot=stack[4],
                              frames=frames, trampoline='2c24:0701'))
        except (KeyError, ValueError) as error:
            raise ValueError(f'invalid move-helper entry ({error}): {line}') from error
    return calls


def summarize_move_callers(events, lines, player=PLAYER):
    """Correlate move-helper stack arguments with verified location changes."""
    calls = move_helper_calls(lines, player)
    output = ['', 'Move-helper entry stacks at verified player location changes:',
              'ms     helper args (object -> parent / slot)   saved BP return chain',
              '------  -------------------------------------  ---------------------']
    count = 0
    for event in events:
        if event['evidence'] != 'verified service' or event['old'] is None:
            continue
        matching = [call for call in calls
                    if abs(call['ms'] - event['ms']) <= 1 and
                    call['destination'] == event['new'] and call['slot'] == 0]
        if not matching:
            output.append(f'{event["ms"]:6}  (no matching 4319:1650 entry)')
            continue
        if len(matching) > 1:
            output.append(f'{event["ms"]:6}  (ambiguous: {len(matching)} matching entries)')
            continue
        call = matching[0]
        chain = ' <- '.join(call['frames'][:3]) if call['frames'] else '(no BP frames)'
        output.append(f'{event["ms"]:6}  0x{call["object"]:03x} -> '
                      f'0x{call["destination"]:03x} / {call["slot"]:<16}  {chain}')
        count += 1
    if not count:
        output.append('(no corroborated move entries; run replay with --trace-navigation)')
    output.append('Far return 2c24:0701 is RTLink manager plumbing, not the '
                  'source navigation handler. Saved BP return addresses are '
                  'upstream frames, not proven direct callsites. Entries match '
                  'verified location changes by destination and guest time (±1 ms).')
    return '\n'.join(output) + '\n'


def direction_dispatch_calls(lines):
    """Decode only the byte argument of the known resident exit scanner.

    This checks the original code's effective far-call frame. Other calls
    to the same routine are deliberately excluded until their ABI is known.
    The direction byte's compass meaning is *not* asserted here.
    """
    calls = []
    for line in lines:
        if not line.startswith('entry '):
            continue
        info = fields(line)
        try:
            if (int(info['pc'], 16) != ROOM_DISPATCH_PC or
                    int(info['cs'], 16) != ROOM_DISPATCH_CS):
                continue
            stack = [int(word, 16) for word in info['stack'].split(',') if word]
            if len(stack) < 3 or stack[:2] != [ROOM_DISPATCH_RETURN_IP,
                                               ROOM_DISPATCH_RETURN_CS]:
                continue
            if stack[2] > 0xff:
                raise ValueError('direction argument is not a byte')
            calls.append(dict(ms=int(info['ms']), direction=stack[2],
                              return_address='148e:0c28'))
        except (KeyError, ValueError) as error:
            raise ValueError(f'invalid resident exit-dispatch entry ({error}): '
                             f'{line}') from error
    return calls


def summarize_direction_calls(events, lines, commands=None, names=None):
    """Show a native direction argument and nearby *verified* location move.

    Match the first verified move after each entry, before the next entry,
    and at most half a guest-second later. A timestamp/slot match is an
    observation, not proof that the dispatcher itself authorized the move.
    """
    calls = direction_dispatch_calls(lines)
    names = names or {}
    verified = [event for event in events if event['evidence'] == 'verified service'
                and event['old'] is not None]
    output = ['', 'Resident type-1 exit-dispatch entries (original EXE file 0x60cf):',
              'ms     recent input       direction byte   subsequent verified move',
              '------  -----------------  ---------------  ------------------------']
    for i, call in enumerate(calls):
        slot = bisect_right(OBJECT_SLOTS, call['ms']) - 1
        label = (commands[slot] if commands and 0 <= slot < len(commands) else
                 f'slot {slot + 1}' if slot >= 0 else 'startup')
        until = call['ms'] + 500
        next_entry = calls[i + 1]['ms'] if i + 1 < len(calls) else None
        matching = [event for event in verified
                    if call['ms'] <= event['ms'] <= until and
                    (next_entry is None or event['ms'] < next_entry)]
        if len(matching) == 1:
            move = matching[0]
            before = f'0x{move["old"]:03x}'
            after = f'0x{move["new"]:03x}'
            if move['old'] in names:
                before += f' {names[move["old"]]}'
            if move['new'] in names:
                after += f' {names[move["new"]]}'
            evidence = f'{before} -> {after} @{move["ms"]}'
        elif matching:
            evidence = f'ambiguous: {len(matching)} verified moves'
        else:
            evidence = '(none within 500 guest ms)'
        output.append(f'{call["ms"]:6} {label:<18} '
                      f'0x{call["direction"]:02x}             {evidence}')
    if not calls:
        output.append('(no verified 148e:05f6 call frames; rerun with '
                      '--trace-navigation and a freshly built probe)')
    output.append('Only the 148e:0c28 effective far-return frame is decoded. '
                  'Input labels are scheduled time slots, and nearby moves '
                  'are separate native service observations; exit codes and '
                  'their reachability are not independently proven.')
    return '\n'.join(output) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='native.trace from trace_game_logic.py')
    parser.add_argument('--world', type=Path, help='export_story_world.py JSON for room names')
    parser.add_argument('--commands', help='the seven scheduled comma-separated commands')
    parser.add_argument('--overlay-map', type=Path,
                        help='RTLink JSON manifest (used with --overlay-file)')
    parser.add_argument('--overlay-file', type=Path,
                        help='original GATEMAIN.OVL for direct-call byte verification')
    parser.add_argument('--child-heads', action='store_true',
                        help='report byte-verified child-head setter calls near moves '
                             '(requires --overlay-map and --overlay-file)')
    parser.add_argument('--move-callers', action='store_true',
                        help='correlate RTLink move-helper entry stacks and saved BP frames '
                             'with verified room changes')
    parser.add_argument('--direction-calls', action='store_true',
                        help='decode the resident exit-dispatch direction argument and '
                             'correlate nearby verified room changes')
    args = parser.parse_args()
    try:
        if bool(args.overlay_map) != bool(args.overlay_file):
            parser.error('--overlay-map and --overlay-file must be used together')
        if args.child_heads and not args.overlay_map:
            parser.error('--child-heads requires --overlay-map and --overlay-file')
        commands = parse_sequence(args.commands) if args.commands else None
        names = room_names(json.loads(args.world.read_text())) if args.world else {}
        lines = args.trace.read_text().splitlines()
        events = location_events(lines)
        manifest = json.loads(args.overlay_map.read_text()) if args.overlay_map else None
        overlay = args.overlay_file.read_bytes() if args.overlay_file else None
        print(summarize(events, names, commands, manifest, overlay), end='')
        if args.child_heads:
            print(summarize_room_heads(events, lines, manifest, overlay), end='')
        if args.move_callers:
            print(summarize_move_callers(events, lines), end='')
        if args.direction_calls:
            print(summarize_direction_calls(events, lines, commands, names), end='')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

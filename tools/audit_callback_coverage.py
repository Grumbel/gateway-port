#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Audit native room callback *events* against a section-qualified RTLink census.

An invocation of callback ID 0x14 with event 1 is not a room exit. Use the
byte-verified resident 1474:001f/0036 hooks to recover both arguments and AX,
then check which event-0x0f calls also returned through the room scanner.
This tool never infers a player relocation from AX alone.
"""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

from inventory_native_handlers import parse_calls
from summarize_navigation_trace import callback_return_entries, fields


GATE_CALL = (0x14759, 0x1474)
GATE_RETURN = (0x1475e, 0x1474)


def get_hex(info, field):
    try:
        return int(info[field], 16)
    except (KeyError, ValueError) as error:
        raise ValueError(f'missing or invalid {field} in native gate trace') from error


def far_pointer(value):
    try:
        parts = value.split(':')
        if len(parts) != 2:
            raise ValueError
        segment, offset = (int(part, 16) for part in parts)
        if not 0 <= segment <= 0xffff or not 0 <= offset <= 0xffff:
            raise ValueError
        return segment, offset
    except (AttributeError, ValueError) as error:
        raise ValueError('invalid far-pointer annotation in native callback gate') from error


def parse_gate_events(lines):
    """Pair each verified gate call with its return via SS:BP stack frame."""
    pending = defaultdict(list)
    events = []
    for line in lines:
        if not line.startswith('entry '):
            continue
        info = fields(line)
        stage = info.get('room_callback_stage')
        if stage not in ('gate-call', 'gate-return'):
            continue
        pc, cs = get_hex(info, 'pc'), get_hex(info, 'cs')
        if (pc, cs) != (GATE_CALL if stage == 'gate-call' else GATE_RETURN):
            raise ValueError('resident gate annotation has an unexpected code address')
        key = get_hex(info, 'ss'), get_hex(info, 'bp')
        ms = int(info['ms'])
        if stage == 'gate-call':
            pointer = info.get('room_callback_target')
            if not pointer or not info.get('room_callback_section'):
                raise ValueError('room callback gate lacks byte-verified trampoline metadata')
            event = dict(callback_id=get_hex(info, 'room_callback_id'),
                         event=get_hex(info, 'room_callback_event'),
                         section=get_hex(info, 'room_callback_section'),
                         pointer=far_pointer(pointer), call_ms=ms)
            if not 0 <= event['callback_id'] < 44 or not 0 <= event['event'] <= 0xffff:
                raise ValueError('room callback ID/event outside native limits')
            pending[key].append(event)
        else:
            if not pending[key]:
                raise ValueError('native room callback return without a matching gate call')
            event = pending[key].pop()
            if ms < event['call_ms']:
                raise ValueError('native callback returns before its entry')
            events.append(dict(**event, return_ms=ms,
                               ax=get_hex(info, 'room_callback_result')))
    if any(stack for stack in pending.values()):
        raise ValueError('unreturned room callback gate calls')
    return events


def audit(lines, callbacks_report, calls, census):
    if callbacks_report.get('schema_version') != 1 or callbacks_report.get('table_count') != 44:
        raise ValueError('unsupported native room callback directory')
    by_id = {}
    for item in callbacks_report['callbacks']:
        cid = item['callback_id']
        if cid in by_id or not 0 <= cid < 44:
            raise ValueError('duplicate or invalid callback ID')
        if item.get('status') != 'rtlink':
            raise ValueError('snapshot callback lacks a validated RTLink trampoline')
        by_id[cid] = item

    events = parse_gate_events(lines)
    if not events:
        raise ValueError('no byte-verified callback gate calls; use --trace-navigation')
    for event in events:
        item = by_id.get(event['callback_id'])
        if item is None:
            raise ValueError('observed callback absent from the snapshot directory')
        pointer = item['pointer_segment'], item['pointer_offset']
        if event['section'] != item['rtlink_reference']['section'] or \
                event['pointer'] != pointer:
            raise ValueError('observed callback trampoline differs from native snapshot')

    # This routine verifies the full resident scanner/gate-return/AX chain.
    # Event 0x0f *without* that scanner chain is only an event-0x0f call.
    scanner = callback_return_entries(lines)
    confirmed = []
    used = set()
    for entry in scanner:
        if not all(field in entry for field in ('callback_id', 'event', 'target_pointer',
                                               'section', 'call_ms')):
            raise ValueError('room scanner return lacks verified callback provenance')
        candidates = [(i, event) for i, event in enumerate(events)
                      if i not in used and event['callback_id'] == entry['callback_id']
                      and event['event'] == entry['event']
                      and event['section'] == entry['section']
                      and event['pointer'] == far_pointer(entry['target_pointer'])
                      and event['call_ms'] == entry['call_ms']
                      and event['ax'] == entry['target']
                      and event['return_ms'] <= entry['ms']]
        if len(candidates) != 1:
            raise ValueError('room scanner result does not match one native gate return')
        index, _ = candidates[0]
        used.add(index)
        confirmed.append(dict(**events[index], scanner_return_ms=entry['ms']))

    counts = Counter(event['callback_id'] for event in events)
    observed = []
    for cid, count in sorted(counts.items()):
        item = by_id[cid]
        ref = item['rtlink_reference']
        key = ref['section'], ref['target_segment'], ref['target_offset']
        total = calls.get(key, {}).get('calls', 0)
        if total < count:
            raise ValueError('gate-hook calls exceed verified RTLink manager calls')
        for_event = [event for event in events if event['callback_id'] == cid]
        outcomes = Counter((event['event'], event['ax']) for event in for_event)
        room = [event for event in confirmed if event['callback_id'] == cid]
        observed.append(dict(callback_id=cid, section=ref['section'],
                             original_file_offset=item['native_code']['file_offset'],
                             rtlink_invocations=total, verified_gate_calls=count,
                             other_rtlink_invocations=total - count,
                             confirmed_room_scanner_returns=len(room),
                             room_return_ax=[event['ax'] for event in room],
                             event_results=[dict(event=event, ax=ax, count=n)
                                            for (event, ax), n in sorted(outcomes.items())]))
    return dict(schema_version=1,
                summary=dict(total_rtlink_calls=census['validated_calls'],
                             distinct_rtlink_targets=census['distinct_targets'],
                             callback_gate_calls=len(events),
                             room_scanner_returns=len(confirmed),
                             other_callback_events=len(events) - len(confirmed),
                             callback_ids_observed=len(observed)),
                observed_callbacks=observed, room_scanner_events=confirmed,
                note='AX is the native callback result, not independently verified movement; '
                     'only returns paired with the resident scanner count as room exits.')


def format_text(report):
    s = report['summary']
    out = [f"RTLink calls: {s['total_rtlink_calls']} to "
           f"{s['distinct_rtlink_targets']} native targets",
           f"Callback gate calls: {s['callback_gate_calls']} across "
           f"{s['callback_ids_observed']} IDs; "
           f"{s['room_scanner_returns']} confirmed room scanner returns",
           'Native callback ID / event / AX counts:']
    for item in report['observed_callbacks']:
        out.append(f"  ID 0x{item['callback_id']:02x}, section {item['section']}, "
                   f"OVL:0x{item['original_file_offset']:x}: "
                   f"{item['rtlink_invocations']} trampoline calls, "
                   f"{item['verified_gate_calls']} gate calls, "
                   f"{item['confirmed_room_scanner_returns']} room exits")
        for outcome in item['event_results']:
            out.append(f"      event=0x{outcome['event']:04x} "
                       f"AX=0x{outcome['ax']:04x}: {outcome['count']} calls")
    out.append(report['note'])
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native_trace', type=Path)
    parser.add_argument('rtlink_calls', type=Path)
    parser.add_argument('callbacks_json', type=Path)
    parser.add_argument('--format', choices=['text', 'json'], default='text')
    args = parser.parse_args()
    try:
        calls, census = parse_calls(args.rtlink_calls.read_text().splitlines())
        report = audit(args.native_trace.read_text().splitlines(),
                       json.loads(args.callbacks_json.read_text()), calls, census)
        print(json.dumps(report, indent=2) if args.format == 'json' else format_text(report),
              end='\n' if args.format == 'json' else '')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

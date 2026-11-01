#!/usr/bin/env python3
"""Correlate Gateway actions with native object-state service calls.

Addresses are specific to the original Gateway replay, not a Legend-wide ABI.
The original trace has three verified section-5 callsites; newer traces also
hook shared state-service entries to capture changes inside nested helpers.
These are calls, not proof that an object record changed (a setter can no-op).
An absent effect does not establish that the action made no other changes.
"""
import argparse
from collections import defaultdict, deque
from pathlib import Path

from inspect_action_dispatch import KNOWN_ACTIONS


DISPATCH_CALL = 0x147c0
DISPATCH_RETURN = 0x147c5
SHARED_HANDLERS = {0x468ab: (0x1d, '4661:029b'),
                   0x46be7: (0x32, '4661:05d7'),
                   0x49121: (0x46, '4661:2b11'),
                   0x47b04: (0x53, '4661:14f4'),
                   0x47b82: (0x71, '4661:1572')}
# Verified static OVL callsites, available in traces made before service hooks.
MUTATION_SITES = {0x46a0c: (0x1d, 'clear'),
                  0x47d44: (0x71, 'set'),
                  0x47d59: (0x71, 'set')}
# Native far-call service entry: stack starts with return IP, CS, object, arg.
STATE_SERVICE_ENTRIES = {0x11950: 'set', 0x11983: 'clear',
                         0x11bbb: 'location-set'}
STATE_SERVICE_EVENTS = {'object-flag-set': ('set', 'property'),
                        'object-flag-clear': ('clear', 'property'),
                        'object-location-set': ('location-set', 'location')}


def parse_entry(line):
    if not line.startswith('entry '):
        return None
    fields = dict(field.split('=', 1) for field in line.split()[1:] if '=' in field)
    try:
        stack = [int(word, 16) for word in fields['stack'].split(',') if word]
        return dict(ms=int(fields['ms']), pc=int(fields['pc'], 16),
                    cs=int(fields['cs'], 16), ax=int(fields.get('ax', '0'), 16),
                    stack=stack, target=fields.get('action_target'))
    except (KeyError, ValueError):
        return None


def parse_state_change(line):
    """Parse an optional native state-service write annotation."""
    kind, _, remainder = line.partition(' ')
    if kind not in STATE_SERVICE_EVENTS:
        return None
    operation, field = STATE_SERVICE_EVENTS[kind]
    fields = dict(item.split('=', 1) for item in remainder.split() if '=' in item)
    try:
        changed = {'0': False, '1': True, 'unsupported': 'unsupported'}[
            fields['changed']]
        return (int(fields['ms']), operation, int(fields['object'], 16),
                int(fields[field], 16), changed)
    except (KeyError, ValueError):
        return None


def collect_effects(lines):
    """Associate native state calls with the current action dispatch frame."""
    lines = list(lines)
    entries = [entry for line in lines if (entry := parse_entry(line))]
    # A new trace provides service-entry hooks; avoid reporting the same call
    # twice from the OVL instruction and from the far-called service entry.
    has_service_hooks = any(entry['pc'] in STATE_SERVICE_ENTRIES and
                            entry['cs'] == 0x1157 and len(entry['stack']) >= 4
                            for entry in entries)
    active = []
    records = []
    pending = defaultdict(deque)
    for line in lines:
        entry = parse_entry(line)
        if not entry:
            change = parse_state_change(line)
            if change:
                ms, operation, obj, arg, changed = change
                queue = pending[(ms, operation, obj, arg)]
                if queue:
                    record, index = queue.popleft()
                    record['effect_changes'][index] = changed
            continue
        pc, cs, stack = entry['pc'], entry['cs'], entry['stack']
        if pc == DISPATCH_CALL and cs == 0x1476 and len(stack) >= 4:
            record = dict(ms=entry['ms'], action=stack[3], target=entry['target'],
                          handler=None, effects=[], effect_changes=[],
                          returned=None, return_ms=None)
            records.append(record)
            active.append(record)
        elif pc == DISPATCH_RETURN and cs == 0x1476 and len(stack) >= 4:
            if active and active[-1]['action'] == stack[3]:
                record = active.pop()
                record['returned'] = entry['ax']
                record['return_ms'] = entry['ms']
        elif active and pc in SHARED_HANDLERS and cs == 0x4661:
            action, handler = SHARED_HANDLERS[pc]
            if active[-1]['action'] == action:
                active[-1]['handler'] = handler
        elif active and pc in STATE_SERVICE_ENTRIES and cs == 0x1157 and len(stack) >= 4:
            record = active[-1]
            effect = (entry['ms'], STATE_SERVICE_ENTRIES[pc], stack[2], stack[3])
            pending[effect].append((record, len(record['effects'])))
            record['effects'].append(effect)
            record['effect_changes'].append(None)
        elif (active and not has_service_hooks and pc in MUTATION_SITES and
              cs == 0x4661 and len(stack) >= 2):
            action, operation = MUTATION_SITES[pc]
            if active[-1]['action'] == action and active[-1]['handler']:
                active[-1]['effects'].append((entry['ms'], operation,
                                              stack[0], stack[1]))
                active[-1]['effect_changes'].append(None)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--changed-only', action='store_true',
                        help='show only bytes that changed; requires --trace-state-writes')
    args = parser.parse_args()
    records = collect_effects(args.trace.read_text().splitlines())
    if not records:
        parser.error('no native action dispatches; check the 147c0 entry hook')
    if args.changed_only and not any(change is not None for record in records
                                     for change in record['effect_changes']):
        parser.error('--changed-only requires a trace with state-write annotations')
    print('ms     action  typed_input    section5_handler  observed_effects  return')
    for record in records:
        action = record['action']
        effects = []
        for (ms, operation, obj, prop), changed in zip(record['effects'],
                                                       record['effect_changes']):
            if args.changed_only and changed is not True:
                continue
            detail = f'{operation}(0x{obj:03x},0x{prop:02x})@{ms}'
            if changed is True:
                detail += '[changed]'
            elif changed is False:
                detail += '[unchanged]'
            elif changed == 'unsupported':
                detail += '[unsupported]'
            effects.append(detail)
        effects = ', '.join(effects) or '-'
        returned = ('?' if record['returned'] is None else
                    f"0x{record['returned']:x}@{record['return_ms']}")
        print(f"{record['ms']:6}  0x{action:02x}    "
              f"{KNOWN_ACTIONS.get(action, '?'):<14} "
              f"{(record['handler'] or '-'):>16}  {effects:<33} {returned}")
    print('Effects are observed service invocations (or legacy OVL callsites); '
          '[changed] and [unchanged] require verified native service deltas. '
          'This is not a complete side-effect list; '
          'input labels come from the bounded scripted replay.')


if __name__ == '__main__':
    main()

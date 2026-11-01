#!/usr/bin/env python3
"""Correlate native Gateway action dispatches with verified section-5 flag calls.

Addresses are specific to the original Gateway replay, not a Legend-wide ABI.
Only explicitly hooked mutation callsites are classified; an absent effect
does not establish that an action made no other state changes.
"""
import argparse
from pathlib import Path

from inspect_action_dispatch import KNOWN_ACTIONS


DISPATCH_CALL = 0x147c0
DISPATCH_RETURN = 0x147c5
SHARED_HANDLERS = {0x468ab: (0x1d, '4661:029b'),
                   0x47b82: (0x71, '4661:1572')}
# The second OPEN setter (47d59) was recovered from OVL file offset eb19.
# It was not included in the original native trace: a new replay is needed.
MUTATION_SITES = {0x46a0c: (0x1d, 'clear'),
                  0x47d44: (0x71, 'set'),
                  0x47d59: (0x71, 'set')}


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


def collect_effects(lines):
    """Return action records with only source-verified flag mutation callsites."""
    active = []
    records = []
    for line in lines:
        entry = parse_entry(line)
        if not entry:
            continue
        pc, cs, stack = entry['pc'], entry['cs'], entry['stack']
        if pc == DISPATCH_CALL and cs == 0x1476 and len(stack) >= 4:
            record = dict(ms=entry['ms'], action=stack[3], target=entry['target'],
                          handler=None, effects=[], returned=None, return_ms=None)
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
        elif active and pc in MUTATION_SITES and cs == 0x4661 and len(stack) >= 2:
            action, operation = MUTATION_SITES[pc]
            if active[-1]['action'] == action and active[-1]['handler']:
                active[-1]['effects'].append((entry['ms'], operation,
                                              stack[0], stack[1]))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    records = collect_effects(args.trace.read_text().splitlines())
    if not records:
        parser.error('no native action dispatches; check the 147c0 entry hook')
    print('ms     action  typed_input    section5_handler  observed_effects  return')
    for record in records:
        action = record['action']
        effects = ', '.join(f'{operation}(0x{obj:03x},0x{prop:02x})@{ms}'
                            for ms, operation, obj, prop in record['effects']) or '-'
        returned = ('?' if record['returned'] is None else
                    f"0x{record['returned']:x}@{record['return_ms']}")
        print(f"{record['ms']:6}  0x{action:02x}    "
              f"{KNOWN_ACTIONS.get(action, '?'):<14} "
              f"{(record['handler'] or '-'):>16}  {effects:<33} {returned}")
    print('Effects are callsite observations, not a complete side-effect list; '
          'input labels come from the bounded scripted replay.')


if __name__ == '__main__':
    main()

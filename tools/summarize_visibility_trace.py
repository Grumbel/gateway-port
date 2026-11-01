#!/usr/bin/env python3
"""Group native Gateway parser child-slot probes by scripted command window.

This reports the result of the original 0385:0a9a predicate and the separate
0157:0825 child accessor, not whether a player command resolved or succeeded.
Windows are timing associations, not a reconstruction of parser call frames.
"""
import argparse
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

from story_scenarios import DEFAULT_OBJECT_COMMANDS, OBJECT_SLOTS, parse_sequence

SITES = {'gate-call': 0x10063, 'gate-return': 0x10068,
         'child-call': 0x10075, 'child-return': 0x1007a}


def parse_visibility(line):
    if not line.startswith('entry ') or ' visibility_stage=' not in line:
        return None
    fields = dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)
    try:
        stage = fields['visibility_stage']
        if stage not in SITES or int(fields['cs'], 16) != 0x1000 or \
                int(fields['pc'], 16) != SITES[stage]:
            raise ValueError('visibility annotation does not match resident callsite')
        if 'visibility_probe' in fields:
            raise ValueError('candidate probe code signature mismatch')
        result_key = {'gate-return': 'visibility_result',
                      'child-return': 'visibility_child'}.get(stage)
        return dict(ms=int(fields['ms']), stage=stage,
                    object=int(fields['visibility_object'], 16),
                    slot=int(fields['visibility_slot'], 16),
                    restricted=int(fields['visibility_restricted'], 16),
                    result=int(fields[result_key], 16) if result_key else None)
    except (KeyError, ValueError) as error:
        raise ValueError(f'invalid visibility entry: {error}: {line[:160]}') from error


def summarize(lines, commands=DEFAULT_OBJECT_COMMANDS, focus=None):
    if len(commands) != len(OBJECT_SLOTS):
        raise ValueError('expected seven scripted commands')
    grouped = defaultdict(lambda: dict(gate_calls=0, gate_0=0, gate_1=0,
                                       child_calls=0, children=set()))
    total = 0
    for line in lines:
        event = parse_visibility(line)
        if not event:
            continue
        total += 1
        if focus is not None and event['object'] != focus:
            continue
        phase = bisect_right(OBJECT_SLOTS, event['ms'])
        key = (phase, event['object'], event['slot'], event['restricted'])
        group = grouped[key]
        if event['stage'] == 'gate-call':
            group['gate_calls'] += 1
        elif event['stage'] == 'gate-return':
            if event['result'] not in (0, 1):
                raise ValueError('candidate eligibility helper returned non-Boolean AX')
            group[f"gate_{event['result']}"] += 1
        elif event['stage'] == 'child-call':
            group['child_calls'] += 1
        else:
            group['children'].add(event['result'])
    if not total:
        raise ValueError('no candidate child-slot entries: check --trace-visibility and build')
    if not grouped:
        raise ValueError('no child-slot entries for requested object')
    result = ["phase          object  slot  restricted  gate (deny/allow)  child IDs (returned)",
              "--------------  ------  ----  ----------  -----------------  --------------------"]
    for (phase, obj, slot, restricted), group in sorted(grouped.items()):
        label = 'startup/LOOK' if phase == 0 else commands[phase - 1]
        children = ', '.join(f'0x{x:03x}' for x in sorted(group['children'])) or '-'
        result.append(f"{label[:14]:14}  0x{obj:03x}   {slot:>3}      {restricted:>4}      "
                      f"{group['gate_0']:>5}/{group['gate_1']:<5}       {children}")
        if group['gate_calls'] != group['gate_0'] + group['gate_1'] or \
                group['child_calls'] < len(group['children']):
            result.append('  [warning: incomplete probe call/return observations]')
    result.append('Rows group observations in scheduled input windows; a returned child ID '
                  'does not prove parser visibility or action success. '
                  'The gate is only called on restricted traversals.')
    return '\n'.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--commands', help='seven scripted comma-separated commands; '
                        'defaults to the objects scenario')
    parser.add_argument('--object', type=lambda value: int(value, 0),
                        help='filter to an object ID, e.g. 0x124 for the drawer')
    args = parser.parse_args()
    try:
        commands = (parse_sequence(args.commands) if args.commands is not None
                    else DEFAULT_OBJECT_COMMANDS)
        print(summarize(args.trace.read_text().splitlines(), commands, args.object))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

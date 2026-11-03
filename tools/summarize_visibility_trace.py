#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
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
            if fields['visibility_probe'] != 'code-mismatch':
                raise ValueError('unknown candidate probe diagnostic')
            # An address alone does not identify a resident instruction.
            # Early versions also compared the runtime far CALL segment
            # immediate against the original unrelocated EXE: those calls
            # failed even during gameplay. Never interpret such hits as
            # visibility decisions, but retain valid return observations.
            return dict(ms=int(fields['ms']), stage=stage, unverified=True,
                        code=fields.get('visibility_code'))
        result_key = {'gate-return': 'visibility_result',
                      'child-return': 'visibility_child'}.get(stage)
        return dict(ms=int(fields['ms']), stage=stage,
                    object=int(fields['visibility_object'], 16),
                    slot=int(fields['visibility_slot'], 16),
                    restricted=int(fields['visibility_restricted'], 16),
                    result=int(fields[result_key], 16) if result_key else None,
                    unverified=False)
    except (KeyError, ValueError) as error:
        raise ValueError(f'invalid visibility entry: {error}: {line[:160]}') from error


def summarize(lines, commands=DEFAULT_OBJECT_COMMANDS, focus=None, details=False):
    if len(commands) != len(OBJECT_SLOTS):
        raise ValueError('expected seven scripted commands')
    grouped = defaultdict(lambda: dict(gate_calls=0, gate_0=0, gate_1=0,
                                       child_calls=0, child_returns=0,
                                       children=set()))
    total = 0
    unverified = []
    observations = []
    for line in lines:
        event = parse_visibility(line)
        if not event:
            continue
        if event['unverified']:
            unverified.append(event)
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
            group['child_returns'] += 1
            group['children'].add(event['result'])
        if event['stage'] in ('gate-return', 'child-return'):
            observations.append(event)
    if not total:
        detail = (f'; skipped {len(unverified)} code-signature mismatches '
                  f'(first ms={unverified[0]["ms"]}, last ms={unverified[-1]["ms"]})'
                  if unverified else '')
        if unverified and unverified[-1]['code']:
            detail += f'; last mismatched bytes={unverified[-1]["code"]}'
        raise ValueError('no verified candidate child-slot entries' + detail +
                         '; check whether the resident code matches the expected addresses')
    if not grouped:
        raise ValueError('no child-slot entries for requested object')
    result = []
    if unverified:
        result.append(f'Skipped {len(unverified)} unverified code-address hits '
                      f'(ms={unverified[0]["ms"]}–{unverified[-1]["ms"]}); '
                      'these are not parser decisions.')
    result += ["phase          object  slot  restricted  gate (deny/allow)  child IDs (returned)",
               "--------------  ------  ----  ----------  -----------------  --------------------"]
    for (phase, obj, slot, restricted), group in sorted(grouped.items()):
        label = 'startup/LOOK' if phase == 0 else commands[phase - 1]
        children = ', '.join(f'0x{x:03x}' for x in sorted(group['children'])) or '-'
        result.append(f"{label[:14]:14}  0x{obj:03x}   {slot:>3}      {restricted:>4}      "
                      f"{group['gate_0']:>5}/{group['gate_1']:<5}       {children}")
        # Bundles <=061 rejected the relocated far CALL instructions but
        # captured their return sites. Zero call observations therefore
        # cannot justify declaring these valid return records "incomplete".
        if (group['gate_calls'] and
                group['gate_calls'] != group['gate_0'] + group['gate_1']) or \
                (group['child_calls'] and
                 group['child_calls'] != group['child_returns']):
            result.append('  [warning: incomplete probe call/return observations]')
    if details:
        result.extend(('', 'ms     command window  object  slot  restricted  stage         value',
                       '------  --------------  ------  ----  ----------  ------------  -----'))
        for event in observations:
            phase = bisect_right(OBJECT_SLOTS, event['ms'])
            label = 'startup/LOOK' if phase == 0 else commands[phase - 1]
            value = (str(event['result']) if event['stage'] == 'gate-return'
                     else f'0x{event["result"]:03x}')
            result.append(f'{event["ms"]:>6}  {label[:14]:14}  '
                          f'0x{event["object"]:03x}   {event["slot"]:>3}      '
                          f'{event["restricted"]:>4}      '
                          f'{event["stage"]:12}  {value}')
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
    parser.add_argument('--details', action='store_true',
                        help='show each verified gate and child return with its guest timestamp')
    args = parser.parse_args()
    try:
        commands = (parse_sequence(args.commands) if args.commands is not None
                    else DEFAULT_OBJECT_COMMANDS)
        print(summarize(args.trace.read_text().splitlines(), commands, args.object,
                        details=args.details))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

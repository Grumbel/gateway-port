#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Summarize one original Gateway picker trace without inventing click acceptance.

The detailed trace already records native picker returns, nearby guest mouse
reads, bounded caller instructions/stores and later *temporal* dispatcher
candidates. This report puts that evidence together per selection, so an
unmarked scene click can be inspected independently of unrelated GUI picks.
"""

import argparse
from collections import Counter
from pathlib import Path

from compare_picker_follow import read_trace


SOURCES = ('room', 'control', 'fallback', 'unmapped', 'unresolved',
           'none', 'unmodeled', 'unknown')
VERDICTS = ('match', 'mismatch', 'unmodeled')


def parse_picker_calls(path):
    """Accept earlier traces, but validate the provenance fields when present."""
    calls = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.startswith('# call='):
            continue
        parts = line.split()[1:]
        if any(part.count('=') != 1 for part in parts):
            raise ValueError('malformed native picker call marker')
        details = dict(part.split('=', 1) for part in parts)
        if len(details) != len(parts):
            raise ValueError('duplicate native picker call field')
        try:
            call = int(details['call'])
            ms = int(details['ms'])
            x, y = (int(details[key]) for key in ('x', 'y'))
            source = details.get('source', 'unknown')
            if source not in SOURCES:
                raise ValueError('invalid source')
            if not (call > 0 and ms >= 0 and -32768 <= x <= 65535 and
                    -32768 <= y <= 65535):
                raise ValueError('invalid call position or timestamp')
            if any(key in details for key in ('group', 'slot', 'kind', 'native_ax')):
                if not all(key in details for key in ('group', 'slot', 'kind', 'native_ax')):
                    raise ValueError('incomplete native selector')
                if any(not 0 <= int(details[key]) <= 0xffff for key in
                       ('group', 'slot', 'kind', 'native_ax')):
                    raise ValueError('invalid native selector')
            if any(key in details for key in ('object', 'overlaps', 'verdict')):
                if not all(key in details for key in ('object', 'overlaps', 'verdict')):
                    raise ValueError('incomplete picker provenance')
                object_id = int(details['object'])
                overlaps = int(details['overlaps'])
                verdict = details['verdict']
                if (not 0 <= object_id <= 0xffff or overlaps < 0 or
                    verdict not in VERDICTS or (source != 'room' and object_id)):
                    raise ValueError('invalid picker provenance')
            else:
                object_id = None
                overlaps = None
                verdict = None
        except (KeyError, ValueError) as error:
            raise ValueError('invalid native picker call marker') from error
        if call in calls:
            raise ValueError('duplicate native picker call')
        calls[call] = dict(details, ms=ms, x=x, y=y, source=source,
                           object=object_id, overlaps=overlaps, verdict=verdict)
    return calls


def context_reference(object_id, events):
    """Distinguish object references from mere time proximity, not causality."""
    if not object_id:
        return 'unknown'
    contexts = [event for event in events if event[0] == 'context']
    if not contexts:
        return 'no-context'
    if any(object_id in (event[2], event[3]) for event in contexts):
        return 'object-referenced'
    return 'other-context-objects'


def describe_event(event):
    kind, action, first, second, cs, ip, age = event
    if kind == 'context':
        return (f'context action=0x{action:x} objects=0x{first:x},0x{second:x} '
                f'age={age}ms')
    return f'action-table action=0x{action:x} target={cs:x}:{ip:x} age={age}ms'


def report(path, *, source=None, limit=40):
    if limit < 0:
        raise ValueError('limit must be nonnegative')
    instructions, endings, writes, dispatches, inputs = read_trace(path)
    selections = parse_picker_calls(path)
    if not selections:
        raise ValueError('trace has no native picker call markers')
    if max(selections) != len(selections) or min(selections) != 1:
        raise ValueError('noncontiguous picker call markers')
    if max(instructions) > len(selections):
        raise ValueError('instruction call exceeds picker call markers')
    sources = Counter(selection['source'] for selection in selections.values())
    verdicts = Counter(selection['verdict'] for selection in selections.values())
    all_dispatches = dispatches or {}
    references = Counter(context_reference(selection['object'],
                         all_dispatches.get(call, []))
                         for call, selection in selections.items()
                         if selection['source'] == 'room' and selection['object'])
    missing_room_object = sum(selection['source'] == 'room' and
                              selection['object'] is None
                              for selection in selections.values())
    context_summary = (', '.join(f'{key}={references[key]}' for key in
                                 ('object-referenced', 'other-context-objects', 'no-context')
                                 if references[key]) or 'no identified room objects')
    if missing_room_object:
        context_summary += f'; object identity unavailable for {missing_room_object} older calls'
    output = [f'Original native picker calls: {len(selections)}',
              'Sources: ' + ', '.join(f'{name}={sources[name]}'
                                     for name in SOURCES if sources[name]),
              'Model prediction: ' +
              (', '.join(f'{name}={verdicts[name]}' for name in VERDICTS
                         if verdicts[name]) or 'unavailable (older trace)'),
              f'Continuation samples: {sum(map(len, instructions.values()))} instructions',
              'Dispatcher candidates (temporal only): ' +
              (str(sum(map(len, all_dispatches.values()))) if dispatches is not None
               else 'unavailable (older trace)'),
              'Recent guest mouse reads: ' +
              (f'{sum(value is not None for value in inputs.values())}/{len(selections)}'
               if inputs is not None else 'unavailable (older trace)'),
              'Room-object context evidence: ' + context_summary,
              '']
    chosen = [(call, selection) for call, selection in sorted(selections.items())
              if source is None or selection['source'] == source]
    count = len(chosen) if limit == 0 else min(limit, len(chosen))
    for call, item in chosen[:count]:
        name = item['source']
        object_id = item['object']
        subject = (' object=unavailable' if object_id is None else
                   ' object=anonymous' if object_id == 0 else
                   f' object=0x{object_id:x}') if name == 'room' else ''
        verdict = f' verdict={item["verdict"]}' if item['verdict'] else ''
        overlaps = (f' overlaps={item["overlaps"]}'
                    if item['overlaps'] is not None else '')
        selector = (f' AX={item["native_ax"]} g={item["group"]} '
                    f'slot={item["slot"]} kind={item["kind"]}'
                    if 'group' in item else '')
        output.append(f'Call {call} at {item["ms"]}ms ({item["x"]},{item["y"]}): '
                      f'{name}{subject}{selector}{verdict}{overlaps}')
        if inputs is not None:
            read = inputs.get(call)
            if read is None:
                output.append('  Guest mouse: no recent read')
            else:
                func, button, buttons, presses, x, y, cs, ip, seq, age, coords = read
                output.append(f'  Guest mouse: INT 33h/{func} button={button} '
                              f'buttons={buttons} count={presses} position={x},{y} '
                              f'coords={coords} age={age}ms')
        evidence = all_dispatches.get(call, [])
        if dispatches is not None:
            output.append('  Context object link: ' + context_reference(object_id, evidence))
            for event in evidence:
                output.append('  Candidate: ' + describe_event(event))
            if not evidence:
                output.append('  Candidates: none at the two monitored resident sites')
        result = endings.get(call, ('pending', 0))[0]
        stores = len(writes.get(call, [])) if writes is not None else 'unavailable'
        output.append(f'  Follow: {len(instructions.get(call, []))} instructions '
                      f'({result}), observed CPU stores={stores}')
    if len(chosen) > count:
        output.append(f'... {len(chosen) - count} more matching calls; use --limit 0')
    output.extend(['', 'Object references, guest reads, CPU stores and nearby dispatches '
                   'are observations, not proof of click acceptance.'])
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='--trace-picker-follow TSV')
    parser.add_argument('--source', choices=SOURCES,
                        help='show only calls with this actual native source')
    parser.add_argument('--limit', type=int, default=40,
                        help='maximum calls to detail (0 means all; default: 40)')
    options = parser.parse_args()
    try:
        print(report(options.trace, source=options.source, limit=options.limit))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

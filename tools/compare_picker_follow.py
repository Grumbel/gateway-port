#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare bounded original-code continuations after two native mouse picks.

Only execution addresses and instruction bytes establish a shared code path.
Matching code paths or different branches do NOT prove action acceptance.
"""

import argparse
import csv
from pathlib import Path
import re


FIELDS = ('call', 'step', 'ms', 'cs', 'ip', 'linear', 'bytes', 'ax', 'bx',
          'cx', 'dx', 'si', 'di', 'bp', 'ss', 'sp', 'flags')


def read_trace(path):
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    summaries = [line for line in lines if line.startswith('# follow-summary ')]
    if len(summaries) != 1:
        raise ValueError('missing or duplicate continuation summary')
    summary = dict(field.split('=', 1) for field in summaries[0].split()[2:])
    required = {'calls', 'completed', 'interrupted', 'missed', 'errors', 'pending', 'steps'}
    if set(summary) != required:
        raise ValueError('unexpected continuation summary fields')
    summary = {key: int(value) for key, value in summary.items()}
    if min(summary.values()) < 0 or summary['pending'] > 1 or summary['steps'] < 1:
        raise ValueError('invalid continuation summary values')
    if (summary['completed'] + summary['interrupted'] + summary['missed'] +
        summary['errors'] + summary['pending'] != summary['calls']):
        raise ValueError('continuation summary does not account for every call')
    endings = {}
    for line in lines:
        if not line.startswith('# end call='):
            continue
        details = dict(field.split('=', 1) for field in line.split()[2:])
        if set(details) != {'call', 'status', 'steps'}:
            raise ValueError('invalid continuation end marker')
        call = int(details['call'])
        if call in endings or details['status'] not in ('complete', 'interrupted', 'missed-return'):
            raise ValueError('duplicate or invalid continuation end marker')
        endings[call] = (details['status'], int(details['steps']))
    for kind, expected in (('complete', summary['completed']),
                           ('interrupted', summary['interrupted']),
                           ('missed-return', summary['missed'])):
        if sum(status == kind for status, _ in endings.values()) != expected:
            raise ValueError('continuation end markers disagree with summary')
    rows = [line for line in lines if line and not line.startswith('#')]
    reader = csv.DictReader(rows, delimiter='\t')
    if tuple(reader.fieldnames or ()) != FIELDS:
        raise ValueError('unexpected picker continuation columns')
    calls = {}
    for line_number, row in enumerate(reader, 2):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f'invalid continuation row {line_number}')
        call, step = int(row['call']), int(row['step'])
        if call <= 0 or step != len(calls.get(call, ())):
            raise ValueError(f'noncontiguous step or invalid call on row {line_number}')
        if not re.fullmatch('[0-9a-f]{2,30}', row['bytes']) or len(row['bytes']) % 2:
            raise ValueError(f'invalid original instruction bytes on row {line_number}')
        for field in ('cs', 'ip', 'linear', 'ax', 'bx', 'cx', 'dx', 'si', 'di',
                      'bp', 'ss', 'sp', 'flags'):
            int(row[field], 16)
        calls.setdefault(call, []).append(row)
    if not calls:
        raise ValueError('trace has no completed continuation instructions')
    if max(calls) > summary['calls'] or len(endings) > summary['calls']:
        raise ValueError('continuation call IDs exceed summary')
    for call, (status, count) in endings.items():
        if count != len(calls.get(call, ())) or (status == 'complete' and
                                                  count != summary['steps']):
            raise ValueError('continuation end marker step count mismatch')
    return calls, endings


def describe(row):
    return (f"{row['cs']}:{row['ip']} linear=0x{row['linear']} "
            f"bytes={row['bytes']} AX=0x{row['ax']} flags=0x{row['flags']}")


def compare(first, second, left_call=None, right_call=None):
    left_calls, left_endings = read_trace(first)
    right_calls, right_endings = read_trace(second)
    if left_call is None:
        left_call = min(left_calls)
    if right_call is None:
        right_call = min(right_calls)
    if left_call not in left_calls or right_call not in right_calls:
        raise ValueError('requested call has no captured continuation steps')
    left, right = left_calls[left_call], right_calls[right_call]
    output = [f'Left: call {left_call}, {len(left)} instructions; '
              f'right: call {right_call}, {len(right)} instructions']
    if (left_endings.get(left_call, ('pending',))[0] != 'complete' or
        right_endings.get(right_call, ('pending',))[0] != 'complete'):
        output.append('Warning: one or both continuation windows are incomplete.')
    count = 0
    for a, b in zip(left, right):
        if (a['linear'], a['bytes']) != (b['linear'], b['bytes']):
            break
        count += 1
    output.append(f'Identical executed instruction prefix: {count}')
    if count == 0:
        output.append('Warning: different immediate return paths; compare equivalent caller states.')
    if count < min(len(left), len(right)):
        output.append(f'First control-flow/code divergence at step {count}:')
        output.append(f'  left:  {describe(left[count])}')
        output.append(f'  right: {describe(right[count])}')
    elif len(left) != len(right):
        output.append('One trace ended inside the shared instruction prefix; '
                      'capture more steps before inferring a branch difference.')
    else:
        output.append('No code-path divergence inside the captured window.')
    output.append('No claim of click dispatch or action acceptance is implied.')
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('--left-call', type=int)
    parser.add_argument('--right-call', type=int)
    args = parser.parse_args()
    try:
        print(compare(args.left, args.right, args.left_call, args.right_call))
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

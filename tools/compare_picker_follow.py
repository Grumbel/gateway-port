#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare bounded original-code continuations after two native mouse picks.

Execution addresses and instruction bytes establish a shared code path; guest
CPU stores provide additional side-effect evidence. Neither proves acceptance.
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
    write_fields = {'writes', 'write_errors'}
    has_writes = set(summary) == required | write_fields
    if not has_writes and set(summary) != required:
        raise ValueError('unexpected continuation summary fields')
    summary = {key: int(value) for key, value in summary.items()}
    if min(summary.values()) < 0 or summary['pending'] > 1 or summary['steps'] < 1:
        raise ValueError('invalid continuation summary values')
    if (summary['completed'] + summary['interrupted'] + summary['missed'] +
        summary['errors'] + summary['pending'] != summary['calls']):
        raise ValueError('continuation summary does not account for every call')
    if has_writes and summary['write_errors']:
        raise ValueError('continuation contains failed memory-write observations')
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
    writes = {}
    for line in lines:
        if not line.startswith('# write '):
            continue
        if not has_writes:
            raise ValueError('write event without write-enabled summary')
        fields = line.split()[2:]
        if len(fields) != 6 or any('=' not in field for field in fields):
            raise ValueError('malformed continuation write marker')
        details = dict(field.split('=', 1) for field in fields)
        if set(details) != {'call', 'step', 'ms', 'linear', 'size', 'value'}:
            raise ValueError('invalid continuation write marker fields')
        try:
            call, step, ms = (int(details[key]) for key in ('call', 'step', 'ms'))
            address, value = (int(details[key], 16) for key in ('linear', 'value'))
            size = int(details['size'])
        except ValueError as error:
            raise ValueError('invalid continuation write marker value') from error
        if (call not in calls or step < 0 or step >= len(calls[call]) or ms < 0 or
            address < 0 or not 1 <= size <= 8 or not 0 <= value < 1 << (8 * size) or
            not all(re.fullmatch('[0-9a-f]+', details[key]) for key in ('linear', 'value'))):
            raise ValueError('invalid continuation write address, width, or sample')
        writes.setdefault(call, []).append((step, address, size, value))
    if has_writes and sum(map(len, writes.values())) != summary['writes']:
        raise ValueError('continuation write count disagrees with summary')
    if any(line.startswith('# write-error ') for line in lines):
        raise ValueError('continuation contains failed memory-write observations')
    return calls, endings, writes if has_writes else None


def describe(row):
    return (f"{row['cs']}:{row['ip']} linear=0x{row['linear']} "
            f"bytes={row['bytes']} AX=0x{row['ax']} flags=0x{row['flags']}")


def describe_write(write):
    step, address, size, value = write
    return f'step={step} address=0x{address:x} size={size} value=0x{value:x}'


def compare(first, second, left_call=None, right_call=None):
    left_calls, left_endings, left_writes = read_trace(first)
    right_calls, right_endings, right_writes = read_trace(second)
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
    if left_writes is None or right_writes is None:
        output.append('Guest-store comparison unavailable: one or both traces use the legacy format.')
    else:
        a_stores = left_writes.get(left_call, [])
        b_stores = right_writes.get(right_call, [])
        output.append(f'Observed guest CPU stores: left={len(a_stores)}, right={len(b_stores)}')
        common = 0
        for a, b in zip(a_stores, b_stores):
            if a != b:
                break
            common += 1
        if common < min(len(a_stores), len(b_stores)):
            output.append(f'First guest-store divergence at event {common}:')
            output.append(f'  left:  {describe_write(a_stores[common])}')
            output.append(f'  right: {describe_write(b_stores[common])}')
        elif len(a_stores) != len(b_stores):
            remaining = a_stores if len(a_stores) > len(b_stores) else b_stores
            side = 'left' if len(a_stores) > len(b_stores) else 'right'
            output.append(f'First guest-store divergence at event {common}:')
            output.append(f'  only {side}: {describe_write(remaining[common])}')
        elif a_stores:
            output.append('No guest-store divergence inside the captured window.')
        else:
            output.append('No guest CPU stores observed inside either captured window.')
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

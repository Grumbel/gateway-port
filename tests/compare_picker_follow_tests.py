#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic continuation trace parsing and first-divergence checks."""

import importlib.util
from pathlib import Path
import tempfile


SCRIPT = Path(__file__).resolve().parents[1] / 'tools/compare_picker_follow.py'
SPEC = importlib.util.spec_from_file_location('compare_picker_follow', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def row(call, step, ip, opcode, ax='1'):
    columns = [str(call), str(step), '1200', '4100', ip, ip, opcode, ax,
               '0', '0', '0', '0', '0', '0', '5200', '100', '202']
    return '\t'.join(columns)


def make(path, rows):
    calls = max(int(row.split('\t', 1)[0]) for row in rows)
    per_call = {}
    for item in rows:
        call = int(item.split('\t', 1)[0])
        per_call[call] = per_call.get(call, 0) + 1
    endings = ''.join(f'# end call={call} status={"complete" if count == 3 else "interrupted"} '
                      f'steps={count}\n' for call, count in per_call.items())
    missing = calls - len(per_call)
    endings += ''.join(f'# end call={call} status=missed-return steps=0\n'
                       for call in range(1, calls + 1) if call not in per_call)
    completed = sum(count == 3 for count in per_call.values())
    path.write_text('\t'.join(MODULE.FIELDS) + '\n' + '\n'.join(rows) + '\n' +
                    endings + f'# follow-summary calls={calls} completed={completed} '
                    f'interrupted={len(per_call) - completed} missed={missing} '
                    'errors=0 pending=0 steps=3\n')


with tempfile.TemporaryDirectory() as dirname:
    left = Path(dirname) / 'left.tsv'
    right = Path(dirname) / 'right.tsv'
    make(left, [row(1, 0, '100', '85c0'), row(1, 1, '102', '7404'),
                row(1, 2, '104', '8b00')])
    make(right, [row(1, 0, '100', '85c0'), row(1, 1, '102', '7404'),
                 row(1, 2, '108', '8b00')])
    report = MODULE.compare(left, right)
    assert 'Identical executed instruction prefix: 2' in report
    assert 'First control-flow/code divergence at step 2' in report
    assert 'left:  4100:104' in report and 'right: 4100:108' in report
    assert 'No claim of click dispatch' in report
    make(right, [row(1, 0, '100', '85c0'), row(1, 1, '102', '7404'),
                 row(1, 2, '104', '8b00', '2')])
    assert 'No code-path divergence' in MODULE.compare(left, right)  # AX alone differs.
    make(right, [row(2, 0, '200', '9090')])
    assert 'different immediate return paths' in MODULE.compare(left, right, 1, 2)
    assert 'continuation windows are incomplete' in MODULE.compare(left, right, 1, 2)
    try:
        MODULE.compare(left, right, 1, 1)
    except ValueError as error:
        assert 'requested call' in str(error)
    else:
        raise AssertionError('missing call should fail')
    make(right, [row(1, 1, '100', '85c0')])
    try:
        MODULE.read_trace(right)
    except ValueError as error:
        assert 'noncontiguous' in str(error)
    else:
        raise AssertionError('noncontiguous steps should fail')
    make(right, [row(1, 0, '100', '85c0'), row(1, 1, '102', '7404'),
                 row(1, 2, '104', '8b00')])
    right.write_text(right.read_text().replace('completed=1', 'completed=0'))
    try:
        MODULE.read_trace(right)
    except ValueError as error:
        assert 'summary does not account' in str(error)
    else:
        raise AssertionError('summary mismatch should fail')

print('Gateway post-picker continuation comparison tests passed')

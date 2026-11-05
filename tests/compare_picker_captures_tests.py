#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Private capture batch validation without original Gateway data/Unicorn."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from compare_picker_captures import analyze_captures  # noqa: E402


def capture(directory, number, first_flags, second_flags):
    base = directory / f'call-{number:04d}'
    x, y, slot = (285, 50, 1) if first_flags == 0 else (320, 50, 2)
    registers = {'AX': 0, 'BX': 0, 'CX': 0, 'DX': 0, 'CS': 0x2649,
                 'IP': 0x541, 'SS': 0, 'SP': 0, 'DS': 0x3123,
                 'ES': 0, 'BP': 0, 'FLAGS': 0}
    metadata = [f'format=gateway-picker-entry-v1', f'call={number}',
                f'guest_ms={number * 1000}', f'x={x}', f'y={y}',
                'return_cs=0x4100', 'return_ip=0x0123',
                'picture=GATE_102.PIC', 'picture_entry=15']
    metadata.extend(f'{key}=0x{value:x}' for key, value in registers.items())
    base.with_suffix('.meta').write_text('\n'.join(metadata) + '\n', encoding='ascii')
    base.with_suffix('.bin').write_bytes(bytes(0x100000))
    rows = [
        (0, 0, 0, 1, 0, 0, 0, 639, 479, 'fallback', 0),
        (0, 1, 1, 3, first_flags, 280, 40, 300, 60, 'room', 292),
        (0, 2, 2, 3, second_flags, 301, 40, 321, 60, 'room', 292),
        (1, 0, 3, 1, 0, 350, 0, 400, 20, 'fallback', 0),
        (1, 1, 4, 5, 0, 305, 45, 312, 55, 'control', 0),
    ]
    header = (f'# call={number} ms={number * 1000} x={x} y={y} '
              f'native_ax=1 group=0 slot={slot} kind=3 source=room '
              'object=292 overlaps=1 verdict=match return=4100:0123')
    scene = (f'# scene call={number} picture=GATE_102.PIC entry=15 aligned=1 '
             f'room_group=0 boxes=2 rows={len(rows)}')
    result = [header, scene]
    for group, index, native_index, kind, flags, x0, y0, x1, y1, role, obj in rows:
        result.append(f'# picker-row call={number} group={group} slot={index} '
                      f'index={native_index} kind={kind} flags={flags} '
                      f'x0={x0} y0={y0} x1={x1} y1={y1} role={role} '
                      f'object={obj} selected={int(group == 0 and index == slot)}')
    base.with_suffix('.result').write_text('\n'.join(result) + '\n', encoding='ascii')
    return base


def must_fail(directory, fragment):
    try:
        analyze_captures(directory)
    except ValueError as error:
        assert fragment in str(error), str(error)
    else:
        raise AssertionError(f'expected failure containing {fragment!r}')


with tempfile.TemporaryDirectory(prefix='gateway-picker-capture-test-') as temporary:
    directory = Path(temporary) / 'captures'
    directory.mkdir()
    first = capture(directory, 1, 0, 128)
    second = capture(directory, 2, 128, 0)
    third = capture(directory, 3, 128, 0)
    summary = analyze_captures(directory)
    assert summary['captured_calls'] == 3
    assert summary['aligned_calls'] == 3
    assert summary['unique_aligned_states'] == 2
    assert summary['conditional_transitions'] == 1
    assert summary['compared_transitions'] == 1
    comparison = summary['comparisons'][0]
    assert (comparison['before_call'], comparison['after_call']) == (1, 2)
    assert comparison['h_changed_pixels'] == 882
    assert comparison['native_changed_pixels'] > 0
    assert all(261 <= point['x'] < 640 and 0 <= point['y'] < 232
               for point in comparison['samples'])

    trace = Path(temporary) / 'private-picker-merged.tsv'
    summary = analyze_captures(directory, trace_output=trace)
    assert trace.exists() and '# capture call=3 base=call-0003' in trace.read_text()
    try:
        analyze_captures(directory, trace_output=trace)
    except ValueError as error:
        assert 'overwrite' in str(error)
    else:
        raise AssertionError('existing trace was overwritten')
    cmd = subprocess.run([sys.executable, str(TOOLS / 'compare_picker_captures.py'),
                          str(directory), '--max-pairs', '1', '--json'],
                         capture_output=True, text=True, check=True)
    assert json.loads(cmd.stdout)['conditional_transitions'] == 1
    trace.unlink()

    second.with_suffix('.result').unlink()
    must_fail(directory, 'never returned')
    capture(directory, 2, 128, 0)
    third.with_suffix('.bin').write_bytes(b'bad')
    must_fail(directory, 'incomplete private RAM')
    capture(directory, 3, 128, 0)
    first_meta = first.with_suffix('.meta')
    content = first_meta.read_text()
    first_meta.write_text(content.replace('picture=GATE_102.PIC',
                                          'picture=GATE_103.PIC'))
    must_fail(directory, 'provenance differs')
    first_meta.write_text(content)
    first_result = first.with_suffix('.result')
    content = first_result.read_text()
    first_result.write_text(content.replace('selected=1', 'selected=0'))
    must_fail(directory, 'selected')
    first_result.write_text(content)

print('Gateway picker capture batch tests passed')

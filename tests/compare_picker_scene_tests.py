#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Conditional scene winner comparison across two frozen picker tables."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from compare_picker_scene import analyze, format_report  # noqa: E402


def snapshot(call, flags1=0, flags2=128, flags_gui=0, frame=15, aligned=True,
             geometry_offset=0, anonymous=False):
    x, y, slot = ((280, 50, 1) if call == 1 else (320, 50, 2))
    object_id = 0 if anonymous else 292
    rows = [
        (0, 0, 0, 1, 0, 0, 0, 639, 479, 'fallback', 0),
        (0, 1, 1, 3, flags1, 280, 40, 300 + geometry_offset, 60, 'room', object_id),
        (0, 2, 2, 3, flags2, 301, 40, 321, 60, 'room', object_id),
        (1, 0, 3, 1, 0, 350, 0, 400, 20, 'fallback', 0),
        (1, 1, 4, 5, 0, 285, 45, 312, 55, 'control', 0),
        (1, 2, 5, 5, flags_gui, 340, 45, 360, 55, 'control', 0),
    ]
    header = (f'# call={call} ms={call * 1000} x={x} y={y} native_ax=1 '
              f'group=0 slot={slot} kind=3 source=room object={object_id} '
              'overlaps=2 verdict=match return=4100:0123')
    scene = (f'# scene call={call} picture=GATE_102.PIC entry={frame} '
             f'aligned={int(aligned)} room_group={0 if aligned else -1} '
             f'boxes=2 rows={len(rows)}')
    record = [header, scene]
    for group, s, index, kind, flags, x0, y0, x1, y1, role, obj in rows:
        if not aligned and s:
            role, obj = 'unmapped', 0
        record.append(f'# picker-row call={call} group={group} slot={s} '
                      f'index={index} kind={kind} flags={flags} '
                      f'x0={x0} y0={y0} x1={x1} y1={y1} role={role} object={obj} '
                      f'selected={int(group == 0 and s == slot)}')
    return '\n'.join(record) + '\n'


with tempfile.TemporaryDirectory() as temp:
    trace = Path(temp) / 'picker-follow.tsv'
    baseline = snapshot(1) + snapshot(2, flags1=128, flags2=0, flags_gui=128)
    trace.write_text(baseline, encoding='utf-8')
    result = analyze(trace, 1, 2)
    assert result['picture'] == 'GATE_102.PIC' and result['entry'] == 15
    assert result['total_pixels'] == 379 * 232
    assert result['h_changed_pixels'] == 882
    assert result['h_gained_pixels'] == 441 and result['h_lost_pixels'] == 441
    assert result['h_retargeted_pixels'] == 0
    assert result['native_only_changed_pixels'] == 231
    assert result['native_changed_pixels'] == 805
    assert len(result['flags_changed']) == 3
    assert len(result['samples']) >= 3
    assert all(261 <= item['x'] < 640 and 0 <= item['y'] < 232
               for item in result['samples'])
    assert any(item['h_before'] == 'object #292' and item['h_after'] == 'none'
               for item in result['samples'])
    assert any(item['h_before'] == 'none' and item['h_after'] == 'object #292'
               for item in result['samples'])
    text = format_report(result)
    assert 'H-highlight pixels changed: 882 (gained 441, lost 441' in text
    assert 'Native winner pixels changed: 805 (231 with H unchanged)' in text
    assert 'same restored game state' in text
    assert len(analyze(trace, 1, 2, limit=1)['samples']) == 1
    assert len(analyze(trace, 1, 2, limit=0)['samples']) == result['sample_groups']
    assert analyze(trace, 2, 1)['h_gained_pixels'] == 441  # reverse changes
    cmd = subprocess.run([sys.executable, str(TOOLS / 'compare_picker_scene.py'),
                          str(trace), '--before', '1', '--after', '2', '--json'],
                         capture_output=True, text=True, check=True)
    assert json.loads(cmd.stdout)['h_changed_pixels'] == 882
    # Two overlapping slots of one engine object remain *one* H region,
    # despite the native last-room-slot winner changing with a skip bit.
    trace.write_text(snapshot(1, flags1=0, flags2=0, geometry_offset=21) +
                     snapshot(2, flags1=0, flags2=128, geometry_offset=21),
                     encoding='utf-8')
    merged = analyze(trace, 1, 2)
    assert merged['h_changed_pixels'] == 0
    assert merged['native_changed_pixels'] > 0
    assert merged['native_only_changed_pixels'] == merged['native_changed_pixels']
    trace.write_text(snapshot(1, flags1=0, flags2=0, geometry_offset=21,
                              anonymous=True) +
                     snapshot(2, flags1=0, flags2=128, geometry_offset=21,
                              anonymous=True), encoding='utf-8')
    separate = analyze(trace, 1, 2)
    assert separate['h_retargeted_pixels'] == 441
    assert separate['h_gained_pixels'] == separate['h_lost_pixels'] == 0
    for altered in (
        snapshot(1) + snapshot(2, frame=16, flags1=128, flags2=0),
        snapshot(1) + snapshot(2, aligned=False, flags1=128, flags2=0),
        snapshot(1) + snapshot(2, geometry_offset=1, flags1=128, flags2=0),
    ):
        trace.write_text(altered, encoding='utf-8')
        try:
            analyze(trace, 1, 2)
        except ValueError as error:
            assert ('scene' in str(error) or 'geometry' in str(error))
        else:
            raise AssertionError('different or unaligned PIC/RGN scene was compared')
    trace.write_text('# call=1 ms=100 x=280 y=50 native_ax=1 group=0 slot=1 '
                     'kind=3 source=room object=292 overlaps=1 verdict=match '
                     'return=4100:0123\n', encoding='utf-8')
    try:
        analyze(trace, 1, 2)
    except ValueError as error:
        assert 'no frozen picker geometry' in str(error)
    else:
        raise AssertionError('legacy trace must not invent conditional geometry')

print('Gateway conditional scene geometry comparison checks passed')

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Offline full picker and independent room-candidate comparisons."""

from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from report_picker_scene import report  # noqa: E402
from report_picker_follow import report as legacy_follow_report  # noqa: E402


RECTS = [
    (0, 0, 0, 1, 0, 0, 0, 639, 479, 'fallback', 0),
    (0, 1, 1, 3, 0, 280, 40, 300, 60, 'room', 123),
    (0, 2, 2, 3, 0, 301, 40, 321, 60, 'room', 123),
    (0, 3, 3, 3, 0, 280, 300, 300, 320, 'room', 124),
    (1, 0, 4, 1, 0, 350, 0, 400, 20, 'fallback', 0),
    (1, 1, 5, 5, 0, 285, 45, 312, 55, 'control', 0),
]


def captured(call, x, y, group, slot, kind, source, object_id, *, aligned=True):
    call_marker = (f'# call={call} ms={call * 1000} x={x} y={y} native_ax=1 '
                   f'group={group} slot={slot} kind={kind} source={source} '
                   f'object={object_id} overlaps=2 verdict=match return=4100:0123')
    scene = (f'# scene call={call} picture=GATE_102.PIC entry=15 '
             f'aligned={int(aligned)} room_group={0 if aligned else -1} '
             f'boxes=3 rows={len(RECTS)}')
    rows = []
    for g, s, index, k, flags, x0, y0, x1, y1, role, obj in RECTS:
        if not aligned and s:
            role, obj = 'unmapped', 0
        selected = int((g, s, k) == (group, slot, kind))
        rows.append(f'# picker-row call={call} group={g} slot={s} index={index} '
                    f'kind={k} flags={flags} x0={x0} y0={y0} x1={x1} y1={y1} '
                    f'role={role} object={obj} selected={selected}')
    return '\n'.join((call_marker, scene, *rows))


with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'original-picker-follow.tsv'
    fixture = '\n'.join((
        captured(1, 280, 50, 0, 1, 3, 'room', 123),
        captured(2, 295, 50, 1, 1, 5, 'control', 0),
        captured(3, 280, 305, 0, 3, 3, 'room', 124),
        captured(4, 360, 15, 1, 0, 1, 'fallback', 0, aligned=False),
    )) + '\n'
    path.write_text(fixture, encoding='utf-8')
    result = report(path)
    assert 'Original picker geometry: 4 calls, 4 H winners, 0 unmarked, 0 unavailable' in result
    assert 'Native selections absent from H winner: 0' in result
    assert 'Legacy scene-only candidates: 2 marked, 1 unmarked, 1 unknown' in result
    assert 'Native room selections absent from scene-only candidate: 1' in result
    assert 'Native room object differs from scene-only owner: 0' in result
    assert 'GUI/fallback selections over scene-only room: 1' in result
    assert 'Native selection differs from frozen picker table: 0' in result
    assert 'call 2 2000ms (295,50): native=control' in result
    assert 'H=control g1/s1, room-candidate=object #123 (slot 1)' in result
    assert 'call 3 3000ms (280,305): native=room' in result
    assert 'H=object #124 (slot 3), room-candidate=none' in result
    assert 'call 4 4000ms (360,15): native=fallback' in result
    assert 'H=fallback g1/s0, room-candidate=unavailable' in result
    assert 'call 3' not in report(path, limit=2)
    assert '... 2 more' in report(path, limit=2)
    assert 'call 4' in report(path, limit=0)
    cmd = subprocess.run([sys.executable, str(TOOLS / 'report_picker_scene.py'),
                          str(path), '--limit', '1'], capture_output=True, text=True,
                         check=True)
    assert 'GUI/fallback selections over scene-only room' in cmd.stdout
    assert '... 3 more' in cmd.stdout
    # The auxiliary records must not break the established instruction TSV,
    # its continuation parser or the existing per-click evidence reporter.
    instruction_header = ('call step ms cs ip linear bytes ax bx cx dx si di bp '
                          'ss sp flags').replace(' ', '\t')
    instruction = '\t'.join(('1', '0', '1000', '4100', '123', '41123', '85c0',
                              '1', '0', '0', '0', '0', '0', '0', '5200', '100', '202'))
    instruction_rows = '\n'.join(instruction.replace('1\t0\t1000',
                      f'{call}\t0\t{call * 1000}', 1) for call in range(1, 5))
    complete = (instruction_header + '\n' + fixture + instruction_rows + '\n' +
                ''.join(f'# end call={call} status=complete steps=1\n'
                        for call in range(1, 5)) +
                '# follow-summary calls=4 completed=4 interrupted=0 missed=0 '
                'errors=0 pending=0 steps=1\n')
    path.write_text(complete, encoding='utf-8')
    assert 'Original native picker calls: 4' in legacy_follow_report(path)
    assert 'Original picker geometry: 4 calls' in report(path)
    # A contradictory native return should be *reported* as a mismatch,
    # not rejected as though a prediction were ground truth.
    skipped_selected = fixture.replace(
        'call=1 group=0 slot=1 index=1 kind=3 flags=0',
        'call=1 group=0 slot=1 index=1 kind=3 flags=128', 1)
    path.write_text(skipped_selected, encoding='utf-8')
    disagreement = report(path)
    assert 'Native selection differs from frozen picker table: 1' in disagreement
    assert 'Native selections absent from H winner: 1' in disagreement
    assert 'Native room selections absent from scene-only candidate: 2' in disagreement
    changes = (
        ('rows=6', 'rows=5'),
        ('selected=1', 'selected=0'),
        ('role=control object=0 selected=1', 'role=room object=0 selected=1'),
        ('group=1 slot=1 index=5', 'group=1 slot=0 index=5'),
        ('object=123 selected=1', 'object=900 selected=1'),
    )
    for old, new in changes:
        path.write_text(fixture.replace(old, new, 1), encoding='utf-8')
        try:
            report(path)
        except ValueError:
            pass
        else:
            raise AssertionError(f'malformed scene trace accepted: {new}')
    path.write_text('# call=1 ms=100 x=280 y=50 native_ax=1 group=0 slot=1 '
                    'kind=3 source=room object=123 overlaps=1 verdict=match '
                    'return=4100:0123\n', encoding='utf-8')
    assert 'legacy follow trace' in report(path)

print('Gateway picker scene geometry reporting checks passed')

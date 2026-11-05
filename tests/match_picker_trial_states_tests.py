#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic original-picker condition matching without original game data."""

import hashlib
import json
from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from match_picker_trial_states import (compare_state, match_study,
                                       read_reference)  # noqa: E402


def follow(call, x, y, *, room_a=0, room_b=128, control=128,
           selection=(0, 1, 3, 'room', 292), geometry=True):
    group, slot, kind, source, object_id = selection
    header = (f'# call={call} ms=22005 x={x} y={y} native_ax=1 group={group} '
              f'slot={slot} kind={kind} source={source} object={object_id} '
              'overlaps=1 verdict=match return=4100:0123')
    if not geometry:
        return header + '\n'
    rows = [
        (0, 0, 0, 1, 0, 0, 0, 639, 479, 'fallback', 0),
        (0, 1, 1, 3, room_a, 280, 40, 300, 60, 'room', 292),
        (0, 2, 2, 3, room_b, 301, 40, 321, 60, 'room', 292),
        (1, 0, 3, 1, 0, 350, 0, 400, 20, 'fallback', 0),
        (1, 1, 4, 5, control, 280, 40, 312, 55, 'control', 0),
    ]
    scene = f'# scene call={call} picture=GATE_102.PIC entry=15 aligned=1 room_group=0 boxes=2 rows=5'
    lines = [header, scene]
    for row_group, row_slot, native_index, row_kind, flags, x0, y0, x1, y1, role, obj in rows:
        selected = int((row_group, row_slot, row_kind) == (group, slot, kind))
        lines.append(f'# picker-row call={call} group={row_group} slot={row_slot} '
                     f'index={native_index} kind={row_kind} flags={flags} '
                     f'x0={x0} y0={y0} x1={x1} y1={y1} role={role} '
                     f'object={obj} selected={selected}')
    return '\n'.join(lines) + '\n'


def capture(directory):
    base = directory / 'call-0001'
    registers = {'AX': 0, 'BX': 0, 'CX': 0, 'DX': 0,
                 'CS': 0x2649, 'IP': 0x541, 'SS': 0, 'SP': 0,
                 'DS': 0x3123, 'ES': 0, 'BP': 0, 'FLAGS': 0}
    metadata = ['format=gateway-picker-entry-v1', 'call=1',
                'guest_ms=1000', 'x=290', 'y=50',
                'return_cs=0x4100', 'return_ip=0x0123',
                'picture=GATE_102.PIC', 'picture_entry=15']
    metadata.extend(f'{key}=0x{value:x}' for key, value in registers.items())
    base.with_suffix('.meta').write_text('\n'.join(metadata) + '\n')
    base.with_suffix('.bin').write_bytes(bytes(0x100000))
    base.with_suffix('.result').write_text(follow(1, 290, 50))


with tempfile.TemporaryDirectory(prefix='gateway-trial-state-test-') as directory:
    root = Path(directory)
    captures = root / 'captures'
    captures.mkdir()
    capture(captures)
    study = root / 'trials'
    study.mkdir()
    points = [[290, 50], [280, 50], [310, 50], [320, 50], [330, 50]]
    (study / 'trial-plan.json').write_text(json.dumps({
        'format': 'gateway-picker-click-trials-v1', 'click_ms': 22000,
        'post_ms': 25000, 'points': points}))
    (study / 'comparison.json').write_text(json.dumps({
        'click_ms': 22000, 'post_ms': 25000, 'points': len(points),
        'trials': [{'name': f'click-{index:03d}', 'x': x, 'y': y,
                    'comparison_qualified': index != 2}
                   for index, (x, y) in enumerate(points, 1)]}))
    text = [follow(1, 290, 50),
            follow(1, 280, 50, room_a=128, selection=(0, 0, 1, 'fallback', 0)),
            follow(1, 310, 50, control=0, selection=(1, 1, 5, 'control', 0)),
            follow(1, 320, 50, geometry=False),
            follow(1, 315, 50)]
    for index, item in enumerate(text, 1):
        trial = study / f'click-{index:03d}'
        trial.mkdir()
        (trial / 'picker-follow.tsv').write_text(item)

    report = match_study(captures, 1, study)
    trials = report['trials']
    assert report['format'] == 'gateway-picker-state-check-v1'
    assert report['trial_plan_sha256'] == hashlib.sha256(
        (study / 'trial-plan.json').read_bytes()).hexdigest()
    assert report['comparison_sha256'] == hashlib.sha256(
        (study / 'comparison.json').read_bytes()).hexdigest()
    assert report['reference_scene'] == 'GATE_102.PIC'
    assert trials[0]['calls'][0]['status'] == 'matching_skip_mask'
    assert trials[0]['calls'][0]['all_flags_equal']
    assert not trials[1]['baseline_qualified']
    assert trials[1]['calls'][0]['status'] == 'different_room_skip_mask'
    assert trials[1]['calls'][0]['room_skip_changes'][0]['object'] == 292
    assert trials[2]['calls'][0]['status'] == 'different_gui_skip_mask'
    assert trials[2]['calls'][0]['other_skip_changes'] == 1
    assert trials[3]['calls'][0]['status'] == 'missing_picker_geometry'
    assert trials[4]['status'] == 'no_matching_picker_call'

    frozen, _ = read_reference(captures, 1)
    different_frame = deepcopy(frozen)
    different_frame[0]['entry'] += 1
    assert compare_state(frozen, different_frame)['status'] == 'different_scene'
    different_geometry = deepcopy(frozen)
    different_geometry[1][1]['x0'] += 1
    assert compare_state(frozen, different_geometry)['status'] == 'different_geometry'
    lower_flag = deepcopy(frozen)
    lower_flag[1][1]['flags'] = 1
    assert compare_state(frozen, lower_flag)['status'] == 'matching_skip_mask'
    assert not compare_state(frozen, lower_flag)['all_flags_equal']
    unaligned = deepcopy(frozen)
    unaligned[0]['aligned'] = 0
    assert compare_state(frozen, unaligned)['status'] == 'trial_unaligned'

    command = subprocess.run([sys.executable, str(TOOLS / 'match_picker_trial_states.py'),
                              str(captures), str(study), '--reference-call', '1'],
                             capture_output=True, text=True, check=True)
    assert json.loads(command.stdout) == report
    try:
        match_study(captures, 2, study)
    except ValueError as error:
        assert 'unavailable' in str(error)
    else:
        raise AssertionError('accepted nonexistent reference picker capture')

    comparison = study / 'comparison.json'
    original = comparison.read_text()
    comparison.write_text(original.replace('"x": 290', '"x": 289'))
    try:
        match_study(captures, 1, study)
    except ValueError as error:
        assert 'differ' in str(error)
    else:
        raise AssertionError('accepted altered experiment point provenance')
    comparison.write_text(original)

print('Gateway captured picker condition-to-trial matching checks passed')

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic original-identity RTLink ranking checks; no DOS guest needed."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from decomp_status import COLUMNS  # noqa: E402
from rank_picker_trial_targets import rank_studies, read_plan  # noqa: E402


def delta(target, before, after):
    return {'target': target, 'before': before, 'after': after, 'delta': after - before}


def trial(*deltas, qualified=True, room=False, ram=0):
    calls = ([{'call': 1, 'source': 'room', 'object': 292,
               'continuation': {'object_context': 'object-referenced'}}]
             if room else [{'call': 1, 'source': 'control', 'object': 0,
                            'continuation': {'object_context': 'unknown'}}])
    return {'comparison_qualified': qualified, 'post_diff': {'changed_bytes': ram},
            'picker_calls_at_injected_point': [1],
            'picker_trace': {'available': True, 'calls': calls},
            'rtlink_diff': {'available': True, 'changed_targets': len(deltas),
                            'first_targets': list(deltas), 'truncated': False}}


def write_study(directory, trials):
    directory.mkdir()
    path = directory / 'comparison.json'
    for index, item in enumerate(trials, 1):
        item.update({'name': f'click-{index:03d}', 'x': 280 + index, 'y': 50})
    path.write_text(json.dumps({'points': len(trials), 'trials': trials}))
    return path


with tempfile.TemporaryDirectory(prefix='gateway-picker-rank-test-') as temporary:
    root = Path(temporary)
    plan = root / 'ghidra-plan.json'
    plan.write_text(json.dumps({'schema_version': 1, 'sections': [
        {'id': 4, 'runtime_segment': 0x4319,
         'original_file_offset': 0xb000, 'file_bytes': 0x2000},
        {'id': 5, 'runtime_segment': 0x4319,
         'original_file_offset': 0x13000, 'file_bytes': 0x2000}]}))
    sections = read_plan(plan)
    tracker = {0xbd5d: {'name': 'gw_dummy_handler', 'kind': 'handler',
                        'state': 'analyzed'}}
    a = delta('4:4319:0d5d', 10, 12)
    b = delta('5:4319:0d5d', 3, 6)
    first = write_study(root / 'first', [
        trial(a, room=True, ram=15), trial(delta(a['target'], 10, 9), b),
        trial(a, qualified=False)])
    second = write_study(root / 'second', [trial(a, delta(b['target'], 3, 4))])

    result = rank_studies([first.parent, second], sections, tracker)
    assert result['studies'] == 2 and result['qualified_trials'] == 3
    assert result['inconclusive_trials'] == 1
    assert result['ranking_is_complete']
    top, other = result['targets']
    assert top['original'] == 'ovl:0x00bd5d'
    assert top['name'] == 'gw_dummy_handler' and top['kind'] == 'handler'
    assert top['trials_with_delta'] == 3 and top['net_call_delta'] == 3
    assert top['total_absolute_delta'] == 5
    assert top['trials_with_room_picker'] == 1
    assert top['trials_with_object_context'] == 1
    assert top['room_objects'] == [292]
    assert len(top['first_examples']) == 3
    assert top['first_examples'][0] == {
        'study': str(first.resolve()), 'trial': 'click-001',
        'x': 281, 'y': 50, 'delta': 2}
    assert other['original'] == 'ovl:0x013d5d' and not other['tracker_exact_entry']
    assert other['trials_with_delta'] == 2 and other['net_call_delta'] == 4
    assert len(rank_studies([first])['targets']) == 2  # no Ghidra needed

    command = subprocess.run([sys.executable, str(TOOLS / 'rank_picker_trial_targets.py'),
                              str(first.parent), str(second), '--plan', str(plan),
                              '--json'], capture_output=True, text=True, check=True)
    assert json.loads(command.stdout)['targets'][0]['original'] == 'ovl:0x00bd5d'

    text = subprocess.run([sys.executable, str(TOOLS / 'rank_picker_trial_targets.py'),
                           str(first), '--plan', str(plan)], capture_output=True,
                          text=True, check=True).stdout
    assert 'Whole-run RTLink associations only' in text

    record = json.loads(first.read_text())
    original = first.read_text()
    record['trials'][0]['rtlink_diff']['first_targets'][0]['delta'] = -7
    first.write_text(json.dumps(record))
    try:
        rank_studies([first], sections, tracker)
    except ValueError as error:
        assert 'call delta' in str(error)
    else:
        raise AssertionError('accepted inconsistent RTLink call delta')
    first.write_text(original)

    record = json.loads(original)
    record['trials'][0]['rtlink_diff']['first_targets'][0]['target'] = '4:4319:ffff'
    first.write_text(json.dumps(record))
    try:
        rank_studies([first], sections, tracker)
    except ValueError as error:
        assert 'not file-backed' in str(error)
    else:
        raise AssertionError('accepted a target outside the verified overlay section')
    first.write_text(original)

    record = json.loads(original)
    record['trials'][1]['rtlink_diff'] = {'available': False}
    first.write_text(json.dumps(record))
    partial = rank_studies([first], sections, tracker)
    assert not partial['ranking_is_complete'] and partial['qualified_missing_censuses'] == 1
    first.write_text(original)

    try:
        rank_studies([first, first.parent], sections, tracker)
    except ValueError as error:
        assert 'duplicate controlled' in str(error)
    else:
        raise AssertionError('accepted duplicate studies')

print('Gateway controlled click RTLink target ranking checks passed')

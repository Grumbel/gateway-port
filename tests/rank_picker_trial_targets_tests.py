#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic original-identity RTLink ranking checks; no DOS guest needed."""

import hashlib
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
    ordered = sorted(deltas, key=lambda row: (-abs(row['delta']), row['target']))
    return {'comparison_qualified': qualified, 'post_diff': {'changed_bytes': ram},
            'picker_calls_at_injected_point': [1],
            'picker_trace': {'available': True, 'calls': calls},
            'rtlink_diff': {'available': True, 'changed_targets': len(ordered),
                            'first_targets': list(ordered[:32]),
                            'truncated': len(ordered) > 32}}


def write_census(directory, counts):
    directory.mkdir(exist_ok=True)
    total = sum(counts.values())
    lines = [f'rtlink-census manager_hits={total} validated={total} unrecognized=0']
    for target, count in sorted(counts.items()):
        sid, cs, ip = target.split(':')
        lines.append(f'rtlink-call section={sid} target={cs}:{ip} '
                     f'count={count} first_ms=1 last_ms=2')
    (directory / 'native.rtlink-calls').write_text('\n'.join(lines) + '\n')


def write_study(directory, trials):
    directory.mkdir()
    path = directory / 'comparison.json'
    for index, item in enumerate(trials, 1):
        item.update({'name': f'click-{index:03d}', 'x': 280 + index, 'y': 50})
    path.write_text(json.dumps({'points': len(trials), 'trials': trials}))
    return path


def write_state_check(comparison, statuses):
    report = json.loads(comparison.read_text())
    assert len(statuses) == len(report['trials'])
    plan = comparison.parent / 'trial-plan.json'
    plan.write_text(json.dumps({'format': 'gateway-picker-click-trials-v1',
                                'click_ms': 22000, 'post_ms': 25000,
                                'points': [[row['x'], row['y']]
                                           for row in report['trials']]}))
    states = []
    for row, statuses_at_point in zip(report['trials'], statuses):
        calls = [{'call': index, 'ms': 23000, 'source': 'room',
                  'object': 292, 'status': status}
                 for index, status in enumerate(statuses_at_point, 1)]
        states.append({'name': row['name'], 'x': row['x'], 'y': row['y'],
                       'baseline_qualified': row['comparison_qualified'],
                       'status': 'observed' if calls else 'no_matching_picker_call',
                       'calls': calls})
    state_path = comparison.parent / 'state-check.json'
    state_path.write_text(json.dumps({
        'format': 'gateway-picker-state-check-v1',
        'reference_call': 2, 'click_ms': 22000, 'post_ms': 25000,
        'trial_plan_sha256': hashlib.sha256(plan.read_bytes()).hexdigest(),
        'comparison_sha256': hashlib.sha256(comparison.read_bytes()).hexdigest(),
        'trials': states}))
    return state_path


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

    first_state = write_state_check(first, [
        ['matching_skip_mask'],
        ['matching_skip_mask', 'different_room_skip_mask'],
        []])
    second_state = write_state_check(second, [['matching_skip_mask']])
    selected = rank_studies([first, second], sections, tracker, state_checks={
        str(first.resolve()): first_state, str(second.resolve()): second_state})
    assert selected['ranking_scope'] == 'matching_picker_state'
    assert selected['qualified_trials'] == 3
    assert selected['qualified_state_matched_trials'] == 2
    assert selected['qualified_state_excluded_trials'] == 1
    assert selected['state_exclusion_reasons'] == {'different_room_skip_mask': 1}
    assert selected['ranking_is_complete']
    assert selected['targets'][0]['trials_with_delta'] == 2
    assert selected['targets'][1]['trials_with_delta'] == 1
    assert selected['targets'][0]['room_objects'] == [292]
    # An ambiguous sequence with one matching native call still gets excluded.
    assert selected['targets'][0]['net_call_delta'] == 4

    state_command = subprocess.run(
        [sys.executable, str(TOOLS / 'rank_picker_trial_targets.py'), str(first),
         '--state-check', f'{first.parent}={first_state}', '--json'],
        capture_output=True, text=True, check=True)
    state_json = json.loads(state_command.stdout)
    assert state_json['qualified_state_matched_trials'] == 1
    assert state_json['qualified_state_excluded_trials'] == 1

    missing_check = {str(first.resolve()): first_state}
    try:
        rank_studies([first, second], state_checks=missing_check)
    except ValueError as error:
        assert 'no picker state check' in str(error)
    else:
        raise AssertionError('accepted unchecked second study in state-filtered ranking')
    try:
        rank_studies([first], state_checks={
            str(first.resolve()): first_state, str(second.resolve()): second_state})
    except ValueError as error:
        assert 'unrequested' in str(error)
    else:
        raise AssertionError('accepted an unrelated picker state check')
    original_state = first_state.read_text()
    first_check = json.loads(original_state)
    first_check['trials'][0]['x'] += 1
    first_state.write_text(json.dumps(first_check))
    try:
        rank_studies([first], state_checks={str(first.resolve()): first_state})
    except ValueError as error:
        assert 'identity differs' in str(error)
    else:
        raise AssertionError('accepted a mislabelled reference state trial')
    first_state.write_text(original_state)
    first_check = json.loads(original_state)
    first_check['trials'][0]['calls'][0]['status'] = 'unrecognized_future_status'
    first_state.write_text(json.dumps(first_check))
    try:
        rank_studies([first], state_checks={str(first.resolve()): first_state})
    except ValueError as error:
        assert 'malformed picker state-check call' in str(error)
    else:
        raise AssertionError('accepted unknown reference picker verdict')
    first_state.write_text(original_state)
    first_check = json.loads(original_state)
    first_check['trials'][0]['calls'][0]['ms'] = 26000
    first_state.write_text(json.dumps(first_check))
    try:
        rank_studies([first], state_checks={str(first.resolve()): first_state})
    except ValueError as error:
        assert 'malformed picker state-check call' in str(error)
    else:
        raise AssertionError('accepted out-of-window native picker state')
    first_state.write_text(original_state)
    original_comparison = first.read_text()
    first.write_text(original_comparison + '\n')
    try:
        rank_studies([first], state_checks={str(first.resolve()): first_state})
    except ValueError as error:
        assert 'provenance differs' in str(error)
    else:
        raise AssertionError('accepted a stale picker state check')
    first.write_text(original_comparison)
    original_plan = (first.parent / 'trial-plan.json').read_text()
    (first.parent / 'trial-plan.json').write_text(original_plan + '\n')
    try:
        rank_studies([first], state_checks={str(first.resolve()): first_state})
    except ValueError as error:
        assert 'provenance differs' in str(error)
    else:
        raise AssertionError('accepted a stale picker trial plan')
    (first.parent / 'trial-plan.json').write_text(original_plan)

    # The saved report intentionally truncates at 32 deltas. Verify every
    # omitted original section-qualified target is recovered from the actual
    # raw sidecars, without changing the immutable comparison.json.
    many = [delta(f'4:4319:{offset:04x}', 1, 2) for offset in range(0x100, 0x123)]
    full = write_study(root / 'full', [trial(*many)])
    write_census(full.parent / 'control-a',
                 {item['target']: item['before'] for item in many})
    write_census(full.parent / 'click-001',
                 {item['target']: item['after'] for item in many})
    # Match the remaining native summary header fields precisely.
    full_report = json.loads(full.read_text())
    full_report['trials'][0]['rtlink_diff'].update({
        'manager_hits_delta': 35, 'validated_calls_delta': 35,
        'unrecognized_hits_delta': 0})
    full.write_text(json.dumps(full_report))
    all_found = rank_studies([full])
    assert len(all_found['targets']) == 35
    assert all_found['ranking_is_complete']
    assert all_found['qualified_truncated_censuses'] == 1
    assert all_found['recovered_truncated_censuses'] == 1
    assert all_found['unrecovered_truncated_censuses'] == 0
    assert all_found['validated_raw_census_pairs'] == 1
    assert '4:4319:0122' in {item['target'] for item in all_found['targets']}
    # Raw census and frozen summary must agree even when the first 32 match.
    saved = full.read_text()
    write_census(full.parent / 'click-001',
                 {item['target']: (3 if item['target'].endswith(':0122') else 2)
                  for item in many})
    try:
        rank_studies([full])
    except ValueError as error:
        assert 'disagree with saved' in str(error)
    else:
        raise AssertionError('accepted modified native census sidecar')
    write_census(full.parent / 'click-001',
                 {item['target']: item['after'] for item in many})
    (full.parent / 'click-001' / 'native.rtlink-calls').unlink()
    try:
        rank_studies([full])
    except ValueError as error:
        assert 'incomplete native' in str(error)
    else:
        raise AssertionError('accepted a partial original census pair')
    write_census(full.parent / 'click-001',
                 {item['target']: item['after'] for item in many})
    assert full.read_text() == saved

    truncated_only = write_study(root / 'truncated-only', [trial(*many)])
    incomplete = rank_studies([truncated_only])
    assert not incomplete['ranking_is_complete']
    assert incomplete['unrecovered_truncated_censuses'] == 1
    assert len(incomplete['targets']) == 32

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

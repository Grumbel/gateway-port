#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Rank original RTLink investigation leads across controlled click studies.

All RTLink deltas are *whole-run counts*, not proof of click-specific calls
or successful game-action dispatch. A source identity is resolved only from
the section-qualified, validated original Ghidra overlay plan.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from decomp_status import read_tracker
from inventory_native_handlers import map_entry
from picker_click_trials import (compare_rtlink_counts, rtlink_census,
                                 rtlink_target_deltas)
from report_picker_follow import SOURCES


TARGET = re.compile(r'(\d+):([0-9a-f]{1,4}):([0-9a-f]{1,4})\Z')


def read_plan(path):
    if path is None:
        return None
    plan = json.loads(path.read_text(encoding='utf-8'))
    if plan.get('schema_version') != 1 or not isinstance(plan.get('sections'), list):
        raise ValueError('unsupported Ghidra overlay plan')
    sections = {item['id']: item for item in plan['sections']}
    if len(sections) != len(plan['sections']):
        raise ValueError('duplicate Ghidra overlay section')
    return sections


def target_identity(name, sections, tracked):
    match = TARGET.fullmatch(name)
    if match is None:
        raise ValueError(f'invalid section-qualified RTLink target {name!r}')
    sid, cs, ip = (int(match[1]), int(match[2], 16), int(match[3], 16))
    if not 0 <= sid < 58:
        raise ValueError('invalid RTLink section')
    result = {'target': name}
    if sections is not None:
        original = map_entry((sid, cs, ip), sections)
        offset = original['file_offset']
        result['original'] = f'ovl:0x{offset:06x}'
        entry = tracked.get(offset)
        if entry is not None:
            result.update({'name': entry['name'] or None,
                           'kind': entry['kind'], 'state': entry['state'],
                           'tracker_exact_entry': True})
        else:
            result['tracker_exact_entry'] = False
    return result


def study_trials(path):
    if path.is_dir():
        path = path / 'comparison.json'
    path = path.resolve()
    record = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(record, dict) or not isinstance(record.get('trials'), list) or \
            record.get('points') != len(record['trials']):
        raise ValueError(f'{path}: invalid controlled click comparison')
    return str(path), record['trials']


def complete_target_rows(report, trial, summary):
    """Recover omitted target deltas from complete native census sidecars.

    The saved summary is our immutable experiment report. If native sidecars
    are present, demand that they reproduce *every* field in it exactly
    before using any of their additional targets.
    """
    root = Path(report).parent
    baseline_file = root / 'control-a' / 'native.rtlink-calls'
    trial_file = root / trial['name'] / 'native.rtlink-calls'
    if not baseline_file.is_file() and not trial_file.is_file():
        return summary['first_targets'], False
    if not baseline_file.is_file() or not trial_file.is_file():
        raise ValueError(f'{report}: incomplete native RTLink census sidecars')
    baseline = rtlink_census(baseline_file.parent)
    observed = rtlink_census(trial_file.parent)
    if compare_rtlink_counts(baseline, observed) != summary:
        raise ValueError(f'{report}: original RTLink sidecars disagree with '
                         f'saved trial {trial["name"]} summary')
    return rtlink_target_deltas(baseline, observed), True


VALID_PICKER_STATES = {
    'matching_skip_mask', 'different_room_skip_mask',
    'different_gui_skip_mask', 'different_scene', 'different_geometry',
    'reference_unaligned', 'trial_unaligned', 'missing_picker_geometry',
}


def verified_state_matches(report, trials, state_path):
    """Check matching-scene evidence is tied to this *exact* native study.

    The state report is produced separately from the immutable click report.
    Binding both SHA-256 values prevents accidentally combining a state check
    from a different walkthrough, point list or revision of the recordings.
    """
    study = Path(report).parent
    plan_file = study / 'trial-plan.json'
    if not plan_file.is_file():
        raise ValueError(f'{report}: missing picker trial manifest for state check')
    plan = json.loads(plan_file.read_text(encoding='utf-8'))
    check = json.loads(state_path.read_text(encoding='utf-8'))
    if (not isinstance(plan, dict) or not isinstance(check, dict) or
        plan.get('format') != 'gateway-picker-click-trials-v1' or
        check.get('format') != 'gateway-picker-state-check-v1' or
        type(plan.get('click_ms')) is not int or
        type(plan.get('post_ms')) is not int or
        not 0 <= plan['click_ms'] < plan['post_ms'] <= 3600000 or
        type(check.get('reference_call')) is not int or
        not 1 <= check['reference_call'] <= 128 or
        check.get('click_ms') != plan.get('click_ms') or
        check.get('post_ms') != plan.get('post_ms') or
        check.get('trial_plan_sha256') != hashlib.sha256(plan_file.read_bytes()).hexdigest() or
        check.get('comparison_sha256') != hashlib.sha256(Path(report).read_bytes()).hexdigest()):
        raise ValueError(f'{state_path}: picker state check provenance differs from '
                         'the recorded study (regenerate with match_picker_trial_states.py)')
    states = check.get('trials')
    if not isinstance(states, list) or len(states) != len(trials):
        raise ValueError(f'{state_path}: incomplete picker state-check trials')
    matches = {}
    for trial, state in zip(trials, states):
        if (not isinstance(state, dict) or
            any(state.get(field) != trial.get(field)
                for field in ('name', 'x', 'y')) or
            type(state.get('baseline_qualified')) is not bool or
            state['baseline_qualified'] != trial.get('comparison_qualified')):
            raise ValueError(f'{state_path}: picker state-check trial identity differs')
        status = state.get('status')
        calls = state.get('calls')
        if (status not in ('observed', 'missing_picker_trace',
                           'no_matching_picker_call') or
            not isinstance(calls, list) or bool(calls) != (status == 'observed')):
            raise ValueError(f'{state_path}: malformed picker state-check observations')
        seen_calls = set()
        observed = []
        for call in calls:
            if (not isinstance(call, dict) or
                type(call.get('call')) is not int or call['call'] <= 0 or
                call['call'] in seen_calls or
                type(call.get('ms')) is not int or
                not plan['click_ms'] <= call['ms'] <= plan['post_ms'] or
                call.get('source') not in SOURCES or
                (call.get('object') is not None and (
                    type(call['object']) is not int or
                    not 0 <= call['object'] <= 0xffff)) or
                call.get('status') not in VALID_PICKER_STATES):
                raise ValueError(f'{state_path}: malformed picker state-check call')
            seen_calls.add(call['call'])
            observed.append(call['status'])
        if observed:
            # Ambiguous multiple picks are *not* a successful state match
            # unless every call at these coordinates agrees with reference.
            matches[trial['name']] = ('matching_skip_mask' if all(
                value == 'matching_skip_mask' for value in observed)
                else ','.join(sorted(set(observed) - {'matching_skip_mask'})))
        else:
            matches[trial['name']] = status
    return matches


def rank_studies(paths, sections=None, tracked=None, *, state_checks=None):
    if not paths:
        raise ValueError('provide one or more completed controlled click studies')
    tracked = tracked or {}
    targets = defaultdict(lambda: {'trials': 0, 'net_delta': 0,
                                   'absolute_delta': 0, 'with_ram_change': 0,
                                   'with_room_picker': 0,
                                   'with_object_context': 0, 'room_objects': set(),
                                   'studies': set(), 'examples': []})
    seen = set()
    qualified = inconclusive = incomplete = recovered = validated = missing = 0
    state_matched = state_excluded = 0
    state_exclusions = defaultdict(int)
    for argument in paths:
        name, trials = study_trials(argument)
        if name in seen:
            raise ValueError(f'duplicate controlled click report {name}')
        seen.add(name)
        states = None
        if state_checks is not None:
            if name not in state_checks:
                raise ValueError(f'{name}: no picker state check supplied for this study')
            states = verified_state_matches(name, trials, Path(state_checks[name]))
        trial_names = set()
        for trial in trials:
            if not isinstance(trial, dict) or type(trial.get('comparison_qualified')) is not bool:
                raise ValueError(f'{name}: missing controlled comparison qualification')
            if (not isinstance(trial.get('name'), str) or
                not re.fullmatch(r'click-[0-9]{3}', trial['name']) or
                trial['name'] in trial_names or
                type(trial.get('x')) is not int or type(trial.get('y')) is not int or
                not 0 <= trial['x'] < 640 or not 0 <= trial['y'] < 480):
                raise ValueError(f'{name}: missing controlled click identity')
            trial_names.add(trial['name'])
            if not trial['comparison_qualified']:
                inconclusive += 1
                continue
            qualified += 1
            if states is not None:
                if states[trial['name']] != 'matching_skip_mask':
                    state_excluded += 1
                    state_exclusions[states[trial['name']]] += 1
                    continue
                state_matched += 1
            delta = trial.get('rtlink_diff', {})
            if not delta.get('available'):
                missing += 1
                continue
            rows = delta.get('first_targets')
            if not isinstance(rows, list) or type(delta.get('changed_targets')) is not int or \
                    delta['changed_targets'] < 0 or \
                    len(rows) != min(32, delta['changed_targets']) or \
                    bool(delta.get('truncated')) != (delta['changed_targets'] > 32):
                raise ValueError(f'{name}: malformed RTLink target differences')
            rows, has_full_census = complete_target_rows(name, trial, delta)
            validated += int(has_full_census)
            recovered += int(bool(delta.get('truncated')) and has_full_census)
            incomplete += int(bool(delta.get('truncated')) and not has_full_census)
            points = trial.get('picker_calls_at_injected_point', [])
            observations = trial.get('picker_trace', {}).get('calls', [])
            matching = [item for item in observations if item.get('call') in points]
            room_objects = {item.get('object') for item in matching
                            if item.get('source') == 'room' and
                            type(item.get('object')) is int and item['object'] > 0}
            object_context = any(item.get('continuation', {}).get('object_context') ==
                                 'object-referenced' for item in matching)
            ram_changed = trial.get('post_diff', {}).get('changed_bytes', 0) > 0
            local = set()
            for row in rows:
                if not isinstance(row, dict):
                    raise ValueError('invalid RTLink delta row')
                key = row.get('target')
                if not isinstance(key, str) or key in local or \
                        any(type(row.get(field)) is not int for field in
                            ('before', 'after', 'delta')) or \
                        row['before'] < 0 or row['after'] < 0 or \
                        row['delta'] == 0 or row['after'] - row['before'] != row['delta']:
                    raise ValueError('invalid or duplicate RTLink call delta')
                target_identity(key, sections, tracked)  # fail closed on bad mappings
                local.add(key)
                item = targets[key]
                item['trials'] += 1
                item['net_delta'] += row['delta']
                item['absolute_delta'] += abs(row['delta'])
                item['with_ram_change'] += int(ram_changed)
                item['with_room_picker'] += int(bool(room_objects))
                item['with_object_context'] += int(object_context)
                item['room_objects'].update(room_objects)
                item['studies'].add(name)
                if len(item['examples']) < 12:
                    item['examples'].append({'study': name, 'trial': trial['name'],
                                             'x': trial['x'], 'y': trial['y'],
                                             'delta': row['delta']})

    if state_checks is not None and set(state_checks) != seen:
        raise ValueError('picker state checks include an unrequested click study')
    ranked = []
    for key, record in targets.items():
        identity = target_identity(key, sections, tracked)
        ranked.append({**identity, 'trials_with_delta': record['trials'],
                       'studies_with_delta': len(record['studies']),
                       'net_call_delta': record['net_delta'],
                       'total_absolute_delta': record['absolute_delta'],
                       'trials_with_ram_change': record['with_ram_change'],
                       'trials_with_room_picker': record['with_room_picker'],
                       'trials_with_object_context': record['with_object_context'],
                       'room_objects': sorted(record['room_objects']),
                       'first_examples': record['examples']})
    ranked.sort(key=lambda row: (-row['trials_with_delta'],
                                  -row['total_absolute_delta'], row['target']))
    return {'studies': len(seen), 'qualified_trials': qualified,
            'inconclusive_trials': inconclusive,
            'ranking_scope': ('matching_picker_state' if state_checks is not None
                              else 'all_qualified'),
            'qualified_state_matched_trials': state_matched if state_checks is not None else None,
            'qualified_state_excluded_trials': state_excluded if state_checks is not None else None,
            'state_exclusion_reasons': dict(sorted(state_exclusions.items())),
            'qualified_missing_censuses': missing,
            'qualified_truncated_censuses': incomplete + recovered,
            'validated_raw_census_pairs': validated,
            'recovered_truncated_censuses': recovered,
            'unrecovered_truncated_censuses': incomplete,
            'ranking_is_complete': incomplete == 0 and missing == 0,
            'targets': ranked}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('studies', nargs='+', type=Path,
                        help='experiment directory or comparison.json (repeatable)')
    parser.add_argument('--plan', type=Path,
                        help='verified build/ghidra/ghidra-plan.json for original offsets')
    parser.add_argument('--tracker', type=Path,
                        default=Path(__file__).resolve().parents[1] /
                                'decomp/functions.tsv')
    parser.add_argument('--state-check', action='append', default=[],
                        metavar='STUDY=REPORT',
                        help='require matching frozen picker condition from a verified '
                             'match_picker_trial_states.py JSON (one per study)')
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        if not 0 <= args.limit <= 10000:
            raise ValueError('--limit must be 0..10000 (0 means all)')
        tracker = {int(row['offset'], 16): row for row in read_tracker(args.tracker)
                   if row['file'] == 'ovl'}
        checks = {}
        for spec in args.state_check:
            study, sep, report = spec.partition('=')
            if not sep or not study or not report:
                raise ValueError('use --state-check STUDY=REPORT')
            study_file = Path(study)
            if study_file.is_dir():
                study_file /= 'comparison.json'
            key = str(study_file.resolve())
            if key in checks:
                raise ValueError(f'duplicate picker state check for {key}')
            checks[key] = Path(report)
        results = rank_studies(args.studies, read_plan(args.plan), tracker,
                               state_checks=checks if checks else None)
        if args.json:
            print(json.dumps(results, indent=2))
            return
        print(f'{results["studies"]} studies, {results["qualified_trials"]} qualified '
              f'trials, {results["inconclusive_trials"]} inconclusive; '
              f'{results["qualified_missing_censuses"]} missing censuses, '
              f'{results["unrecovered_truncated_censuses"]} unrecovered truncated lists, '
              f'{results["recovered_truncated_censuses"]} recovered lists, '
              f'{results["validated_raw_census_pairs"]} validated raw census pairs')
        if checks:
            print(f'Matching frozen picker state: '
                  f'{results["qualified_state_matched_trials"]} included, '
                  f'{results["qualified_state_excluded_trials"]} excluded; '
                  f'reasons: {results["state_exclusion_reasons"]}')
        for item in results['targets'][:args.limit or None]:
            identity = item.get('original', item['target'])
            label = item.get('name') or '(untracked)'
            print(f'{identity} {label}: {item["trials_with_delta"]} trials, '
                  f'net {item["net_call_delta"]:+d} calls, '
                  f'abs {item["total_absolute_delta"]}, '
                  f'RAM changes {item["trials_with_ram_change"]}, '
                  f'room picks {item["trials_with_room_picker"]}, '
                  f'context references {item["trials_with_object_context"]}')
        print('Whole-run RTLink associations only; no click acceptance is established.')
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

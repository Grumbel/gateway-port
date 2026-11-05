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
import json
from pathlib import Path
import re

from decomp_status import read_tracker
from inventory_native_handlers import map_entry


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


def rank_studies(paths, sections=None, tracked=None):
    if not paths:
        raise ValueError('provide one or more completed controlled click studies')
    tracked = tracked or {}
    targets = defaultdict(lambda: {'trials': 0, 'net_delta': 0,
                                   'absolute_delta': 0, 'with_ram_change': 0,
                                   'with_room_picker': 0,
                                   'with_object_context': 0, 'room_objects': set(),
                                   'studies': set(), 'examples': []})
    seen = set()
    qualified = inconclusive = incomplete = missing = 0
    for argument in paths:
        name, trials = study_trials(argument)
        if name in seen:
            raise ValueError(f'duplicate controlled click report {name}')
        seen.add(name)
        for trial in trials:
            if not isinstance(trial, dict) or type(trial.get('comparison_qualified')) is not bool:
                raise ValueError(f'{name}: missing controlled comparison qualification')
            if (not isinstance(trial.get('name'), str) or
                type(trial.get('x')) is not int or type(trial.get('y')) is not int or
                not 0 <= trial['x'] < 640 or not 0 <= trial['y'] < 480):
                raise ValueError(f'{name}: missing controlled click identity')
            if not trial['comparison_qualified']:
                inconclusive += 1
                continue
            qualified += 1
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
            incomplete += int(bool(delta.get('truncated')))
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
            'qualified_missing_censuses': missing,
            'qualified_truncated_censuses': incomplete,
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
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        if not 0 <= args.limit <= 10000:
            raise ValueError('--limit must be 0..10000 (0 means all)')
        tracker = {int(row['offset'], 16): row for row in read_tracker(args.tracker)
                   if row['file'] == 'ovl'}
        results = rank_studies(args.studies, read_plan(args.plan), tracker)
        if args.json:
            print(json.dumps(results, indent=2))
            return
        print(f'{results["studies"]} studies, {results["qualified_trials"]} qualified '
              f'trials, {results["inconclusive_trials"]} inconclusive; '
              f'{results["qualified_missing_censuses"]} missing censuses, '
              f'{results["qualified_truncated_censuses"]} truncated delta lists')
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

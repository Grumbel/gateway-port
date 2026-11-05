#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Check if isolated native click trials reached a captured picker condition.

Compare frozen original picker rectangle identities and dynamic 0x80 skip
bits, never framebuffer pixels or inferred game action acceptance. Reference
capture entry coordinates need not equal the tested trial coordinates.
"""

import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from compare_picker_captures import load_captures
from compare_picker_scene import IDENTITY
from report_picker_follow import parse_picker_calls
from report_picker_scene import read_geometry


def native_state(scene, rows):
    return (tuple(scene[key] for key in
                  ('picture', 'entry', 'aligned', 'room_group', 'boxes', 'rows')),
            [tuple(row[key] for key in IDENTITY) for row in rows],
            [row['flags'] & 0x80 for row in rows])


def compare_state(reference, candidate):
    original_scene, original_rows = reference
    scene, rows = candidate
    if not original_scene['aligned'] or not original_rows:
        return {'status': 'reference_unaligned'}
    if not scene['aligned'] or not rows:
        return {'status': 'trial_unaligned'}
    reference_key, reference_geometry, reference_mask = native_state(
        original_scene, original_rows)
    scene_key, geometry, mask = native_state(scene, rows)
    if scene_key != reference_key:
        return {'status': 'different_scene', 'reference': original_scene['picture'],
                'trial': scene['picture'], 'reference_entry': original_scene['entry'],
                'trial_entry': scene['entry']}
    if geometry != reference_geometry:
        return {'status': 'different_geometry'}
    room_changes = [
        {'group': old['group'], 'slot': old['slot'], 'object': old['object'],
         'reference_disabled': bool(a), 'trial_disabled': bool(b)}
        for old, a, b in zip(original_rows, reference_mask, mask)
        if a != b and old['role'] == 'room']
    other_changes = sum(a != b and old['role'] != 'room'
                        for old, a, b in zip(original_rows, reference_mask, mask))
    other_flag_changes = sum(old['flags'] != new['flags'] and
                             (old['flags'] & 0x80) == (new['flags'] & 0x80)
                             for old, new in zip(original_rows, rows))
    status = ('different_room_skip_mask' if room_changes else
              'different_gui_skip_mask' if other_changes else
              'matching_skip_mask')
    return {'status': status, 'room_skip_changes': room_changes,
            'other_skip_changes': other_changes,
            'non_skip_flag_changes': other_flag_changes,
            'all_flags_equal': all(old['flags'] == new['flags']
                                   for old, new in zip(original_rows, rows))}


def read_reference(captures, reference_call):
    text, metadata = load_captures(captures)
    if reference_call not in metadata:
        raise ValueError(f'reference picker capture call {reference_call} unavailable')
    # Reuse the complete native picker scene validator on the merged original
    # return/geometry records; never parse a .result partially by hand.
    with tempfile.TemporaryDirectory(prefix='gateway-trial-state-ref-') as directory:
        trace = Path(directory) / 'original-picker-returns.tsv'
        trace.write_text(text, encoding='ascii')
        calls = parse_picker_calls(trace)
        geometry = read_geometry(trace, calls)
    if geometry is None or reference_call not in geometry:
        raise ValueError('reference capture has no validated picker geometry')
    return geometry[reference_call], metadata[reference_call]


def match_study(captures, reference_call, study):
    reference, metadata = read_reference(captures, reference_call)
    plan_bytes = (study / 'trial-plan.json').read_bytes()
    comparison_bytes = (study / 'comparison.json').read_bytes()
    plan = json.loads(plan_bytes)
    comparison = json.loads(comparison_bytes)
    if plan.get('format') != 'gateway-picker-click-trials-v1':
        raise ValueError('unsupported trial manifest')
    points = plan.get('points')
    click_ms, post_ms = plan.get('click_ms'), plan.get('post_ms')
    if (not isinstance(points, list) or not 1 <= len(points) <= 32 or
        any(not isinstance(point, list) or len(point) != 2 or
            any(type(value) is not int or value < 0 or value > bound
                for value, bound in zip(point, (639, 479)))
            for point in points) or len({tuple(point) for point in points}) != len(points) or
        type(click_ms) is not int or type(post_ms) is not int or
        not 0 <= click_ms < post_ms <= 3600000 or
        comparison.get('click_ms') != click_ms or
        comparison.get('post_ms') != post_ms or
        comparison.get('points') != len(points) or
        len(comparison.get('trials', [])) != len(points)):
        raise ValueError('inconsistent trial plan and controlled comparison')
    observations = []
    for index, (x, y) in enumerate(points, 1):
        name = f'click-{index:03d}'
        prior = comparison['trials'][index - 1]
        if (prior.get('name') != name or prior.get('x') != x or
            prior.get('y') != y):
            raise ValueError('trial labels differ from recorded point plan')
        trace = study / name / 'picker-follow.tsv'
        if not trace.is_file():
            observations.append({'name': name, 'x': x, 'y': y,
                                 'baseline_qualified': prior['comparison_qualified'],
                                 'calls': [], 'status': 'missing_picker_trace'})
            continue
        calls = parse_picker_calls(trace)
        geometry = read_geometry(trace, calls)
        matched = [(call, item) for call, item in sorted(calls.items())
                   if click_ms <= item['ms'] <= post_ms and
                   item['x'] == x and item['y'] == y]
        entries = []
        for call, item in matched:
            state = (compare_state(reference, geometry[call])
                     if geometry is not None else {'status': 'missing_picker_geometry'})
            entries.append({'call': call, 'ms': item['ms'], 'source': item['source'],
                            'object': item['object'], **state})
        observations.append({'name': name, 'x': x, 'y': y,
                             'baseline_qualified': prior['comparison_qualified'],
                             'calls': entries,
                             'status': 'observed' if matched else 'no_matching_picker_call'})
    return {'format': 'gateway-picker-state-check-v1',
            'reference_call': reference_call,
            'trial_plan_sha256': hashlib.sha256(plan_bytes).hexdigest(),
            'comparison_sha256': hashlib.sha256(comparison_bytes).hexdigest(),
            'reference_scene': reference[0]['picture'],
            'reference_entry': reference[0]['entry'],
            'reference_ms': metadata['guest_ms'],
            'click_ms': click_ms, 'post_ms': post_ms,
            'trials': observations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path, help='original validated picker-entry capture directory')
    parser.add_argument('study', type=Path, help='completed controlled click experiment directory')
    parser.add_argument('--reference-call', type=int, required=True)
    args = parser.parse_args()
    try:
        report = match_study(args.captures, args.reference_call, args.study)
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Batch-compare private native picker entry/return captures.

Capture .bin files stay private and are checked only for size. Original
picker returns and the separately frozen row table are read from .result.
Only matching, PIC/RGN-aligned scene geometries are compared, and native
GUI/control rows are never assigned object identities.
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import tempfile

from compare_picker_scene import IDENTITY, analyze as compare_scenes
from report_picker_follow import parse_picker_calls
from report_picker_scene import read_geometry


REGISTERS = ('AX', 'BX', 'CX', 'DX', 'CS', 'IP', 'SS', 'SP', 'DS', 'ES', 'BP', 'FLAGS')
FIELDS = {'format', 'call', 'guest_ms', 'x', 'y', 'return_cs', 'return_ip',
          'picture', 'picture_entry', *REGISTERS}
NAMES = re.compile(r'call-([0-9]{4})\.meta\Z')


def read_meta(path):
    details = {}
    for line in path.read_text(encoding='ascii').splitlines():
        if line.count('=') != 1:
            raise ValueError(f'{path.name}: malformed metadata')
        key, value = line.split('=', 1)
        if key in details:
            raise ValueError(f'{path.name}: duplicate metadata key')
        details[key] = value
    if set(details) != FIELDS or details['format'] != 'gateway-picker-entry-v1':
        raise ValueError(f'{path.name}: unsupported or incomplete capture')
    if not re.fullmatch(r'[A-Z0-9_.-]*', details['picture']):
        raise ValueError(f'{path.name}: invalid picture name')
    try:
        numbers = {key: int(details[key], 0) for key in REGISTERS +
                   ('return_cs', 'return_ip')}
        numbers.update({key: int(details[key], 10) for key in
                        ('call', 'guest_ms', 'x', 'y', 'picture_entry')})
    except ValueError as error:
        raise ValueError(f'{path.name}: invalid capture number') from error
    if (not all(0 <= numbers[key] <= 65535 for key in REGISTERS +
                ('return_cs', 'return_ip')) or
        numbers['CS'] * 16 + numbers['IP'] != 0x269d1 or
        not 1 <= numbers['call'] <= 128 or
        not 0 <= numbers['guest_ms'] <= 3600000 or
        not -32768 <= numbers['x'] <= 32767 or
        not -32768 <= numbers['y'] <= 32767 or
        not 0 <= numbers['picture_entry'] <= 65535):
        raise ValueError(f'{path.name}: out-of-range capture field')
    return dict(details, **numbers)


def load_captures(directory):
    if not directory.is_dir():
        raise ValueError('no picker capture directory')
    metas = sorted(directory.glob('*.meta'))
    if not metas:
        raise ValueError('no picker capture metadata')
    records, metadata = [], {}
    seen_files = set()
    for expected, path in enumerate(metas, 1):
        match = NAMES.fullmatch(path.name)
        if not match or int(match[1]) != expected:
            raise ValueError('picker capture filenames must be consecutive call-NNNN')
        fields = read_meta(path)
        if fields['call'] != expected:
            raise ValueError(f'{path.name}: capture call ID mismatch')
        base = path.stem
        memory, result = (directory / f'{base}.{suffix}' for suffix in ('bin', 'result'))
        if not memory.is_file() or memory.stat().st_size != 0x100000:
            raise ValueError(f'{base}: missing or incomplete private RAM')
        if not result.is_file():
            raise ValueError(f'{base}: native picker never returned')
        lines = result.read_text(encoding='ascii').splitlines()
        if (not lines or sum(line.startswith('# call=') for line in lines) != 1 or
            not lines[0].startswith('# call=') or
            any(not (line.startswith('# call=') or line.startswith('# scene ') or
                     line.startswith('# picker-row ')) for line in lines)):
            raise ValueError(f'{base}: invalid original picker result')
        records.extend(lines)
        records.append(f'# capture call={expected} base={base}')
        metadata[expected] = fields
        seen_files.update((path.name, memory.name, result.name))
    unexpected = {p.name for p in directory.iterdir()} - seen_files
    if unexpected:
        raise ValueError(f'extraneous capture files: {", ".join(sorted(unexpected))}')
    return '\n'.join(records) + '\n', metadata


def analyze_captures(directory, *, max_pairs=12, sample_limit=12,
                     trace_output=None, oracle_points=None):
    if not 0 <= max_pairs <= 128 or not 0 <= sample_limit <= 10000:
        raise ValueError('invalid comparison or sample limit')
    text, metadata = load_captures(directory)
    with tempfile.TemporaryDirectory(prefix='gateway-picker-batch-') as temp:
        trace = Path(temp) / 'capture-summary.tsv'
        trace.write_text(text, encoding='ascii')
        calls = parse_picker_calls(trace)
        scenes = read_geometry(trace, calls)
        if scenes is None or set(calls) != set(metadata):
            raise ValueError('missing captured picker scene or call')
        for number, fields in metadata.items():
            call, (scene, _) = calls[number], scenes[number]
            if (call['x'] != fields['x'] or call['y'] != fields['y'] or
                call['ms'] < fields['guest_ms'] or
                scene['picture'] != (fields.get('picture') or '-') or
                scene['entry'] != fields['picture_entry']):
                raise ValueError(f'call-{number:04d}: entry/return provenance differs')

        groups = defaultdict(list)
        for number, (scene, rows) in scenes.items():
            if not scene['aligned'] or not rows:
                continue
            identity = (scene['picture'], scene['entry'], scene['room_group'],
                        tuple(tuple(row[key] for key in IDENTITY) for row in rows))
            # Conditional state is the actual skip mask. Ignore unrelated
            # flag bits when choosing potentially different hitbox winners.
            groups[identity].append((number, tuple(row['flags'] & 0x80 for row in rows)))
        transitions = []
        unique_states = 0
        for series in groups.values():
            series.sort()
            unique_states += len({mask for _, mask in series})
            previous = series[0]
            for current in series[1:]:
                if current[1] != previous[1]:
                    transitions.append((previous[0], current[0]))
                previous = current
        transitions.sort(key=lambda pair: (pair[1], pair[0]))
        comparisons = [compare_scenes(trace, before, after, sample_limit)
                       for before, after in transitions[:max_pairs]]
        if trace_output is not None:
            if trace_output.exists():
                raise ValueError('refusing to overwrite capture trace')
            trace_output.write_text(text, encoding='ascii')
        if oracle_points is not None:
            if oracle_points.exists() or oracle_points == trace_output:
                raise ValueError('refusing to overwrite oracle points')
            positions = sorted({(call, point['x'], point['y'])
                                for item in comparisons
                                for point in item['samples']
                                for call in (item['before_call'], item['after_call'])})
            oracle_points.write_text('call\tx\ty\n' + ''.join(
                f'{call}\t{x}\t{y}\n' for call, x, y in positions), encoding='ascii')
    return {'captured_calls': len(metadata), 'aligned_calls': sum(
                scene['aligned'] for scene, _ in scenes.values()),
            'unique_aligned_states': unique_states,
            'conditional_transitions': len(transitions),
            'compared_transitions': len(comparisons),
            'comparisons': comparisons}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path)
    parser.add_argument('--max-pairs', type=int, default=12)
    parser.add_argument('--sample-limit', type=int, default=12)
    parser.add_argument('--trace-output', type=Path,
                        help='optional private merged #scene/#picker-row trace')
    parser.add_argument('--oracle-points', type=Path,
                        help='save per-capture native picker coordinates for the Unicorn oracle')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        report = analyze_captures(args.captures, max_pairs=args.max_pairs,
                                  sample_limit=args.sample_limit,
                                  trace_output=args.trace_output,
                                  oracle_points=args.oracle_points)
    except (OSError, ValueError, KeyError, UnicodeError) as error:
        parser.exit(1, f'{error}\n')
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f'Picker captures: {report["captured_calls"]}; '
              f'aligned calls: {report["aligned_calls"]}; '
              f'distinct aligned states: {report["unique_aligned_states"]}')
        print(f'Conditional transitions: {report["conditional_transitions"]} '
              f'(compared {report["compared_transitions"]})')
        for item in report['comparisons']:
            print(f'  calls {item["before_call"]}->{item["after_call"]}: '
                  f'{item["picture"]} frame={item["entry"]} '
                  f'H changed={item["h_changed_pixels"]}, '
                  f'native changed={item["native_changed_pixels"]}')
            for point in item['samples']:
                print(f'    ({point["x"]},{point["y"]}): '
                      f'{point["h_before"]} -> {point["h_after"]}')
        print('Positions are candidates for separate same-save clicks, not acceptance proof.')


if __name__ == '__main__':
    main()

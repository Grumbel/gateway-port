#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Audit original picker returns against the complete H winner and room candidates.

Reads # scene / # picker-row records from --trace-picker-follow. The row data
is sampled at picker entry, while the return is observed from original code.
The H winner follows all native rows; the legacy scene-only room candidates
remain an independent diagnostic. Neither proves rendering or acceptance.
"""

import argparse
from collections import Counter
from pathlib import Path
import re

from report_picker_follow import parse_picker_calls


SCENE_KEYS = {'call', 'picture', 'entry', 'aligned', 'room_group', 'boxes', 'rows'}
ROW_KEYS = {'call', 'group', 'slot', 'index', 'kind', 'flags', 'x0', 'y0',
            'x1', 'y1', 'role', 'object', 'selected'}
ROLES = {'room', 'control', 'fallback', 'unmapped'}


def fields(line, prefix, required):
    parts = line[len(prefix):].split()
    if len(parts) != len(required) or any(part.count('=') != 1 for part in parts):
        raise ValueError(f'malformed {prefix.strip()} record')
    result = dict(part.split('=', 1) for part in parts)
    if set(result) != required:
        raise ValueError(f'missing or duplicate {prefix.strip()} fields')
    return result


def read_geometry(path, calls):
    scenes, rows = {}, {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.startswith('# scene '):
            item = fields(line, '# scene ', SCENE_KEYS)
            try:
                for name in ('call', 'entry', 'aligned', 'room_group', 'boxes', 'rows'):
                    item[name] = int(item[name])
            except ValueError as error:
                raise ValueError('invalid scene numeric field') from error
            call = item['call']
            if (call not in calls or call in scenes or item['entry'] < 0 or
                item['aligned'] not in (0, 1) or not 0 <= item['boxes'] <= 65535 or
                not 0 <= item['rows'] <= 210 or
                not re.fullmatch(r'[A-Z0-9_.-]+', item['picture']) or
                (item['aligned'] == 0 and item['room_group'] != -1) or
                (item['aligned'] == 1 and not 0 <= item['room_group'] <= 5)):
                raise ValueError('invalid or duplicate scene snapshot')
            scenes[call] = item
        elif line.startswith('# picker-row '):
            item = fields(line, '# picker-row ', ROW_KEYS)
            try:
                for name in ROW_KEYS - {'role'}:
                    item[name] = int(item[name])
            except ValueError as error:
                raise ValueError('invalid picker-row numeric field') from error
            if (item['call'] not in calls or item['role'] not in ROLES or
                not 0 <= item['group'] <= 5 or not 0 <= item['slot'] <= 35 or
                not 0 <= item['index'] < 64 or not 1 <= item['kind'] <= 6 or
                not 0 <= item['flags'] <= 255 or
                not -32768 <= item['x0'] <= item['x1'] <= 32767 or
                not -32768 <= item['y0'] <= item['y1'] <= 32767 or
                not 0 <= item['object'] <= 734 or item['selected'] not in (0, 1)):
                raise ValueError('invalid picker-row geometry or identity')
            rows.setdefault(item['call'], []).append(item)
    if not scenes and not rows:
        return None  # Old traces deliberately remain readable.
    if set(scenes) != set(calls):
        raise ValueError('missing entry-time scene snapshots for some picker calls')
    for call, scene in scenes.items():
        if ('native_ax' not in calls[call] or calls[call]['object'] is None):
            raise ValueError('scene geometry requires a new-style native picker call marker')
        table = rows.get(call, [])
        if len(table) != scene['rows']:
            raise ValueError(f'picker-row count mismatch for call {call}')
        group_slot = set()
        for row in table:
            pair = row['group'], row['slot']
            if pair in group_slot:
                raise ValueError('duplicate picker group/slot')
            group_slot.add(pair)
            room = (scene['aligned'] == 1 and row['slot'] > 0 and
                    row['group'] == scene['room_group'] and
                    row['slot'] <= scene['boxes'])
            expected = ('fallback' if row['slot'] == 0 else
                        'unmapped' if scene['aligned'] == 0 else
                        'room' if room else 'control')
            if row['role'] != expected or (not room and row['object']):
                raise ValueError('picker-row role does not match frozen room association')
            native = calls[call]
            is_selected = (int(native['native_ax']) == 1 and
                           int(native['group']) == row['group'] and
                           int(native['slot']) == row['slot'] and
                           int(native['kind']) == row['kind'])
            if row['selected'] != is_selected:
                raise ValueError('picker-row selected flag contradicts native return')
        if sum(row['selected'] for row in table) > 1:
            raise ValueError('multiple native-selected picker rows')
    return {call: (scene, rows.get(call, [])) for call, scene in scenes.items()}


def contains(row, x, y):
    return not row['flags'] & 0x80 and row['x0'] <= x <= row['x1'] and \
        row['y0'] <= y <= row['y1']


def describe_owner(row):
    if row is None:
        return 'none'
    if row['object'] == 0:
        return f'anonymous slot {row["slot"]}'
    return f'object #{row["object"]} (slot {row["slot"]})'


def describe_winner(row):
    if row is None:
        return 'none'
    if row['role'] == 'room':
        return describe_owner(row)
    return f'{row["role"]} g{row["group"]}/s{row["slot"]}'


def report(path, limit=40):
    if limit < 0:
        raise ValueError('--limit must be nonnegative')
    calls = parse_picker_calls(path)
    if not calls:
        raise ValueError('trace has no native picker call markers')
    geometry = read_geometry(path, calls)
    if geometry is None:
        return ('No entry-time picker geometry in this legacy follow trace.\n'
                'Capture another --trace-picker-follow run with the updated runtime.')
    counts = Counter()
    lines = []
    for call, item in sorted(calls.items()):
        scene, rows = geometry[call]
        x, y = item['x'], item['y']
        # Keep the legacy room-only candidate picture for diagnostics, but H
        # now renders the last enabled row across *all* groups at this pixel.
        room_rows = [row for row in rows if row['role'] == 'room' and contains(row, x, y)
                     and 261 <= x < 640 and 0 <= y < 232]
        candidate = room_rows[-1] if room_rows else None
        native = next((row for row in rows if row['selected']), None)
        overall = next((row for row in reversed(rows) if contains(row, x, y)), None)
        winner = overall if 0 <= x < 640 and 0 <= y < 480 else None
        source = item['source']
        if winner:
            counts['highlighted'] += 1
        elif not rows:
            counts['unavailable'] += 1
        else:
            counts['unmarked'] += 1
        if native and native != winner:
            counts['native_not_h'] += 1
        if candidate:
            counts['room_candidates'] += 1
        elif scene['aligned']:
            counts['room_unmarked_all'] += 1
        else:
            counts['room_unavailable'] += 1
        if source == 'room' and scene['aligned']:
            if not candidate:
                counts['room_unmarked'] += 1
            elif item['object'] != candidate['object']:
                counts['room_different_object'] += 1
        if source in ('control', 'fallback') and candidate:
            counts['gui_override'] += 1
        if not scene['rows']:
            counts['model_unavailable'] += 1
        elif native != overall:
            counts['native_model_difference'] += 1
        if limit == 0 or len(lines) < limit:
            h = describe_winner(winner) if rows else 'unavailable'
            room = describe_owner(candidate) if scene['aligned'] else 'unavailable'
            selected = (f'{native["role"]} g{native["group"]}/s{native["slot"]}'
                        if native else 'none/unresolved')
            modeled = ('unavailable' if not scene['rows'] else
                       'match' if native == overall else 'DIFFER')
            lines.append(f'  call {call} {item["ms"]}ms ({x},{y}): '
                         f'native={source} [{selected}], H={h}, '
                         f'room-candidate={room}, room-overlaps={len(room_rows)}, '
                         f'picker-model={modeled}')
    output = [f'Original picker geometry: {len(calls)} calls, '
              f'{counts["highlighted"]} H winners, {counts["unmarked"]} unmarked, '
              f'{counts["unavailable"]} unavailable (no native rows)',
              f'Native selections absent from H winner: {counts["native_not_h"]}',
              f'Legacy scene-only candidates: {counts["room_candidates"]} marked, '
              f'{counts["room_unmarked_all"]} unmarked, '
              f'{counts["room_unavailable"]} unknown (unaligned scene)',
              f'Native room selections absent from scene-only candidate: '
              f'{counts["room_unmarked"]}',
              f'Native room object differs from scene-only owner: '
              f'{counts["room_different_object"]}',
              f'GUI/fallback selections over scene-only room: {counts["gui_override"]}',
              f'Native selection differs from frozen picker table: '
              f'{counts["native_model_difference"]} '
              f'({counts["model_unavailable"]} unavailable)',
              '']
    output.extend(lines)
    if len(lines) < len(calls):
        output.append(f'  ... {len(calls) - len(lines)} more; use --limit 0')
    output.extend(['', 'H is the modeled last-enabled winner across all native groups; '
                   'room-candidate is the legacy room-only picture diagnostic. Native '
                   'is the original picker return, not proof that a click was consumed '
                   'by a GUI or game handler.'])
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='--trace-picker-follow TSV')
    parser.add_argument('--limit', type=int, default=40,
                        help='number of calls to list (0: all)')
    options = parser.parse_args()
    try:
        print(report(options.trace, options.limit))
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

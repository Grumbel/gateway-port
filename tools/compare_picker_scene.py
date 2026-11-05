#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compare complete picker winners and legacy room candidates between snapshots.

Both snapshots must be from the same original picture frame, with a verified
room group and identical rectangle geometry. Only entry-time skip flags may
change. This gives reproducible coordinates for *future* original-code click
tests, not evidence of what the game actually drew or accepted.
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path

from report_picker_follow import parse_picker_calls
from report_picker_scene import read_geometry


SCENE_X = 261
SCENE_Y = 0
SCENE_WIDTH = 640 - SCENE_X
SCENE_HEIGHT = 232
IDENTITY = ('group', 'slot', 'index', 'kind', 'x0', 'y0', 'x1', 'y1',
            'role', 'object')


def paint(rows, origin_x=SCENE_X, origin_y=SCENE_Y,
          raster_width=SCENE_WIDTH, raster_height=SCENE_HEIGHT):
    """Rasterize native rows, legacy room candidates, and complete H identity."""
    pixels = raster_width * raster_height
    native, highlighted = [None] * pixels, [None] * pixels
    identities = {}
    for row in rows:
        selected = (row['role'], row['group'], row['slot'])
        owned = (('object', row['object']) if row['object'] else
                 ('anonymous', row['group'], row['slot']))
        identities[selected] = owned if row['role'] == 'room' else selected
        if row['flags'] & 0x80:
            continue
        left = max(origin_x, row['x0'])
        right = min(origin_x + raster_width - 1, row['x1'])
        top = max(origin_y, row['y0'])
        bottom = min(origin_y + raster_height - 1, row['y1'])
        if left > right or top > bottom:
            continue
        width = right - left + 1
        for y in range(top, bottom + 1):
            start = (y - origin_y) * raster_width + left - origin_x
            end = start + width
            native[start:end] = [selected] * width
            if row['role'] == 'room':
                highlighted[start:end] = [owned] * width
    # A later GUI/fallback rectangle masks a room row in H even though the
    # independent room-only candidate layer retains that room association.
    display = [identities[row] if row is not None else None for row in native]
    return native, highlighted, display


def label_owner(value):
    if value is None:
        return 'none'
    if value[0] == 'object':
        return f'object #{value[1]}'
    if value[0] == 'anonymous':
        return f'anonymous g{value[1]}/s{value[2]}'
    return f'{value[0]} g{value[1]}/s{value[2]}'


def label_native(value):
    if value is None:
        return 'none'
    return f'{value[0]} g{value[1]}/s{value[2]}'


def analyze(path, before, after, limit=12):
    if before == after or not 0 <= limit <= 10000:
        raise ValueError('choose two different calls and a limit from 0 to 10000')
    calls = parse_picker_calls(path)
    geometry = read_geometry(path, calls)
    if geometry is None:
        raise ValueError('trace contains no frozen picker geometry; capture a new follow trace')
    if before not in geometry or after not in geometry:
        raise ValueError('requested call has no frozen scene snapshot')
    first_scene, first_rows = geometry[before]
    second_scene, second_rows = geometry[after]
    comparable = ('picture', 'entry', 'aligned', 'room_group', 'boxes', 'rows')
    if (any(first_scene[field] != second_scene[field] for field in comparable) or
        not first_scene['aligned'] or not first_rows):
        raise ValueError('different or unaligned scenes: cannot attribute changes to flags')
    if [tuple(row[key] for key in IDENTITY) for row in first_rows] != [
            tuple(row[key] for key in IDENTITY) for row in second_rows]:
        raise ValueError('picker rectangle geometry changed; flags-only comparison is unsafe')

    toggles = []
    for left, right in zip(first_rows, second_rows):
        if left['flags'] != right['flags']:
            toggles.append({'role': left['role'], 'group': left['group'],
                            'slot': left['slot'], 'object': left['object'],
                            'before_flags': left['flags'], 'after_flags': right['flags'],
                            'enabled_changed': bool((left['flags'] ^ right['flags']) & 0x80)})

    first_native, first_h, first_display = paint(first_rows)
    second_native, second_h, second_display = paint(second_rows)
    full_first_native, _, full_first_display = paint(first_rows, 0, 0, 640, 480)
    full_second_native, _, full_second_display = paint(second_rows, 0, 0, 640, 480)
    changed_h = changed_native = h_gained = h_lost = h_swapped = native_only = 0
    changed_display = display_gained = display_lost = display_swapped = 0
    native_only_display = 0
    samples = defaultdict(list)
    for offset, (old_h, new_h, old_n, new_n, old_d, new_d) in enumerate(zip(
            first_h, second_h, first_native, second_native,
            first_display, second_display)):
        different_h = old_h != new_h
        different_native = old_n != new_n
        different_display = old_d != new_d
        if different_h:
            changed_h += 1
            if old_h is None:
                h_gained += 1
            elif new_h is None:
                h_lost += 1
            else:
                h_swapped += 1
        if different_display:
            changed_display += 1
            if old_d is None:
                display_gained += 1
            elif new_d is None:
                display_lost += 1
            else:
                display_swapped += 1
        if different_native:
            changed_native += 1
            if not different_h:
                native_only += 1
            if not different_display:
                native_only_display += 1
        if different_h or different_native:
            samples[(old_h, new_h, old_n, new_n, old_d, new_d)].append(
                (SCENE_X + offset % SCENE_WIDTH, offset // SCENE_WIDTH))

    # A representative is always an *actual* differing pixel, even for
    # disjoint L-shaped regions whose bounding-box midpoint would be wrong.
    groups = sorted(samples.items(), key=lambda item: (
        item[0][0] == item[0][1], -len(item[1]), repr(item[0])))
    points = []
    for (old_h, new_h, old_n, new_n, old_d, new_d), coords in groups[:limit or None]:
        x, y = coords[len(coords) // 2]
        points.append({'x': x, 'y': y, 'pixels': len(coords),
                       'h_before': label_owner(old_h), 'h_after': label_owner(new_h),
                       'display_before': label_owner(old_d),
                       'display_after': label_owner(new_d),
                       'native_before': label_native(old_n),
                       'native_after': label_native(new_n)})
    full_display_changed = full_native_changed = full_native_only_display = 0
    full_gained = full_lost = full_swapped = 0
    full_groups = defaultdict(list)
    for offset, (old_d, new_d, old_n, new_n) in enumerate(zip(
            full_first_display, full_second_display,
            full_first_native, full_second_native)):
        display_different = old_d != new_d
        native_different = old_n != new_n
        if display_different:
            full_display_changed += 1
            if old_d is None:
                full_gained += 1
            elif new_d is None:
                full_lost += 1
            else:
                full_swapped += 1
        if native_different:
            full_native_changed += 1
            if not display_different:
                full_native_only_display += 1
        if display_different or native_different:
            full_groups[(old_d, new_d, old_n, new_n)].append((offset % 640, offset // 640))
    full_ranked = sorted(full_groups.items(), key=lambda item: (-len(item[1]), repr(item[0])))
    full_points = []
    for (old_d, new_d, old_n, new_n), coords in full_ranked[:limit or None]:
        x, y = coords[len(coords) // 2]
        full_points.append({'x': x, 'y': y, 'pixels': len(coords),
                            'display_before': label_owner(old_d),
                            'display_after': label_owner(new_d),
                            'native_before': label_native(old_n),
                            'native_after': label_native(new_n)})
    return {'picture': first_scene['picture'], 'entry': first_scene['entry'],
            'before_call': before, 'after_call': after, 'total_pixels': SCENE_WIDTH * SCENE_HEIGHT,
            'full_window_pixels': 640 * 480,
            'full_display_changed_pixels': full_display_changed,
            'full_display_gained_pixels': full_gained,
            'full_display_lost_pixels': full_lost,
            'full_display_retargeted_pixels': full_swapped,
            'full_native_changed_pixels': full_native_changed,
            'full_native_only_display_changed_pixels': full_native_only_display,
            'full_sample_groups': len(full_groups), 'full_samples': full_points,
            'flags_changed': toggles, 'native_changed_pixels': changed_native,
            'display_changed_pixels': changed_display,
            'display_gained_pixels': display_gained,
            'display_lost_pixels': display_lost,
            'display_retargeted_pixels': display_swapped,
            'native_only_display_changed_pixels': native_only_display,
            # Historical h_* fields remain the room-only candidate raster
            # for existing capture consumers, not the current interactive H.
            'h_changed_pixels': changed_h, 'h_gained_pixels': h_gained,
            'h_lost_pixels': h_lost, 'h_retargeted_pixels': h_swapped,
            'native_only_changed_pixels': native_only, 'sample_groups': len(groups),
            'samples': points}


def format_report(data):
    result = [f'Original picker scene {data["picture"]} frame {data["entry"]}, '
              f'calls {data["before_call"]} -> {data["after_call"]}',
              f'Changed picker-row flags: {len(data["flags_changed"])}']
    for row in data['flags_changed']:
        change = 'skip bit changed' if row['enabled_changed'] else 'other flag bits only'
        result.append(f'  {row["role"]} g{row["group"]}/s{row["slot"]} '
                      f'object={row["object"]}: 0x{row["before_flags"]:02x} -> '
                      f'0x{row["after_flags"]:02x} ({change})')
    result.extend([
        f'Full-window H winner pixels changed: {data["full_display_changed_pixels"]} '
        f'(gained {data["full_display_gained_pixels"]}, '
        f'lost {data["full_display_lost_pixels"]}, '
        f'retargeted {data["full_display_retargeted_pixels"]})',
        f'Full-window native row pixels changed: {data["full_native_changed_pixels"]} '
        f'({data["full_native_only_display_changed_pixels"]} with H unchanged)',
        f'H native-winner pixels changed: {data["display_changed_pixels"]} '
        f'(gained {data["display_gained_pixels"]}, lost {data["display_lost_pixels"]}, '
        f'retargeted {data["display_retargeted_pixels"]})',
        f'Legacy room-candidate pixels changed: {data["h_changed_pixels"]} '
        f'(gained {data["h_gained_pixels"]}, lost {data["h_lost_pixels"]}, '
        f'retargeted {data["h_retargeted_pixels"]})',
        f'Native winner pixels changed: {data["native_changed_pixels"]} '
        f'({data["native_only_display_changed_pixels"]} with H unchanged; '
        f'{data["native_only_changed_pixels"]} with room candidates unchanged)',
        f'Representative differing pixels: {len(data["samples"])} '
        f'of {data["sample_groups"]} signatures:',
    ])
    for item in data['samples']:
        result.append(f'  ({item["x"]},{item["y"]}) [{item["pixels"]} pixels]: '
                      f'H {item["display_before"]} -> {item["display_after"]}; '
                      f'room {item["h_before"]} -> {item["h_after"]}; '
                      f'native {item["native_before"]} -> {item["native_after"]}')
    result.append(f'Full-window differing points: {len(data["full_samples"])} '
                  f'of {data["full_sample_groups"]} signatures:')
    for item in data['full_samples']:
        result.append(f'  ({item["x"]},{item["y"]}) [{item["pixels"]} pixels]: '
                      f'H {item["display_before"]} -> {item["display_after"]}; '
                      f'native {item["native_before"]} -> {item["native_after"]}')
    result.extend(['', 'Positions are logical VGA coordinates for separate '
                   'native click experiments from the same restored game state. '
                   'Flag changes and predicted picker winners do not prove '
                   'rendering or action acceptance.'])
    return '\n'.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='--trace-picker-follow TSV')
    parser.add_argument('--before', type=int, required=True, help='picker call ID before change')
    parser.add_argument('--after', type=int, required=True, help='picker call ID after change')
    parser.add_argument('--limit', type=int, default=12, help='max representative signatures; 0: all')
    parser.add_argument('--json', action='store_true', help='emit machine-readable coordinates')
    options = parser.parse_args()
    try:
        data = analyze(options.trace, options.before, options.after, options.limit)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(data, indent=2) if options.json else format_report(data))


if __name__ == '__main__':
    main()

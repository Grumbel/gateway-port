#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Summarize native Gateway picker calls, source rows and overlap hotspots.

Unlike check_picker_trace.py this is an investigation report: mismatches are
shown, not rejected. A returned picker selection is NOT an accepted game action.
"""

import argparse
from collections import Counter
import csv
from pathlib import Path

from check_picker_trace import FIELDS, LEGACY_FIELDS, check_provenance


def read_trace(path):
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    rows = [line for line in lines if line and not line.startswith('#')]
    reader = csv.DictReader(rows, delimiter='\t')
    columns = tuple(reader.fieldnames or ())
    if columns not in (FIELDS, LEGACY_FIELDS):
        raise ValueError('unexpected picker trace columns')
    results = list(reader)
    for number, row in enumerate(results, 2):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f'invalid TSV row {number}')
        if row['verdict'] not in ('match', 'mismatch', 'unmodeled'):
            raise ValueError(f'invalid picker verdict on row {number}')
        if columns == FIELDS:
            check_provenance(row, number)
    return results, columns == FIELDS


def format_result(row, prefix):
    return (f"{prefix} AX={row[prefix + '_result']} "
            f"g={row[prefix + '_group']} s={row[prefix + '_slot']} "
            f"k={row[prefix + '_kind']}")


def report(path, limit=20):
    rows, extended = read_trace(path)
    verdicts = Counter(row['verdict'] for row in rows)
    result = [f'Calls: {len(rows)}, match={verdicts["match"]}, '
              f'mismatch={verdicts["mismatch"]}, unmodeled={verdicts["unmodeled"]}']
    if extended:
        sources = Counter(row['native_source'] for row in rows)
        result.append('Native source (from original return, not cursor hover):')
        for source, count in sorted(sources.items(), key=lambda item: (-item[1], item[0])):
            result.append(f'  {source:12s} {count}')
        overlaps = [row for row in rows if int(row['enabled_overlaps']) > 1]
        result.append(f'Calls with competing enabled rectangles: {len(overlaps)}')
        hotspots = Counter((row['native_group'], row['native_slot'],
                            row['native_index'], row['native_source']) for row in overlaps)
        for (group, slot, index, source), count in hotspots.most_common(limit):
            result.append(f'  g={group} s={slot} index={index} {source}: {count} calls')
    else:
        result.append('No source information (legacy trace; capture a new trace for provenance).')
    problems = [row for row in rows if row['verdict'] == 'mismatch' or
                extended and row['native_source'] == 'unresolved']
    result.append(f'Mismatches and unresolved native rows: {len(problems)}')
    for row in problems[:limit]:
        source = f" source={row['native_source']}" if extended else ''
        result.append(f"  {row['ms']}ms at ({row['x']},{row['y']}): "
                      f"{format_result(row, 'model')}; {format_result(row, 'native')}"
                      f'{source}')
    if len(problems) > limit:
        result.append(f'  ... {len(problems) - limit} further calls (increase --limit)')
    result.append('Note: native picker selection does not prove later action dispatch.')
    return '\n'.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--limit', type=int, default=20,
                        help='maximum displayed overlap hotspots and mismatches')
    args = parser.parse_args()
    if args.limit < 0:
        parser.error('--limit must be nonnegative')
    try:
        print(report(args.trace, args.limit))
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

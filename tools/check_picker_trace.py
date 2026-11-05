#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Validate a --trace-picker TSV against the native routine's own outputs.

"match" is a sampled pointer-selection equivalence, not proof that a click
was consumed by the game's later GUI or story handlers.
"""

import argparse
import csv
from pathlib import Path


LEGACY_FIELDS = ('ms', 'x', 'y', 'model_result', 'model_group', 'model_slot',
                 'model_kind', 'native_result', 'native_group', 'native_slot',
                 'native_kind', 'verdict')
FIELDS = LEGACY_FIELDS + ('native_source', 'native_index', 'native_flags',
                          'native_object', 'enabled_overlaps')
SOURCES = {'unmodeled', 'none', 'fallback', 'room', 'control',
           'unmapped', 'unresolved'}


def check_provenance(row, line_number):
    source = row['native_source']
    if source not in SOURCES:
        raise ValueError(f'invalid picker source on row {line_number}: {source}')
    index, flags, object_id, overlaps = (
        int(row[field]) for field in
        ('native_index', 'native_flags', 'native_object', 'enabled_overlaps'))
    has_row = 0 <= index < 64 and 0 <= flags <= 255
    no_row = index == -1 and flags == -1
    if not (has_row or no_row) or not (0 <= object_id <= 65535) or overlaps < 0:
        raise ValueError(f'invalid picker provenance values on row {line_number}')
    if (source in ('unmodeled', 'none') and not no_row or
        source in ('fallback', 'room', 'control', 'unmapped') and not has_row or
        source != 'room' and object_id != 0 or
        source in ('fallback', 'room', 'control', 'unmapped') and
            ((flags & 0x80) or overlaps < 1) or
        source == 'none' and int(row['native_result']) != 0 or
        source == 'unmodeled' and row['verdict'] != 'unmodeled' or
        row['verdict'] == 'unmodeled' and source != 'unmodeled' or
        row['verdict'] == 'match' and int(row['native_result']) == 1 and
            source in ('none', 'unmodeled', 'unresolved')):
        raise ValueError(f'inconsistent picker provenance on row {line_number}')


def check_trace(path, allow_unmodeled=False):
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    summaries = [line for line in lines if line.startswith('# picker-summary ')]
    if len(summaries) != 1:
        raise ValueError('missing or duplicate picker summary')
    summary = dict(field.split('=', 1) for field in summaries[0].split()[2:])
    required = ('calls', 'matched', 'mismatched', 'unmodeled', 'errors', 'unfinished')
    if set(summary) != set(required):
        raise ValueError('invalid picker summary fields')
    summary = {name: int(summary[name]) for name in required}
    table = [line for line in lines if not line.startswith('#')]
    reader = csv.DictReader(table, delimiter='\t')
    columns = tuple(reader.fieldnames or ())
    if columns not in (LEGACY_FIELDS, FIELDS):
        raise ValueError('unexpected picker trace columns')
    counts = {'match': 0, 'mismatch': 0, 'unmodeled': 0}
    for line_number, row in enumerate(reader, start=2):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f'invalid TSV row {line_number}')
        verdict = row['verdict']
        if verdict not in counts:
            raise ValueError(f'invalid picker verdict on row {line_number}: {verdict}')
        counts[verdict] += 1
        if columns == FIELDS:
            check_provenance(row, line_number)
        if verdict != 'unmodeled':
            model = tuple(int(row[f'model_{field}'])
                          for field in ('result', 'group', 'slot', 'kind'))
            native = tuple(int(row[f'native_{field}'])
                           for field in ('result', 'group', 'slot', 'kind'))
            if (model == native) != (verdict == 'match'):
                raise ValueError(f'inconsistent picker verdict on row {line_number}')
    if (sum(counts.values()) != summary['calls'] or
        counts['match'] != summary['matched'] or
        counts['mismatch'] != summary['mismatched'] or
        counts['unmodeled'] != summary['unmodeled']):
        raise ValueError('picker summary does not match recorded calls')
    if not counts['match']:
        raise ValueError('no model-verified native picker calls')
    if counts['mismatch'] or summary['unfinished'] or summary['errors']:
        raise ValueError(f'picker disagreements, errors or unfinished calls: {summary}')
    if counts['unmodeled'] and not allow_unmodeled:
        raise ValueError(f'unmodeled native picker calls: {counts["unmodeled"]}')
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--allow-unmodeled', action='store_true',
                        help='permit calls before matching PIC/RGN scene setup')
    args = parser.parse_args()
    try:
        counts = check_trace(args.trace, args.allow_unmodeled)
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    print('picker trace verified: ' + ', '.join(f'{k}={v}' for k, v in counts.items()))


if __name__ == '__main__':
    main()

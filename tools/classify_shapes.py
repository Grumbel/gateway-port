#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Conservatively classify original Gateway routines from Ghidra static facts.

Only handler/action/callback shapes are updated; original bytes and Ghidra
pseudocode remain in build/. This is a triage heuristic, NOT a verified ABI or
routine-level behavioral analysis. Manual shape assignments are preserved.
"""

import argparse
import collections
import json
from pathlib import Path
import sys

from decomp_status import DEFAULT_TRACKER, TrackerError, key_of, read_tracker, write_tracker

KINDS = ('handler', 'action', 'callback')
TRANSLATOR_THRESHOLD = 0.70


def measure(fact):
    if (fact['has_loop'] or fact['indexed_memory'] or fact['computed_calls'] or
            fact['computed_jumps'] > 1 or fact['instruction_count'] > 400):
        return 'complex'
    if fact['direct_memory']:
        # Distinguishing constants from shared globals requires proof; treat
        # every unproven DS/ES read as shared state, including tables.
        return 'direct-memory'
    return 'formulaic'


def classify(tracker_path, facts_path, write=False):
    report = json.loads(facts_path.read_text())
    if report.get('schema_version') != 1 or not isinstance(report.get('functions'), list):
        raise TrackerError(f'{facts_path}: not an ExportGatewayFacts.py report')
    facts = {}
    for item in report['functions']:
        identity = item['identity']
        key = identity['file'], identity['offset']
        if key in facts or key[0] not in ('exe', 'ovl'):
            raise TrackerError(f'{facts_path}: duplicate or invalid function {key}')
        facts[key] = item
    rows = read_tracker(tracker_path)
    counts = collections.Counter()
    changed = 0
    for row in rows:
        if row['kind'] not in KINDS:
            continue
        key = key_of(row)
        fact = facts.get(key)
        if fact is None:
            raise TrackerError(f'{key}: missing Ghidra function facts')
        if fact['instruction_count'] < 1:
            raise TrackerError(f'{key}: no decoded instructions')
        measured = measure(fact)
        if row['shape'] and row['shape'] != measured:
            raise TrackerError(f'{key}: existing {row["shape"]} conflicts with {measured}; '
                               'review manually, do not overwrite')
        if not row['shape']:
            row['shape'] = measured
            changed += 1
        if row['kind'] in ('handler', 'action'):
            counts[measured] += 1
    total = sum(counts.values())
    if not total:
        raise TrackerError('no handler/action routines classified')
    formulaic_share = counts['formulaic'] / total
    if write:
        write_tracker(tracker_path, rows)
    return changed, counts, formulaic_share


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('facts', type=Path, help='build/ghidra/ghidra-facts.json')
    parser.add_argument('--tracker', type=Path, default=DEFAULT_TRACKER)
    parser.add_argument('--write', action='store_true',
                        help='fill blank script-routine shape fields (validated atomically)')
    args = parser.parse_args(argv)
    try:
        changed, counts, formulaic_share = classify(args.tracker, args.facts, args.write)
        total = sum(counts.values())
        print(f'script shapes: {total} handlers/actions; '
              f'formulaic={counts["formulaic"]}, '
              f'direct-memory={counts["direct-memory"]}, complex={counts["complex"]}')
        print(f'formulaic={formulaic_share:.1%}; '
              f'{"build translator" if formulaic_share >= TRANSLATOR_THRESHOLD else "hand-port first"} '
              f'(70% decision threshold; conservative static estimate)')
        print(f'{changed} fields {"written" if args.write else "would be written"}')
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f'classify_shapes: {error}\n')


if __name__ == '__main__':
    sys.exit(main())

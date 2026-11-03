#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Maintain the Gateway native-code recovery tracker.

The tracker, decomp/functions.tsv, lists original native routines by their
ORIGINAL file offset (GATEMAIN.EXE for resident code, GATEMAIN.OVL plus RTLink
section for overlay code) and records how far each one has been recovered.
It holds addresses, sizes, IDs and project-chosen names only: never code
bytes, Ghidra pseudocode, or game text such as object names. See
docs/decompilation-plan.md, sections 3 and 4.

Commands:
  seed     merge a Ghidra function export, the native handler inventory and
           the action table
  summary  print counts per state, kind and shape
  next     propose the next routines to recover
  check    validate the tracker (used by nix flake check)
"""

import argparse
import collections
import csv
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TRACKER = ROOT / 'decomp/functions.tsv'

COLUMNS = ('file', 'offset', 'section', 'size', 'kind', 'handler', 'objects', 'callback',
           'action', 'name', 'state', 'shape', 'module', 'test', 'notes')
FILES = ('exe', 'ovl')
# Ordered: a routine moves left to right through the pipeline.
PIPELINE = ('unknown', 'named', 'analyzed', 'ported', 'verified')
SIDE_STATES = ('blocked', 'library', 'data', 'dead')
STATES = PIPELINE + SIDE_STATES
# service: engine code reached from handlers or through RTLink trampolines
# without being an object handler, room callback or verb (action) handler.
KINDS = ('unclassified', 'service', 'action', 'handler', 'callback', 'library', 'rtlink')
# Evidence for these kinds is specific; it replaces unclassified or service.
SPECIFIC_KINDS = ('action', 'handler', 'callback')
LIBRARY_KINDS = ('library', 'rtlink')
# Plan step 3.4: what a routine needs beyond engine-service calls.
SHAPES = ('', 'formulaic', 'direct-memory', 'complex')
DONE_STATES = ('verified', 'library', 'dead', 'data')
STARTED_STATES = ('analyzed', 'ported')
# Ghidra placeholders and generated per-object aliases carry no evidence of
# their own (object aliases also embed game text); keep the name empty.
AUTO_NAME = re.compile(r'^(FUN|thunk_FUN|LAB|SUB|caseD|switchD)_[0-9A-Za-z_]+$|'
                       r'^gw_object_[0-9a-f]{3}(_.*)?$|^gw_room_callback_[0-9a-f]+$|'
                       r'^gw_action_[0-9a-f]{2}$')
NAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
MODULE = re.compile(r'^src/story/[a-z0-9_]+\.(cpp|hpp)$')
KIND_ORDER = {'service': 0, 'action': 1, 'callback': 2, 'handler': 3, 'unclassified': 4}


class TrackerError(ValueError):
    pass


def parse_offset(text):
    try:
        value = int(text, 16)
    except ValueError as error:
        raise TrackerError(f'invalid offset: {text!r}') from error
    if not 0 <= value <= 0xffffff:
        raise TrackerError(f'offset out of range: {text!r}')
    return value


def format_offset(value):
    return f'0x{value:06x}'


def key_of(row):
    return row['file'], parse_offset(row['offset'])


def read_tracker(path):
    if not path.exists():
        return []
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if tuple(reader.fieldnames or ()) != COLUMNS:
            raise TrackerError(f'{path}: header must be: ' + '\t'.join(COLUMNS))
        return list(reader)


def write_tracker(path, rows):
    rows = sorted(rows, key=lambda row: (FILES.index(row['file']), parse_offset(row['offset'])))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, COLUMNS, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def new_row(file, offset, section=''):
    row = dict.fromkeys(COLUMNS, '')
    row.update(file=file, offset=format_offset(offset), section=section,
               kind='unclassified', state='unknown')
    return row


def read_functions(path):
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if not {'file', 'offset', 'section', 'body_bytes', 'name'} <= set(reader.fieldnames or ()):
            raise TrackerError(f'{path}: not a ghidra-functions.tsv export')
        functions = {}
        for row in reader:
            if row['file'] not in FILES:
                raise TrackerError(f'{path}: unknown file {row["file"]!r}')
            name = '' if AUTO_NAME.match(row['name']) else row['name']
            functions[(row['file'], parse_offset(row['offset']))] = (
                row['section'], int(row['body_bytes']), name)
        return functions


def read_inventory(path):
    """Map (file, offset) to (section, kind, handler, objects, callback)."""
    report = json.loads(path.read_text())
    if report.get('schema_version') != 1:
        raise TrackerError(f'{path}: not an inventory_native_handlers.py JSON report')
    entries = {}

    def add(entry, **fields):
        key = ('ovl', entry['file_offset'])
        if key in entries:
            raise TrackerError(f'{path}: {format_offset(key[1])} listed twice')
        record = dict(section=str(entry['section']), handler='', objects='',
                      callback='', action='', notes='')
        record.update(fields)
        entries[key] = record

    for item in report['handlers']:
        add(item['entry'], kind='handler', handler=str(item['handler']),
            objects=','.join(f'0x{obj["id"]:03x}' for obj in item['objects']))
    for item in report['callbacks']:
        add(item['entry'], kind='callback', callback=f'0x{item["callback_id"]:02x}')
    for item in report.get('unclassified_rtlink_targets', ()):
        add(item['entry'], kind='service')
    return entries


def read_actions(path):
    """Map original action handlers and selector callbacks from inspect_action_dispatch.py."""
    report = json.loads(path.read_text())
    if report.get('schema_version') != 1 or report.get('action_count') != len(report['actions']):
        raise TrackerError(f'{path}: not an inspect_action_dispatch.py --format json report')
    entries = {}
    for item in report['actions']:
        for code_key, kind in (('native_code', 'action'), ('selector_native_code', 'service')):
            code = item.get(code_key)
            if not code:
                continue
            filename = code['filename']
            if filename not in ('GATEMAIN.OVL', 'GATEMAIN.EXE'):
                raise TrackerError(f'{path}: unexpected file {code["filename"]!r}')
            file = 'ovl' if filename == 'GATEMAIN.OVL' else 'exe'
            key = (file, code['file_offset'])
            section = str(code['section']) if file == 'ovl' else ''
            entry = entries.setdefault(key, dict(section=section, kind=kind,
                                                 actions=[], selectors=[]))
            if entry['kind'] != kind:
                entry['kind'] = 'action'
            (entry['actions'] if kind == 'action' else entry['selectors']).append(
                f'0x{item["action"]:02x}')
    return {key: dict(section=entry['section'], kind=entry['kind'], handler='', objects='',
                      callback='', action=','.join(entry['actions']),
                      notes=('selector callback for actions ' + ','.join(entry['selectors'])
                             if entry['selectors'] else ''))
            for key, entry in entries.items()}


def seed(tracker_path, functions_path=None, inventory_path=None, actions_path=None):
    """Merge new evidence; never discard manually recorded columns."""
    rows = {key_of(row): row for row in read_tracker(tracker_path)}
    added = updated = 0

    def get(key, section):
        nonlocal added
        row = rows.get(key)
        if row is None:
            row = rows[key] = new_row(key[0], key[1], section)
            added += 1
        return row

    def fill(row, field, value):
        nonlocal updated
        if value and not row[field]:
            row[field] = value
            updated += 1
        elif value and row[field] != value and field in ('section', 'handler', 'callback',
                                                          'action'):
            raise TrackerError(f'{row["file"]}:{row["offset"]}: {field} changed from '
                               f'{row[field]!r} to {value!r}')

    evidence = []
    if inventory_path:
        evidence.append(read_inventory(inventory_path))
    if actions_path:
        evidence.append(read_actions(actions_path))
    for entries in evidence:
        for key, entry in entries.items():
            row = get(key, entry['section'])
            if row['kind'] != entry['kind'] and (
                    row['kind'] == 'unclassified' or
                    (row['kind'] == 'service' and entry['kind'] in SPECIFIC_KINDS)):
                row['kind'] = entry['kind']
                updated += 1
            elif row['kind'] in SPECIFIC_KINDS and entry['kind'] in SPECIFIC_KINDS and \
                    row['kind'] != entry['kind']:
                raise TrackerError(f'{row["file"]}:{row["offset"]}: is both '
                                   f'{row["kind"]} and {entry["kind"]}')
            for field in ('section', 'handler', 'objects', 'callback', 'action', 'notes'):
                fill(row, field, entry[field])
    if functions_path:
        for key, (section, size, name) in read_functions(functions_path).items():
            row = get(key, section)
            fill(row, 'section', section)
            if row['size'] != str(size):
                row['size'] = str(size)
                updated += 1
            if name and not row['name']:
                row['name'] = name
                updated += 1
    for row in rows.values():
        if row['name'] and row['state'] == 'unknown':
            row['state'] = 'named'
            updated += 1
    write_tracker(tracker_path, rows.values())
    return added, updated


def summary(rows, out=sys.stdout):
    sizes = [int(row['size']) for row in rows if row['size']]
    print(f'{len(rows)} routines, {sum(sizes)} known body bytes, '
          f'{len(rows) - len(sizes)} without a Ghidra function', file=out)
    by_state = collections.Counter(row['state'] for row in rows)
    bytes_by_state = collections.Counter()
    for row in rows:
        bytes_by_state[row['state']] += int(row['size'] or 0)
    total = sum(sizes)
    print('state        routines      bytes', file=out)
    for state in STATES:
        if by_state[state]:
            share = 100.0 * bytes_by_state[state] / total if total else 0.0
            print(f'{state:<12} {by_state[state]:>8} {bytes_by_state[state]:>10}  ({share:5.1f}%)',
                  file=out)
    for column, values in (('kind', KINDS), ('shape', SHAPES)):
        counts = collections.Counter(row[column] for row in rows)
        print(f'{column}: ' + ', '.join(f'{value or "unmeasured"}={counts[value]}'
                                         for value in values if counts[value]), file=out)
    by_file = collections.Counter(row['file'] for row in rows)
    print('files: ' + ', '.join(f'{name}={by_file[name]}' for name in FILES), file=out)


def propose(rows, kind=None, limit=10):
    """Started work first, then services before callbacks and handlers, smallest first."""
    workable = [row for row in rows
                if row['state'] not in DONE_STATES + ('blocked',) and
                row['kind'] not in LIBRARY_KINDS and (not kind or row['kind'] == kind)]
    workable.sort(key=lambda row: (row['state'] not in STARTED_STATES,
                                   KIND_ORDER.get(row['kind'], 9),
                                   not row['size'], int(row['size'] or 0),
                                   key_of(row)))
    return workable[:limit] if limit else workable


def check(rows, root=ROOT):
    errors = []
    previous = None
    seen_handlers = set()
    for number, row in enumerate(rows, start=2):
        where = f'line {number} ({row.get("file")}:{row.get("offset")})'
        if any(value is None for value in row.values()):
            errors.append(f'{where}: wrong number of columns')
            continue
        if row['file'] not in FILES:
            errors.append(f'{where}: file must be one of {", ".join(FILES)}')
            continue
        try:
            key = (FILES.index(row['file']), parse_offset(row['offset']))
        except TrackerError as error:
            errors.append(f'{where}: {error}')
            continue
        if row['offset'] != format_offset(key[1]):
            errors.append(f'{where}: offset must be written as {format_offset(key[1])}')
        if previous is not None and key <= previous:
            errors.append(f'{where}: rows must be unique and sorted by file, offset')
        previous = key
        if row['file'] == 'ovl' and not row['section'].isdigit():
            errors.append(f'{where}: overlay routines need their RTLink section')
        if row['file'] == 'exe' and row['section']:
            errors.append(f'{where}: resident routines have no RTLink section')
        if row['size'] and not row['size'].isdigit():
            errors.append(f'{where}: size must be a decimal byte count')
        for column, values in (('state', STATES), ('kind', KINDS), ('shape', SHAPES)):
            if row[column] not in values:
                errors.append(f'{where}: unknown {column} {row[column]!r}')
        if any('\t' in value or '\n' in value for value in row.values()):
            errors.append(f'{where}: fields must not contain tabs or newlines')
        if row['name'] and not NAME.match(row['name']):
            errors.append(f'{where}: name must be a C identifier')
        if row['handler']:
            if row['kind'] != 'handler' or not row['handler'].isdigit():
                errors.append(f'{where}: handler IDs belong to kind=handler rows')
            elif row['handler'] in seen_handlers:
                errors.append(f'{where}: handler {row["handler"]} listed twice')
            seen_handlers.add(row['handler'])
        elif row['kind'] == 'handler':
            errors.append(f'{where}: kind=handler needs a handler ID')
        if row['kind'] == 'callback' and not row['callback']:
            errors.append(f'{where}: kind=callback needs a callback ID')
        if (row['kind'] == 'action') != bool(row['action']):
            errors.append(f'{where}: action IDs belong to, and are required for, kind=action')
        state = row['state']
        if state in PIPELINE and PIPELINE.index(state) >= PIPELINE.index('named') and \
                not row['name']:
            errors.append(f'{where}: state {state} requires a name')
        if state in PIPELINE and PIPELINE.index(state) >= PIPELINE.index('analyzed') and \
                not row['shape']:
            errors.append(f'{where}: state {state} requires a measured shape')
        if state in ('ported', 'verified'):
            if not MODULE.match(row['module']):
                errors.append(f'{where}: state {state} requires module src/story/<name>.cpp')
            elif not (root / row['module']).is_file():
                errors.append(f'{where}: module not found: {row["module"]}')
        if state == 'verified':
            if not row['test']:
                errors.append(f'{where}: verified routines need a test path')
            elif not (root / row['test']).is_file():
                errors.append(f'{where}: test file not found: {row["test"]}')
        if state in ('blocked', 'dead', 'data') and not row['notes']:
            errors.append(f'{where}: state {state} requires notes with the evidence')
        if state == 'library' and row['kind'] not in LIBRARY_KINDS:
            errors.append(f'{where}: state library requires kind library or rtlink')
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tracker', type=Path, default=DEFAULT_TRACKER)
    sub = parser.add_subparsers(dest='command', required=True)
    seed_parser = sub.add_parser('seed')
    seed_parser.add_argument('--functions', type=Path,
                             help='ghidra-functions.tsv from tools/run_ghidra.py')
    seed_parser.add_argument('--inventory', type=Path,
                             help='tools/inventory_native_handlers.py --format json output')
    seed_parser.add_argument('--actions', type=Path,
                             help='tools/inspect_action_dispatch.py --all --format json output')
    sub.add_parser('summary')
    next_parser = sub.add_parser('next')
    next_parser.add_argument('--kind', choices=KINDS)
    next_parser.add_argument('--limit', type=int, default=10)
    sub.add_parser('check')
    args = parser.parse_args(argv)
    try:
        if args.command == 'seed':
            if not (args.functions or args.inventory or args.actions):
                parser.error('seed needs --functions, --inventory and/or --actions')
            added, updated = seed(args.tracker, args.functions, args.inventory, args.actions)
            print(f'tracker: {added} added, {updated} fields updated; {args.tracker}')
            return 0
        rows = read_tracker(args.tracker)
        if args.command == 'summary':
            summary(rows)
        elif args.command == 'next':
            for row in propose(rows, args.kind, args.limit):
                ref = (f'handler {row["handler"]}' if row['handler'] else
                       f'callback {row["callback"]}' if row['callback'] else
                       f'action {row["action"]}' if row['action'] else row['kind'])
                print('\t'.join((row['file'], row['offset'], row['section'] or '-',
                                 row['size'] or '?', row['state'], ref, row['name'] or '-')))
        elif args.command == 'check':
            errors = check(rows)
            for error in errors:
                print(f'{args.tracker}: {error}', file=sys.stderr)
            if errors:
                return 1
            print(f'tracker ok: {len(rows)} routines')
    except (OSError, TrackerError, KeyError, json.JSONDecodeError) as error:
        print(f'decomp_status: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

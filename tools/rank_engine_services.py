#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Rank native service callees from original encoded CALL sites.

The raw Ghidra reference graph is NOT reliable for DOS far calls: some
far-segment references resolve to executable *data*. This tool uses Ghidra's
per-function CALL-site addresses, decodes original EXE/OVL instructions and
verifies RTLink ten-byte stubs. Resident far-call targets require native RAM
and multiple matching code signatures to map to their true EXE/RTL file offsets.
No executable bytes, RAM images or game text appear in its report.
"""

import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys


class CallGraphError(ValueError):
    pass


def identity(item):
    key = item['file'], item['offset']
    if key[0] not in ('exe', 'ovl', 'rtl') or not isinstance(key[1], int) or key[1] < 0:
        raise CallGraphError(f'invalid source identity: {key}')
    return key


def decode_stub(exe, offset, sections):
    code = exe[offset:offset + 10]
    if len(code) != 10 or code[0] != 0xe8 or code[3] != 0xea:
        return None
    section = int.from_bytes(code[8:10], 'little')
    descriptor = sections.get(section)
    if descriptor is None:
        return None
    target_offset = int.from_bytes(code[4:6], 'little')
    target_segment = int.from_bytes(code[6:8], 'little')
    delta = (target_segment - descriptor['linked_segment']) * 16 + target_offset
    if not 0 <= delta < descriptor['file_bytes']:
        return None
    return dict(file='ovl', offset=descriptor['original_file_offset'] + delta,
                section=section, via='rtlink', stub=offset)


def decode_call(site, exe, ovl, sections, header_size):
    """Return a verified candidate callee, or None for indirect/unknown CALLs."""
    source = identity(site)
    data = exe if source[0] == 'exe' else ovl
    offset = source[1]
    if offset >= len(data):
        raise CallGraphError(f'CALL site {source} exceeds original file')
    opcode = data[offset]
    if opcode == 0xe8 and offset + 3 <= len(data):
        displacement = int.from_bytes(data[offset+1:offset+3], 'little', signed=True)
        target = offset + 3 + displacement
        if source[0] == 'ovl':
            section = sections.get(site.get('section'))
            if section is None or not (section['original_file_offset'] <= target <
                                       section['original_file_offset'] + section['file_bytes']):
                return None
            return dict(file='ovl', offset=target, section=site['section'], via='near')
        if header_size <= target < len(exe):
            return dict(file='exe', offset=target, via='near')
        return None
    if opcode == 0x9a and offset + 5 <= len(data):
        target_offset = int.from_bytes(data[offset+1:offset+3], 'little')
        segment = int.from_bytes(data[offset+3:offset+5], 'little')
        if segment == 0x1c24:
            # The directory and early action thunks use different EXE bases.
            candidates = [candidate for base in (0xa0da, 0xa020)
                          if (candidate := decode_stub(exe, base + target_offset,
                                                        sections)) is not None]
            # Later manager thunks use a third base (0xa73a). The original
            # 1c24:3db5/3dbf entries map to section-4 child-list services.
            # Require the near CALL to reach the manager at 1c24:058d;
            # otherwise unrelated bytes can look like an RTLink stub.
            stub_offset = 0xa73a + target_offset
            code = exe[stub_offset:stub_offset + 10]
            if len(code) == 10 and code[0] == 0xe8 and code[3] == 0xea:
                displacement = int.from_bytes(code[1:3], 'little', signed=True)
                if (target_offset + 3 + displacement) & 0xffff == 0x058d:
                    if candidate := decode_stub(exe, stub_offset, sections):
                        candidates.append(candidate)
            if len(candidates) == 1:
                return candidates[0]
            if candidates:
                raise CallGraphError(f'ambiguous RTLink stub at {source}')
            return None
        if source[0] == 'ovl':
            section = sections.get(site.get('section'))
            if section is not None:
                delta = (segment - section['linked_segment']) * 16 + target_offset
                if 0 <= delta < section['file_bytes']:
                    return dict(file='ovl', offset=section['original_file_offset'] + delta,
                                section=site['section'], via='far-same-section')
        # RTLink shifts/loads multiple resident segments independently; a
        # linked far segment does NOT map linearly into the original EXE file.
        return dict(file='linked', segment=segment, offset=target_offset,
                    via='far-linked')
    # Indirect FF /2 or /3 calls, instruction prefixes, invalid Ghidra sites.
    return None


def resolve_linked_origins(targets, ram, exe, rtl, load_segment):
    """Match live far-entry bytes to stable original EXE/RTL offsets.

    Multiple independent entry points must support a segment/file base, or
    there must be one unambiguous 16-byte entry. A segment may hold pieces
    from both files, so EVERY target still needs its own byte match.
    """
    images = dict(exe=exe, rtl=rtl)
    candidates = {}
    supporters = collections.defaultdict(set)
    for segment, offset in targets:
        linear = (load_segment + segment) * 16 + offset
        if linear < 0 or linear + 16 > len(ram):
            continue
        signature = ram[linear:linear + 16]
        options = {}
        for file, source in images.items():
            start = 0
            for _ in range(32):
                pos = source.find(signature[:8], start)
                if pos < 0:
                    break
                length = 8
                while length < 16 and pos + length < len(source) and \
                        source[pos + length] == signature[length]:
                    length += 1
                options[file, pos] = max(length, options.get((file, pos), 0))
                supporters[(segment, file, pos - offset)].add(offset)
                start = pos + 1
        if not any(length == 16 for length in options.values()):
            # Early relocation words can alter the entry bytes. A unique
            # intact window later in the same native routine establishes a
            # candidate origin; require supporting entries where possible.
            for shift in range(8, 128, 4):
                window = ram[linear+shift:linear+shift+16]
                if len(window) < 16:
                    break
                for file, source in images.items():
                    pos = source.find(window)
                    if pos >= shift:
                        begin = pos - shift
                        options[file, begin] = 16
                        supporters[(segment, file, begin - offset)].add(offset)
        candidates[segment, offset] = [(file, pos, pos-offset, length)
                                       for (file, pos), length in options.items()]
    mapped = {}
    for key, options in candidates.items():
        scored = []
        for file, pos, base, length in options:
            count = len(supporters[(key[0], file, base)])
            if count >= 2 or length == 16:
                scored.append(((count, length), file, pos))
        scored.sort(reverse=True)
        if not scored or len(scored) > 1 and scored[0][0] == scored[1][0]:
            continue
        score, file, pos = scored[0]
        mapped[key] = dict(file=file, offset=pos, via='live-matched-far',
                           matched_bytes=score[1], segment_support=score[0])
    return mapped


def analyze(facts_path, plan_path, exe_path, ovl_path, tracker_path,
            rtl_path=None, snapshot_path=None, load_segment=0x1000):
    plan = json.loads(plan_path.read_text())
    facts = json.loads(facts_path.read_text())
    if (plan.get('schema_version') != 1 or facts.get('schema_version') != 1 or
            not plan.get('fingerprinted_gateway')):
        raise CallGraphError('need fingerprinted Gateway plan and Ghidra facts')
    exe, ovl = exe_path.read_bytes(), ovl_path.read_bytes()
    for key, data in (('exe_sha256', exe), ('ovl_sha256', ovl)):
        if hashlib.sha256(data).hexdigest() != plan[key]:
            raise CallGraphError(f'{key} does not match the Ghidra plan')
    if exe[:2] != b'MZ':
        raise CallGraphError('not an MZ executable')
    header_size = int.from_bytes(exe[8:10], 'little') * 16
    sections = {item['id']: item for item in plan['sections']}
    with tracker_path.open(newline='') as stream:
        import csv
        rows = {(row['file'], int(row['offset'], 16)): row for row in
                csv.DictReader(stream, delimiter='\t')}
    pending = []
    linked = set()
    for function in facts['functions']:
        caller = identity(function['identity'])
        for site in function.get('call_sites', []):
            if site['file'] != caller[0] or (caller[0] == 'ovl' and
                                           site.get('section') != function['identity'].get('section')):
                raise CallGraphError(f'CALL site from wrong image/bank: {caller}')
            callee = decode_call(site, exe, ovl, sections, header_size)
            pending.append((caller, callee))
            if callee is not None and callee['file'] == 'linked':
                linked.add((callee['segment'], callee['offset']))
    if (rtl_path is None) != (snapshot_path is None):
        raise CallGraphError('--rtl and --native-snapshot must be supplied together')
    mappings = (resolve_linked_origins(linked, snapshot_path.read_bytes(), exe,
                                      rtl_path.read_bytes(), load_segment)
                if rtl_path is not None else {})
    edges = collections.defaultdict(dict)
    unresolved = collections.Counter()
    unmapped = collections.defaultdict(set)
    for caller, callee in pending:
        if callee is not None and callee['file'] == 'linked':
            linked_key = callee['segment'], callee['offset']
            resolved = mappings.get(linked_key)
            if resolved is None:
                unmapped[linked_key].add(caller)
            callee = resolved
        if callee is None:
            unresolved[caller] += 1
            continue
        key = identity(callee)
        if key != caller:
            edges[key][caller] = callee
    report = []
    for key, callers in edges.items():
        kinds = collections.Counter(rows.get(caller, {}).get('kind', '') for caller in callers)
        example = next(iter(callers.values()))
        details = rows.get(key, {})
        report.append(dict(file=key[0], offset=key[1],
                           section=example.get('section') if key[0] == 'ovl' else None,
                           kind=details.get('kind', 'external-rtl' if key[0] == 'rtl' else None),
                           name=details.get('name'),
                           tracked=bool(details),
                           native_match_bytes=max((value.get('matched_bytes', 0)
                                                   for value in callers.values()), default=0),
                           native_segment_support=max((value.get('segment_support', 0)
                                                       for value in callers.values()), default=0),
                           handler_callers=kinds['handler'], action_callers=kinds['action'],
                           callback_callers=kinds['callback'],
                           total_callers=len(callers),
                           call_kinds=sorted(set(value['via'] for value in callers.values()))))
    report.sort(key=lambda item: (-item['handler_callers'], -item['total_callers'],
                                  item['file'], item['offset']))
    unmapped_report = []
    for (segment, offset), callers in unmapped.items():
        kinds = collections.Counter(rows.get(caller, {}).get('kind', '') for caller in callers)
        unmapped_report.append(dict(linked_segment=segment, linked_offset=offset,
                                    handler_callers=kinds['handler'],
                                    total_callers=len(callers)))
    unmapped_report.sort(key=lambda item: (-item['handler_callers'],
                                           item['linked_segment'], item['linked_offset']))
    return dict(schema_version=1, call_sites=len(pending),
                unresolved_sites=sum(unresolved.values()),
                callees=report, unmapped_linked=unmapped_report)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--facts', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--ovl', type=Path, required=True)
    parser.add_argument('--rtl', type=Path, help='original GATE.RTL for matched resident code')
    parser.add_argument('--native-snapshot', type=Path,
                        help='private full-RAM snapshot from the original replay')
    parser.add_argument('--load-segment', type=lambda text: int(text, 0), default=0x1000,
                        help='native executable load segment for this snapshot (default 0x1000)')
    parser.add_argument('--tracker', type=Path, default=Path('decomp/functions.tsv'))
    parser.add_argument('--output', type=Path, help='ignored build/ JSON report')
    parser.add_argument('--min-handlers', type=int, default=20)
    args = parser.parse_args(argv)
    try:
        report = analyze(args.facts, args.plan, args.exe, args.ovl, args.tracker,
                         args.rtl, args.native_snapshot, args.load_segment)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(f'original CALL sites={report["call_sites"]}; '
              f'unresolved={report["unresolved_sites"]}')
        for item in report['callees']:
            if item['handler_callers'] < args.min_handlers:
                continue
            print(f'{item["file"]}:0x{item["offset"]:06x} '
                  f'handlers={item["handler_callers"]:3d} '
                  f'actions={item["action_callers"]:3d} '
                  f'kind={item["kind"] or "untracked":13s} '
                  f'name={item["name"] or "-"}')
        for item in report['unmapped_linked']:
            if item['handler_callers'] >= args.min_handlers:
                print(f'unmapped linked {item["linked_segment"]:04x}:'
                      f'{item["linked_offset"]:04x} '
                      f'handlers={item["handler_callers"]} (requires native file mapping)')
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'rank_engine_services: {error}\n')


if __name__ == '__main__':
    sys.exit(main())

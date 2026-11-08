#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Check the final Gateway launcher FAR targets against live DOS code.

The segment values are original *load-relative* linker operands, not fixed
physical addresses. The snapshot must contain the relocated GATE.EXE startup
image; a later capture may have overwritten one or more service banks.
An internal code-window match is only a *candidate*: overlays can replace
functions, and an exact window may begin in the middle of an instruction.
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys

from rank_engine_services import resolve_linked_origins
from prepare_intro_ghidra import RTL_BANKS, validate_bank


TAIL_ENTRIES = (
    ('music_init', 0x0a46, 0x01ec, 'rtl', 0xc3bd),
    ('control_key', 0x123f, 0x0004, None, None),
    ('intro_diagnostic', 0x1258, 0x172e, None, None),
)

# The original resident entry boundaries are identifiable by their code
# shape, despite the LINK-generated four-byte FAR target operands. No other
# bytes are skipped: these templates are much stronger than a coincidental
# 16-byte *internal* match. The diagnostic uses only its first 45 bytes;
# later branches and stack arguments are still under investigation.
# (original RTL entry, length to compare, FAR CALL opcode offsets)
RELOCATED_RTL_ENTRIES = {
    'control_key': (0x157b8, 45, (0, 5, 10, 15, 24, 36)),
    'intro_diagnostic': (0x170c7, 45, (7, 29)),
}

# The original diagnostic ends at RETF 1712d, with three additional FAR
# calls after its already verified 45-byte prefix. Attempt the complete
# signature as a *separate*, stronger check: the validated prefix and its
# two calls remain available even if this extension does not match live RAM.
EXTENDED_RTL_ENTRIES = {
    'intro_diagnostic': (0x170c7, 103, (7, 29, 60, 69, 78)),
}

# These original source entries can be checked without treating an internal
# byte-window match as a function boundary. The control-key video cleanup
# routine 10da1..10dc6 has two repeated DS operands AND two absolute buffer
# immediates: 0000/0080 -> 555c/55dc in the user's 22-second live snapshot.
# Both pairs preserve their original WORD differences after relocation.
# 1139d and 10573 follow RETF instructions; 1054d ends at its own RETF,
# and its exact target offset selects it after the bank's initial records.
# The 10573 check uses only its first 48 bytes of a longer function.
# The first Ctrl-Break callee 15706..15729 precedes a RETF/NOOP/data boundary.
# Its repeated flag references and two call argument offsets relocate under
# different DS deltas, so validate them as two independent WORD-pair groups.
# Its nested 02a5:0f62 target projects through an independently validated
# bank to RTL:6640. Only its first 48 bytes (and first FAR call) are known
# well enough to match before the rest of its long, relocated body is read.
# The 02a5:17da target of that call is a complete 142-byte buffered stream
# flush at RTL:6eb8..6f45, with one FAR write call and two independent DS
# table addresses; the new snapshot confirms its first 16 source bytes.
# At 145d:016c the complete 86-byte CA00 image destructor has one relocated
# DS-segment immediate and one FAR call to the already named pixel allocator.
# The final 153a:0048 DOS heap release has a single relocated DS immediate;
# its 49-byte body is adjacent to the heap allocator at RTL:18edf.
# (relative segment, offset) -> original entry, length, FAR calls, zero WORDs,
#                                pairs of WORDs preserving their delta
RELOCATED_CALLEE_ENTRIES = {
    (0x121b, 0x021f): (0x15706, 36, (15,), (), ((2, 25), (25, 31), (9, 13))),
    (0x02a5, 0x0f62): (0x6640, 48, (35,), (), ()),
    (0x02a5, 0x17da): (0x6eb8, 142, (87,), (32, 50), ()),
    (0x0e48, 0x0202): (0x10da1, 38, (16, 26), (), ((6, 33), (12, 22))),
    (0x0e48, 0x07fe): (0x1139d, 48, (41,), (13,), ()),
    (0x0df2, 0x000a): (0x1054d, 38, (4,), (), ((18, 25),)),
    (0x0df2, 0x0030): (0x10573, 48, (41,), (), ((8, 15),)),
    (0x145d, 0x016c): (0x180fe, 86, (43,), (11,), ()),
    (0x153a, 0x0048): (0x18f23, 49, (), (11,), ()),
}

# These are names already grounded in decomp/launcher/symbol-names.tsv and
# backed by the existing bounded DOS heap adapter. A name does not imply
# that the original resident entry is independently source-matched.
KNOWN_LINKED_PORT_NAMES = {
    (0x153a, 0x0004): 'gate_linked_allocate_vfx_pixels',
    (0x153a, 0x0048): 'gate_linked_release_vfx_pixels',
}


def match_relocated_far_entry(original, live, call_offsets, length,
                              word_offsets=(), word_pairs=()):
    """Compare an original entry, masking only known relocated operand slots."""
    if len(original) < length or len(live) < length:
        return False
    ignored = set()
    for start in call_offsets:
        if start < 0 or start + 5 > length:
            return False
        if original[start:start + 5] != b'\x9a\x00\x00\x00\x00':
            return False
        if live[start] != 0x9a:
            return False
        ignored.update(range(start + 1, start + 5))
    for start in word_offsets:
        if start < 0 or start + 2 > length:
            return False
        if original[start:start + 2] != b'\x00\x00':
            return False
        ignored.update(range(start, start + 2))
    for first, second in word_pairs:
        if first < 0 or second < 0 or first + 2 > length or second + 2 > length:
            return False
        if abs(first - second) < 2:
            return False
        source_delta = (int.from_bytes(original[second:second + 2], 'little') -
                        int.from_bytes(original[first:first + 2], 'little')) & 0xffff
        linked_delta = (int.from_bytes(live[second:second + 2], 'little') -
                        int.from_bytes(live[first:first + 2], 'little')) & 0xffff
        if source_delta != linked_delta:
            return False
        ignored.update(range(first, first + 2))
        ignored.update(range(second, second + 2))
    return all(original[index] == live[index]
               for index in range(length) if index not in ignored)


def read_linked_far_calls(live, source_offset, call_offsets):
    """Report relocated FAR operands without presuming callee semantics."""
    return [
        {'original_call_offset': source_offset + start,
         'target_offset': int.from_bytes(live[start + 1:start + 3], 'little'),
         'target_segment': int.from_bytes(live[start + 3:start + 5], 'little')}
        for start in call_offsets
    ]


def nonrelocated_byte_count(length, call_offsets, word_offsets, word_pairs):
    """Count unique unaffected bytes (WORD pairs may share one operand)."""
    relocated = set()
    for start in call_offsets:
        relocated.update(range(start + 1, start + 5))
    for start in word_offsets:
        relocated.update(range(start, start + 2))
    for first, second in word_pairs:
        relocated.update(range(first, first + 2))
        relocated.update(range(second, second + 2))
    return length - len(relocated)


def project_verified_bank_call(call, bank, rtl, ram, load_segment):
    """Project a FAR target only through an independently validated RTL bank."""
    relative_segment = call['target_segment'] - load_segment
    offset = call['target_offset']
    if relative_segment != bank['segment'] or not 0 <= offset < bank['size']:
        return None
    original = bank['file_base'] + offset
    physical = (call['target_segment'] << 4) + offset
    prefix = entry_prefix_bytes(rtl[original:original + 16],
                                ram[physical:physical + 16])
    return {'file': 'rtl', 'offset': original,
            'via': 'independently-validated-resident-bank',
            'bank_relative_segment': relative_segment,
            'matching_entry_prefix_bytes': prefix}


def annotate_relocated_callees(rows, exe, rtl, ram, load_segment):
    """Resolve directly observed and template-verified nested FAR targets.

    Only a verified bounded callee may disclose further static FAR CALLs.
    Unmatched targets are reported as bytes/candidates, not claimed routines.
    """
    calls = [call for row in rows for call in row.get('relocated_far_calls', ())]
    if not calls:
        return
    verified_banks = {}
    for bank in RTL_BANKS:
        try:
            validate_bank(rtl, ram, bank, load_segment)
        except ValueError:
            continue  # Other checkpoints may have replaced resident banks.
        verified_banks[bank['segment']] = bank
    # Depth 0: control/diagnostic calls; depth 1/2: validated helper chains;
    # depth 3: the stream flush's bounded write call (or the VFX heap leaf).
    # Never recurse through mere bank projections or unknown code windows.
    for _depth in range(4):
        pending = calls
        if not pending:
            break
        targets = {(call['target_segment'] - load_segment, call['target_offset'])
                   for call in pending if call['target_segment'] >= load_segment}
        candidates = resolve_linked_origins(targets, ram, exe, rtl, load_segment)
        nested = []
        for call in pending:
            segment = call['target_segment'] - load_segment
            offset = call['target_offset']
            known_name = KNOWN_LINKED_PORT_NAMES.get((segment, offset))
            if known_name is not None:
                call['existing_port_symbol'] = known_name
            special = RELOCATED_CALLEE_ENTRIES.get((segment, offset))
            if special is not None:
                source_offset, length, far_offsets, word_offsets, word_pairs = special
                physical = (call['target_segment'] << 4) + offset
                linked = ram[physical:physical + length]
                source = rtl[source_offset:source_offset + length]
                matched = match_relocated_far_entry(
                    source, linked, far_offsets, length, word_offsets, word_pairs)
                if matched:
                    call['relocated_source_match'] = {
                        'file': 'rtl', 'offset': source_offset,
                        'via': 'entry-anchored-relocated-operands',
                        'matched_nonrelocated_bytes': nonrelocated_byte_count(
                            length, far_offsets, word_offsets, word_pairs),
                        'relocated_word_pairs': [
                            {'source_offsets': [source_offset + first,
                                                source_offset + second],
                             'runtime_values': [int.from_bytes(
                                 linked[index:index + 2], 'little')
                                 for index in (first, second)]}
                            for first, second in word_pairs]}
                    call['callee_far_calls'] = read_linked_far_calls(
                        linked, source_offset, far_offsets)
                    nested.extend(call['callee_far_calls'])
                    continue
            bank = verified_banks.get(segment)
            if bank is not None:
                projection = project_verified_bank_call(call, bank, rtl, ram,
                                                         load_segment)
                if projection is not None:
                    call['original_source_projection'] = projection
                    # A bank projection alone never verifies the relocated
                    # operands of a known entry. Preserve its live window
                    # when the stronger entry check failed.
                    if special is not None:
                        physical = (call['target_segment'] << 4) + offset
                        call['live_entry_hex'] = ram[physical:physical + 48].hex()
                    continue
            candidate = candidates.get((segment, offset))
            if candidate is not None:
                image = {'rtl': rtl, 'exe': exe}[candidate['file']]
                physical = (call['target_segment'] << 4) + offset
                prefix = entry_prefix_bytes(
                    image[candidate['offset']:candidate['offset'] + 16],
                    ram[physical:physical + 16])
                call['raw_source_candidate'] = {**candidate,
                                                'entry_prefix_bytes': prefix}
            physical = (call['target_segment'] << 4) + offset
            call['live_entry_hex'] = (ram[physical:physical + 48].hex()
                                      if 0 <= physical < len(ram) else None)
        calls = nested

    # Two independently matched function boundaries in the same runtime
    # segment establish a shared original file base. This does not license
    # projecting arbitrary unverified entries elsewhere in that segment.
    bases = defaultdict(list)
    for row in rows:
        for call in row.get('relocated_far_calls', ()):
            match = call.get('relocated_source_match')
            if match is None:
                continue
            relative = call['target_segment'] - load_segment
            file_base = match['offset'] - call['target_offset']
            if file_base >= 0:
                bases[relative, file_base].append(call)
    for (relative, file_base), group in bases.items():
        entries = {call['target_offset'] for call in group}
        if len(entries) < 2:
            continue
        if any(other_relative == relative and other_base != file_base
               for other_relative, other_base in bases):
            continue
        evidence = {'file': 'rtl', 'file_base': file_base,
                    'bank_relative_segment': relative,
                    'via': 'two-entry-anchored-relocation-matches',
                    'linked_entry_offsets': sorted(entries)}
        for call in group:
            call['corroborated_resident_bank'] = evidence


def entry_prefix_bytes(original, live):
    """Number of identical bytes at the two entry addresses (max 16)."""
    count = 0
    for left, right in zip(original[:16], live[:16]):
        if left != right:
            break
        count += 1
    return count


def inspect(exe, rtl, ram, load_segment=0x1000):
    if len(ram) != (1 << 20):
        raise ValueError(f'expected a 1-MiB conventional RAM capture, got {len(ram)} bytes')
    if not exe.startswith(b'MZ'):
        raise ValueError('GATE.EXE must start with an MZ header')
    targets = {(segment, offset) for _, segment, offset, _, _ in TAIL_ENTRIES}
    matches = resolve_linked_origins(targets, ram, exe, rtl, load_segment)
    rows = []
    for name, segment, offset, source_file, source_offset in TAIL_ENTRIES:
        physical = ((load_segment + segment) << 4) + offset
        candidate = matches.get((segment, offset))
        live = ram[physical:physical + 32]
        prefix = 0
        if candidate is not None:
            original = {'exe': exe, 'rtl': rtl}[candidate['file']]
            candidate_offset = candidate['offset']
            prefix = entry_prefix_bytes(original[candidate_offset:candidate_offset + 16],
                                        live)
        row = {
            'name': name,
            'load_relative_target': {'segment': segment, 'offset': offset},
            'physical_entry': physical,
            'live_entry_hex': (live.hex()
                               if physical < len(ram) else None),
            'source_candidate': ({**candidate, 'entry_prefix_bytes': prefix}
                                 if candidate is not None else None),
        }
        if source_file is not None:
            image = {'rtl': rtl, 'exe': exe}[source_file]
            source = image[source_offset:source_offset + 16]
            row['independent_original_entry'] = {
                'file': source_file,
                'offset': source_offset,
                'matches_live_16_bytes': (physical + 16 <= len(ram) and
                                          ram[physical:physical + 16] == source),
            }
        if name == 'music_init':
            # Original GATE.RTL:c3c5/c3cf: the two direct DS operands are
            # relocated in the original 22-second entry capture from
            # 004a/0048 to linked DS:0662/0660. Comparing the original 16
            # bytes verbatim would incorrectly reject this genuine entry.
            row['relocated_music_guard'] = (
                rtl[0xc3c5:0xc3ca] == bytes.fromhex('f6064a0006') and
                rtl[0xc3cf:0xc3d4] == bytes.fromhex('833e480000') and
                ram[physical:physical + 8] == rtl[0xc3bd:0xc3c5] and
                ram[physical + 8:physical + 13] == bytes.fromhex('f606620606') and
                ram[physical + 18:physical + 23] == bytes.fromhex('833e600600'))
            row['entry_status'] = ('original_music_entry' if row['relocated_music_guard']
                                   else 'original_music_entry_replaced_or_not_loaded')
            row['live_source_match'] = (
                {'file': 'rtl', 'offset': 0xc3bd, 'via': 'relocated-original-guard'}
                if row['relocated_music_guard'] else None)
        elif name in RELOCATED_RTL_ENTRIES:
            entry_offset, length, call_offsets = RELOCATED_RTL_ENTRIES[name]
            original = rtl[entry_offset:entry_offset + length]
            relocated = ram[physical:physical + length]
            matched = match_relocated_far_entry(original, relocated,
                                                call_offsets, length)
            row['relocated_original_entry'] = {
                'file': 'rtl',
                'offset': entry_offset,
                'matched': matched,
                'compared_nonrelocated_bytes': length - 4 * len(call_offsets),
                'far_call_count': len(call_offsets),
            }
            row['entry_status'] = ('relocated_original_entry' if matched
                                   else 'entry_unmapped_or_relocated')
            row['live_source_match'] = (
                {'file': 'rtl', 'offset': entry_offset,
                 'via': 'relocated-original-far-calls',
                 'matched_nonrelocated_bytes': length - 4 * len(call_offsets)}
                if matched else None)
            if matched:
                row['relocated_far_calls'] = read_linked_far_calls(
                    relocated, entry_offset, call_offsets)
                extended = EXTENDED_RTL_ENTRIES.get(name)
                if extended is not None:
                    extended_offset, extended_length, extended_calls = extended
                    original_full = rtl[extended_offset:extended_offset + extended_length]
                    relocated_full = ram[physical:physical + extended_length]
                    full_match = match_relocated_far_entry(
                        original_full, relocated_full, extended_calls,
                        extended_length)
                    row['complete_original_entry'] = {
                        'file': 'rtl', 'offset': extended_offset,
                        'matched': full_match,
                        'compared_nonrelocated_bytes': (
                            extended_length - 4 * len(extended_calls)),
                        'far_call_count': len(extended_calls)}
                    if full_match:
                        row['relocated_far_calls'] = read_linked_far_calls(
                            relocated_full, extended_offset, extended_calls)
            elif candidate is not None and prefix >= 8:
                row['entry_status'] = 'source_entry_candidate_not_semantically_verified'
                row['live_source_match'] = candidate
        elif candidate is not None and prefix >= 8:
            # Matching the beginning as well as an internal code window
            # avoids the observed false matches at RTL:7ce0/eeba. Even an
            # 8-byte prefix does not prove an original function boundary.
            row['entry_status'] = 'source_entry_candidate_not_semantically_verified'
            row['live_source_match'] = candidate
        else:
            row['entry_status'] = 'entry_unmapped_or_relocated'
            row['live_source_match'] = None
        rows.append(row)
    annotate_relocated_callees(rows, exe, rtl, ram, load_segment)
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--rtl', required=True, type=Path)
    parser.add_argument('--ram', required=True, type=Path)
    parser.add_argument('--load-segment', type=lambda value: int(value, 0),
                        default=0x1000)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        if not args.ram.exists():
            raise ValueError(f'guest RAM snapshot not found: {args.ram}\n'
                             'Capture the early original intro (see docs/intro-ghidra.md):\n'
                             '  nix run -L .#probe -- ../gateway-dos/GATE.EXE '
                             '--headless --read-only --deterministic-clock '
                             '--milliseconds 24000 '
                             '--dump-at 22000:build/intro-analysis/at-22000\n'
                             'Use --ram build/intro-analysis/at-22000.bin.')
        rows = inspect(args.exe.read_bytes(), args.rtl.read_bytes(),
                       args.ram.read_bytes(), args.load_segment)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.json:
        print(json.dumps({'services': rows}, indent=2))
    else:
        for row in rows:
            match = row['live_source_match']
            identity = (f"{match['file']}:{match['offset']:05x}"
                        if match is not None else 'unmapped')
            candidate = row['source_candidate']
            caution = (f" (unverified window {candidate['file']}:"
                       f"{candidate['offset']:05x}, first "
                       f"{candidate['entry_prefix_bytes']} bytes agree)"
                       if match is None and candidate is not None else '')
            print(f"{row['name']}: live {row['physical_entry']:05x} -> "
                  f"{identity} [{row['entry_status']}]{caution}")
    return 0


if __name__ == '__main__':
    sys.exit(main())

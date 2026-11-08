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

# The third control-key call leads to a complete, independently bounded RTL
# routine: previous RETF at 10da0, entry at 10da1, next entry at 10dc7.
# Its DS word at offsets 6 and 33 is relocated twice, and its two FAR CALL
# operands are relocated. The 16-bit values must agree after relocation.
# (relative segment, target offset) -> (file entry, bytes, FARs, DS words)
RELOCATED_CALLEE_ENTRIES = {
    (0x0e48, 0x0202): (0x10da1, 38, (16, 26), (6, 33)),
}


def match_relocated_far_entry(original, live, call_offsets, length, word_offsets=()):
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
    # Depth 0: original control/diagnostic entry calls; depth 1: calls from
    # exact relocated callee templates. Do not recursively guess byte streams.
    for _depth in range(2):
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
            bank = verified_banks.get(segment)
            if bank is not None:
                projection = project_verified_bank_call(call, bank, rtl, ram,
                                                         load_segment)
                if projection is not None:
                    call['original_source_projection'] = projection
                    continue
            special = RELOCATED_CALLEE_ENTRIES.get((segment, offset))
            if special is not None:
                source_offset, length, far_offsets, word_offsets = special
                physical = (call['target_segment'] << 4) + offset
                linked = ram[physical:physical + length]
                source = rtl[source_offset:source_offset + length]
                matched = match_relocated_far_entry(
                    source, linked, far_offsets, length, word_offsets)
                # Both instructions in this particular function reference
                # the same DS word, so relocation must preserve equality.
                matched = matched and linked[word_offsets[0]:word_offsets[0] + 2] == \
                    linked[word_offsets[1]:word_offsets[1] + 2]
                if matched:
                    call['relocated_source_match'] = {
                        'file': 'rtl', 'offset': source_offset,
                        'via': 'entry-anchored-far-and-ds-relocations',
                        'matched_nonrelocated_bytes': (length - 4 * len(far_offsets)
                                                      - 2 * len(word_offsets)),
                        'relocated_ds_word': int.from_bytes(
                            linked[word_offsets[0]:word_offsets[0] + 2], 'little')}
                    call['callee_far_calls'] = read_linked_far_calls(
                        linked, source_offset, far_offsets)
                    nested.extend(call['callee_far_calls'])
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

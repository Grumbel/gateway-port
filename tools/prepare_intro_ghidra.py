#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Prepare the *linked*, relocated Gateway launcher for 16-bit Ghidra.

The raw GATE.RTL has zero-filled relocation operands.  Only a DOS RAM image
captures the actual instructions and far pointers.  Keep those bytes and all
Ghidra output in ignored build/, never in Git.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re


EXE_SHA256 = '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b'
RTL_SHA256 = 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566'

# Established by *multiple* live far-entry signatures, not by interpreting
# file offsets as DOS segment offsets.  The sizes stop before the succeeding
# section overwrites the mapped image (there are different RTL load layouts).
# There is intentionally no assertion that all of GATE.RTL is mapped here.
RTL_BANKS = (
    # Five original 0c33 file-service entry points map to e531/e582/e5c5/
    # e5e3/e68b.  The last function ends at 0c33:01a9; after that address
    # the snapshot has the overlapping 0c4d image instead of its RTL bytes.
    dict(segment=0x0c33, file_base=0xe4f1, size=0x1aa,
         witnesses=(0xe531, 0xe582, 0xe5c5, 0xe5e3)),
    dict(segment=0x0c4d, file_base=0xe6e6, size=0x900,
         witnesses=(0xec0a, 0xed39, 0xed7e, 0xedff)),
    dict(segment=0x0cdc, file_base=0xf149, size=0x1100,
         witnesses=(0xf473, 0xf61b, 0xf937, 0xf997)),
    dict(segment=0x0f10, file_base=0x11b05, size=0x1b00,
         witnesses=(0x11ecb, 0x1266f, 0x12b19, 0x12f6b, 0x131f7)),
)

EXE_ENTRIES = (0x0bb8, 0x0e37, 0x0f18, 0x0f61, 0x103a, 0x117e,
               0x1302, 0x1a14, 0x1c11, 0x1c6c)

# The first five 0cdc entries and the 0c4d helper are independently mapped
# from original far-call operands, but their function bounds are NOT proved.
# Ghidra may have to merge/reject them.  0x8298 belongs to a not-yet-linked
# bank and must remain explicitly out of coverage.
RTL_CANDIDATES = {
    0x8298: 'unmapped-strlen-candidate',
    0xe531: 'file-open-entry-candidate',
    0xe582: 'file-seek-entry-candidate',
    0xe5c5: 'file-word-read-entry-candidate',
    0xe5e3: 'file-buffer-read-entry-candidate',
    0xe68b: 'file-close-entry-candidate',
    0xe873: 'linked-far-entry-candidate',
    0xf14d: 'linked-far-entry-candidate',
    0xf4df: 'linked-far-entry-candidate',
    0xf579: 'linked-far-entry-candidate',
    0xf921: 'linked-far-entry-candidate',
    0xfbf5: 'linked-far-entry-candidate',
    0xfc37: 'linked-far-entry-candidate',
    0xff99: 'linked-far-entry-candidate',
    0x130ab: 'animation-entry-candidate',
}

# The 0x1000 base is added only for *relative* RTL banks. The physical
# (0x1000+0x24a7):0000 region holds a byte-for-byte unlinked RTL copy;
# however absolute guest 24a7:000c is independently verified executable VFX
# code. These are different locations. Never import the former as the latter.
RAW_COPY_CANDIDATE = dict(segment=0x24a7, file_base=0x101ef, size=0x400)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def original_bytes(path, expected):
    data = path.read_bytes()
    if sha256(data) != expected:
        raise ValueError('%s differs from the pinned original Gateway binary' % path)
    return data


def find_source_entries(source, prefix):
    """Use *defined* source-C routines as hints, not all references to them."""
    expression = re.compile(r'^\s*(?:void|uint16_t|int16_t|GateFarPtr)\s+'
                            + prefix + r'_([0-9a-f]{4,6})\s*\(', re.M)
    entries = set()
    for path in sorted(source.glob('*.c')):
        entries.update(int(match.group(1), 16) for match in expression.finditer(
            path.read_text()))
    return entries


def exe_signature(exe, entry, relocations, image_start):
    """Find one distinctive 16-byte anchor unaffected by MZ fixups.

    Later Ghidra can locate this in the *loaded* EXE independently of where
    its DOS/MZ loader chose to place the program image.
    """
    for delta in range(0, 96):
        offset = entry + delta
        if offset + 16 > len(exe):
            break
        if any(offset <= relocate + 1 and relocate < offset + 16
               for relocate in relocations):
            continue
        signature = exe[offset:offset + 16]
        if len(set(signature)) < 7 or exe.find(signature, image_start) != offset:
            continue
        if exe.find(signature, offset + 1) != -1:
            continue
        return dict(delta=delta, hex=signature.hex())
    raise ValueError('no unrelocated, unique EXE signature for 0x%x' % entry)


def mz_relocations(exe):
    if exe[:2] != b'MZ':
        raise ValueError('expected GATE.EXE MZ header')
    u16 = lambda at: int.from_bytes(exe[at:at + 2], 'little')
    header_bytes = u16(8) * 16
    count, table = u16(6), u16(0x18)
    if (header_bytes < 0x1c or header_bytes >= len(exe) or
            table + count * 4 > header_bytes):
        raise ValueError('invalid GATE.EXE MZ relocation directory')
    relocations = set()
    for i in range(count):
        offset = u16(table + i * 4)
        segment = u16(table + i * 4 + 2)
        file_offset = header_bytes + segment * 16 + offset
        if file_offset + 2 > len(exe):
            raise ValueError('MZ relocation points outside file')
        relocations.add(file_offset)
    return header_bytes, relocations


def source_bank(banks, offset):
    return next((bank for bank in banks
                 if bank['file_base'] <= offset < bank['file_base'] + bank['size']), None)


def validate_bank(rtl, snapshot, bank, load_segment):
    segment = bank['segment']
    start = (load_segment + segment) * 16
    original = bank['file_base']
    size = bank['size']
    if original + size > len(rtl) or start + size > len(snapshot):
        raise ValueError('truncated RTL bank 0x%04x' % segment)
    # Relocations necessarily differ.  A high *aggregate* match and several
    # individually matched instruction windows establish the bank identity;
    # never demand that a relocated RTL is equal to the on-disk file.
    matches = sum(a == b for a, b in zip(rtl[original:original + size],
                                         snapshot[start:start + size]))
    if matches < size * 0.60:
        raise ValueError('snapshot does not contain RTL bank 0x%04x (%.1f%% match)' %
                         (segment, matches * 100.0 / size))
    verified = []
    for offset in bank['witnesses']:
        relative = offset - original
        if not 0 <= relative < size - 16:
            raise ValueError('witness outside RTL bank: 0x%x' % offset)
        # Some dense routines contain segment fixups in almost every 12-byte
        # interval. Allow at most two altered operand bytes per witness; the
        # whole bank must independently pass the aggregate check above.
        if max(sum(a == b for a, b in zip(
                   rtl[offset + i:offset + i + 12],
                   snapshot[start + relative + i:start + relative + i + 12]))
               for i in range(0, 49)) < 10:
            raise ValueError('RTL 0x%x does not match live bank 0x%04x' %
                             (offset, segment))
        verified.append(offset)
    return start, matches, verified


def staged_unlinked_copy(rtl, snapshot, load_segment):
    """Flag the observed unrelocated RTL staging region, if still present."""
    candidate = RAW_COPY_CANDIDATE
    start = (load_segment + candidate['segment']) * 16
    original = candidate['file_base']
    size = candidate['size']
    if (start + size > len(snapshot) or original + size > len(rtl) or
            snapshot[start:start + size] != rtl[original:original + size]):
        return []
    return [dict(candidate, reason='exact on-disk bytes including unresolved '
                 'FAR operands; not proven linked executable code')]


def prepare(exe_path, rtl_path, snapshot_path, output, load_segment=0x1000,
            source=None):
    if not 0 <= load_segment <= 0xffff:
        raise ValueError('load segment outside 16-bit range')
    exe = original_bytes(exe_path, EXE_SHA256)
    rtl = original_bytes(rtl_path, RTL_SHA256)
    snapshot = snapshot_path.read_bytes()
    if len(snapshot) != 0x100000:
        raise ValueError('expected the full 1 MiB DOS guest memory image')
    image_start, relocations = mz_relocations(exe)
    source = source or Path(__file__).resolve().parents[1] / 'decomp/launcher'
    exe_entries = find_source_entries(source, 'gate_exe')
    if not set(EXE_ENTRIES) <= exe_entries:
        raise ValueError('missing reconstructed EXE entry labels')
    reconstructed_rtl_entries = find_source_entries(source, 'gate_rtl')
    rtl_entries = reconstructed_rtl_entries | set(RTL_CANDIDATES)
    sections = []
    for bank in RTL_BANKS:
        start, matches, verified = validate_bank(rtl, snapshot, bank, load_segment)
        segment = bank['segment']
        file_base = bank['file_base']
        size = bank['size']
        path = output / 'sections' / ('rtl-%04x.linked.bin' % segment)
        path.parent.mkdir(parents=True, exist_ok=True)
        image = snapshot[start:start + size]
        path.write_bytes(image)
        entries = [dict(name='gate_rtl_%x' % off, file_offset=off,
                        offset=off - file_base,
                        role=('source-reconstructed' if off in
                              reconstructed_rtl_entries else
                              RTL_CANDIDATES.get(off, 'source-reconstructed')))
                   for off in sorted(rtl_entries)
                   if file_base <= off < file_base + size]
        sections.append(dict(name='INTRO_RTL_%04X' % segment, segment=segment,
                             file_base=file_base, size=size,
                             relocated_file=str(path.resolve()),
                             relocated_sha256=sha256(image),
                             exact_byte_matches=matches, witnesses=verified,
                             entries=entries))
    unmapped = sorted(off for off in rtl_entries if source_bank(RTL_BANKS, off) is None)
    exe_labels = [dict(name='gate_exe_%x' % offset, file_offset=offset,
                       role='source-reconstructed',
                       signature=exe_signature(exe, offset, relocations, image_start))
                  for offset in sorted(exe_entries)]
    plan = dict(schema_version=1, exe_file=str(exe_path.resolve()),
                exe_sha256=sha256(exe), rtl_file=str(rtl_path.resolve()),
                rtl_sha256=sha256(rtl), snapshot_sha256=sha256(snapshot),
                load_segment=load_segment, exe_labels=exe_labels,
                sections=sections, unmapped_rtl_entries=unmapped,
                unlinked_staging_candidates=staged_unlinked_copy(
                    rtl, snapshot, load_segment),
                note='Live relocated image from one 22-second DOS launch; '
                     'not a complete RTL section map or proof of function ABIs')
    output.mkdir(parents=True, exist_ok=True)
    plan_path = output / 'intro-ghidra-plan.json'
    plan_path.write_text(json.dumps(plan, indent=2) + '\n')
    return plan_path, plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('rtl', type=Path)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/ghidra-intro'))
    parser.add_argument('--load-segment', type=lambda value: int(value, 0), default=0x1000)
    args = parser.parse_args()
    try:
        path, plan = prepare(args.exe, args.rtl, args.snapshot, args.output.resolve(),
                             args.load_segment)
        print('Intro plan: %s (%d linked sections, %d source RTL entries; '
              '%d known RTL entries unmapped)' % (
                  path, len(plan['sections']),
                  sum(len(section['entries']) for section in plan['sections']),
                  len(plan['unmapped_rtl_entries'])))
    except (OSError, ValueError) as error:
        parser.exit(1, 'prepare_intro_ghidra: %s\n' % error)


if __name__ == '__main__':
    main()

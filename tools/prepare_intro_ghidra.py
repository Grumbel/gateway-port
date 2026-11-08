#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Prepare the *linked*, relocated Gateway launcher for 16-bit Ghidra.

The raw GATE.RTL has zero-filled relocation operands.  Only a DOS RAM image
captures the actual instructions and far pointers.  Keep those bytes and all
Ghidra output in ignored build/, never in Git.
"""

import argparse
from collections import Counter
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
RTL_CORE_BANKS = (
    # EXE:134c calls original 0a46:01ec, loaded at resident 1a46:01ec.
    # Its first eight instruction bytes uniquely locate RTL:c3bd. The
    # c1d1 base is checked against six independent instruction windows.
    # The linked image is overwritten after relative offset ~0x0b00 in
    # the 22-second capture, so do not extend this bank into that region.
    dict(segment=0x0a46, file_base=0xc1d1, size=0xa80,
         witnesses=(0xc1d1, 0xc29d, 0xc3bd, 0xc4d0, 0xc680, 0xc8c0)),
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
    # Original EXE-relative 02a5 loads at resident 12a5 with the default
    # 1000h load segment. The raw RTL file offset 8298 appears verbatim at
    # resident 12a5:2bba; 91d8/91ea contain the recovered srand/rand code.
    # Stop at file 93de, before the resident block is overwritten by the
    # next RTL region near file 9480 in the 22-second snapshot.
    dict(segment=0x02a5, file_base=0x56de, size=0x3d00,
         witnesses=(0x8298, 0x91d8, 0x91ea)),
)

# Reconstructed physical code windows from original startup snapshots. The
# original RTL file inserts metadata between linked code fragments, so a
# single file offset delta cannot explain either entire window. Every span
# below is independently checked against the 22-second capture (90-100%
# exact bytes), with function-entry witnesses. The A window starts with ten
# unmapped bytes, B with four; B also has two single-byte fragment gaps.
# All such bytes are retained in the live snapshot but NOT given a false
# source-file identity. B ends before seven observed zeroed source bytes.
# All file_base values identify original GATE.RTL bytes (not embedded data).
RTL_GRAPHICS_BANKS = (
    dict(segment=0x152c, file_base=0x18e00, size=0x0ab0,
         spans=(
             dict(offset=0x000a, file_base=0x18e00, size=0x00da),
             dict(offset=0x00e4, file_base=0x18edf, size=0x009e),
             dict(offset=0x0182, file_base=0x18f8c, size=0x04e6),
             dict(offset=0x0668, file_base=0x194d7, size=0x010a),
             dict(offset=0x0772, file_base=0x19606, size=0x0146),
             dict(offset=0x08b8, file_base=0x19777, size=0x01f8),
         ),
         witnesses=(0x18e00, 0x18e28, 0x18e49, 0x18e5b, 0x18edf,
                    0x18f8c, 0x194d7, 0x19606, 0x19777)),
    dict(segment=0x15d7, file_base=0x19997, size=0x0d79,
         spans=(
             dict(offset=0x0004, file_base=0x19997, size=0x04a9),
             dict(offset=0x04ae, file_base=0x19ec2, size=0x0767),
             dict(offset=0x0c16, file_base=0x1a6ee, size=0x0163),
         ),
         witnesses=(0x19a20, 0x19ae6, 0x19b9d, 0x19c2c, 0x19dfa,
                    0x19ec2, 0x19ef2, 0x19f2d, 0x1a6ee)),
)
RTL_BANKS = RTL_CORE_BANKS + RTL_GRAPHICS_BANKS

EXE_ENTRIES = (0x0bb8, 0x0e37, 0x0f18, 0x0f61, 0x103a, 0x117e,
               0x1302, 0x1a14, 0x1c11, 0x1c6c)

# The first five 0cdc entries and the 0c4d helper are independently mapped
# from original far-call operands, but their function bounds are NOT proved.
# Ghidra may have to merge/reject them.  0x8298 is now part of the verified
# 02a5 bank; retain it as a named source reconstruction seed.
RTL_CANDIDATES = {
    0xc3bd: 'exe-134c-startup-callee',
    0x8298: 'verified-linked-strlen',
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
    0x18e00: 'linked-graphics-helper-entry',
    0x18e28: 'linked-graphics-coordinate-entry',
    0x18e49: 'linked-graphics-coordinate-entry',
    0x18e5b: 'linked-graphics-helper-entry',
    0x18edf: 'linked-graphics-entry',
    0x18f23: 'linked-graphics-entry',
    0x18f54: 'linked-graphics-entry',
    0x18f8c: 'linked-graphics-entry',
    0x194d7: 'linked-graphics-entry',
    0x1953c: 'linked-graphics-entry',
    0x19599: 'linked-graphics-entry',
    0x19606: 'linked-graphics-entry',
    0x19658: 'linked-graphics-entry',
    0x19688: 'linked-graphics-entry',
    0x196b2: 'linked-graphics-entry',
    0x196d5: 'linked-graphics-entry',
    0x196ff: 'linked-graphics-entry',
    0x19722: 'linked-graphics-entry',
    0x19777: 'linked-graphics-entry',
    0x197a9: 'linked-graphics-entry',
    0x1980c: 'linked-graphics-entry',
    0x19997: 'linked-graphics-entry',
    0x199fe: 'linked-graphics-entry',
    0x19a69: 'linked-graphics-pixel-entry',
    0x19ac6: 'linked-graphics-pixel-entry',
    0x19b1c: 'linked-graphics-entry',
    0x19b76: 'linked-graphics-entry',
    0x19c05: 'linked-graphics-entry',
    0x19dd3: 'linked-graphics-entry',
    0x19ec2: 'linked-graphics-entry',
    0x19ef2: 'linked-graphics-entry',
    0x19f2d: 'linked-graphics-entry',
    0x1a6ee: 'linked-graphics-entry',
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


def source_symbol_aliases(source):
    """Map original-offset symbols to readable names from the tracked manifest."""
    aliases = {}
    alias_file = source / 'symbol-names.tsv'
    if alias_file.is_file():
        for line in alias_file.read_text().splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            parts = line.split('\t')
            if len(parts) != 2:
                raise ValueError('malformed launcher symbol alias: %s' % line)
            aliases[parts[0]] = parts[1]
    return aliases


def find_source_entries(source, prefix):
    """Resolve original offsets through *defined* numeric or renamed C symbols."""
    expression = re.compile(r'^\s*(?:void|uint16_t|int16_t|GateFarPtr)\s+'
                            r'([a-zA-Z_]\w*)\s*\(', re.M)
    direct = re.compile(r'^' + prefix + r'_([0-9a-f]{4,6})$')
    definitions = set()
    for path in sorted(source.glob('*.c')):
        definitions.update(expression.findall(path.read_text()))
    entries = set()
    for name in definitions:
        match = direct.fullmatch(name)
        if match:
            entries.add(int(match.group(1), 16))
    # The tracked aliases retain original-file identity after the public C
    # functions were renamed. A declaration/reference alone is not enough.
    for original, readable in source_symbol_aliases(source).items():
        match = direct.fullmatch(original)
        if match and readable in definitions:
            entries.add(int(match.group(1), 16))
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


def source_spans(bank):
    """Return the bounded file-to-RAM fragments (not guessed linker gaps)."""
    return bank.get('spans', (dict(offset=0, file_base=bank['file_base'],
                                   size=bank['size']),))


def source_span(bank, file_offset):
    return next((span for span in source_spans(bank)
                 if span['file_base'] <= file_offset <
                 span['file_base'] + span['size']), None)


def source_bank(banks, offset):
    return next((bank for bank in banks if source_span(bank, offset)), None)


def validate_bank(rtl, snapshot, bank, load_segment):
    segment = bank['segment']
    start = (load_segment + segment) * 16
    size = bank['size']
    spans = source_spans(bank)
    if start + size > len(snapshot) or not spans:
        raise ValueError('truncated RTL bank 0x%04x' % segment)
    # File metadata between fragments is NOT part of the running image.
    # Validate every fragment independently; a strong match on one cannot
    # disguise a bad or accidentally overlapping mapping on another.
    matches = 0
    total = 0
    last_end = 0
    for span in spans:
        off, original, length = (span['offset'], span['file_base'], span['size'])
        if (off < last_end or length < 16 or off + length > size or
                original < 0 or original + length > len(rtl)):
            raise ValueError('invalid RTL bank source span 0x%04x' % segment)
        last_end = off + length
        exact = sum(a == b for a, b in zip(
            rtl[original:original + length],
            snapshot[start + off:start + off + length]))
        if exact < length * 0.60:
            raise ValueError('snapshot does not contain RTL bank 0x%04x '
                             'source span +0x%x (%.1f%% match)' %
                             (segment, off, exact * 100.0 / length))
        matches += exact
        total += length
    if matches < total * 0.60:
        raise ValueError('snapshot does not contain RTL bank 0x%04x (%.1f%% match)' %
                         (segment, matches * 100.0 / total))
    verified = []
    for offset in bank['witnesses']:
        span = source_span(bank, offset)
        if span is None or offset + 16 > span['file_base'] + span['size']:
            raise ValueError('witness outside RTL bank: 0x%x' % offset)
        relative = span['offset'] + offset - span['file_base']
        # Some dense routines contain segment fixups in almost every 12-byte
        # interval. Allow at most two altered operand bytes per witness; the
        # whole bank must independently pass the aggregate check above.
        if max(sum(a == b for a, b in zip(
                   rtl[offset + i:offset + i + 12],
                   snapshot[start + relative + i:start + relative + i + 12]))
               for i in range(0, min(49, span['file_base'] + span['size'] - offset - 11))) < 10:
            raise ValueError('RTL 0x%x does not match live bank 0x%04x' %
                             (offset, segment))
        verified.append(offset)
    return start, matches, verified


def observed_zero_word_changes(rtl, snapshot, bank, load_segment):
    """Classify changed code bytes WITHOUT asserting linker-fixup semantics.

    A candidate is an originally 0000h 16-bit word whose live value is
    nonzero. A one-byte difference is accepted only if its following high
    byte is zero in *both* images. All other differences are left explicit.
    These are observations, not a portable relocation table: targets must
    still be identified from machine instructions and caller allocation.
    """
    start = (load_segment + bank['segment']) * 16
    candidates = []
    unclassified = []
    covered = 0
    equal = 0
    for span in source_spans(bank):
        file_base = span['file_base']
        offset = span['offset']
        length = span['size']
        original = rtl[file_base:file_base + length]
        live = snapshot[start + offset:start + offset + length]
        if len(original) != length or len(live) != length:
            raise ValueError('truncated source span in graphics word analysis')
        covered += length
        equal += sum(a == b for a, b in zip(original, live))
        i = 0
        while i < length:
            if original[i] == live[i]:
                i += 1
                continue
            end = i + 1
            while end < length and original[end] != live[end]:
                end += 1
            size = end - i
            # Zero-source changes of two or four consecutive bytes can be
            # interpreted as one or two adjacent complete words. The
            # original instruction operand boundaries remain unproven.
            if size in (2, 4) and not any(original[i:end]):
                for site in range(i, end, 2):
                    candidates.append(dict(runtime_offset=offset + site,
                                           rtl_file_offset=file_base + site,
                                           observed_value=int.from_bytes(
                                               live[site:site + 2], 'little'),
                                           changed_bytes=2))
            elif (size == 1 and original[i] == 0 and i + 1 < length and
                  original[i + 1] == live[i + 1] == 0):
                candidates.append(dict(runtime_offset=offset + i,
                                       rtl_file_offset=file_base + i,
                                       observed_value=live[i], changed_bytes=1))
            else:
                unclassified.append(dict(runtime_offset=offset + i,
                                         rtl_file_offset=file_base + i,
                                         length=size))
            i = end

    changed = covered - equal
    candidate_bytes = sum(item['changed_bytes'] for item in candidates)
    leftover = sum(item['length'] for item in unclassified)
    if candidate_bytes + leftover != changed:
        raise AssertionError('graphics word classification lost changed bytes')
    # Only two instruction patterns are recognized here, both verifiable
    # without guessing a function boundary or running a decompiler:
    # B8 <word> 8E D8/C0 loads DS/ES; 9A/EA <off> <seg> calls/jumps FAR.
    # A generic zero-word substitution remains unclassified even if its
    # observed value resembles a segment address.
    by_offset = {item['runtime_offset']: item for item in candidates}
    for item in candidates:
        site = item['runtime_offset']
        span = source_span(bank, item['rtl_file_offset'])
        pos = item['rtl_file_offset'] - span['file_base']
        source = rtl[span['file_base']:span['file_base'] + span['size']]
        live = snapshot[start + span['offset']:start + span['offset'] + span['size']]
        kind = 'unresolved-zero-word'
        if (pos >= 1 and pos + 4 <= len(source) and
                source[pos - 1] == live[pos - 1] == 0xb8 and
                source[pos + 2:pos + 4] == live[pos + 2:pos + 4]):
            if source[pos + 2:pos + 4] == b'\x8e\xd8':
                kind = 'mov-ax-ds'
            elif source[pos + 2:pos + 4] == b'\x8e\xc0':
                kind = 'mov-ax-es'
        if (kind == 'unresolved-zero-word' and pos >= 1 and
                pos + 4 <= len(source) and
                source[pos - 1] == live[pos - 1] and
                source[pos - 1] in (0x9a, 0xea) and
                site + 2 in by_offset):
            kind = ('far-call-offset' if source[pos - 1] == 0x9a
                    else 'far-jump-offset')
        if (kind == 'unresolved-zero-word' and pos >= 3 and
                source[pos - 3] == live[pos - 3] and
                source[pos - 3] in (0x9a, 0xea) and
                site - 2 in by_offset):
            kind = ('far-call-segment' if source[pos - 3] == 0x9a
                    else 'far-jump-segment')
        item['instruction_context'] = kind
    return dict(covered_source_bytes=covered, equal_source_bytes=equal,
                candidate_words=candidates, candidate_changed_bytes=candidate_bytes,
                unclassified=unclassified, unclassified_bytes=leftover,
                unmapped_live_bytes=bank['size'] - covered,
                context_counts=dict(sorted(Counter(
                    item['instruction_context'] for item in candidates).items())),
                note='Zero-source 16-bit word changes only. DS/ES immediate '
                     'and FAR transfer opcode contexts are verified; other '
                     'operand and relocation semantics are not')


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
    aliases = source_symbol_aliases(source)
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
        entries = [dict(name=aliases.get('gate_rtl_%x' % off,
                                       'gate_rtl_%x' % off), file_offset=off,
                        offset=(source_span(bank, off)['offset'] + off -
                                source_span(bank, off)['file_base']),
                        role=('source-reconstructed' if off in
                              reconstructed_rtl_entries else
                              RTL_CANDIDATES.get(off, 'source-reconstructed')))
                   for off in sorted(rtl_entries)
                   if source_span(bank, off)]
        sections.append(dict(name='INTRO_RTL_%04X' % segment, segment=segment,
                             file_base=file_base, size=size,
                             source_spans=list(source_spans(bank)),
                             relocated_file=str(path.resolve()),
                             relocated_sha256=sha256(image),
                             exact_byte_matches=matches, witnesses=verified,
                             entries=entries,
                             zero_word_changes=(observed_zero_word_changes(
                                 rtl, snapshot, bank, load_segment)
                                 if bank in RTL_GRAPHICS_BANKS else None)))
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
                note='Live relocated image from one DOS launch; every bank '
                     'is verified against that snapshot, including graphics. '
                     'Not a complete RTL map or proof of function ABIs')
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
    parser.add_argument('--graphics-word-changes', action='store_true',
                        help='summarize source-zero word candidates in linked graphics banks')
    args = parser.parse_args()
    try:
        path, plan = prepare(args.exe, args.rtl, args.snapshot, args.output.resolve(),
                             args.load_segment)
        print('Intro plan: %s (%d linked sections, %d source RTL entries; '
              '%d known RTL entries unmapped)' % (
                  path, len(plan['sections']),
                  sum(len(section['entries']) for section in plan['sections']),
                  len(plan['unmapped_rtl_entries'])))
        if args.graphics_word_changes:
            for section in plan['sections']:
                change = section['zero_word_changes']
                if change is None:
                    continue
                print('  graphics %04x: %d/%d source bytes unchanged; '
                      '%d changed bytes in %d zero-source word candidates; '
                      '%d unclassified bytes, %d unmapped live bytes' % (
                          section['segment'], change['equal_source_bytes'],
                          change['covered_source_bytes'],
                          change['candidate_changed_bytes'],
                          len(change['candidate_words']),
                          change['unclassified_bytes'],
                          change['unmapped_live_bytes']))
                print('    opcode contexts: %s' % ', '.join(
                    '%s=%d' % (name, count) for name, count
                    in change['context_counts'].items()))
    except (OSError, ValueError) as error:
        parser.exit(1, 'prepare_intro_ghidra: %s\n' % error)


if __name__ == '__main__':
    main()

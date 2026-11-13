#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Recursive, reassemblable NASM disassembly of GATEMAIN.EXE and its overlays.

Each output file (``GATEMAIN.EXE`` with its resident records, ``GATE.RTL``
with the library records that share the resident address space, and every
RTLink section of GATEMAIN.OVL in its *unrelocated* on-disk form) is an
``.asm`` file that NASM assembles back to the identical bytes; see
``docs/disassembly.md``. Code is found by
recursive traversal from the entry point, the tracker in
``decomp/functions.tsv``, optional Ghidra function starts, far-call and
RTLink-trampoline targets and ``jmp cs:[bx+table]`` switch tables; all
other bytes stay ``db`` data.

The output contains original program code and therefore belongs under
``build/`` only. Run, for example::

    python3 tools/disassemble_gateway.py ../gateway-dos/GATEMAIN.EXE \\
        ../gateway-dos/GATEMAIN.OVL --rtl ../gateway-dos/GATE.RTL \\
        --manifest build/rtlink-manifest.json --output build/disasm --verify
"""
import argparse
import collections
import csv
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import x86_16 as X  # noqa: E402
import rtlink_resident as R  # noqa: E402

EXE_HEADER = 0x20


class Unit:
    """One code/data address space.

    ``data`` holds the unrelocated bytes at their *linked* offsets (relative
    to ``linear_base``). ``regions`` lists the file-backed pieces as
    ``(offset, length, file_offset, file_tag)``; only those bytes may be
    decoded. ``linked`` optionally holds the same bytes with every RTLink
    fixup applied for load segment 0; far pointers are read from it.
    """

    def __init__(self, name, data, linear_base, file_base, section=None, parent=None,
                 linked_segment=None, fixups=(), regions=None, file_tag='ovl'):
        self.name = name
        self.data = data
        self.linear_base = linear_base     # linked linear address of data[0]
        self.section = section
        self.parent = parent
        self.linked_segment = linked_segment
        self.file_tag = file_tag
        self.regions = sorted(regions or [(0, len(data), file_base, file_tag)])
        self.linked = None
        self.present = bytearray(len(data))
        for off, length, _, _ in self.regions:
            self.present[off:off + length] = b'\x01' * length
        self.fixups = set(fixups)          # offsets of relocated words
        self.default_cs = {}               # region offset -> CS base for that region
        self.insns = {}                    # offset -> Instr
        self.owner = {}                    # byte offset -> instruction start
        self.functions = {}                # offset -> source description
        self.cs = {}                       # instruction offset -> CS base (unit offset)
        self.tables = {}                   # offset -> (cs_base, [targets], src, bounded)
        self.conflicts = []
        self.pred = {}                     # offset -> (from offset, edge kind)
        self.errors = []
        self.unresolved = []

    def region_of(self, off):
        lo, hi = 0, len(self.regions)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.regions[mid][0] <= off:
                lo = mid + 1
            else:
                hi = mid
        if lo:
            r = self.regions[lo - 1]
            if r[0] <= off < r[0] + r[1]:
                return r
        return None

    def file_offset(self, off):
        r = self.region_of(off)
        return None if r is None else r[2] + off - r[0]

    def tag_of(self, off):
        r = self.region_of(off) if isinstance(off, int) else None
        return None if r is None else r[3]

    def is_present(self, start, end):
        return 0 <= start and end <= len(self.data) and all(self.present[start:end])

    def contains_linear(self, linear):
        off = linear - self.linear_base
        return 0 <= off < len(self.data) and bool(self.present[off])

    def cs_guess(self, off):
        r = self.region_of(off)
        return None if r is None else self.default_cs.get(r[0])

    def ident(self, off):
        fo = self.file_offset(off) if isinstance(off, int) else None
        if fo is None:
            return '%s+0x%x' % (self.name, off) if isinstance(off, int) else str(off)
        if self.section is None:
            return '%s:0x%06x' % (self.tag_of(off), fo)
        return 'ovl:0x%06x (section %d)' % (fo, self.section)

    def label(self, kind, off):
        fo = self.file_offset(off)
        if fo is None:
            return '%s_%s_%x' % (kind, self.name, off)
        tag = self.tag_of(off)
        return '%s_%06x' % (kind, fo) if tag in ('exe', 'ovl') else '%s_%s_%06x' % (kind, tag, fo)


class Program:
    def __init__(self, exe_path, ovl_path, manifest, rtl_path=None):
        exe = pathlib.Path(exe_path).read_bytes()
        if exe[:2] != b'MZ':
            raise ValueError('not an MZ executable')
        header, image = R.load_exe(exe_path)
        if header != EXE_HEADER:
            raise ValueError('unexpected MZ header layout')
        self.exe_image = image
        ip, cs = struct.unpack_from('<HH', exe, 0x14)
        self.entry = cs * 16 + ip
        self.entry_cs = cs * 16
        self.resident = R.ResidentImage(image)
        first = self.resident.first_record
        # RTLink header, segment table and startup stub: plain MZ image bytes.
        self.startup = Unit('exe-startup', bytes(image[:first]), 0, EXE_HEADER, file_tag='exe')
        # Resident records, and the GATE.RTL library records RTLINKST loads
        # into the same linked address space, at their linked addresses.
        self.rtl_bytes = None
        if rtl_path:
            self.rtl_bytes = pathlib.Path(rtl_path).read_bytes()
            self.resident.load_rtl(self.rtl_bytes)
        placed = [(r, 'exe', EXE_HEADER) for r in self.resident.records] + \
                 [(r, 'rtl', 0) for r in self.resident.rtl_records]
        end = max(r.linear + r.length for r, _, _ in placed)
        data, linked = bytearray(end), bytearray(end)
        regions = []
        chunks = self.resident.build(0, [r for r, _, _ in placed])
        for r, tag, base in placed:
            if r.has_data:
                if any(data[r.linear:r.linear + r.length]) or \
                        any(linked[r.linear:r.linear + r.length]):
                    raise ValueError('records overlap at %05x' % r.linear)
                data[r.linear:r.linear + r.length] = r.source[r.data_pos:r.data_pos + r.length]
                linked[r.linear:r.linear + r.length] = chunks[r.linear]
                regions.append((r.linear, r.length, base + r.data_pos, tag))
        self.exe = Unit('exe', bytes(data), 0, EXE_HEADER, regions=regions, file_tag='exe',
                        fixups=[r.linear + s for r, _, _ in placed for s in r.simple_sites()])
        self.exe.linked = bytes(linked)
        for r, _, _ in placed:
            if r.has_data:
                para = self.resident.segments[r.seg - 1][1]
                self.exe.default_cs[r.linear] = para * 16
        self.startup.default_cs[0] = None
        ovl = pathlib.Path(ovl_path).read_bytes()
        self.sections = []
        for s in manifest['sections']:
            start = s['image_file_offset']
            sdata = ovl[start:start + s['file_bytes']]
            base = s['linked_segment'] * 16
            fix = [f['image_offset'] for f in s['fixups']] if 'fixups' in s else []
            u = Unit('ovl-%03d' % s['id'], sdata, base, start, s['id'],
                     s['parent'], s['linked_segment'], fix)
            u.default_cs[0] = 0
            self.sections.append(u)
        self.units = [self.startup, self.exe] + self.sections

    def unit_for_far(self, src, seg, off):
        """Resolve a linked far address seen in ``src``; returns (unit, offset)."""
        linear = (seg * 16 + off) & 0xfffff
        if src is self.startup:
            return (src, linear) if src.contains_linear(linear) else (None, None)
        u = src
        while u is not None and u.section is not None:
            if u.contains_linear(linear):
                return u, linear - u.linear_base
            u = self.sections[u.parent] if u.parent is not None else None
        if self.exe.contains_linear(linear):
            return self.exe, linear
        return None, None

    def locate_exe(self, file_offset):
        """Map a GATEMAIN.EXE file offset to (unit, offset), or (None, None)."""
        img = file_offset - EXE_HEADER
        if 0 <= img < self.resident.first_record:
            return self.startup, img
        for r in self.resident.records:
            if r.has_data and r.data_pos <= img < r.data_pos + r.length:
                return self.exe, r.linear + img - r.data_pos
        return None, None

    def locate_rtl(self, file_offset):
        for r in self.resident.rtl_records:
            if r.has_data and r.data_pos <= file_offset < r.data_pos + r.length:
                return self.exe, r.linear + file_offset - r.data_pos
        return None, None


def load_seeds(prog, tracker, ghidra):
    seeds, skipped = [], collections.Counter()
    for path, source in ((tracker, 'tracker'), (ghidra, 'ghidra')):
        if not path:
            continue
        with open(path, newline='') as fh:
            for row in csv.DictReader(fh, delimiter='\t'):
                off = int(row['offset'], 16)
                if row['file'] in ('exe', 'rtl'):
                    unit, img = (prog.locate_exe if row['file'] == 'exe' else prog.locate_rtl)(off)
                    if unit is None:
                        skipped[source + ' exe row in RTLink record metadata'] += 1
                        continue
                elif row['file'] == 'ovl' and row['section'] != '':
                    unit = prog.sections[int(row['section'])]
                    img = off - unit.regions[0][2]
                else:
                    continue
                if 0 <= img < len(unit.data):
                    name = row.get('name') or ''
                    seeds.append((source, unit, img, name))
    return seeds, skipped


class Traversal:
    """Recursive descent with transactional seeds.

    Inside ``speculate()`` every state change is journaled. If anything
    reached from the seed fails to decode, overlaps existing code inside an
    instruction, or runs out of the image, the whole seed is rolled back:
    seeds from Ghidra-derived lists frequently point into data.
    """

    def __init__(self, prog):
        self.prog = prog
        self.work = collections.deque()
        self.journal = None
        self.failure = None
        self.rejected = []
        self.deferred = []                 # far targets: own transactions

    def put(self, unit, attr, key, value):
        d = getattr(unit, attr)
        if self.journal is not None:
            self.journal.append((d, key, key in d, d.get(key)))
        d[key] = value

    def fail(self, unit, off, why, report):
        if self.journal is not None:
            if self.failure is None:
                self.failure = (unit, off, why)
            return
        report.append((off, why))

    def speculate(self, unit, off, cs, why, src=None):
        """Traverse from one seed and then from the far targets it reached;
        each is kept only if it is clean."""
        ok = self.speculate_one(unit, off, cs, why, src)
        while self.deferred:
            self.speculate_one(*self.deferred.pop())
        return ok

    def speculate_one(self, unit, off, cs, why, src=None):
        if off in unit.insns:
            # Known code; a new CS may still propagate, which cannot fail.
            self.add(unit, off, cs, why, function=True, src=src)
            self.run()
            return True
        mark = len(self.deferred)
        self.journal, self.failure = [], None
        lists = [(u, len(u.unresolved)) for u in self.prog.units]
        self.add(unit, off, cs, why, function=True, src=src)
        self.run()
        journal, failure = self.journal, self.failure
        self.journal = self.failure = None
        if failure is None:
            return True
        for d, key, had, old in reversed(journal):
            if had:
                d[key] = old
            else:
                d.pop(key, None)
        for u, n in lists:
            del u.unresolved[n:]
        del self.deferred[mark:]
        self.work.clear()
        fu, foff, fwhy = failure
        self.rejected.append(dict(seed=unit.ident(off), reason=why, failure=fu.ident(foff)
                                  if 0 <= foff < len(fu.data) else foff, detail=fwhy))
        return False

    def add(self, unit, off, cs_base, why, function=False, src=None):
        if 0 <= off < len(unit.data) and off not in unit.pred:
            self.put(unit, 'pred', off, src if src is not None else (None, why))
        if not 0 <= off < len(unit.data):
            if self.journal is not None:
                self.fail(unit, off, 'target outside image', unit.unresolved)
            else:
                unit.unresolved.append((why, off))
            return
        if function and off not in unit.functions:
            self.put(unit, 'functions', off, why)
        if off in unit.insns and (cs_base is None or unit.cs.get(off) is not None):
            return
        self.work.append((unit, off, cs_base))

    def run(self):
        while self.work:
            unit, off, cs = self.work.popleft()
            self.flow(unit, off, cs)
            if self.failure is not None:
                self.work.clear()
                return

    def flow(self, unit, off, cs):
        while True:
            if off in unit.insns:
                # Already decoded; only continue to propagate a newly known CS.
                if cs is None or unit.cs.get(off) is not None:
                    return
                self.put(unit, 'cs', off, cs)
                ins = unit.insns[off]
            else:
                if not unit.is_present(off, off + 1):
                    self.fail(unit, off, 'outside file-backed bytes', unit.conflicts)
                    return
                if off in unit.owner and not self.truncate_table(unit, off):
                    self.fail(unit, off, 'inside %s' % unit.owner[off], unit.conflicts)
                    return
                try:
                    ins = X.decode(unit.data, off)
                except X.DecodeError as exc:
                    self.fail(unit, off, str(exc), unit.errors)
                    return
                if (not unit.is_present(off, ins.end)
                        or unit.region_of(off) != unit.region_of(ins.end - 1)):
                    self.fail(unit, off, 'instruction crosses region end', unit.conflicts)
                    return
                clash = next((b for b in range(off, ins.end) if b in unit.owner), None)
                if clash is not None and self.truncate_table(unit, clash):
                    clash = next((b for b in range(off, ins.end) if b in unit.owner), None)
                if clash is not None:
                    self.fail(unit, off, 'overlaps %s' % unit.owner[clash], unit.conflicts)
                    return
                self.put(unit, 'insns', off, ins)
                self.put(unit, 'cs', off, cs)
                for b in range(off, ins.end):
                    self.put(unit, 'owner', b, off)
            self.successors(unit, ins, cs)
            if self.failure is not None:
                return
            if ins.flow in ('jmp', 'ret', 'stop', 'jmpind', 'jmpfar'):
                return
            if ins.mnemonic == 'int' and ins.operands[0].value == 0x20:
                return
            if ins.end not in unit.pred:
                self.put(unit, 'pred', ins.end, (off, 'next'))
            off = ins.end

    def successors(self, unit, ins, cs):
        if ins.flow in ('jcc', 'jmp'):
            self.add(unit, ins.target, cs, 'branch', src=(ins.offset, 'branch'))
        elif ins.flow == 'call':
            self.add(unit, ins.target, cs, 'near call', function=True, src=(ins.offset, 'call'))
        elif ins.flow in ('callfar', 'jmpfar'):
            self.far(unit, ins)
        elif ins.flow == 'jmpind':
            self.jump_table(unit, ins, cs)

    def far(self, unit, ins):
        if unit.linked is not None and ins.opcode in (0x9a, 0xea):
            # Read the pointer as RTLINKST leaves it (target-based fixups).
            o, sg = struct.unpack_from('<HH', unit.linked, ins.end - 4)
            if (sg, o) != (ins.far.seg, ins.far.off):
                ins.far = X.Far(sg, o)
                ins.extra['fixed_up'] = True
        target, off = self.prog.unit_for_far(unit, ins.far.seg, ins.far.off)
        # A resident ten-byte RTLink trampoline: CALL gate; JMP FAR x; DW sid.
        if ins.flow == 'jmpfar' and unit is self.prog.exe and ins.end + 2 <= len(unit.data):
            sid = struct.unpack_from('<H', unit.data, ins.end)[0]
            if sid < len(self.prog.sections):
                sec = self.prog.sections[sid]
                linear = ins.far.seg * 16 + ins.far.off
                if sec.contains_linear(linear):
                    ins.extra['trampoline'] = sid
                    ins.extra['far_ref'] = (sec, linear - sec.linear_base)
                    self.far_edge(sec, linear - sec.linear_base,
                                  ins.far.seg * 16 - sec.linear_base, 'trampoline',
                                  ((unit.name, ins.offset), 'trampoline'))
                    return
        if target is None:
            unit.unresolved.append(('far %04x:%04x' % (ins.far.seg, ins.far.off), ins.offset))
            return
        ins.extra['far_target'] = target.ident(off)
        ins.extra['far_ref'] = (target, off)
        self.far_edge(target, off, ins.far.seg * 16 - target.linear_base,
                      'far call' if ins.flow == 'callfar' else 'far jump',
                      ((unit.name, ins.offset), 'far'))

    def far_edge(self, unit, off, cs, why, src):
        if self.journal is None:
            self.add(unit, off, cs, why, function=True, src=src)
        elif not (off in unit.insns and unit.cs.get(off) is not None):
            self.deferred.append((unit, off, cs, why, src))

    def truncate_table(self, unit, off):
        """Code reached inside an *unbounded* table proves the scan ran too
        far: cut the table before ``off``. Returns True if ``off`` is free."""
        owner = unit.owner.get(off)
        if owner is None or owner >= 0:
            return False
        table = -1 - owner
        cs, targets, src, bounded = unit.tables[table]
        keep = (off - table) // 2
        if bounded or keep == 0:
            return False
        self.put(unit, 'tables', table, (cs, targets[:keep], src, bounded))
        for b in range(table + 2 * keep, table + 2 * len(targets)):
            self.journal_pop(unit, 'owner', b)
        return True

    def journal_pop(self, unit, attr, key):
        d = getattr(unit, attr)
        if key not in d:
            return
        if self.journal is not None:
            self.journal.append((d, key, True, d[key]))
        del d[key]

    def jump_table(self, unit, ins, cs):
        m = ins.operands[0]
        if (not isinstance(m, X.Mem) or ins.note == 'far' or m.seg != 'cs'
                or m.base not in ('bx', 'si', 'di') or m.disp_size != 2):
            unit.unresolved.append(('indirect jump', ins.offset))
            return
        if cs is None:
            unit.unresolved.append(('jump table without known CS', ins.offset))
            return
        table = cs + (m.disp & 0xffff)
        count = self.table_bound(unit, ins)
        targets = []
        pos = table
        while unit.is_present(pos, pos + 2) and len(targets) < (count or 256):
            if pos in unit.owner or (pos + 1) in unit.owner:
                break
            if pos != table and pos in unit.tables:
                break
            t = cs + struct.unpack_from('<H', unit.data, pos)[0]
            if not unit.is_present(t, t + 1):
                break
            if count is None and t in unit.owner and t not in unit.insns:
                break
            targets.append(t)
            pos += 2
        if not targets:
            unit.unresolved.append(('empty jump table', ins.offset))
            return
        if table in unit.tables:
            return
        self.put(unit, 'tables', table, (cs, targets, ins.offset, count is not None))
        for b in range(table, table + 2 * len(targets)):
            self.put(unit, 'owner', b, -1 - table)
        for t in targets:
            self.add(unit, t, cs, 'switch', src=(ins.offset, 'switch'))

    @staticmethod
    def table_bound(unit, ins):
        """Find ``cmp r, N`` + ``ja``/``jbe`` a few instructions before."""
        off = ins.offset
        seen = []
        for back in range(1, 24):
            start = off - back
            cand = unit.insns.get(start)
            if cand and cand.end <= off:
                seen.append(cand)
                off = start
                if len(seen) >= 8:
                    break
        for i, cand in enumerate(seen):
            if cand.mnemonic in ('ja', 'jbe') and i + 1 < len(seen):
                cmp = seen[i + 1]
                if cmp.mnemonic == 'cmp' and isinstance(cmp.operands[1], X.Imm):
                    return (cmp.operands[1].value & 0xffff) + 1
        return None


# ------------------------------------------------------------------ output

def label_names(unit):
    names = {}
    for off in unit.functions:
        if off in unit.insns:
            names[off] = unit.label('F', off)
    for ins in unit.insns.values():
        if ins.target is not None and ins.target in unit.insns and ins.target not in names:
            names[ins.target] = unit.label('L', ins.target)
    for table, (cs, targets, _, _) in unit.tables.items():
        names.setdefault(table, unit.label('T', table))
        for t in targets:
            if t in unit.insns:
                names.setdefault(t, unit.label('L', t))
    return names


def instruction_texts(unit, names):
    """Return {offset: (text, ok)}; text is NASM or a db fallback."""
    out = {}
    for off, ins in unit.insns.items():
        tag = unit.tag_of(off)
        # Labels only resolve inside one output file (EXE vs. RTL).
        def label_for(t, tag=tag):
            return names.get(t) if unit.tag_of(t) == tag else None
        try:
            out[off] = X.format_nasm(ins, label_for)
        except X.Unformattable:
            out[off] = None
    return out


def check_encodings(nasm, texts):
    """Assemble each distinct label-free instruction text alone; return the
    set of texts whose bytes are confirmed, mapped to their bytes."""
    uniq = sorted(t for t in texts if t)
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp, 'check.asm')
        lines = ['bits 16', X.NASM_PRELUDE]
        for i, t in enumerate(uniq):
            lines.append('s%d: %s' % (i, t))
            lines.append('e%d: times 14-(e%d-s%d) db 0' % (i, i, i))
            lines.append('db e%d-s%d, 0' % (i, i))
        src.write_text('\n'.join(lines) + '\n')
        subprocess.run([nasm, '-f', 'bin', '-o', str(pathlib.Path(tmp, 'check.bin')), str(src)],
                       check=True)
        data = pathlib.Path(tmp, 'check.bin').read_bytes()
    result = {}
    for i, t in enumerate(uniq):
        slot = data[i * 16:(i + 1) * 16]
        result[t] = bytes(slot[:slot[14]])
    return result


def emit_range(unit, names, texts, start, end, lines):
    data = unit.data
    pos = start
    pending = []

    def flush():
        while pending:
            chunk = pending[:16]
            del pending[:16]
            lines.append('    db ' + ', '.join('0x%02x' % b for b in chunk))

    while pos < end:
        if pos in unit.tables:
            flush()
            cs, targets, src, bounded = unit.tables[pos]
            lines.append('%s:  ; switch table for %s%s' % (
                names[pos], unit.ident(src), '' if bounded else ' (unbounded scan)'))
            for t in targets:
                lbl = names.get(t) if unit.tag_of(t) == unit.tag_of(pos) else None
                if lbl:
                    lines.append('    dw %s - 0x%x' % (lbl, cs))
                else:
                    lines.append('    dw 0x%04x' % ((t - cs) & 0xffff))
            pos += 2 * len(targets)
            continue
        ins = unit.insns.get(pos)
        if ins is None:
            if pos in names:
                flush()
            pending.append(data[pos])
            pos += 1
            if pos in unit.insns or pos in unit.tables:
                flush()
            continue
        flush()
        if pos in names:
            if pos in unit.functions:
                lines.append('')
                lines.append('; ---- %s (%s)' % (unit.ident(pos), unit.functions[pos]))
            lines.append('%s:' % names[pos])
        text = texts[pos]
        comment = ''
        if ins.extra.get('trampoline') is not None:
            comment = ' ; -> section %d' % ins.extra['trampoline']
        elif 'far_target' in ins.extra:
            comment = ' ; -> %s%s' % (ins.extra['far_target'],
                                      ' (fixed up)' if ins.extra.get('fixed_up') else '')
        if text is None:
            lines.append('    %s ; %s%s' % (X.format_db(ins.raw), X.format_text(ins), comment))
        else:
            lines.append('    %s%s' % (text, comment))
        pos = ins.end
    flush()


HEADER = '; %s -- generated by tools/disassemble_gateway.py; contains original code.'


def emit_overlay(unit, names, texts):
    lines = [HEADER % unit.name,
             '; RTLink section %d, linked segment 0x%04x, OVL file offset 0x%06x'
             % (unit.section, unit.linked_segment, unit.regions[0][2]),
             'bits 16', '%include "prelude.inc"', '']
    emit_range(unit, names, texts, 0, len(unit.data), lines)
    return '\n'.join(lines) + '\n'


def emit_linked(prog, title, file_bytes, records, base, per_unit, preamble):
    """A whole RTLink file in file order; data records become NASM sections
    with start = file offset and vstart = linked linear address."""
    lines = [HEADER % title,
             '; Records are NASM sections: start = file offset,',
             '; vstart = linked linear address (load segment 0).',
             'bits 16', '%include "prelude.inc"', '']
    pos = [0]

    def raw(name, end):
        if end > pos[0]:
            lines.append('section %s progbits start=0x%x vstart=0 align=1' % (name, pos[0]))
            chunk = file_bytes[pos[0]:end]
            for i in range(0, len(chunk), 16):
                lines.append('    db ' + ', '.join('0x%02x' % b for b in chunk[i:i + 16]))
            pos[0] = end

    pos[0] = preamble(lines, raw)
    names, texts = per_unit[prog.exe.name]
    for r in records:
        raw('rec%02d_meta' % r.index, base + r.data_pos)
        if not r.has_data:
            continue
        para = prog.resident.segments[r.seg - 1][1]
        lines.append('')
        lines.append('; ==== record %d: segment #%d (paragraph 0x%04x) +0x%x, 0x%x bytes, '
                     '%d fixup sites' % (r.index, r.seg, para, r.offset, r.length,
                                         sum(len(f.sites) for f in r.fixups)))
        lines.append('section rec%02d progbits start=0x%x vstart=0x%x align=1'
                     % (r.index, base + r.data_pos, r.linear))
        emit_range(prog.exe, names, texts, r.linear, r.linear + r.length, lines)
        pos[0] = base + r.data_pos + r.length
    raw('trailer', len(file_bytes))
    return '\n'.join(lines) + '\n'


def emit_exe(prog, exe_bytes, per_unit):
    def preamble(lines, raw):
        raw('mz_header', EXE_HEADER)
        lines.append('section startup progbits start=0x%x vstart=0 align=1' % EXE_HEADER)
        lines.append('; RTLink header, segment table and startup stub (MZ image offsets)')
        names, texts = per_unit[prog.startup.name]
        emit_range(prog.startup, names, texts, 0, len(prog.startup.data), lines)
        return EXE_HEADER + len(prog.startup.data)
    return emit_linked(prog, 'GATEMAIN.EXE', exe_bytes, prog.resident.records, EXE_HEADER,
                       per_unit, preamble)


def emit_rtl(prog, per_unit):
    def preamble(lines, raw):
        raw('rtl_header', prog.resident.rtl_records[0].header_pos)
        return prog.resident.rtl_records[0].header_pos
    return emit_linked(prog, 'GATE.RTL', prog.rtl_bytes, prog.resident.rtl_records, 0,
                       per_unit, preamble)


def provenance(unit, off, limit=4000):
    """Walk predecessor edges back to the seed; returns a short path."""
    path, seen = [], set()
    while off is not None and off not in seen and len(seen) < limit:
        seen.add(off)
        src, kind = unit.pred.get(off, (None, '?'))
        if kind != 'next':
            path.append('%s<-%s' % (unit.ident(off) if isinstance(off, int) else off, kind))
        if isinstance(src, tuple) or src is None:
            if src is not None:
                path.append('%s:0x%x' % src)
            break
        off = src
    return path[-6:]


TERMINAL = ('jmp', 'ret', 'stop', 'jmpind', 'jmpfar')


def function_body(unit, entry, entries):
    """Instruction offsets reachable from ``entry`` without following calls.
    A jump to another function entry is a tail call, not part of the body."""
    body, todo, tails = set(), [entry], set()
    while todo:
        off = todo.pop()
        if off in body or off not in unit.insns:
            continue
        body.add(off)
        ins = unit.insns[off]
        nxt = []
        if ins.flow not in TERMINAL and not (ins.mnemonic == 'int'
                                             and ins.operands[0].value == 0x20):
            nxt.append(ins.end)
        if ins.flow in ('jcc', 'jmp') and ins.target is not None:
            if ins.flow == 'jmp' and ins.target in entries and ins.target != entry:
                tails.add((off, ins.target))
            else:
                nxt.append(ins.target)
        if ins.flow == 'jmpind':
            for table, (_, targets, src, _) in unit.tables.items():
                if src == off:
                    nxt.extend(targets)
        todo.extend(nxt)
    return body, tails


def trampoline_target(unit, off):
    """A resident RTLink stub ``CALL gate; JMP FAR x; DW sid``: its section target."""
    ins = unit.insns.get(off)
    if ins is None or ins.flow != 'call':
        return None
    nxt = unit.insns.get(ins.end)
    if nxt is None or nxt.extra.get('trampoline') is None:
        return None
    return nxt.extra['far_ref']


def call_graph(prog):
    """Static function-level call graph over all units (see docs/disassembly.md)."""
    def key(unit, off):
        return '%s:%x' % (unit.name, off)

    functions, edges = {}, collections.defaultdict(list)
    for unit in prog.units:
        entries = {o for o in unit.functions if o in unit.insns}
        for entry in sorted(entries):
            body, tails = function_body(unit, entry, entries)
            rets = sorted({unit.insns[o].mnemonic for o in body
                           if unit.insns[o].flow == 'ret'})
            stub = trampoline_target(unit, entry)
            fn = dict(id=key(unit, entry), unit=unit.name, section=unit.section,
                      ident=unit.ident(entry), label=unit.label('F', entry),
                      instructions=len(body),
                      bytes=sum(unit.insns[o].length for o in body), returns=rets,
                      indirect_calls=0, indirect_jumps=0)
            if stub is not None:
                fn['trampoline_to'] = key(*stub)
            functions[fn['id']] = fn
            for off in sorted(body):
                ins = unit.insns[off]
                site = unit.ident(off)
                if ins.flow == 'call' and ins.target in unit.insns:
                    edges[(fn['id'], key(unit, ins.target), 'near')].append(site)
                elif ins.flow in ('callfar', 'jmpfar') and 'far_ref' in ins.extra:
                    kind = 'far' if ins.flow == 'callfar' else (
                        'trampoline' if ins.extra.get('trampoline') is not None else 'farjmp')
                    edges[(fn['id'], key(*ins.extra['far_ref']), kind)].append(site)
                elif ins.flow == 'callind':
                    fn['indirect_calls'] += 1
                elif ins.flow == 'jmpind' and not any(
                        t[2] == off for t in unit.tables.values()):
                    fn['indirect_jumps'] += 1
            for off, target in tails:
                edges[(fn['id'], key(unit, target), 'tail')].append(unit.ident(off))
    return dict(functions=list(functions.values()),
                edges=[dict(caller=a, callee=b, kind=k, sites=v)
                       for (a, b, k), v in sorted(edges.items())])


def coverage(unit):
    code = sum(i.length for i in unit.insns.values())
    table = sum(2 * len(t[1]) for t in unit.tables.values())
    return dict(bytes=len(unit.data), code_bytes=code, table_bytes=table,
                instructions=len(unit.insns), functions=len(unit.functions),
                db_fallback=0, conflicts=len(unit.conflicts), errors=len(unit.errors),
                unresolved=len(unit.unresolved))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('exe')
    ap.add_argument('ovl')
    ap.add_argument('--rtl', help='GATE.RTL, placed into the resident address space')
    ap.add_argument('--manifest', required=True,
                    help='gateway-overlay --fixups JSON manifest')
    ap.add_argument('--tracker', default=str(pathlib.Path(__file__).resolve().parents[1]
                                             / 'decomp' / 'functions.tsv'))
    ap.add_argument('--ghidra-functions', help='optional ghidra-functions.tsv seeds')
    ap.add_argument('--output', required=True)
    ap.add_argument('--nasm', default=shutil.which('nasm') or 'nasm')
    ap.add_argument('--verify', action='store_true',
                    help='assemble every unit and compare with the original bytes')
    args = ap.parse_args(argv)

    prog = Program(args.exe, args.ovl, json.loads(pathlib.Path(args.manifest).read_text()),
                   args.rtl)
    trav = Traversal(prog)
    trav.add(prog.startup, prog.entry, prog.entry_cs, 'MZ entry', function=True)
    trav.run()
    seeds, skipped = load_seeds(prog, args.tracker, args.ghidra_functions)
    # Runtime-confirmed overlay entries first, then the rest, Ghidra last.
    def rank(seed):
        source, unit, off, name = seed
        return (source != 'tracker', unit.section is None, off)
    for source, unit, off, name in sorted(seeds, key=rank):
        cs = unit.cs_guess(off)
        trav.speculate(unit, off, cs, source + (' ' + name if name else ''))
    out = pathlib.Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'prelude.inc').write_text(X.NASM_PRELUDE)
    all_texts = {}
    per_unit = {}
    for unit in prog.units:
        names = label_names(unit)
        texts = instruction_texts(unit, names)
        per_unit[unit.name] = (names, texts)
        for off, t in texts.items():
            if t and unit.insns[off].flow not in ('jcc', 'jmp', 'call'):
                all_texts.setdefault(t, []).append(unit.insns[off].raw)
    confirmed = check_encodings(args.nasm, all_texts)
    report = dict(units={}, encoding_mismatches=collections.Counter())
    for unit in prog.units:
        names, texts = per_unit[unit.name]
        fallback = 0
        for off, t in list(texts.items()):
            ins = unit.insns[off]
            if t is None:
                fallback += 1
            elif t in confirmed and confirmed[t] != ins.raw:
                report['encoding_mismatches'][ins.mnemonic] += 1
                texts[off] = None
                fallback += 1
        if unit.section is not None:
            (out / ('%s.asm' % unit.name)).write_text(emit_overlay(unit, names, texts))
        cov = coverage(unit)
        cov['db_fallback'] = fallback
        cov['unresolved_detail'] = collections.Counter(w for w, _ in unit.unresolved).most_common()
        cov['error_detail'] = [(unit.ident(o), e, provenance(unit, o)) for o, e in unit.errors[:40]]
        per_unit[unit.name] = (names, texts)
        cov['conflict_detail'] = [(unit.ident(o), unit.ident(w) if w is not None and w >= 0 else w)
                                  for o, w in unit.conflicts[:20]]
        report['units'][unit.name] = cov

    exe_bytes = pathlib.Path(args.exe).read_bytes()
    (out / 'gatemain-exe.asm').write_text(emit_exe(prog, exe_bytes, per_unit))
    linked_files = [('gatemain-exe', exe_bytes)]
    if prog.rtl_bytes is not None:
        (out / 'gate-rtl.asm').write_text(emit_rtl(prog, per_unit))
        linked_files.append(('gate-rtl', prog.rtl_bytes))
    report['skipped_seeds'] = dict(skipped)
    if args.verify:
        failed = []
        for name, original in linked_files:
            subprocess.run([args.nasm, '-f', 'bin', '-i', str(out) + os.sep, '-o',
                            str(out / (name + '.bin')), str(out / (name + '.asm'))], check=True)
            built = (out / (name + '.bin')).read_bytes()
            (out / (name + '.bin')).unlink()
            report[name + '_reassembles'] = built == original
            if built != original:
                failed.append(name)
                report[name + '_first_difference'] = next(
                    (hex(i) for i in range(min(len(built), len(original)))
                     if built[i] != original[i]),
                    'length %d vs %d' % (len(built), len(original)))
        for unit in prog.sections:
            src = out / ('%s.asm' % unit.name)
            binf = out / ('%s.bin' % unit.name)
            subprocess.run([args.nasm, '-f', 'bin', '-i', str(out) + os.sep, '-o', str(binf),
                            str(src)], check=True)
            built = binf.read_bytes()
            binf.unlink()
            ok = built == unit.data
            report['units'][unit.name]['reassembles'] = ok
            if not ok:
                first = next((i for i in range(min(len(built), len(unit.data)))
                              if built[i] != unit.data[i]), min(len(built), len(unit.data)))
                report['units'][unit.name]['first_difference'] = unit.ident(first)
                failed.append(unit.name)
        report['verified'] = not failed
    total = collections.Counter()
    for cov in report['units'].values():
        for k in ('bytes', 'code_bytes', 'table_bytes', 'instructions', 'functions',
                  'db_fallback', 'conflicts', 'errors', 'unresolved'):
            total[k] += cov[k]
    report['total'] = dict(total)
    report['rejected_seeds'] = trav.rejected
    (out / 'callgraph.json').write_text(json.dumps(call_graph(prog), indent=1) + '\n')
    (out / 'report.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print(json.dumps(dict(total=report['total'], verified=report.get('verified'),
                          rejected_seeds=len(trav.rejected),
                          encoding_mismatches=report['encoding_mismatches']), indent=1))
    if args.verify and failed:
        print('reassembly differs: ' + ', '.join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

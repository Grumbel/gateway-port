#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Parse the RTLink resident load image inside an RTLink-linked MZ file.

``GATEMAIN.EXE`` has zero MZ relocations because its load image is not a
flat program. It starts with an RTLink header; a sequence of *records*
follows, and RTLINKST.COM copies each record's bytes to its linked
segment and applies the record's segment fixups. The grammar below follows
the loader in RTLINKST.COM (record loop at CS:0898, fixups at CS:09F5,
varint decoders at CS:0D91 and CS:0E06; fixup masks set at CS:10C8):

Header (image offset 0): ``+08`` segment table offset, ``+0A`` its count
(4-byte linked far pointers ``offset, paragraph``), ``+0C``/``+0E`` group
table, ``+10``/``+12`` external table (8-byte entries), ``+14`` offset
of the first record, ``+38``/``+3A`` variant patch table (11-byte entries).

Record: ``seg`` ``length`` ``offset`` ``has_data`` (varints), then
``length`` data bytes when ``has_data``, then fixups: ``groups`` varint
and per group a little-endian flags word ``W``:

- ``W & 0x0001``: target-based fixup; two varints (frame, target) follow.
- ``W & 0x0020 and W & 0x1000``: a varint run count follows.
- per run: ``W & 0x0020``: varint ``(addend << 1) | counted``; otherwise
  ``counted = W & 0x1000``. ``counted``: varint count; otherwise one site.
- each site: varint offset relative to the record's destination.

Simple fixups (``W & 1 == 0``) add the program load segment to the word.
Target-based fixups resolve a frame and a target (``ResidentImage.resolve``,
RTLINKST CS:0CA4) and patch a byte, offset word, segment word or far
pointer (``ResidentImage.apply``, CS:0A37). ``GATE.RTL`` (CS:0745) holds
further records for the same linked address space; ``load_rtl`` parses them.
Varints use a UTF-8-like prefix: ``0xxxxxxx``, ``10xxxxxx b``,
``110xxxxx b b``, ``1110xxxx b b b`` (big endian). Version-< 200 images
use another varint form; it is implemented but unverified here.
"""
from dataclasses import dataclass, field
import argparse
import json
import pathlib
import struct
import sys

MASK_TARGET = 0x0001
MASK_ADDEND = 0x0020
MASK_COUNTED = 0x1000


class RtlinkError(Exception):
    pass


@dataclass
class Fixup:
    flags: int
    frame: int | None
    target: int | None
    addend: int
    sites: list            # offsets relative to the record destination


@dataclass
class Record:
    index: int
    seg: int               # 1-based segment table index
    length: int
    offset: int            # destination offset within the segment
    has_data: bool
    header_pos: int        # image offset of the record header
    data_pos: int          # image offset of the data bytes
    end_pos: int           # image offset after the fixup block
    fixups: list = field(default_factory=list)
    linear: int = 0        # linked linear destination (before load segment)
    source: bytes = b''    # file image the record was parsed from

    def simple_sites(self):
        return [s for f in self.fixups if not f.flags & MASK_TARGET for s in f.sites]


class Reader:
    def __init__(self, data, pos, sized=False):
        self.data, self.pos, self.sized = data, pos, sized

    def byte(self):
        if self.pos >= len(self.data):
            raise RtlinkError('record runs past image end')
        v = self.data[self.pos]
        self.pos += 1
        return v

    def varint(self):
        b = self.byte()
        if b < 0x80:
            return b
        if self.sized:
            n = {0x81: 2, 0x84: 3, 0x88: 4}.get(b)
            if n is None:
                raise RtlinkError('bad sized varint %02x' % b)
            v = 0
            for i in range(n):
                v |= self.byte() << (8 * i)
            return v
        if b & 0x40 == 0:
            extra, v = 1, b & 0x3f
        elif b & 0x20 == 0:
            extra, v = 2, b & 0x1f
        elif b & 0x10 == 0:
            extra, v = 3, b & 0x0f
        else:
            raise RtlinkError('bad varint prefix %02x' % b)
        for _ in range(extra):
            v = (v << 8) | self.byte()
        return v

    def word(self):
        lo = self.byte()
        return lo | (self.byte() << 8)


class ResidentImage:
    def __init__(self, image, version=None):
        self.image = image
        h = struct.unpack_from('<16H', image, 0)
        self.seg_table, self.seg_count = h[4], h[5]
        self.group_table, self.group_count = h[6], h[7]
        self.ext_table, self.ext_count = h[8], h[9]
        self.first_record = h[10]
        self.variant_table, self.variant_count = struct.unpack_from('<HH', image, 0x38)
        if self.variant_count:
            raise RtlinkError('variant patch table not supported')
        if not (0 < self.seg_table < self.first_record and
                self.seg_table + 4 * self.seg_count <= self.first_record):
            raise RtlinkError('implausible segment table')
        self.segments = [struct.unpack_from('<HH', image, self.seg_table + 4 * i)
                         for i in range(self.seg_count)]
        self.records = []
        self.record_count = h[11]
        self.records = self.parse_records(image, self.first_record, self.record_count,
                                          sized=bool(version is not None and version < 200))
        self.end = self.records[-1].end_pos if self.records else self.first_record
        self.rtl_records = []

    def load_rtl(self, rtl):
        """Parse an external ``.RTL`` library linked into this program."""
        # RTLINKST reads 18 header bytes (CS:0745) and checks them at CS:0754.
        (sig, kind, sub, program_id, count, version, _flags, _r0, _r1, _r2,
         header_paras) = struct.unpack_from('<HBBHHHBBHHH', rtl, 0)
        if sig != 0x37ba:
            raise RtlinkError('not an RTLink .RTL file')
        if program_id != struct.unpack_from('<H', self.image, 0x26)[0]:
            raise RtlinkError('.RTL belongs to another program')
        start = header_paras * 16 if version >= 300 else 0x20
        self.rtl_records = self.parse_records(rtl, start, count, sized=version < 200)
        self.rtl_end = self.rtl_records[-1].end_pos if self.rtl_records else start
        return self.rtl_records

    def segment_linear(self, seg):
        if not 1 <= seg <= self.seg_count:
            raise RtlinkError('segment index %d out of range' % seg)
        off, para = self.segments[seg - 1]
        return para * 16 + off

    def parse_records(self, image, pos, count, sized):
        records = []
        for _ in range(count):
            rd = Reader(image, pos, sized)
            seg = rd.varint()
            length, offset, has = rd.varint(), rd.varint(), rd.varint()
            data_pos = rd.pos
            if has:
                rd.pos += length
                if rd.pos > len(image):
                    raise RtlinkError('record data past image end at %x' % pos)
            rec = Record(len(records), seg, length, offset, bool(has), pos, data_pos, 0)
            rec.source = image
            rec.linear = self.segment_linear(seg) + offset
            groups = rd.varint()
            for _ in range(groups):
                w = rd.word()
                frame = target = None
                if w & MASK_TARGET:
                    frame, target = rd.varint(), rd.varint()
                runs = rd.varint() if (w & MASK_ADDEND and w & MASK_COUNTED) else 1
                for _ in range(runs):
                    addend = 0
                    if w & MASK_ADDEND:
                        v = rd.varint()
                        counted, addend = v & 1, v >> 1
                    else:
                        counted = w & MASK_COUNTED
                    count = rd.varint() if counted else 1
                    sites = [rd.varint() for _ in range(count)]
                    for s in sites:
                        if not 0 <= s < length:
                            raise RtlinkError('fixup site outside record at %x' % pos)
                    rec.fixups.append(Fixup(w, frame, target, addend, sites))
            rec.end_pos = rd.pos
            records.append(rec)
            pos = rd.pos
        return records

    def linked_map(self):
        """Sorted (linear, length, record) for records with data."""
        return sorted((r.linear, r.length, r) for r in self.records if r.has_data)

    def resolve(self, method, index):
        """Return (linear_base, displacement, resolved) for a fixup frame or
        target, as RTLINKST CS:0CA4 does. Method 0 indexes the segment table,
        1 the group table, 3 is an absolute paragraph, 2 and 4 index 8-byte
        external tables (header +10 and +4A) whose ``'R'`` entries name a
        group, a segment and an offset."""
        if method == 3:
            return index * 16, 0, True
        if method in (0, 1):
            table = self.seg_table if method == 0 else self.group_table
            off, para = struct.unpack_from('<HH', self.image, table + 4 * (index - 1))
            return para * 16 + off, 0, True
        if method in (2, 4):
            table = self.ext_table if method == 2 else struct.unpack_from('<H', self.image, 0x4a)[0]
            kind, group, seg, off = struct.unpack_from('<BHHH', self.image, table + 8 * (index - 1))
            if kind != ord('R'):
                return group * 16, seg, False          # unresolved import
            target = self.segment_linear(seg) + off
            if group:
                base, _, _ = self.resolve(1, group)
                return base, target - base, True
            return self.segment_linear(seg), off, True
        raise RtlinkError('fixup method %d not supported' % method)

    def apply(self, data, rec, load_segment):
        """Apply all fixups of ``rec`` to ``data`` (RTLINKST CS:0A37-0CA1).

        Flags (version >= 410): 0x0002 segment-relative (else self-relative),
        0x001c location kind (0 low byte, 1 offset word, 2 segment word,
        3 far pointer, 4 high byte), 0x01c0 target method, 0x0e00 frame method,
        0x4000 shift the stored segment word left by 12 first."""
        for f in rec.fixups:
            if not f.flags & MASK_TARGET:
                for s in f.sites:
                    struct.pack_into('<H', data, s,
                                     (struct.unpack_from('<H', data, s)[0] + load_segment) & 0xffff)
                continue
            w = f.flags
            kind = (w & 0x1c) >> 2
            fbase, _, _ = self.resolve((w & 0xe00) >> 9, f.frame)
            tbase, tdisp, ok = self.resolve((w & 0x1c0) >> 6, f.target)
            relocate = ok and (w & 0xe00) >> 9 != 3
            if not ok:
                fbase = tbase
            target = tbase + tdisp
            if w & 0x0002:
                frame = fbase & ~0xf
                target -= frame
                frame_para = frame >> 4
            for s in f.sites:
                value = target + f.addend
                if not w & 0x0002:
                    value -= rec.linear + s + (1 if kind == 0 else 2)
                    if kind == 0:
                        data[s] = (data[s] + value) & 0xff
                    elif kind == 1:
                        _addw(data, s, value)
                    else:
                        raise RtlinkError('self-relative kind %d' % kind)
                    continue
                if kind == 0:
                    data[s] = (data[s] + value) & 0xff
                elif kind == 1:
                    _addw(data, s, value)
                elif kind == 4:
                    data[s] = (data[s] + (value >> 8)) & 0xff
                elif kind in (2, 3):
                    seg_at = s
                    if kind == 3:
                        _addw(data, s, value)
                        seg_at = s + 2
                    if w & 0x4000:
                        struct.pack_into('<H', data, seg_at,
                                         (struct.unpack_from('<H', data, seg_at)[0] << 12) & 0xffff)
                    _addw(data, seg_at, frame_para + (load_segment if relocate else 0))
                else:
                    raise RtlinkError('fixup kind %d not supported' % kind)

    def build(self, load_segment, records=None):
        """Return {linear: bytes} chunks as RTLINKST would write them."""
        out = {}
        for r in self.records if records is None else records:
            data = bytearray(r.source[r.data_pos:r.data_pos + r.length]) if r.has_data \
                else bytearray(r.length)
            self.apply(data, r, load_segment)
            out[load_segment * 16 + r.linear] = bytes(data)
        return out


def _addw(data, pos, value):
    struct.pack_into('<H', data, pos, (struct.unpack_from('<H', data, pos)[0] + value) & 0xffff)


def load_exe(path):
    exe = pathlib.Path(path).read_bytes()
    if exe[:2] != b'MZ':
        raise RtlinkError('not an MZ file')
    last, pages, nreloc, hdr = struct.unpack_from('<HHHH', exe, 2)
    size = pages * 512 - (512 - last if last else 0) - hdr * 16
    return hdr * 16, exe[hdr * 16:hdr * 16 + size]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('exe')
    ap.add_argument('--rtl', help='GATE.RTL library to parse as well')
    ap.add_argument('--snapshot', help='1 MiB RAM snapshot to verify placement against')
    ap.add_argument('--load-segment', type=lambda s: int(s, 16), default=0x1000)
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args(argv)
    header, image = load_exe(args.exe)
    res = ResidentImage(image)
    if args.rtl:
        res.load_rtl(pathlib.Path(args.rtl).read_bytes())
    stats = dict(records=len(res.records), segments=res.seg_count,
                 data_bytes=sum(r.length for r in res.records if r.has_data),
                 bss_bytes=sum(r.length for r in res.records if not r.has_data),
                 simple_fixups=sum(len(r.simple_sites()) for r in res.records),
                 target_fixups=sum(len(f.sites) for r in res.records + res.rtl_records
                                   for f in r.fixups
                                   if f.flags & MASK_TARGET),
                 parsed_to=res.end, image_bytes=len(image),
                 rtl_records=len(res.rtl_records))
    if args.snapshot:
        ram = pathlib.Path(args.snapshot).read_bytes()
        good, bad_records = 0, []
        for name, records in (('exe', res.records), ('rtl', res.rtl_records)):
            chunks = res.build(args.load_segment, records)
            for r in records:
                if not r.has_data:
                    continue
                lin = args.load_segment * 16 + r.linear
                data = chunks[lin]
                mism = sum(1 for i in range(len(data))
                           if lin + i < len(ram) and ram[lin + i] != data[i])
                if mism:
                    bad_records.append(dict(file=name, record=r.index, seg=r.seg,
                                            linear='%05x' % r.linear, length=r.length,
                                            mismatches=mism))
                else:
                    good += 1
        stats.update(snapshot_matching_records=good,
                     snapshot_mismatching_records=len(bad_records), mismatching=bad_records)
    if args.json:
        print(json.dumps(dict(stats=stats, records=[dict(
            index=r.index, seg=r.seg, linear=r.linear, length=r.length, offset=r.offset,
            has_data=r.has_data, file_offset=header + r.data_pos,
            fixups=[dict(flags=f.flags, frame=f.frame, target=f.target, addend=f.addend,
                         sites=f.sites) for f in r.fixups]) for r in res.records]), indent=1))
    else:
        print(json.dumps(stats, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())

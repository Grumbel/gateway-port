#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Checks the RTLink resident-record parser and fixup application on a
synthetic image (no original data)."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import rtlink_resident as R  # noqa: E402

PROGRAM_ID = 0xbeef


def synthetic_image():
    img = bytearray(0x64)
    struct.pack_into('<HHHHHHHH', img, 0x08, 0x50, 2, 0x58, 1, 0x5c, 1, 0x64, 2)
    struct.pack_into('<H', img, 0x26, PROGRAM_ID)
    struct.pack_into('<HHHH', img, 0x50, 0, 0x10, 0, 0x20)   # seg1 0x100, seg2 0x200
    struct.pack_into('<HH', img, 0x58, 0, 0x10)              # group1 0x100
    img[0x5c:0x64] = b'R' + struct.pack('<HHH', 1, 2, 4) + b'\0'   # seg2:4 in group1
    rec = bytearray([1, 8, 0, 1]) + bytes(range(1, 9))
    rec += bytes([4])
    rec += struct.pack('<H', 0x0000) + bytes([0])                # simple, site 0
    rec += struct.pack('<H', 0x0207) + bytes([1, 2, 2])          # group:seg2 offset, site 2
    rec += struct.pack('<H', 0x000b) + bytes([2, 2, 4])          # seg2 paragraph, site 4
    rec += struct.pack('<H', 0x02a7) + bytes([1, 1, 3 << 1, 6])  # extern + addend 3, site 6
    rec += bytes([2, 4, 8, 0, 0])                                # BSS record
    return bytes(img + rec)


class ReaderTests(unittest.TestCase):
    def test_prefix_varints(self):
        rd = R.Reader(bytes([0x7f, 0x81, 0x23, 0xc1, 0x02, 0x03]), 0)
        self.assertEqual([rd.varint(), rd.varint(), rd.varint()], [0x7f, 0x123, 0x10203])

    def test_sized_varints(self):
        rd = R.Reader(bytes([0x81, 0x34, 0x12, 0x05]), 0, sized=True)
        self.assertEqual([rd.varint(), rd.varint()], [0x1234, 5])

    def test_truncated(self):
        with self.assertRaises(R.RtlinkError):
            R.Reader(bytes([0x81]), 0).varint()


class ResidentTests(unittest.TestCase):
    def setUp(self):
        self.res = R.ResidentImage(synthetic_image())

    def test_records(self):
        a, b = self.res.records
        self.assertEqual((a.seg, a.length, a.linear, a.has_data), (1, 8, 0x100, True))
        self.assertEqual((b.seg, b.length, b.linear, b.has_data), (2, 4, 0x208, False))
        self.assertEqual(a.simple_sites(), [0])
        self.assertEqual(self.res.end, len(synthetic_image()))

    def test_fixups(self):
        chunks = self.res.build(0x1000)
        data = chunks[0x10100]
        words = struct.unpack('<4H', data)
        self.assertEqual(words[0], 0x0201 + 0x1000)            # + load segment
        self.assertEqual(words[1], 0x0403 + 0x100)             # seg2 relative to group1
        self.assertEqual(words[2], 0x0605 + 0x20 + 0x1000)     # seg2 paragraph, relocated
        self.assertEqual(words[3], 0x0807 + 0x104 + 3)         # extern + addend
        self.assertEqual(chunks[0x10208], bytes(4))

    def test_resolve_absolute(self):
        self.assertEqual(self.res.resolve(3, 0x1234), (0x12340, 0, True))

    def test_rtl(self):
        rtl = bytearray(0x20)
        struct.pack_into('<HBBHHHBBHHH', rtl, 0, 0x37ba, ord('S'), 1, PROGRAM_ID, 1, 502,
                         0, 0, 0, 0, 2)
        rtl += bytes([2, 2, 0, 1]) + b'ab' + bytes([0])
        recs = self.res.load_rtl(bytes(rtl))
        self.assertEqual((len(recs), recs[0].linear, self.res.rtl_end), (1, 0x200, len(rtl)))
        self.assertEqual(self.res.build(0, recs), {0x200: b'ab'})
        struct.pack_into('<H', rtl, 4, PROGRAM_ID ^ 1)
        with self.assertRaises(R.RtlinkError):
            self.res.load_rtl(bytes(rtl))

    def test_rejects_site_outside_record(self):
        img = bytearray(synthetic_image())
        img[0x64 + 13 + 2] = 9                                  # simple site 9 >= length 8
        with self.assertRaises(R.RtlinkError):
            R.ResidentImage(bytes(img))


if __name__ == '__main__':
    unittest.main()

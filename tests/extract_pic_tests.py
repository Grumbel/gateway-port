"""Check the extractor's Mission Critical XOR-frame CLI modes using synthetic PICs."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

TOOL = sys.argv.pop(1)


def make_pic():
    data = bytearray(0x1400)
    records = [
        (0x0000, 0, 0, 4, 2, bytes([10, 11, 12, 13, 20, 21, 22, 23])),
        (0x0003, 0, 0, 4, 2, bytes([0, 1, 2, 3, 4, 5, 6, 7])),
        (0x0003, 1, 0, 2, 1, bytes([8, 9])),
        (0x0003, 0, 0, 3, 2, bytes([1, 2, 3, 4, 5, 6])),
        (0x0000, 0, 0, 4, 2, bytes([30, 31, 32, 33, 40, 41, 42, 43])),
        (0x0003, 0, 0, 4, 2, bytes([1, 2, 4, 8, 16, 32, 64, 128])),
    ]
    for index, (flags, x, y, width, height, pixels) in enumerate(records):
        struct.pack_into('<II6H', data, index * 20, len(data), len(pixels), flags,
                         x, y, width, height, 0)
        data.extend(pixels)
    return bytes(data)


def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    width, height, bits, kind = struct.unpack_from('>IIBB', data, 16)
    assert bits == 8 and kind == 3
    pos, compressed = 8, bytearray()
    while pos < len(data):
        size, = struct.unpack_from('>I', data, pos)
        tag = data[pos + 4:pos + 8]
        if tag == b'IDAT':
            compressed += data[pos + 8:pos + 8 + size]
        pos += size + 12
    rows = zlib.decompress(compressed)
    assert len(rows) == height * (width + 1)
    assert all(rows[y * (width + 1)] == 0 for y in range(height))
    pixels = b''.join(rows[y * (width + 1) + 1:y * (width + 1) + 1 + width]
                      for y in range(height))
    return width, height, pixels


class ExtractPicTests(unittest.TestCase):
    def test_mc_xor_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / 'SYNTH.PIC'
            source.write_bytes(make_pic())

            def extract(mode, *options):
                target = root / mode
                subprocess.run([TOOL, *options, '-o', str(target), str(source)],
                               capture_output=True, text=True, check=True)
                return target / 'SYNTH'

            normal = extract('normal')
            self.assertEqual(read_png(normal / '000.png'),
                             (4, 2, bytes([10, 11, 12, 13, 20, 21, 22, 23])))
            self.assertEqual(read_png(normal / '001.png'),
                             (4, 2, bytes([10, 10, 14, 14, 16, 16, 16, 16])))
            # An independently stored second frame must use the original base, not #001.
            self.assertEqual(read_png(normal / '003.png'),
                             (4, 2, bytes([11, 9, 15, 13, 16, 16, 16, 23])))
            # By default, a smaller sprite keeps its original size and pixels.
            self.assertEqual(read_png(normal / '002.png'), (2, 1, bytes([8, 9])))
            # The second full image resets the reference for subsequent deltas.
            self.assertEqual(read_png(normal / '005.png'),
                             (4, 2, bytes([31, 29, 36, 41, 56, 9, 106, 171])))

            raw = extract('raw', '--raw-overlays')
            self.assertEqual(read_png(raw / '001.png'),
                             (4, 2, bytes([0, 1, 2, 3, 4, 5, 6, 7])))
            self.assertEqual(read_png(raw / '003.png'),
                             (3, 2, bytes([1, 2, 3, 4, 5, 6])))

            all_overlays = extract('all', '--mc-all-overlays')
            self.assertEqual(read_png(all_overlays / '002.png'),
                             (4, 2, bytes([10, 11 ^ 8, 12 ^ 9, 13, 20, 21, 22, 23])))
            conflict = subprocess.run([TOOL, '--raw-overlays', '--mc-all-overlays',
                                       '-o', str(root / 'conflict'), str(source)],
                                      capture_output=True, text=True)
            self.assertNotEqual(conflict.returncode, 0)
            self.assertIn('mutually exclusive', conflict.stderr)


if __name__ == '__main__':
    unittest.main()

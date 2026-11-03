# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Exercise the overlay CLI's manifest, extraction and snapshot comparison contracts."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = sys.argv.pop(1)


class OverlayToolTests(unittest.TestCase):
    def test_manifest_extraction_snapshot_and_bad_numeric_option(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            exe = bytearray(0x1000)
            struct.pack_into('<H', exe, 0xc19, 2)
            # One section, one relocation, 16 initialized bytes and 16 BSS bytes.
            struct.pack_into('<HHHBBHHHHH', exe, 0xc1f, 0x2000, 0xf00, 0, 0, 4,
                             2, 1, 0xffff, 0, 1)
            exe[0xf01:0xf0a] = b'TEST.OVL\0'
            ovl = bytearray(32)
            struct.pack_into('<HH', ovl, 0, 3, 0x2000)
            struct.pack_into('<H', ovl, 19, 0x1234)
            (root / 'TEST.EXE').write_bytes(exe)
            (root / 'TEST.OVL').write_bytes(ovl)
            command = [TOOL, str(root / 'TEST.EXE'), str(root / 'TEST.OVL'),
                       '--manager-offset', '0', '--manager-segment', '0', '--relocation', '1000']
            result = subprocess.run(command + ['--fixups', '--output', str(root / 'out')],
                                    capture_output=True, text=True, check=True)
            manifest = json.loads(result.stdout)
            self.assertEqual(manifest['section_count'], 1)
            section = manifest['sections'][0]
            self.assertEqual(section['image_file_offset'], 16)
            self.assertEqual(section['fixups'][0]['image_offset'], 3)
            raw = (root / 'out/section-000.raw.bin').read_bytes()
            relocated = (root / 'out/section-000.relocated.bin').read_bytes()
            self.assertEqual(raw, ovl[16:])
            self.assertEqual(struct.unpack_from('<H', relocated, 3)[0], 0x2234)
            memory = bytearray(0xa0000)
            struct.pack_into('<H', memory, 0x10000 + 0xc19, 2)
            struct.pack_into('<H', memory, 0x10000 + 0xc1f, 0x3000)
            memory[0x10000 + 0xc1f + 7] = 6
            memory[0x30000:0x30010] = relocated
            (root / 'snapshot.bin').write_bytes(memory)
            check = subprocess.run(command + ['--snapshot', str(root / 'snapshot')],
                                   capture_output=True, text=True, check=True)
            self.assertEqual(json.loads(check.stdout)['snapshot_loaded_count'], 1)
            memory[0x30005] ^= 1
            (root / 'snapshot.bin').write_bytes(memory)
            mismatch = subprocess.run(command + ['--snapshot', str(root / 'snapshot')],
                                      capture_output=True, text=True)
            self.assertEqual(mismatch.returncode, 1)
            self.assertEqual(json.loads(mismatch.stdout)['snapshot_mismatched_sections'], 1)
            invalid = subprocess.run(command + ['--manager-segment', 'ffffffff'],
                                     capture_output=True, text=True)
            self.assertEqual(invalid.returncode, 1)
            self.assertIn('out of range', invalid.stderr)


if __name__ == '__main__':
    unittest.main()

"""Synthetic real-mode programs; no original game data required."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

PROBE = sys.argv.pop(1)


class ProbeTests(unittest.TestCase):
    def run_guest(self, code, files=None):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, data in (files or {}).items():
                (root / name).write_bytes(data)
            # 32-byte MZ header, one paragraph of code padding, 256-byte stack.
            header = bytearray(32)
            total = 32 + len(code)
            struct.pack_into('<14H', header, 0, 0x5A4D, total % 512,
                             (total + 511) // 512, 0, 2, 32, 0xFFFF,
                             (len(code) + 15) // 16, 256, 0, 0, 0, 28, 0)
            exe = root / 'TEST.EXE'
            exe.write_bytes(header + code)
            return subprocess.run([PROBE, str(exe)], text=True,
                                  capture_output=True, timeout=10)

    def test_exit(self):
        result = self.run_guest(bytes.fromhex('b8004ccd21'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DOS exit code: 0', result.stdout)

    def test_drive(self):
        result = self.run_guest(bytes.fromhex('b419cd21b44ccd21'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('DOS exit code: 2', result.stdout)

    def test_print(self):
        code = bytes.fromhex('0e1fba0e00b409cd21b8004ccd21')
        result = self.run_guest(code + b'hello$')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('hello', result.stdout)

    def test_case_insensitive_root_open_and_read(self):
        # Read 3 bytes and exit with the returned byte count in AL.
        code = bytes.fromhex(
            '0e1fba1a00b8003dcd2189c3b90300ba4000b43fcd21b44ccd21')
        self.assertEqual(len(code), 26)
        result = self.run_guest(code + b'C:\\test.dat\0', {'TEST.DAT': b'abc'})
        self.assertIn('DOS exit code: 3', result.stdout)

    def test_vector_round_trip(self):
        # Set vector 23h to CS:1234, get it, exit with low offset byte.
        code = bytes.fromhex('0e1fba3412b82325cd21b82335cd2189d8b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 52', result.stdout)

    def test_seek_and_close(self):
        # Open, seek to byte 2, read one byte, close; exit with that byte.
        code = bytes.fromhex(
            '0e1fba2b00b8003dcd2189c3b8004231c9ba0200cd21'
            'b90100ba6000b43fcd21b43ecd21a06000b44ccd21')
        self.assertEqual(len(code), 43)
        result = self.run_guest(code + b'test.dat\0', {'TEST.DAT': b'ABC'})
        self.assertIn('DOS exit code: 67', result.stdout)

    def test_missing_file_sets_error(self):
        code = bytes.fromhex('0e1fba0e00b8003dcd21b44ccd21')
        result = self.run_guest(code + b'ABSENT.DAT\0')
        self.assertIn('DOS exit code: 2', result.stdout)

    def test_parent_path_rejected(self):
        code = bytes.fromhex('0e1fba0e00b8003dcd21b44ccd21')
        result = self.run_guest(code + b'../outside\0')
        self.assertEqual(result.returncode, 1)
        self.assertIn('flat game-directory filename', result.stderr)

    def test_unsupported_service(self):
        result = self.run_guest(bytes.fromhex('b4ffcd21'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('unsupported interrupt/service', result.stderr)

    def test_hardware_access_stops(self):
        result = self.run_guest(bytes.fromhex('e460'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('unsupported hardware port read', result.stderr)

    def test_execution_is_bounded(self):
        result = self.run_guest(bytes.fromhex('ebfe'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('execution limit', result.stderr)


if __name__ == '__main__':
    unittest.main()

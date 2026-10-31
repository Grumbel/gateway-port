"""Synthetic real-mode programs; no original game data required."""
import pathlib
import datetime
import struct
import subprocess
import sys
import tempfile
import unittest

PROBE = sys.argv.pop(1)


class ProbeTests(unittest.TestCase):
    def run_guest(self, code, files=None, dump=False):
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
            command = [PROBE, str(exe)]
            if dump:
                command += ['--dump-prefix', str(root / 'snapshot')]
            result = subprocess.run(command, text=True, capture_output=True, timeout=15)
            if dump:
                result.picture = (root / 'snapshot.ppm').read_bytes()
                result.memory = (root / 'snapshot.bin').read_bytes()
            return result

    def test_vga_detection(self):
        result = self.run_guest(bytes.fromhex('b8001acd1089d8b44ccd21'))
        self.assertIn('DOS exit code: 8', result.stdout)

    def test_vga_plane_output_and_snapshot(self):
        # Select mode 12h, enable plane 0, and set the leftmost pixel.
        code = bytes.fromhex('b81200cd10bac403b80201efb800a08ec026c606000080b8004ccd21')
        result = self.run_guest(code, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        header, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(header, b'P6\n640 480\n')
        self.assertEqual(pixels[:6], bytes([0, 0, 170, 0, 0, 0]))
        self.assertEqual(len(pixels), 640 * 480 * 3)
        self.assertEqual(len(result.memory), 1024 * 1024)
        self.assertEqual(result.memory[0x10000:0x10000 + len(code)], code)

    def test_guest_timer_handler(self):
        code = bytearray.fromhex(
            '0e1fba0000b81c25cd21fb803e00000074f9a00000b44ccd21fe060000cf00')
        handler = len(code) - 6
        counter = len(code) - 1
        struct.pack_into('<H', code, 3, handler)
        for address in (13, 19, handler + 2):
            struct.pack_into('<H', code, address, counter)
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 1', result.stdout, result.stderr)

    def test_invalid_calendar_probe(self):
        # DESQview-style installation probe uses an invalid DOS calendar year.
        result = self.run_guest(bytes.fromhex('b90144ba5153b8012bcd21b44ccd21'))
        self.assertIn('DOS exit code: 255', result.stdout, result.stderr)

    def test_virtual_calendar_set_get(self):
        # Change only the guest's date, then read it back as a year low byte.
        code = bytes.fromhex('b9e807ba1d02b42bcd21b42acd2189c8b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 232', result.stdout, result.stderr)

    def test_empty_keyboard_poll(self):
        # BIOS AH=01h must set ZF when no key is available.
        result = self.run_guest(bytes.fromhex('b401cd169c58c1e806250100b44ccd21'))
        self.assertIn('DOS exit code: 1', result.stdout, result.stderr)

    def test_exit(self):
        result = self.run_guest(bytes.fromhex('b8004ccd21'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DOS exit code: 0', result.stdout)

    def test_resize_and_allocate(self):
        # Shrink the PSP allocation to 100h paragraphs; next data segment is 10f1h.
        code = bytes.fromhex('bb0001b44acd21bb2000b448cd21b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 241', result.stdout)

    def test_failed_growth_sets_carry(self):
        code = bytes.fromhex('bbffffb44acd219c58250100b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 1', result.stdout)

    def test_invalid_free_returns_error(self):
        code = bytes.fromhex('b834128ec0b449cd21b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 9', result.stdout)

    def test_console_device_info(self):
        # Standard output is a character device; return its low attribute byte.
        code = bytes.fromhex('bb0100b80044cd2189d0b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 211', result.stdout)

    def test_invalid_device_handle(self):
        code = bytes.fromhex('bbffffb80044cd21b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 6', result.stdout)

    def test_calendar_year(self):
        before = datetime.date.today().year
        result = self.run_guest(bytes.fromhex('b42acd2189c8b44ccd21'))
        after = datetime.date.today().year
        self.assertTrue(any(f'DOS exit code: {year & 255}' in result.stdout
                            for year in (before, after)), result.stderr)

    def test_time_ranges(self):
        # Return seconds; check the other fields with independent fixtures below.
        for register_move, limit in [('88f0', 60), ('88d0', 100), ('88e8', 24), ('88c8', 60)]:
            result = self.run_guest(bytes.fromhex('b42ccd21' + register_move + 'b44ccd21'))
            self.assertIn('DOS exit code:', result.stdout, result.stderr)
            value = int(result.stdout.split('DOS exit code: ')[1])
            self.assertGreaterEqual(value, 0)
            self.assertLess(value, limit)

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

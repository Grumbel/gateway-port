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
    def run_guest(self, code, files=None, dump=False, game_args=None, press=None, break_at=None, state=False, state_files=None, exe_name="TEST.EXE"):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            assets = root / 'assets'
            assets.mkdir()
            for name, data in (files or {}).items():
                (assets / name).write_bytes(data)
            if state_files is not None:
                (root / 'state').mkdir()
                for name, data in state_files.items():
                    (root / 'state' / name).write_bytes(data)
            # 32-byte MZ header, one paragraph of code padding, 256-byte stack.
            header = bytearray(32)
            total = 32 + len(code)
            struct.pack_into('<14H', header, 0, 0x5A4D, total % 512,
                             (total + 511) // 512, 0, 2, 32, 0xFFFF,
                             (len(code) + 15) // 16, 256, 0, 0, 0, 28, 0)
            exe = assets / exe_name
            exe.write_bytes(header + code)
            command = [PROBE, str(exe)]
            if state or state_files is not None:
                command += ['--state-dir', str(root / 'state')]
            if break_at is not None:
                command += ['--break-at', break_at]
            if press is not None:
                command += ['--press', press]
            if game_args is not None:
                command += ['--game-args', game_args]
            if dump:
                command += ['--dump-prefix', str(root / 'snapshot')]
            result = subprocess.run(command, text=True, capture_output=True, timeout=15)
            if dump:
                result.picture = (root / 'snapshot.ppm').read_bytes()
                result.memory = (root / 'snapshot.bin').read_bytes()
                result.registers = dict(line.split('=') for line in
                                        (root / 'snapshot.regs').read_text().splitlines())
            result.files = {name: (assets / name).read_bytes() for name in (files or {})}
            result.state_files = {path.name: path.read_bytes() for path in (root / 'state').glob('*')
                                  if path.is_file()}
            return result

    @staticmethod
    def text_font_fixture():
        font = bytearray(770)
        font[:10] = bytes.fromhex('010101217f0008080000')
        font[10 + (ord('G') - 0x21) * 8] = 0x80
        return bytes(font)

    def test_mouse_far_call_through_driver_vector(self):
        # DOS clients may PUSHF/CALL FAR the vector instead of executing INT 33h.
        code = bytes.fromhex(
            'b83335cd21891e20038c062203'
            '31c09cff1e2003a32403891e2603'
            'b8004ccd21')
        result = self.run_guest(code, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        base = 0x0ff0 * 16 + 0x324
        self.assertEqual(struct.unpack_from('<2H', result.memory, base), (0xffff, 3))

    def test_mouse_detection_position_and_ranges(self):
        # Discover non-null/non-IRET driver entry, reset, then clamp a position.
        code = bytes.fromhex(
            'b83335cd218cc0a30003'  # DOS get vector; save segment at DS:0300.
            '268a07883e0203'        # Save entry opcode at DS:0302.
            'b81200cd1031c0cd33a30403891e0603'
            'b80700b90a00ba6400cd33'
            'b80800b91400bac800cd33'
            'b80400b9ff7fba0000cd33'
            'b80300cd33891e0803890e0a0389160c03'
            'b80b00cd33890e0e0389161003'
            'b8004ccd21')
        result = self.run_guest(code, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        base = 0x0ff0 * 16 + 0x300
        self.assertEqual(result.memory[base:base+2], b'\x00\xf0')
        self.assertNotEqual(result.memory[base+2], 0xcf)
        self.assertEqual(struct.unpack_from('<7H', result.memory, base+4),
                         (0xffff, 3, 0, 100, 20, 0, 0))

    def test_bios_font_uses_executable_prefix_and_native_height(self):
        # No Gateway assets: a mixed-case TEST_200 font has distinct adjacent rows.
        font = bytearray(10 + 95 * 16)
        font[:10] = bytes.fromhex('010101217f0010080000')
        offset = 10 + (ord('L') - 0x21) * 16
        font[offset] = 0x80
        font[offset + 1] = 0x40
        code = bytes.fromhex('b81200cd10bb0f00b90100b84c09cd10b8004ccd21')
        result = self.run_guest(code, {'test_200.fnt': bytes(font)}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        _, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(pixels[:6], b'\xff\xff\xff\0\0\0')
        self.assertEqual(pixels[640*3:640*3+6], b'\0\0\0\xff\xff\xff')

    def test_bios_font_uses_main_prefix_and_ega_height(self):
        ega = bytearray(10 + 95 * 14)
        ega[:10] = bytes.fromhex('010101217f000e080000')
        offset = 10 + (ord('L') - 0x21) * 14
        ega[offset] = 0x40
        ega[offset + 1] = 0x20
        vga = bytearray(10 + 95 * 16)
        vga[:10] = bytes.fromhex('010101217f0010080000')
        vga[10 + (ord('L') - 0x21) * 16] = 0x80
        code = bytes.fromhex('b81000cd10bb0f00b90100b84c09cd10b8004ccd21')
        result = self.run_guest(code, {'TEST_100.FNT': bytes(ega), 'TEST_200.FNT': bytes(vga)},
                                dump=True, exe_name='TESTMAIN.EXE')
        self.assertEqual(result.returncode, 0, result.stderr)
        header, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(header, b'P6\n640 350\n')
        self.assertEqual(pixels[:6], b'\0\0\0\xff\xff\xff')
        self.assertEqual(pixels[640*3:640*3+9], b'\0\0\0' * 2 + b'\xff\xff\xff')

    def test_bios_font_rejects_truncated_native_height(self):
        font = bytearray(self.text_font_fixture())
        font[6] = 16
        result = self.run_guest(bytes.fromhex('b8004ccd21'), {'TEST_200.FNT': bytes(font)})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unexpected BIOS fallback font layout', result.stderr)

    def test_graphics_to_text_bios_character(self):
        # Reproduce the reported mode-12 -> mode-3 -> cursor query -> AH=09h sequence.
        code = bytes.fromhex(
            'b81200cd10ba4e01b402cd10b80300cd1031dbb403cd10'
            'b84709bb0f00b90200cd10b8004ccd21')
        result = self.run_guest(code, {'TEST_000.FNT': self.text_font_fixture()}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.memory[0xb8000:0xb8006], b'G\x0fG\x0f \x07')
        self.assertEqual(result.memory[0x450:0x452], b'\0\0')  # Mode reset; AH=09h does not move cursor.
        self.assertEqual(result.memory[0x449], 3)
        self.assertEqual(struct.unpack_from('<H', result.memory, 0x44a)[0], 80)
        self.assertEqual(result.memory[0x484], 24)
        self.assertEqual(struct.unpack_from('<H', result.memory, 0x485)[0], 16)
        header, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(header, b'P6\n640 400\n')
        self.assertEqual(pixels[:6], b'\xff\xff\xff\0\0\0')

    def test_text_page_repeat_read_and_direct_memory(self):
        # Page 2, last column; repeated AH=09h must wrap to the next row without moving cursor.
        code = bytes.fromhex(
            'b80300cd10bb0002ba4f00b402cd10b84709bb0f02b90200cd10'
            'b408cd10b80205cd10b8004ccd21')
        result = self.run_guest(code, {'TEST_000.FNT': self.text_font_fixture()}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.memory[0xba09e:0xba0a2], b'G\x0fG\x0f')
        self.assertEqual(result.memory[0xb8000:0xb8002], b' \x07')
        self.assertEqual(result.memory[0x462], 2)
        self.assertEqual(result.memory[0x454:0x456], b'\x4f\0')
        _, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(pixels[632 * 3:633 * 3], b'\xff\xff\xff')

    def test_text_memory_written_by_guest_is_presented(self):
        code = bytes.fromhex('b80300cd10b800b88ec026c7060000470fb8004ccd21')
        result = self.run_guest(code, {'TEST_000.FNT': self.text_font_fixture()}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        _, pixels = result.picture.split(b'255\n', 1)
        self.assertEqual(pixels[:3], b'\xff\xff\xff')

    def test_dos_list_of_lists_exposes_live_mcb_chain(self):
        for prefix, expected_blocks in [('', 2), ('bb0001b44acd21', 3)]:
            with self.subTest(blocks=expected_blocks):
                code = bytes.fromhex(prefix + 'b452cd21b8004ccd21')
                result = self.run_guest(code, dump=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                address = int(result.registers['ES'], 16) * 16 + int(result.registers['BX'], 16)
                segment = struct.unpack_from('<H', result.memory, address - 2)[0]
                self.assertLess(address, segment * 16)
                blocks = []
                while True:
                    signature, owner, size = struct.unpack_from('<BHH', result.memory, segment * 16)
                    self.assertIn(signature, (ord('M'), ord('Z')))
                    blocks.append((segment, owner, size))
                    self.assertLessEqual(len(blocks), expected_blocks)
                    if signature == ord('Z'):
                        self.assertEqual(segment + size + 1, 0xa000)
                        break
                    segment += size + 1
                self.assertEqual(len(blocks), expected_blocks)
                self.assertEqual(blocks[0], (0x0fcf, 0x0ff0, 31))
                if prefix:
                    self.assertEqual(blocks[-1][1], 0)  # New free tail after PSP shrink.

    def test_dos_list_pointer_guest_dereference(self):
        code = bytes.fromhex('b452cd21268b47fe8ed8a00000b44ccd21')
        result = self.run_guest(code)
        self.assertIn('DOS exit code: 77', result.stdout, result.stderr)  # First MCB is 'M'.

    def test_dos_dta_default_and_round_trip(self):
        result = self.run_guest(bytes.fromhex('b42fcd21b8004ccd21'), dump=True)
        self.assertEqual(int(result.registers['ES'], 16), 0x0ff0)
        self.assertEqual(int(result.registers['BX'], 16), 0x80)
        code = bytes.fromhex('0e1fba3412b41acd2131dbb42fcd21b8004ccd21')
        result = self.run_guest(code, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(int(result.registers['ES'], 16), 0x1000)
        self.assertEqual(int(result.registers['BX'], 16), 0x1234)

    def test_dos_default_fcbs_from_numeric_command_tail(self):
        code = bytearray.fromhex('0e1fbe0000bf5c00b80129cd21bf6c00b80129cd21b8004ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b' 0 258 9\r', dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.memory[0xff5c:0xff6c], b'\0' + b'0       ' + b'   ' + b'\0' * 4)
        self.assertEqual(result.memory[0xff6c:0xff7c], b'\0' + b'258     ' + b'   ' + b'\0' * 4)

    def test_overlay_exec_replaces_cached_code_and_preserves_psp(self):
        # Execute an old target first, then replace it with an MZ overlay at the same address.
        prefix = bytes.fromhex('b800208ec026c7060000b80126c706020000cb9a00000020')
        code = bytearray.fromhex('0e1f1e07ba0000bb0000b8034bcd219a00000020b44ccd21')
        name = b'CHILD.EXE\0'
        struct.pack_into('<H', code, 5, len(prefix) + len(code))
        struct.pack_into('<H', code, 8, len(prefix) + len(code) + len(name))
        overlay_code = bytes.fromhex('b82a00cb')
        header = bytearray(32)
        struct.pack_into('<14H', header, 0, 0x5a4d, 36, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 28, 0)
        result = self.run_guest(prefix + code + name + struct.pack('<HH', 0x2000, 0x1111),
                                {'CHILD.EXE': header + overlay_code}, dump=True)
        self.assertIn('DOS exit code: 42', result.stdout, result.stderr)
        self.assertEqual(result.memory[0x20000:0x20004], overlay_code)
        self.assertEqual(result.memory[0xff00:0xff02], b'\xcd\x20')

    def test_overlay_exec_format_error(self):
        code = bytearray.fromhex('0e1f1e07ba0000bb0000b8034bcd21b44ccd21')
        name = b'BAD.EXE\0'
        struct.pack_into('<H', code, 5, len(code))
        struct.pack_into('<H', code, 8, len(code) + len(name))
        result = self.run_guest(code + name + struct.pack('<HH', 0x2000, 0),
                                {'BAD.EXE': b'not an executable'})
        self.assertIn('DOS exit code: 11', result.stdout, result.stderr)

    def test_closed_dos_handles_are_reused(self):
        code = bytearray.fromhex('0e1fba0000b91900b8003dcd2189c3b43ecd21e2f3b8003dcd21b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'TEST.DAT\0', {'TEST.DAT': b'data'})
        self.assertIn('DOS exit code: 5', result.stdout, result.stderr)

    def test_dos_handle_exhaustion(self):
        code = bytearray.fromhex('0e1fba0000b91000b8003dcd21e2f9b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'TEST.DAT\0', {'TEST.DAT': b'data'})
        self.assertIn('DOS exit code: 4', result.stdout, result.stderr)

    def test_file_reusing_standard_slot_can_be_closed(self):
        code = bytearray.fromhex('0e1fba000031dbb43ecd21b8003dcd2189c3b43ecd21b8003dcd21b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'TEST.DAT\0', {'TEST.DAT': b'data'})
        self.assertIn('DOS exit code: 0', result.stdout, result.stderr)

    def test_read_only_file_in_stdout_slot_is_not_console(self):
        code = bytearray.fromhex('0e1fba0000bb0100b43ecd21b8003dcd2189c3b90100b440cd21b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'TEST.DAT\0', {'TEST.DAT': b'original'})
        self.assertIn('DOS exit code: 5', result.stdout, result.stderr)
        self.assertEqual(result.files['TEST.DAT'], b'original')

    def test_dos_create_write_seek_read_state(self):
        code = bytearray.fromhex('0e1f31c9ba0000b43ccd2189c3b90500ba0000b440cd21'
                                 'b8004231c931d2cd21b90500ba0002b43fcd21b43ecd21b8004ccd21')
        name, data = b'RESTART.DAT\0', b'hello'
        struct.pack_into('<H', code, 5, len(code))
        struct.pack_into('<H', code, 17, len(code) + len(name))
        result = self.run_guest(code + name + data, state=True, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.state_files['RESTART.DAT'], b'hello')
        self.assertEqual(result.memory[0x10200:0x10205], b'hello')
        self.assertNotIn('RESTART.DAT', result.files)

    def test_dos_zero_write_truncates_saved_state(self):
        code = bytearray.fromhex('0e1fba0000b8023dcd2189c3b8004231c9ba0200cd2131c9b440cd21b8004ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'RESTART.DAT\0', state_files={'RESTART.DAT': b'hello'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.state_files['RESTART.DAT'], b'he')

    def test_dos_write_copies_asset_to_state(self):
        code = bytearray.fromhex('0e1fba0000b8023dcd2189c3b90100ba0000b440cd21b43ecd21b8004ccd21')
        name = b'data.dat\0'
        struct.pack_into('<H', code, 3, len(code))
        struct.pack_into('<H', code, 16, len(code) + len(name))
        result = self.run_guest(code + name + b'Z', files={'Data.DAT': b'ABCDE'}, state=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.files['Data.DAT'], b'ABCDE')
        self.assertEqual(result.state_files['DATA.DAT'], b'ZBCDE')

    def test_dos_create_without_state_reports_access_denied(self):
        code = bytearray.fromhex('0e1f31c9ba0000b43ccd21b44ccd21')
        struct.pack_into('<H', code, 5, len(code))
        result = self.run_guest(code + b'RESTART.DAT\0')
        self.assertIn('DOS exit code: 5', result.stdout, result.stderr)
        self.assertEqual(result.state_files, {})

    def test_dos_find_first_next_merged_state(self):
        code = bytearray.fromhex('0e1fba000031c9b44ecd21b44fcd21b8004ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'GATE*.SAV\0', files={'GATE001.SAV': b'original', 'GATE002.SAV': b'two'},
                                state_files={'GATE001.SAV': b'saved'}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        dta = result.memory[0xff80:0xff80 + 43]
        self.assertEqual(dta[30:].split(b'\0')[0], b'GATE002.SAV')
        self.assertEqual(struct.unpack_from('<I', dta, 26)[0], 3)
        self.assertEqual(dta[21], 0x21)
        self.assertEqual(result.files['GATE001.SAV'], b'original')

    def test_dos_find_first_reports_no_matches(self):
        code = bytearray.fromhex('0e1fba000031c9b44ecd21b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'GATE*.SAV\0', state=True)
        self.assertIn('DOS exit code: 18', result.stdout, result.stderr)

    def test_dos_find_next_exhaustion(self):
        code = bytearray.fromhex('0e1fba000031c9b44ecd21b44fcd21b44ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'GATE*.SAV\0', state_files={'GATE001.SAV': b'saved'})
        self.assertIn('DOS exit code: 18', result.stdout, result.stderr)

    def test_dos_search_resumes_from_copied_dta(self):
        code = bytearray.fromhex('0e1fba000031c9b44ecd21b8f00f8ed8be80000e07bf0002b92b00f3a4'
                                 '0e1fba0002b41acd21b44fcd21b8004ccd21')
        struct.pack_into('<H', code, 3, len(code))
        result = self.run_guest(code + b'GATE*.SAV\0', files={'GATE001.SAV': b'one', 'GATE002.SAV': b'two'}, dump=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.memory[0x10200 + 30:0x10200 + 43].split(b'\0')[0], b'GATE002.SAV')

    def test_dos_code_read_invalidates_translation(self):
        prefix = bytes.fromhex('b800208ec026c7060000b80126c706020000cb9a00000020')
        code = bytearray.fromhex('0e1fba0000b8003dcd2189c3b800208ed831d2b90400b43fcd21'
                                 '9a00000020b44ccd21')
        struct.pack_into('<H', code, 3, len(prefix) + len(code))
        result = self.run_guest(prefix + code + b'CODE.BIN\0', {'CODE.BIN': bytes.fromhex('b82a00cb')})
        self.assertIn('DOS exit code: 42', result.stdout, result.stderr)

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

    def test_psp_command_tail(self):
        result = self.run_guest(bytes.fromhex('a08000b44ccd21'), game_args='VGA')
        self.assertIn('DOS exit code: 4', result.stdout, result.stderr)

    def test_console_write(self):
        code = bytes.fromhex('0e1fbb0100b90500ba1400b440cd21b8004ccd21')
        self.assertEqual(len(code), 20)
        result = self.run_guest(code + b'hello')
        self.assertIn('hello', result.stdout, result.stderr)

    def test_bios_cursor_round_trip(self):
        result = self.run_guest(bytes.fromhex('31dbba0705b402cd10b403cd1088d0b44ccd21'))
        self.assertIn('DOS exit code: 7', result.stdout, result.stderr)

    def test_dos_input_consumes_bios_polled_key(self):
        code = bytes.fromhex('b401cd16b407cd21b44ccd21')
        result = self.run_guest(code, press='escape')
        self.assertIn('DOS exit code: 27', result.stdout, result.stderr)

    def test_close_standard_handle(self):
        result = self.run_guest(bytes.fromhex('31dbb43ecd219c58250100b44ccd21'))
        self.assertIn('DOS exit code: 0', result.stdout, result.stderr)

    def test_environment_layout(self):
        result = self.run_guest(bytes.fromhex('b8004ccd21'), dump=True)
        segment = struct.unpack_from('<H', result.memory, 0xff00 + 0x2c)[0]
        self.assertEqual(result.memory[segment * 16:segment * 16 + 24],
                         b'PATH=C:\\\0\0\x01\0C:\\TEST.EXE\0')
        self.assertEqual(result.memory[(segment - 1) * 16], ord('M'))

    def test_breakpoint_snapshot(self):
        result = self.run_guest(bytes.fromhex('b83412b8004ccd21'), dump=True, break_at='10003')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(int(result.registers['AX'], 16), 0x1234)
        self.assertEqual(int(result.registers['IP'], 16), 3)

    def test_read_only_file_attributes(self):
        code = bytes.fromhex('0e1fba1000b80043cd2189c8b44ccd21')
        self.assertEqual(len(code), 16)
        result = self.run_guest(code + b'test.dat\0', files={'TEST.DAT': b'data'})
        self.assertIn('DOS exit code: 33', result.stdout, result.stderr)

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

    def test_missing_writable_file_reports_not_found(self):
        for access in (1, 2):
            with self.subTest(access=access):
                code = bytes.fromhex('0e1fba0e00b8') + bytes([access, 0x3d]) + bytes.fromhex('cd21b44ccd21')
                result = self.run_guest(code + b'RESTART.DAT\0')
                self.assertIn('DOS exit code: 2', result.stdout, result.stderr)

    def test_writable_asset_open_denied_and_preserved(self):
        for access in (1, 2):
            with self.subTest(access=access):
                code = bytes.fromhex('0e1fba0e00b8') + bytes([access, 0x3d]) + bytes.fromhex('cd21b44ccd21')
                result = self.run_guest(code + b'test.dat\0', {'TEST.DAT': b'original'})
                self.assertIn('DOS exit code: 5', result.stdout, result.stderr)
                self.assertEqual(result.files['TEST.DAT'], b'original')

    def test_invalid_open_mode_reports_dos_error(self):
        code = bytes.fromhex('0e1fba0e00b8033dcd21b44ccd21')
        result = self.run_guest(code + b'RESTART.DAT\0')
        self.assertIn('DOS exit code: 12', result.stdout, result.stderr)

    def test_failed_writable_open_sets_carry(self):
        code = bytes.fromhex('0e1fba1300b8013dcd219c58250100b44ccd21')
        self.assertEqual(len(code), 19)
        result = self.run_guest(code + b'RESTART.DAT\0')
        self.assertIn('DOS exit code: 1', result.stdout, result.stderr)

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

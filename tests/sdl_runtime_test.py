"""Exercise guest VGA/text transitions through SDL using dummy drivers."""
import pathlib
import struct
import subprocess
import sys
import tempfile


def run_guest(code, milliseconds=32, files=None):
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        assets = root / 'assets'
        assets.mkdir()
        for name, data in (files or {}).items():
            (assets / name).write_bytes(data)
        header = bytearray(32)
        struct.pack_into('<14H', header, 0, 0x5A4D, 32 + len(code), 1, 0, 2, 32,
                         0xFFFF, 2, 256, 0, 0, 0, 28, 0)
        exe = assets / 'VIDEO.EXE'
        exe.write_bytes(header + code)
        prefix = root / 'frame'
        result = subprocess.run([sys.argv[1], str(exe), '--state-dir', str(root / 'state'), '--milliseconds', str(milliseconds),
                                 '--dump-prefix', str(prefix)], capture_output=True,
                                text=True, timeout=15)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return prefix.with_suffix('.ppm').read_bytes()


# Mode 12h; map plane 0; set one blue pixel; wait for the bounded host run.
code = bytes.fromhex('b81200cd10bac403b80201efb800a08ec026c606000080ebfe')
picture = run_guest(code)
if not picture.startswith(b'P6\n640 480\n255\n\x00\x00\xaa\x00\x00\x00'):
    raise RuntimeError('SDL guest run produced the wrong graphics framebuffer')

# Wait for a BIOS tick so SDL has presented graphics before returning to text mode.
font = bytearray(770)
font[:10] = bytes.fromhex('010101217f0008080000')
font[10 + (ord('G') - 0x21) * 8] = 0x80
code = bytes.fromhex(
    'b81200cd1031c08ed8833e6c040074f9'
    'b80300cd10bb0f00b90100b84709cd10ebfe')
picture = run_guest(code, milliseconds=96, files={'VIDEO_000.FNT': bytes(font)})
if not picture.startswith(b'P6\n640 400\n255\n\xff\xff\xff\x00\x00\x00'):
    raise RuntimeError('SDL graphics-to-text transition produced the wrong text framebuffer')
print('SDL guest graphics/text transition and audio-stream checks passed')

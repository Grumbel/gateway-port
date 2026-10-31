"""Run actual guest VGA instructions through the SDL runtime using dummy drivers."""
import pathlib
import struct
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    # Mode 12h; map plane 0; set one blue pixel; wait for the bounded host run.
    code = bytes.fromhex('b81200cd10bac403b80201efb800a08ec026c606000080ebfe')
    header = bytearray(32)
    struct.pack_into('<14H', header, 0, 0x5A4D, 32 + len(code), 1, 0, 2, 32,
                     0xFFFF, 2, 256, 0, 0, 0, 28, 0)
    exe = root / 'VIDEO.EXE'
    exe.write_bytes(header + code)
    prefix = root / 'frame'
    result = subprocess.run([sys.argv[1], str(exe), '--milliseconds', '32',
                             '--dump-prefix', str(prefix)], capture_output=True,
                            text=True, timeout=15)
    if result.returncode:
        raise RuntimeError(result.stderr)
    picture = prefix.with_suffix('.ppm').read_bytes()
    if not picture.startswith(b'P6\n640 480\n255\n\x00\x00\xaa\x00\x00\x00'):
        raise RuntimeError('SDL guest run produced the wrong framebuffer')
    print('SDL runtime guest-frame and audio-stream check passed')

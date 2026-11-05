#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Check the local static-Unicorn fallback without needing the DOS game."""

from pathlib import Path
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_probe_from_unicorn_zip import (core_sources, ensure_executable,  # noqa: E402
                                          extract_unicorn, picker_oracle_sources)


sources = core_sources((ROOT / 'CMakeLists.txt').read_text(encoding='utf-8'))
assert sources[0] == 'src/probe.cpp'
assert 'src/rtlink.cpp' in sources and 'src/picker_capture.cpp' in sources
assert len(sources) == len(set(sources))
oracle_sources = picker_oracle_sources(sources)
assert oracle_sources[-1] == 'tests/native_picker_oracle_tests.cpp'
assert 'src/probe.cpp' not in oracle_sources
assert len(oracle_sources) == len(set(oracle_sources))
try:
    picker_oracle_sources(['src/wrong.cpp'])
except ValueError as error:
    assert 'probe' in str(error)
else:
    raise AssertionError('accepted a core source list without probe entry')
try:
    core_sources('add_library(gateway_core src/foo.cpp bad_source)')
except ValueError as error:
    assert 'source list' in str(error)
else:
    raise AssertionError('accepted an unexpected CMake core source')

with tempfile.TemporaryDirectory(prefix='gateway-probe-static-') as temporary:
    root = Path(temporary)
    existing = root / 'old-probe'
    existing.write_bytes(b'not a real executable')
    existing.chmod(0o644)
    ensure_executable(existing)
    assert existing.stat().st_mode & 0o700 == 0o700
    assert existing.stat().st_mode & 0o077 == 0o044
    archive = root / 'unicorn.zip'
    prefix = 'nix/store/abcdefghijkl-unicorn-2.1.4/'
    with zipfile.ZipFile(archive, 'w') as output:
        output.writestr(prefix + 'lib/libunicorn.a', b'archive')
        output.writestr(prefix + 'include/unicorn/unicorn.h', b'header')
        output.writestr(prefix + 'include/unicorn/x86.h', b'x86')
        output.writestr(prefix + 'include/unicorn/../../unsafe.h', b'unsafe')
        output.writestr('unrelated/executable.exe', b'game')
    include, library = extract_unicorn(archive, root / 'out')
    assert (include / 'unicorn/unicorn.h').read_bytes() == b'header'
    assert (include / 'unicorn/x86.h').read_bytes() == b'x86'
    assert library.read_bytes() == b'archive'
    assert not (root / 'out/unsafe.h').exists()
    assert not (root / 'out/unrelated').exists()

    with zipfile.ZipFile(archive, 'w') as output:
        output.writestr(prefix + 'lib/libunicorn.a', b'archive')
    try:
        extract_unicorn(archive, root / 'missing-header')
    except ValueError as error:
        assert 'unicorn.h' in str(error)
    else:
        raise AssertionError('accepted an archive without Unicorn headers')

print('Gateway restricted static-Unicorn build checks passed')

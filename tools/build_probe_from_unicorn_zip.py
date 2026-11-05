#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Build headless Gateway tools with GCC and a supplied Nix Unicorn ZIP.

This is a fallback for restricted analysis containers without Nix/CMake.
The archive contains native Unicorn headers and libunicorn.a, never game data.
Build output and extracted dependencies remain under the ignored build/.
For SDL/interactive runs, use the supported CMake/Nix build instead.
"""

import argparse
from pathlib import Path
import re
import stat
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
CORE = re.compile(r'add_library\(gateway_core\s+(.*?)\)', re.DOTALL)
SOURCE = re.compile(r'src/[\w/-]+\.cpp\Z')


def core_sources(cmake):
    found = CORE.search(cmake)
    if found is None:
        raise ValueError('cannot find gateway_core in CMakeLists.txt')
    names = found[1].split()
    if not names or any(SOURCE.fullmatch(name) is None for name in names):
        raise ValueError('unexpected gateway_core source list')
    return ['src/probe.cpp', *names]


def extract_unicorn(archive, directory):
    """Extract only the exact static library and public Unicorn headers."""
    with zipfile.ZipFile(archive) as source:
        libraries = [item for item in source.namelist()
                     if re.fullmatch(r'nix/store/[^/]+-unicorn-[^/]+/lib/libunicorn\.a',
                                     item)]
        if len(libraries) != 1:
            raise ValueError('ZIP must contain one Nix Unicorn static library')
        prefix = libraries[0].removesuffix('lib/libunicorn.a')
        header_prefix = prefix + 'include/unicorn/'
        headers = [item for item in source.namelist()
                   if item.startswith(header_prefix) and
                   re.fullmatch(r'[a-zA-Z0-9_-]+\.h',
                                item[len(header_prefix):])]
        if not any(item.endswith('/unicorn.h') for item in headers):
            raise ValueError('ZIP has no matching unicorn/unicorn.h')
        selected = [(libraries[0], directory / 'lib' / 'libunicorn.a')]
        selected.extend((item, directory / 'include' / 'unicorn' /
                         item.rsplit('/', 1)[-1]) for item in headers)
        for member, destination in selected:
            info = source.getinfo(member)
            if info.file_size > (64 * 1024 * 1024 if member == libraries[0]
                                 else 1024 * 1024):
                raise ValueError(f'oversized Unicorn ZIP member: {member}')
            destination.parent.mkdir(parents=True, exist_ok=True)
            with source.open(member) as original, destination.open('wb') as result:
                while chunk := original.read(256 * 1024):
                    result.write(chunk)
    return directory / 'include', directory / 'lib' / 'libunicorn.a'


def ensure_executable(output):
    """A preexisting output can retain mode 0644 after the linker rewrites it."""
    output.chmod(output.stat().st_mode | stat.S_IXUSR)


def picker_oracle_sources(sources):
    """The oracle needs the core but must not link the probe's main()."""
    if not sources or sources[0] != 'src/probe.cpp':
        raise ValueError('expected the probe as the first source')
    return [*sources[1:], 'tests/native_picker_oracle_tests.cpp']


def build(archive, output, cc, cxx, with_picker_oracle=False):
    output = output.resolve()
    # A local fallback must not overwrite source or leave compiled guest bytes
    # in the committed tree. Keep all intermediates near the build output.
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('output must be inside the ignored build/ directory')
    oracle = output.with_name('native-picker-oracle-tests')
    if with_picker_oracle and oracle == output:
        raise ValueError('probe and oracle output paths must differ')
    output.parent.mkdir(parents=True, exist_ok=True)
    include, library = extract_unicorn(archive, output.parent / 'unicorn')
    sources = core_sources((ROOT / 'CMakeLists.txt').read_text(encoding='utf-8'))
    oracle_sources = picker_oracle_sources(sources) if with_picker_oracle else []
    probe_sources = ['third_party/nuked-opl3/opl3.c', *sources]
    names = list(probe_sources)
    if with_picker_oracle:
        names.append('tests/native_picker_oracle_tests.cpp')
    objects = {}
    for index, name in enumerate(names):
        source = ROOT / name
        obj = output.parent / 'obj' / f'{index:02d}-{source.stem}.o'
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = ([cc, '-std=c11'] if name.endswith('.c') else
                   [cxx, '-std=c++20'])
        command += ['-O1', '-I', str(ROOT / 'src'),
                    '-I', str(ROOT / 'third_party/nuked-opl3'),
                    '-I', str(include), '-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True)
        objects[name] = obj
    link_args = ['-pthread', '-ldl', '-lm', '-latomic']
    subprocess.run([cxx, *map(str, (objects[name] for name in probe_sources)),
                    str(library),
                    '-o', str(output), *link_args],
                   check=True)
    ensure_executable(output)
    print(f'Headless Unicorn probe: {output}')
    if with_picker_oracle:
        source_names = ['third_party/nuked-opl3/opl3.c', *oracle_sources]
        subprocess.run([cxx, *map(str, (objects[name] for name in source_names)),
                        str(library), '-o', str(oracle), *link_args], check=True)
        ensure_executable(oracle)
        print(f'Original-code native picker oracle: {oracle}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unicorn-zip', type=Path, required=True,
                        help='user-supplied Nix Unicorn 2.1.x static library ZIP')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'build/local-probe/gateway-probe')
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--with-picker-oracle', action='store_true',
                        help='also build the original-code picker differential test')
    args = parser.parse_args()
    try:
        build(args.unicorn_zip, args.output, args.cc, args.cxx,
              args.with_picker_oracle)
    except (OSError, ValueError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()

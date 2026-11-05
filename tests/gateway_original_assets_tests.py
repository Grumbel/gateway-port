#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Optional original-Gateway asset oracle; never retains proprietary output.

Runs outside normal public test-data requirements. If the user-provided
Gateway 1 files are absent, CTest records a skip (exit 77). If present, their
content must match the pinned executable/resource revision exactly.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


PINNED = {
    'GATE.EXE': '767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b',
    'GATEMAIN.EXE': '1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb',
    'GATEMAIN.OVL': '05e14a53904c356cba5f9fbbc9de98cf8fb1a5bdb3b6bcbeff2ae97482982d71',
    'GATE.RTL': 'c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566',
    'GATESTR.DAT': '940c12fc7c615e6643562457942f97879457d338aa4ec14dc3a2e1827a5f5d89',
    'OBJECT.DAT': '736581a6bc32dac41aa4504380a83837e6ef2da46f9c42c2f9417d976933e9ca',
    'VOCAB.DAT': '33468f585c4d6fdf9dd1dd4e1231400647954c433aba27ae7e7cc4f83b5be309',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_assets(folder):
    for filename, expected in PINNED.items():
        actual = hashlib.sha256((folder / filename).read_bytes()).hexdigest()
        require(actual == expected, f'{filename}: wrong original revision (SHA-256 mismatch)')
    pics = sorted(folder.glob('GATE_*.PIC'))
    rgns = sorted(folder.glob('GATE_*.RGN'))
    require(len(pics) == 40, f'expected 40 original Gateway PIC files, got {len(pics)}')
    require(len(rgns) == 17, f'expected 17 original Gateway RGN files, got {len(rgns)}')
    return pics


def run_checked(command, *, input_text=None):
    result = subprocess.run(command, input=input_text, capture_output=True, text=True,
                            check=False)
    require(result.returncode == 0,
            f'{Path(command[0]).name} failed: {result.stderr.strip() or result.stdout.strip()}')
    return result.stdout


def verify_original(data, extractor, overlay, tools):
    pics = verify_assets(data)
    with tempfile.TemporaryDirectory(prefix='gateway-original-oracle-') as directory:
        output = Path(directory)
        # Tests the *real* PIC payloads, not only the synthetic compressor
        # fixtures. The output is private and deleted on test completion.
        decoded = run_checked([str(extractor), '--dialect', 'gateway', '-o',
                               str(output / 'pictures'), *(str(pic) for pic in pics)])
        require('1504 images written' in decoded and '0 failed' in decoded,
                'original PIC extraction count changed')
        files = list((output / 'pictures').rglob('*.png'))
        require(len(files) == 1504, f'expected 1504 decoded images, got {len(files)}')

        # One original RTLink manifest is shared by the independent RGN and
        # picture-condition analyzers, keeping their source identities fixed.
        manifest = run_checked([str(overlay), str(data / 'GATEMAIN.EXE'),
                                str(data / 'GATEMAIN.OVL'), '--fixups'])
        directory_data = json.loads(manifest)
        sections = directory_data['sections']
        require(len(sections) == 58, 'original RTLink section count changed')
        require(sum(section['relocation_count'] for section in sections) == 13947,
                'original RTLink relocation count changed')
        require(all(len(section['fixups']) == section['relocation_count']
                    for section in sections), 'RTLink fixup expansion disagrees with counts')
        for name, expected in (
            ('inspect_region_conditions.py',
             ('75 of 75 direct calls validated;', '433 unique rule records')),
            ('inspect_picture_conditions.py',
             ('81 of 81 calls validated;', '244 unique potential picture-layer records')),
        ):
            result = run_checked([sys.executable, str(tools / name),
                                  str(data / 'GATEMAIN.OVL'), '--manifest', '-',
                                  '--scan-calls', '--exe', str(data / 'GATEMAIN.EXE')],
                                 input_text=manifest)
            require(all(fragment in result for fragment in expected),
                    f'{name}: native conditional rule inventory changed')
    return 'Gateway pinned asset audit: 7 hashes, 40 PIC files, 1504 images, ' \
           '58 overlays, 13947 relocations, 433 RGN rules, 244 picture rules passed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--extract', type=Path, required=True)
    parser.add_argument('--overlay', type=Path, required=True)
    options = parser.parse_args()
    if not all((options.data / filename).is_file() for filename in PINNED):
        print('Gateway original asset audit skipped: no complete private Gateway 1 data')
        return 77
    try:
        print(verify_original(options.data, options.extract, options.overlay,
                              Path(__file__).resolve().parents[1] / 'tools'))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())

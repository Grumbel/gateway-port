#!/usr/bin/env python3
"""Compare native and experimental object-flag HLE with external Gateway assets."""
import argparse
import hashlib
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=pathlib.Path, default=pathlib.Path('build/gateway-probe'))
parser.add_argument('--data', type=pathlib.Path, default=pathlib.Path('../gateway-dos'))
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('build/game-logic'))
parser.add_argument('--scenario', choices=['look', 'drawer'], default='look')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
keys = [(10000, 0x3920), (11000, 0x3920), (12000, 0x3920)]
keys += [(14000, 0x266c), (14100, 0x186f), (14200, 0x186f),
         (14300, 0x256b), (14400, 0x1c0d)]
if args.scenario == 'drawer':
    keys += [(15000, 0x3920), (16000, 0x3920), (17000, 0x3920)]
    for start, command in [(18000, 'open drawer'), (21000, 'look')]:
        keys += [(start + i * 30, ord(c)) for i, c in enumerate(command)]
        keys.append((start + len(command) * 30, 0x1c0d))
    keys += [(22000, 0x3920), (23000, 0x3920)]
hashes = []
flag_sequences = []
for mode in ['native', 'verify', 'hle']:
    prefix = args.output / mode
    command = [str(args.probe.resolve()), str(args.data.resolve() / 'GATE.EXE'),
               '--press', 'escape', '--state-dir', str(args.output / (mode + '-state')),
               '--milliseconds', '24000' if args.scenario == 'drawer' else '18000',
               '--trace-research', str(prefix.with_suffix('.trace')),
               '--trace-entry', '147a6', '--trace-entry', '28d2b',
               '--dump-prefix', str(prefix)]
    for time, key in sorted(keys):
        command += ['--key-at', f'{time}:{key:04x}']
    if mode != 'native':
        command += ['--object-flags', mode]
    with prefix.with_suffix('.out').open('w') as out, prefix.with_suffix('.dos').open('w') as err:
        subprocess.run(command, stdout=out, stderr=err, check=True)
    if mode != 'native':
        log = prefix.with_suffix('.dos').read_text()
        summary = [line for line in log.splitlines() if line.startswith('Object flag ')]
        if not summary or int(summary[-1].rsplit(' ', 1)[1]) == 0:
            raise RuntimeError(f'{mode}: no supported object-flag calls intercepted')
        print(summary[-1])
        flag_sequences.append([line.split(' object=', 1)[1]
                               for line in prefix.with_suffix('.trace').read_text().splitlines()
                               if line.startswith('object-flag ')])
    digest = hashlib.sha256(prefix.with_suffix('.ppm').read_bytes()).hexdigest()
    hashes.append(digest)
    print(f'{mode} framebuffer SHA-256: {digest}')
if len(set(hashes)) != 1:
    raise RuntimeError('framebuffers differ; inspect traces and captures')
if args.scenario == 'drawer':
    for result in [0, 1]:
        expected = f'124 property=c result={result} return=49d8:1cf'
        if expected not in flag_sequences[0]:
            raise RuntimeError('drawer description did not observe both closed and open states')
if flag_sequences[0] != flag_sequences[1]:
    raise RuntimeError('verification and HLE flag-query sequences differ')
print('All three final framebuffers and both flag-query sequences match. This is a bounded replay, not full gameplay validation.')

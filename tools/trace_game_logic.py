#!/usr/bin/env python3
"""Compare native and experimental object-state HLE with external Gateway assets."""
import argparse
import hashlib
import pathlib
import subprocess
from inspect_object_state import inspect_objects

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=pathlib.Path, default=pathlib.Path('build/gateway-probe'))
parser.add_argument('--data', type=pathlib.Path, default=pathlib.Path('../gateway-dos'))
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('build/game-logic'))
parser.add_argument('--scenario', choices=['look', 'drawer', 'objects'], default='look')
parser.add_argument('--services', choices=['flags', 'state'], default='flags')
parser.add_argument('--verify-overlays', action='store_true')
parser.add_argument('--hle-overlays', action='store_true', help='host section loading in HLE mode')
args = parser.parse_args()
if args.hle_overlays:
    args.verify_overlays = True
args.output.mkdir(parents=True, exist_ok=True)
keys = [(10000, 0x3920), (11000, 0x3920), (12000, 0x3920)]
keys += [(14000, 0x266c), (14100, 0x186f), (14200, 0x186f),
         (14300, 0x256b), (14400, 0x1c0d)]
if args.scenario in ['drawer', 'objects']:
    keys += [(15000, 0x3920), (16000, 0x3920), (17000, 0x3920)]
    for start, command in [(18000, 'open drawer'), (21000, 'look')]:
        keys += [(start + i * 30, ord(c)) for i, c in enumerate(command)]
        keys.append((start + len(command) * 30, 0x1c0d))
    keys += [(22000, 0x3920), (23000, 0x3920)]
if args.scenario == 'objects':
    for start, text in [(25000, 'take book'), (28000, 'inventory'), (31000, 'drop book'),
                        (34000, 'close drawer'), (37000, 'look')]:
        keys += [(start + i * 30, ord(c)) for i, c in enumerate(text)]
        keys.append((start + len(text) * 30, 0x1c0d))
    keys += [(39000, 0x3920), (40000, 0x3920), (41000, 0x3920)]
hashes = []
service_sequences = []
object_states = []
overlay_sequences = []
for mode in ['native', 'verify', 'hle']:
    prefix = args.output / mode
    command = [str(args.probe.resolve()), str(args.data.resolve() / 'GATE.EXE'),
               '--press', 'escape', '--state-dir', str(args.output / (mode + '-state')),
               '--milliseconds', '42000' if args.scenario == 'objects' else '24000' if args.scenario == 'drawer' else '18000',
               '--trace-research', str(prefix.with_suffix('.trace')),
               '--trace-entry', '147a6', '--trace-entry', '28d2b',
               '--dump-prefix', str(prefix)]
    for time, key in sorted(keys):
        command += ['--key-at', f'{time}:{key:04x}']
    if args.verify_overlays:
        command += ['--verify-overlays']
    if args.hle_overlays and mode == 'hle':
        command += ['--hle-overlays']
    if mode != 'native':
        command += ['--object-state' if args.services == 'state' else '--object-flags', mode]
    with prefix.with_suffix('.out').open('w') as out, prefix.with_suffix('.dos').open('w') as err:
        subprocess.run(command, stdout=out, stderr=err, check=True, timeout=180)
    if args.verify_overlays:
        lines = prefix.with_suffix('.dos').read_text().splitlines()
        summary = [line for line in lines if line.startswith('RTLink verified loads: ')]
        if not summary or int(summary[-1].split()[3]) == 0:
            raise RuntimeError('no supported RTLink loads verified')
        print(summary[-1])
        if args.hle_overlays and mode == 'hle':
            host = [line for line in lines if line.startswith('RTLink host loads: ')]
            if not host or int(host[-1].split()[3]) == 0:
                raise RuntimeError('no sections loaded through the host')
            print(host[-1])
        if args.hle_overlays and mode == 'hle':
            trace_lines = prefix.with_suffix('.trace').read_text().splitlines()
            if any(line.startswith('read ') and 'file=C:\\GATEMAIN.OVL ' in line
                   for line in trace_lines):
                raise RuntimeError('native DOS overlay reads remain in the host-loading replay')
        overlay_sequences.append([line.split(' section=', 1)[1]
                                  for line in prefix.with_suffix('.trace').read_text().splitlines()
                                  if line.startswith('overlay-load ')])
    if mode != 'native':
        log = prefix.with_suffix('.dos').read_text()
        summary = [line for line in log.splitlines() if line.startswith('Object flag ')]
        if not summary or int(summary[-1].rsplit(' ', 1)[1]) == 0:
            raise RuntimeError(f'{mode}: no supported object-flag calls intercepted')
        print(summary[-1])
        if args.services == 'state':
            for line in log.splitlines():
                if line.startswith('Object service '):
                    print(line)
                    if args.scenario == 'objects' and int(line.rsplit(' ', 1)[1]) == 0:
                        raise RuntimeError('object scenario did not exercise every state service')
        service_sequences.append([(line.split()[0], line.split(' object=', 1)[1])
                               for line in prefix.with_suffix('.trace').read_text().splitlines()
                               if line.startswith('object-')])
    digest = hashlib.sha256(prefix.with_suffix('.ppm').read_bytes()).hexdigest()
    hashes.append(digest)
    regs = dict(line.split('=') for line in prefix.with_suffix('.regs').read_text().splitlines())
    object_states.append(inspect_objects(prefix.with_suffix('.bin').read_bytes(), int(regs['DS'], 16)))
    print(f'{mode} framebuffer SHA-256: {digest}')
if args.verify_overlays and not overlay_sequences[0] == overlay_sequences[1] == overlay_sequences[2]:
    raise RuntimeError('native, verification and HLE overlay-load sequences differ')
if object_states[0] != object_states[1] or object_states[0] != object_states[2]:
    raise RuntimeError('final object records or handler mappings differ')
if len(set(hashes)) != 1:
    raise RuntimeError('framebuffers differ; inspect traces and captures')
if args.scenario in ['drawer', 'objects']:
    for result in [0, 1]:
        expected = f'124 property=c result={result} return=49d8:1cf'
        if ('object-flag', expected) not in service_sequences[0]:
            raise RuntimeError('drawer description did not observe both closed and open states')
if args.scenario == 'objects' and args.services == 'state':
    for location in ['d3', 'ee']:
        if not any(name == 'object-location-set' and body.startswith(f'123 location={location} ')
                   for name, body in service_sequences[0]):
            raise RuntimeError('book did not move through inventory and back to the room')
    if not any(name == 'object-flag-clear' and body.startswith('124 property=c ')
               for name, body in service_sequences[0]):
        raise RuntimeError('drawer was not closed through the flag-clear service')
if service_sequences[0] != service_sequences[1]:
    raise RuntimeError('verification and HLE service-call sequences differ')
print(f'All three final framebuffers, {len(object_states[0])} object records, handler mappings, '
      'and both service-call sequences match. This is a bounded replay, not full gameplay validation.')

#!/usr/bin/env python3
"""Compare native and experimental object-state HLE with external Gateway assets."""
import argparse
import hashlib
import os
import pathlib
import subprocess
from inspect_object_state import inspect_objects
from story_scenarios import parse_sequence, schedule

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=pathlib.Path,
                    default=pathlib.Path(os.environ.get('GATEWAY_BUILD_DIR', 'build')) / 'gateway-probe')
parser.add_argument('--data', type=pathlib.Path, default=pathlib.Path('../gateway-dos'))
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('build/game-logic'))
parser.add_argument('--scenario', choices=['look', 'drawer', 'objects'], default='look')
parser.add_argument('--services', choices=['flags', 'state'], default='flags')
parser.add_argument('--verify-overlays', action='store_true')
parser.add_argument('--hle-overlays', action='store_true', help='host section loading in HLE mode')
parser.add_argument('--native-only', action='store_true',
                    help='collect a quick native trace without the two comparison replays')
parser.add_argument('--trace-state-writes', action='store_true',
                    help='verify native state services and mark actual byte changes in one replay')
parser.add_argument('--trace-admission', action='store_true',
                    help='trace the earlier resident action-selector callback and return')
parser.add_argument('--trace-visibility', action='store_true',
                    help='trace restricted candidate child-slot checks in the resident parser')
parser.add_argument('--trace-navigation', action='store_true',
                    help='trace entry into the original shared OVL section-4 move helper')
parser.add_argument('--trace-object-writes', metavar='HEX-ID',
                    help='watch native writes to an object record (e.g. 124 for drawer)')
parser.add_argument('--command-sequence', metavar='CMD,CMD,...',
                    help='replace the seven objects-scenario commands at 18,21,25,28,31,34,37 s')
args = parser.parse_args()
if args.native_only and args.hle_overlays:
    parser.error('--native-only and --hle-overlays cannot be combined')
if args.trace_state_writes and not args.native_only:
    parser.error('--trace-state-writes requires --native-only')
if args.trace_admission and (not args.native_only or args.scenario != 'objects'):
    parser.error('--trace-admission requires --native-only --scenario objects')
if args.trace_visibility and (not args.native_only or args.scenario != 'objects'):
    parser.error('--trace-visibility requires --native-only --scenario objects')
if args.trace_navigation and (not args.native_only or args.scenario != 'objects'):
    parser.error('--trace-navigation requires --native-only --scenario objects')
if args.trace_object_writes:
    if not args.native_only or args.scenario != 'objects':
        parser.error('--trace-object-writes requires --native-only --scenario objects')
    try:
        watched = int(args.trace_object_writes, 16)
        if not 0 < watched <= 0xffff:
            raise ValueError
    except ValueError:
        parser.error('--trace-object-writes requires a nonzero 16-bit hex object ID')
if args.command_sequence and not args.native_only:
    parser.error('--command-sequence currently requires --native-only')
if args.hle_overlays:
    args.verify_overlays = True
try:
    custom_commands = parse_sequence(args.command_sequence) if args.command_sequence else None
    keys = schedule(args.scenario, custom_commands)
except ValueError as error:
    parser.error(str(error))
# The expected action_code= field is emitted by the C++ gateway-probe, not the
# Python driver. An old binary accepts --trace-entry but silently omits the
# new fields, wasting an entire replay and leaving misleading diagnostics.
if args.scenario in ['drawer', 'objects']:
    try:
        probe_bytes = args.probe.read_bytes()
    except OSError as error:
        parser.error(f'cannot read {args.probe}: {error}; run gateway-build first')
    if b' action_code=' not in probe_bytes:
        parser.error(f'{args.probe} lacks action-dispatch trace support; '
                     'run gateway-build, or point --probe to the freshly built gateway-probe')
    if args.trace_state_writes and b' changed=' not in probe_bytes:
        parser.error(f'{args.probe} lacks state-write delta tracing; run gateway-build')
    if args.trace_admission and b' admission_target=' not in probe_bytes:
        parser.error(f'{args.probe} lacks action-selector trace support; run gateway-build')
    if args.trace_visibility and b' visibility_stage=' not in probe_bytes:
        parser.error(f'{args.probe} lacks candidate-visibility trace support; run gateway-build')
    if args.trace_object_writes and (b'object-write ms=' not in probe_bytes or
                                     b' child_set_object=' not in probe_bytes):
        parser.error(f'{args.probe} lacks native object-write tracing; run gateway-build')
args.output.mkdir(parents=True, exist_ok=True)
if custom_commands:
    print('Scripted commands: ' + ' | '.join(custom_commands))
hashes = []
service_sequences = []
object_states = []
overlay_sequences = []
for mode in (['native'] if args.native_only else ['native', 'verify', 'hle']):
    prefix = args.output / mode
    command = [str(args.probe.resolve()), str(args.data.resolve() / 'GATE.EXE'),
               '--press', 'escape', '--state-dir', str(args.output / (mode + '-state')),
               '--milliseconds', '42000' if args.scenario == 'objects' else '24000' if args.scenario == 'drawer' else '18000',
               '--trace-research', str(prefix.with_suffix('.trace')),
               '--trace-entry', '147a6', '--trace-entry', '28d2b',
               '--dump-prefix', str(prefix)]
    if args.scenario in ['drawer', 'objects']:
        command += ['--trace-entry', '49db5', '--trace-entry', '4a378',
                    '--trace-entry', '4a390',
                    # Check the resident action dispatcher before/after its
                    # indirect far call, not merely the speculative section-7
                    # destinations (which were absent in the objects replay).
                    '--trace-entry', '147c0', '--trace-entry', '147c5',
                    # Actual OPEN/CLOSE far-call operands in the RTLink manager.
                    # The callee hook and callsite record live code bytes.
                    '--trace-entry', '2de99', '--trace-entry', '2da39',
                    # All five nonresident section-5 action entry points.
                    # Their overlay offsets were mapped from RTLink trampolines.
                    '--trace-entry', '47b82', '--trace-entry', '468ab',
                    '--trace-entry', '46be7', '--trace-entry', '49121',
                    '--trace-entry', '47b04',
                    # Native object-state service entries: flag set/clear and
                    # direct location set, including calls from nested helpers.
                    '--trace-entry', '11950', '--trace-entry', '11983',
                    '--trace-entry', '11bbb',
                    # Keep specific OVL mutation hooks for source correlation
                    # and compatibility with old traces.
                    '--trace-entry', '47d44', '--trace-entry', '47d59',
                    '--trace-entry', '46a0c']
    if args.trace_admission and mode == 'native':
        # File offsets 0x5956/0x5987/0x5990 in original GATEMAIN.EXE:
        # selector function entry, indirect callback call, and return AX.
        command += ['--trace-entry', '14768', '--trace-entry', '14799',
                    '--trace-entry', '147a2']
    if args.trace_visibility and mode == 'native':
        # GATEMAIN.EXE resident section at 1000:0000: restricted slot gate
        # before/after call, and child-link accessor before/after call.
        command += ['--trace-entry', '10063', '--trace-entry', '10068',
                    '--trace-entry', '10075', '--trace-entry', '1007a']
    if args.trace_navigation and mode == 'native':
        # Section 4 image starts at GATEMAIN.OVL:0xb1e0 in 4319:0000;
        # the native child-list relocation helper starts at file 0xc830.
        # Capturing its entry stack identifies its *caller*, unlike the
        # location setter return at 4319:16e7 inside the helper itself.
        # This is a default-layout, original-code research hook, not HLE.
        command += ['--trace-entry', '447e0']
    if args.trace_object_writes and mode == 'native':
        # 0157:08fc (EXE 0x2e3a): native indexed child-link setter.
        command += ['--trace-entry', '11e6c']
    if mode == 'native' and (args.trace_state_writes or args.trace_object_writes):
        # Observe the native implementation's writes without replacing them.
        # Its existing verification hooks compare the predicted bytes with
        # the guest's result at each service epilogue.
        command += ['--object-state', 'verify']
    if mode == 'native' and args.trace_object_writes:
        command += ['--trace-object-writes', f'{watched:x}']
    for time, key in keys:
        command += ['--key-at', f'{time}:{key:04x}']
    if args.verify_overlays:
        command += ['--verify-overlays']
    if args.hle_overlays and mode == 'hle':
        command += ['--hle-overlays']
    if mode != 'native':
        command += ['--object-state' if args.services == 'state' else '--object-flags', mode]
    with prefix.with_suffix('.out').open('w') as out, prefix.with_suffix('.dos').open('w') as err:
        subprocess.run(command, stdout=out, stderr=err, check=True, timeout=180)
    if mode == 'native' and args.trace_object_writes:
        if not any(line.startswith('object-watch ') for line in
                   prefix.with_suffix('.trace').read_text().splitlines()):
            raise RuntimeError('native object-write watch never activated; check trace and probe')
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
if args.native_only:
    print('Native trace complete; no verification/HLE equivalence was tested.')
    raise SystemExit(0)
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

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Hash deterministic native walkthrough checkpoints and find the first divergence.

No guest RAM, original game assets, or save files are stored in golden manifests.
Run native guest replays under a private Xvfb display on a machine with Unicorn.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from run_walkthrough import compile_steps, parse_script
from run_walkthrough_campaign import load_campaign


MEMORY_SIZE = 1024 * 1024
CONVENTIONAL_END = 0xa0000
# BIOS Data Area tick counter. This is host-paced state, not Gateway story state.
VOLATILE_RANGES = ((0x046c, 0x0470, 'BIOS timer ticks'),)
SHA256 = re.compile(r'[0-9a-f]{64}\Z')


def read_registers(path):
    registers = {}
    for line in path.read_text(encoding='ascii').splitlines():
        if not re.fullmatch(r'[A-Z]+=[0-9a-fA-F]+', line):
            raise ValueError(f'invalid register line in {path}: {line!r}')
        name, hex_value = line.split('=', 1)
        if name in registers:
            raise ValueError(f'duplicate register {name} in {path}')
        registers[name] = int(hex_value, 16)
    if not {'SS', 'SP'} <= registers.keys():
        raise ValueError(f'snapshot lacks SS/SP: {path}')
    if any(registers[name] > 0xffff for name in ('SS', 'SP')):
        raise ValueError(f'invalid 16-bit stack register: {path}')
    return registers


def excluded_ranges(registers):
    """Use current SS:SP up to segment end, not the entire DS/SS segment.

    DOS programs often share DS and SS. Masking the whole 64 KiB segment
    would silently discard object state, so only the live stack tail is masked.
    """
    ranges = [dict(start=start, end=end, reason=reason)
              for start, end, reason in VOLATILE_RANGES]
    base = registers['SS'] << 4
    top = min(base + 0x10000, CONVENTIONAL_END)
    start = base + registers['SP']
    if start < top:
        ranges.append(dict(start=start, end=top, reason='active SS:SP stack tail'))
    return sorted(ranges, key=lambda item: (item['start'], item['end']))


def normalized_memory(path, registers):
    raw = path.read_bytes()
    if len(raw) != MEMORY_SIZE:
        raise ValueError(f'{path}: expected {MEMORY_SIZE} RAM bytes, got {len(raw)}')
    conventional = bytearray(raw[:CONVENTIONAL_END])
    ranges = excluded_ranges(registers)
    for region in ranges:
        start, end = region['start'], region['end']
        if not 0 <= start < end <= CONVENTIONAL_END:
            raise ValueError(f'out-of-bounds volatile region: {region}')
        conventional[start:end] = bytes(end - start)
    return conventional, ranges


def digest_snapshot(prefix, checkpoint):
    registers = read_registers(prefix.with_suffix('.regs'))
    memory, ranges = normalized_memory(prefix.with_suffix('.bin'), registers)
    return dict(label=checkpoint['label'], time_ms=checkpoint['time_ms'],
                action_index=checkpoint['action_index'],
                command_index=checkpoint['command_index'],
                sha256=hashlib.sha256(memory).hexdigest(),
                stack=dict(ss=registers['SS'], sp=registers['SP']),
                excluded=ranges)


def planned_checkpoints(stage, every):
    replay = compile_steps(parse_script(stage.script.read_text(encoding='utf-8')),
                           golden_every=every)
    checkpoints = [dict(label=point.label, time_ms=point.time_ms,
                        action_index=point.action_index,
                        command_index=point.command_index)
                   for point in replay.checkpoints]
    checkpoints.append(dict(label='final', time_ms=replay.duration_ms,
                            action_index=len(replay.actions),
                            command_index=sum(action.operation == 'COMMAND'
                                              for action in replay.actions)))
    return checkpoints


def snapshot_prefix(root, stage_name, label):
    return root / stage_name / ('native' if label == 'final' else 'checkpoint-' + label)


def capture_campaign(manifest, campaign_dir, every):
    if not 1 <= every <= 10000:
        raise ValueError('--every must be between 1 and 10000')
    stages = []
    for stage in load_campaign(manifest):
        checkpoints = []
        for checkpoint in planned_checkpoints(stage, every):
            prefix = snapshot_prefix(campaign_dir, stage.name, checkpoint['label'])
            checkpoints.append(digest_snapshot(prefix, checkpoint))
        stages.append(dict(name=stage.name, script_sha256=stage.script_sha256,
                           checkpoints=checkpoints))
    return dict(schema_version=1, campaign_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                every=every, memory_end=CONVENTIONAL_END, stages=stages)


def save_manifest(path, manifest):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


def read_manifest(path):
    record = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(record, dict) or record.get('schema_version') != 1 or \
            record.get('memory_end') != CONVENTIONAL_END or \
            not isinstance(record.get('stages'), list) or \
            not isinstance(record.get('every'), int):
        raise ValueError(f'invalid golden manifest: {path}')
    return record


def first_divergence(left, right):
    """Return (stage, checkpoint, reason), in execution order, or None."""
    for key in ('schema_version', 'campaign_sha256', 'every', 'memory_end'):
        if left.get(key) != right.get(key):
            return ('campaign', '-', f'{key} differs')
    left_stages, right_stages = left['stages'], right['stages']
    if len(left_stages) != len(right_stages):
        return ('campaign', '-', 'stage count differs')
    for a, b in zip(left_stages, right_stages):
        name = a.get('name', '?')
        if name != b.get('name') or a.get('script_sha256') != b.get('script_sha256'):
            return (name, '-', 'stage name or script digest differs')
        ac, bc = a.get('checkpoints'), b.get('checkpoints')
        if not isinstance(ac, list) or not isinstance(bc, list) or len(ac) != len(bc):
            return (name, '-', 'checkpoint count differs')
        for x, y in zip(ac, bc):
            label = x.get('label', '?')
            for key in ('label', 'time_ms', 'action_index', 'command_index'):
                if x.get(key) != y.get(key):
                    return (name, label, f'{key} differs')
            for key in ('stack', 'excluded'):
                if x.get(key) != y.get(key):
                    return (name, label, f'{key} differs at command {x["command_index"]}')
            if not all(isinstance(c.get('sha256'), str) and SHA256.fullmatch(c['sha256'])
                       for c in (x, y)):
                return (name, label, 'missing or malformed SHA-256')
            if x['sha256'] != y['sha256']:
                return (name, label, f'RAM SHA-256 differs after command '
                        f'{x["command_index"]} (action {x["action_index"]}, '
                        f'guest {x["time_ms"]} ms)')
    return None


def explain_difference(a, b, left_dir, right_dir, stage, label):
    """Optional local diagnostic: print the first unmasked address, never bytes."""
    if not left_dir or not right_dir or label == '-':
        return ''
    try:
        left_prefix = snapshot_prefix(left_dir, stage, label)
        right_prefix = snapshot_prefix(right_dir, stage, label)
        lregs = read_registers(left_prefix.with_suffix('.regs'))
        rregs = read_registers(right_prefix.with_suffix('.regs'))
        lmem, _ = normalized_memory(left_prefix.with_suffix('.bin'), lregs)
        rmem, _ = normalized_memory(right_prefix.with_suffix('.bin'), rregs)
        first = next((index for index, (l, r) in enumerate(zip(lmem, rmem)) if l != r), None)
        return f'; first unmasked byte at 0x{first:05x}' if first is not None else ''
    except (OSError, ValueError):
        return ''


def check_pair(left, right, left_dir=None, right_dir=None):
    divergence = first_divergence(left, right)
    if divergence is not None:
        stage, label, reason = divergence
        detail = explain_difference(left, right, left_dir, right_dir, stage, label)
        raise ValueError(f'first divergence: stage={stage} checkpoint={label}: {reason}{detail}')
    count = sum(len(stage['checkpoints']) for stage in left['stages'])
    print(f'Golden replay matched: {len(left["stages"])} stages, {count} RAM checkpoints')


def run_campaign(manifest, probe, data, output, every, timeout, verify_overlays, probe_args):
    command = [sys.executable, str(Path(__file__).with_name('run_walkthrough_campaign.py')),
               str(manifest), '--probe', str(probe), '--data', str(data),
               '--output', str(output), '--golden-every', str(every),
               '--timeout', str(timeout)]
    if verify_overlays:
        command.append('--verify-overlays')
    for argument in probe_args:
        command.append('--probe-arg=' + argument)
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='mode', required=True)
    capture = commands.add_parser('capture', help='hash an existing native campaign recording')
    capture.add_argument('manifest', type=Path)
    capture.add_argument('--campaign', type=Path, required=True)
    capture.add_argument('--every', type=int, default=10)
    capture.add_argument('--output', type=Path, required=True, help='write hashes (no guest bytes)')
    compare = commands.add_parser('compare', help='compare saved golden hash manifests')
    compare.add_argument('left', type=Path)
    compare.add_argument('right', type=Path)
    compare.add_argument('--left-campaign', type=Path)
    compare.add_argument('--right-campaign', type=Path)
    run = commands.add_parser('run', help='run two isolated campaigns and compare RAM checkpoints')
    run.add_argument('manifest', type=Path)
    run.add_argument('--probe', type=Path, default=Path('/tmp/gateway-port-build/gateway-probe'))
    run.add_argument('--data', type=Path, default=Path('../gateway-dos'))
    run.add_argument('--output', type=Path, default=Path('build/golden'))
    run.add_argument('--every', type=int, default=10)
    run.add_argument('--timeout', type=int, default=1800)
    run.add_argument('--verify-overlays', action='store_true')
    run.add_argument('--probe-arg', action='append', default=[], metavar='ARG',
                     help='forward backend flag; requires support in the selected probe')
    run.add_argument('--baseline', type=Path, help='compare each run against known good hashes')
    run.add_argument('--write-baseline', type=Path,
                     help='write validated hashes only after both runs agree')
    args = parser.parse_args()
    try:
        if args.mode == 'capture':
            save_manifest(args.output, capture_campaign(args.manifest, args.campaign,
                                                        args.every))
            print(f'Golden hashes saved: {args.output}')
        elif args.mode == 'compare':
            check_pair(read_manifest(args.left), read_manifest(args.right),
                       args.left_campaign, args.right_campaign)
        else:
            if args.timeout <= 0:
                raise ValueError('--timeout must be positive')
            if args.output.joinpath('run-a').resolve() == args.output.joinpath('run-b').resolve():
                raise ValueError('run directories must be distinct')
            baseline = read_manifest(args.baseline) if args.baseline else None
            # Never reuse a stale game state directory; two independent runs
            # must start from scratch or their saved-game handoffs are invalid.
            for suffix in ('a', 'b'):
                root = args.output / f'run-{suffix}'
                if root.exists():
                    raise ValueError(f'{root} already exists; choose a fresh --output')
            records = []
            for suffix in ('a', 'b'):
                root = args.output / f'run-{suffix}'
                print(f'Starting independent native campaign {suffix}', flush=True)
                run_campaign(args.manifest, args.probe, args.data, root, args.every,
                             args.timeout, args.verify_overlays, args.probe_arg)
                hashed = capture_campaign(args.manifest, root, args.every)
                save_manifest(args.output / f'run-{suffix}.json', hashed)
                if baseline is not None:
                    check_pair(baseline, hashed)
                records.append(hashed)
            check_pair(*records, args.output / 'run-a', args.output / 'run-b')
            if args.write_baseline:
                save_manifest(args.write_baseline, records[0])
                print(f'Validated baseline written to {args.write_baseline}')
    except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError,
            subprocess.CalledProcessError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()

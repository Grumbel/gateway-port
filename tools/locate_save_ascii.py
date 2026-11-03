#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Locate a differing original-save ASCII field in native RAM checkpoints.

This is a read-only diagnostic. It does not equate two states, mask bytes, or
assume that a copy of the string in RAM is the authoritative story variable.
"""

import argparse
import hashlib
from pathlib import Path

from inspect_gateway_save import GLOBAL_STATE_BYTES, parse_gateway_save
from run_golden import (CONVENTIONAL_END, MEMORY_SIZE, digest_snapshot,
                        read_manifest, snapshot_prefix, verified_recorded_save)


def saved_ascii_field(saved, offset, length):
    if offset < 0 or length < 1 or length > 64 or offset + length > GLOBAL_STATE_BYTES:
        raise ValueError('requested global ASCII field exceeds the 1,071-byte save block')
    value = saved.global_state[offset:offset + length]
    if any(byte < 0x20 or byte > 0x7e for byte in value):
        raise ValueError('requested original-save field is not entirely printable ASCII')
    return value


def occurrences(memory, needle):
    """Return physical guest addresses, searching only conventional memory."""
    if len(memory) != MEMORY_SIZE:
        raise ValueError(f'native snapshot has {len(memory)} bytes, expected {MEMORY_SIZE}')
    if not needle:
        raise ValueError('empty needle')
    result = []
    offset = memory.find(needle, 0, CONVENTIONAL_END)
    while offset != -1:
        if offset + len(needle) <= CONVENTIONAL_END:
            result.append(offset)
        offset = memory.find(needle, offset + 1, CONVENTIONAL_END)
    return result


def check_matching_checkpoints(left, right):
    for field in ('name', 'script_sha256'):
        if left.get(field) != right.get(field):
            raise ValueError(f'stage {field} differs between manifests')
    a, b = left['checkpoints'], right['checkpoints']
    if len(a) != len(b):
        raise ValueError('stage checkpoints differ in number')
    for x, y in zip(a, b):
        for field in ('label', 'time_ms', 'action_index', 'command_index'):
            if x.get(field) != y.get(field):
                raise ValueError(f'checkpoint {field} differs: {x.get("label")}')


def verified_memory(root, stage, checkpoint):
    prefix = snapshot_prefix(root, stage, checkpoint['label'])
    captured = digest_snapshot(prefix, checkpoint)
    for field in ('sha256', 'stack', 'excluded'):
        if captured[field] != checkpoint.get(field):
            raise ValueError(f'{prefix}: native snapshot no longer matches golden manifest')
    return prefix.with_suffix('.bin').read_bytes()


def trace_field(left_manifest, right_manifest, left_root, right_root, stage,
                offset, length):
    if left_manifest['campaign_sha256'] != right_manifest['campaign_sha256']:
        raise ValueError('campaign manifest hashes differ')
    candidates = []
    for manifest in (left_manifest, right_manifest):
        matches = [entry for entry in manifest['stages'] if entry['name'] == stage]
        if len(matches) != 1:
            raise ValueError(f'stage {stage!r} is not unique in campaign')
        candidates.append(matches[0])
    a, b = candidates
    check_matching_checkpoints(a, b)
    def load_save(root, recorded):
        data = verified_recorded_save(root, recorded).read_bytes()
        if hashlib.sha256(data).hexdigest() != recorded.get('save_sha256'):
            raise ValueError(f'{root}: original save differs from golden manifest')
        return parse_gateway_save(data)

    left_save = load_save(left_root, a)
    right_save = load_save(right_root, b)
    value_a = saved_ascii_field(left_save, offset, length)
    value_b = saved_ascii_field(right_save, offset, length)
    if value_a == value_b:
        raise ValueError('requested ASCII field is identical in both saved games')
    print(f'stage={stage} original-save global=+0x{offset:03x} length={length}')
    print(f'  run-a={value_a.decode("ascii")!r} run-b={value_b.decode("ascii")!r}')
    print('  Locations are guest *linear* addresses; copies may not be authoritative.')
    for left, right in zip(a['checkpoints'], b['checkpoints']):
        data_a = verified_memory(left_root, stage, left)
        data_b = verified_memory(right_root, stage, right)
        hits_a = occurrences(data_a, value_a)
        hits_b = occurrences(data_b, value_b)
        both = sorted(set(hits_a).intersection(hits_b))
        def preview(hits):
            return ','.join(f'0x{address:05x}' for address in hits[:12]) or '-'
        print(f'  {left["label"]} (command {left["command_index"]}, '
              f'{left["time_ms"]}ms):')
        print(f'    a[{len(hits_a)}]: {preview(hits_a)}')
        print(f'    b[{len(hits_b)}]: {preview(hits_b)}')
        print(f'    same-address[{len(both)}]: {preview(both)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left_manifest', type=Path)
    parser.add_argument('right_manifest', type=Path)
    parser.add_argument('--left-campaign', type=Path, required=True)
    parser.add_argument('--right-campaign', type=Path, required=True)
    parser.add_argument('--stage', required=True)
    parser.add_argument('--global-offset', type=lambda raw: int(raw, 0), required=True)
    parser.add_argument('--length', type=int, required=True)
    args = parser.parse_args()
    try:
        trace_field(read_manifest(args.left_manifest), read_manifest(args.right_manifest),
                    args.left_campaign, args.right_campaign, args.stage,
                    args.global_offset, args.length)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

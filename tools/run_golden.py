#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Hash deterministic native walkthrough checkpoints and find the first divergence.

No guest RAM, original game assets, or save files are stored in golden manifests.
Run native guest replays under a private Xvfb display on a machine with Unicorn.
"""

import argparse
from collections import deque
import hashlib
from itertools import zip_longest
import json
from pathlib import Path
import re
import shutil
import string
import struct
import subprocess
import sys

from run_walkthrough import compile_steps, parse_script
from run_walkthrough_campaign import load_campaign, metadata_path, verified_save


MEMORY_SIZE = 1024 * 1024
CONVENTIONAL_END = 0xa0000
# BIOS Data Area tick counter. This is host-paced state, not Gateway story state.
VOLATILE_RANGES = ((0x046c, 0x0470, 'BIOS timer ticks'),)
SHA256 = re.compile(r'[0-9a-f]{64}\Z')
RTLINK_SEQUENCE = re.compile(
    r'rtlink-seq id=([0-9]+) ms=([0-9]+) section=([0-9]+) '
    r'target=([0-9a-f]+):([0-9a-f]+) caller=([0-9a-f]+):([0-9a-f]+)\Z')


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


def selected_stages(manifest, from_stage=None, through_stage=None):
    stages = load_campaign(manifest)
    names = [stage.name for stage in stages]
    if from_stage and from_stage not in names:
        raise ValueError(f'unknown --from-stage: {from_stage}')
    if through_stage and through_stage not in names:
        raise ValueError(f'unknown --through-stage: {through_stage}')
    first = names.index(from_stage) if from_stage else 0
    last = names.index(through_stage) + 1 if through_stage else len(stages)
    if first >= last:
        raise ValueError('--from-stage must not follow --through-stage')
    return stages[first:last]


def capture_campaign(manifest, campaign_dir, every, through_stage=None, from_stage=None):
    if not 1 <= every <= 10000:
        raise ValueError('--every must be between 1 and 10000')
    stages = []
    for stage in selected_stages(manifest, from_stage, through_stage):
        checkpoints = []
        for checkpoint in planned_checkpoints(stage, every):
            prefix = snapshot_prefix(campaign_dir, stage.name, checkpoint['label'])
            checkpoints.append(digest_snapshot(prefix, checkpoint))
        result = dict(name=stage.name, script_sha256=stage.script_sha256,
                      checkpoints=checkpoints)
        if metadata_path(campaign_dir, stage).exists():
            # A complete native campaign has a verified original save for
            # each stage. Synthetic snapshot fixtures may omit save metadata.
            verified_save(campaign_dir, stage)
            metadata = json.loads(metadata_path(campaign_dir, stage).read_text(encoding='utf-8'))
            result['save_sha256'] = metadata['save_sha256']
        stages.append(result)
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
        if a.get('save_sha256') != b.get('save_sha256'):
            return (name, 'save', 'native saved-game SHA-256 differs at stage end')
        digest = a.get('save_sha256')
        if digest is not None and (not isinstance(digest, str) or not SHA256.fullmatch(digest)):
            return (name, 'save', 'missing or malformed native saved-game SHA-256')
    return None


def align_common_checkpoints(left, right):
    """Compare shared stage/label snapshots despite different capture intervals.

    Each original snapshot must retain its stage script hash, action ordinal,
    command ordinal and guest timestamp. Only the checkpoint *selection* may
    differ. This is diagnostic, not a replacement for full-campaign equality.
    """
    for key in ('schema_version', 'campaign_sha256', 'memory_end'):
        if left.get(key) != right.get(key):
            raise ValueError(f'cannot align campaigns: {key} differs')
    right_by_stage = {item['name']: item for item in right['stages']}
    aligned_left = []
    aligned_right = []
    for a in left['stages']:
        b = right_by_stage.get(a['name'])
        if b is None:
            continue
        if a.get('script_sha256') != b.get('script_sha256'):
            raise ValueError(f'cannot align stage {a["name"]}: script digest differs')
        right_labels = {point['label']: point for point in b['checkpoints']}
        common_a = [point for point in a['checkpoints'] if point['label'] in right_labels]
        if not common_a:
            continue
        common_b = [right_labels[point['label']] for point in common_a]
        aligned_left.append({**a, 'checkpoints': common_a})
        aligned_right.append({**b, 'checkpoints': common_b})
    if not aligned_left:
        raise ValueError('campaigns have no common stage checkpoints')
    # The interval controls *which* labels were captured, not the underlying
    # guest state. Normalize it only after selecting identical labels.
    return ({**left, 'stages': aligned_left},
            {**right, 'every': left['every'], 'stages': aligned_right})


def describe_cpu_difference(left, right):
    names = ('CS', 'IP', 'SS', 'SP', 'BP', 'DS', 'ES', 'FLAGS')
    differences = [f'{name}={left[name]:04x}/{right[name]:04x}'
                   for name in names if name in left and name in right
                   and left[name] != right[name]]
    return 'CPU a/b ' + ', '.join(differences) if differences else 'CPU registers match'


def object_table_segment_candidates(memory):
    """Find possible Gateway DS values using the original 734-count indirection.

    The resident code reads count from [word ptr DS:8d54]:38dc. This is
    necessary, not sufficient, evidence for a Gateway data segment; callers
    must verify every record and reject multiple surviving candidates.
    """
    end = min(len(memory), CONVENTIONAL_END)
    for segment in range((end - 0x8d56) // 16 + 1):
        pointer_segment = struct.unpack_from('<H', memory, segment * 16 + 0x8d54)[0]
        count_address = pointer_segment * 16 + 0x38dc
        if count_address + 2 <= end and \
                struct.unpack_from('<H', memory, count_address)[0] == 734:
            yield segment


def extract_native_objects(memory, registers):
    """Require an unambiguous, complete native object table at DS/ES or in RAM.

    DS/ES can temporarily refer to an interrupt or runtime segment at a
    timed checkpoint. Only when both fail, seek the original DS-indirection
    signature and insist on a unique, fully parsed 734-object table.
    """
    from inspect_object_state import inspect_objects

    def parsed(segment):
        try:
            found = inspect_objects(memory, segment)
        except (ValueError, IndexError):
            return None
        return found if len(found) == 734 else None

    found_tables = []
    for name in ('DS', 'ES'):
        if name not in registers:
            continue
        found = parsed(registers[name])
        if found is not None:
            found_tables.append((name, found))
    if not found_tables:
        for segment in object_table_segment_candidates(memory):
            found = parsed(segment)
            if found is not None:
                found_tables.append((f'RAM:{segment:04x}', found))
        if not found_tables:
            raise ValueError('object records unavailable (no verified 734-object table '
                             'at DS/ES or unique resident RAM address)')
        if len(found_tables) != 1:
            segments = ', '.join(name for name, _ in found_tables[:8])
            raise ValueError(f'ambiguous resident object tables: {segments}')
    primary = found_tables[0][1]
    for name, other in found_tables[1:]:
        if object_state_changes(primary, other):
            raise ValueError(f'ambiguous native object tables (DS/ES disagree at {name})')
    return primary


def object_state_changes(left, right):
    """Compare original record kinds and all record bytes, not host pointers."""
    if len(left) != 734 or len(right) != 734:
        raise ValueError('expected 734 native object records on both sides')
    return [a['object'] for a, b in zip(left, right)
            if a['object'] != b['object'] or a['type'] != b['type']
            or a['raw_state'] != b['raw_state']]


def verified_checkpoint_objects(root, stage, checkpoint):
    """Reject snapshots modified after the original golden hash capture."""
    prefix = snapshot_prefix(root, stage, checkpoint['label'])
    recorded = digest_snapshot(prefix, checkpoint)
    for key in ('sha256', 'stack', 'excluded'):
        if recorded[key] != checkpoint.get(key):
            raise ValueError(f'{prefix}: stale native snapshot ({key} differs from manifest)')
    try:
        return extract_native_objects(prefix.with_suffix('.bin').read_bytes(),
                                      read_registers(prefix.with_suffix('.regs')))
    except ValueError as exc:
        raise ValueError(f'{prefix}: {exc}') from exc


def verified_recorded_save(root, stage):
    """Validate original stage save bytes, not just the recorded metadata."""
    from types import SimpleNamespace

    metadata = root / stage['name'] / 'stage-meta.json'
    meta = json.loads(metadata.read_text(encoding='utf-8'))
    if not isinstance(meta, dict) or not isinstance(meta.get('save_description'), str):
        raise ValueError(f'{metadata}: missing native save description')
    item = SimpleNamespace(name=stage['name'], script_sha256=stage['script_sha256'],
                           description=meta['save_description'])
    verified_save(root, item)
    if meta['save_sha256'] != stage.get('save_sha256'):
        raise ValueError(f'{metadata}: native save hash differs from golden manifest')


def describe_object_difference(left_memory, left_registers, right_memory, right_registers):
    """Check complete native object records, independent of volatile RTLink buffers."""
    try:
        left = extract_native_objects(left_memory, left_registers)
        right = extract_native_objects(right_memory, right_registers)
    except ValueError:
        return 'object records unavailable (no unambiguous verified native table)'
    changed = object_state_changes(left, right)
    if not changed:
        return 'object records identical (734/734)'
    sample = ','.join(f'{number:x}' for number in changed[:12])
    suffix = ',...' if len(changed) > 12 else ''
    return f'object records changed {len(changed)}/734 (IDs {sample}{suffix})'


def compare_object_checkpoints(left, right, left_dir, right_dir, all_checkpoints=False):
    """Diagnostic object-state oracle; deliberately independent of RAM hashes.

    Every checkpoint must have readable guest RAM and exactly 734 validated
    native records. This is weaker than the strict full-RAM oracle and must
    never be used to approve a native/HLE replacement as fully equivalent.
    """
    if left_dir is None or right_dir is None:
        raise ValueError('--objects-only requires --left-campaign and --right-campaign')
    left = with_stage_save_hashes(left, left_dir)
    right = with_stage_save_hashes(right, right_dir)
    for key in ('schema_version', 'campaign_sha256', 'every', 'memory_end'):
        if left.get(key) != right.get(key):
            raise ValueError(f'cannot compare object state: {key} differs')
    if len(left['stages']) != len(right['stages']):
        raise ValueError('cannot compare object state: stage count differs')

    first = None
    count = 0
    for a, b in zip(left['stages'], right['stages']):
        stage = a['name']
        if stage != b.get('name') or a.get('script_sha256') != b.get('script_sha256'):
            raise ValueError(f'cannot compare object state: stage {stage} name/script differs')
        ac, bc = a.get('checkpoints'), b.get('checkpoints')
        if not isinstance(ac, list) or not isinstance(bc, list) or len(ac) != len(bc):
            raise ValueError(f'cannot compare object state: {stage} checkpoint count differs')
        stage_diffs = []
        for x, y in zip(ac, bc):
            label = x['label']
            for key in ('label', 'time_ms', 'action_index', 'command_index'):
                if x.get(key) != y.get(key):
                    raise ValueError(f'cannot compare object state: {stage}/{label} {key} differs')
            changes = object_state_changes(
                verified_checkpoint_objects(left_dir, stage, x),
                verified_checkpoint_objects(right_dir, stage, y))
            count += 1
            if changes:
                sample = ','.join(f'{number:x}' for number in changes[:12])
                suffix = ',...' if len(changes) > 12 else ''
                reason = (f'{len(changes)}/734 object records differ (IDs {sample}{suffix})'
                          f' at command {x["command_index"]}, ms={x["time_ms"]}')
                stage_diffs.append((label, reason))
                if first is None:
                    first = f'stage={stage} checkpoint={label}: {reason}'
        hashes = (a.get('save_sha256'), b.get('save_sha256'))
        if any(not isinstance(digest, str) or not SHA256.fullmatch(digest)
               for digest in hashes):
            raise ValueError(f'{stage}: missing or malformed native saved-game SHA-256')
        verified_recorded_save(left_dir, a)
        verified_recorded_save(right_dir, b)
        if hashes[0] != hashes[1]:
            stage_diffs.append(('save', 'native saved-game SHA-256 differs'))
            if first is None:
                first = f'stage={stage} checkpoint=save: native saved-game SHA-256 differs'
        if stage_diffs:
            print(f'{stage}: {len(stage_diffs)} object/save differences '
                  f'across {len(ac)} checkpoints')
            if all_checkpoints:
                for label, reason in stage_diffs:
                    print(f'  {label}: {reason}')
        else:
            print(f'{stage}: {len(ac)}/{len(ac)} object checkpoints and native save match')
    if first:
        raise ValueError(f'first object-state divergence: {first}')
    print(f'Object-state replay matched: {len(left["stages"])} stages, '
          f'{count} checkpoints (734 raw object records each), native stage saves')


def describe_stage_save(left_dir, right_dir, stage):
    """Stage-end native save hashes distinguish transient from persistent drift."""
    try:
        a = json.loads((left_dir / stage / 'stage-meta.json').read_text(encoding='utf-8'))
        b = json.loads((right_dir / stage / 'stage-meta.json').read_text(encoding='utf-8'))
        x, y = a['save_sha256'], b['save_sha256']
        if not SHA256.fullmatch(x) or not SHA256.fullmatch(y):
            return ''
        return ('stage-end saves identical' if x == y else 'stage-end saves differ')
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
        return ''


def rtlink_sequence_record(line, index):
    match = RTLINK_SEQUENCE.fullmatch(line.rstrip('\n'))
    if match is None or int(match[1]) != index:
        raise ValueError(f'malformed RTLink call sequence at event {index}')
    ms = int(match[2])
    section = int(match[3])
    target = (int(match[4], 16), int(match[5], 16))
    caller = (int(match[6], 16), int(match[7], 16))
    if section >= 58 or any(number > 0xffff for number in target + caller):
        raise ValueError(f'invalid RTLink code identity at event {index}')
    return ms, (section, *target, *caller)


def format_rtlink_record(record):
    if record is None:
        return 'end-of-trace'
    ms, (section, cs, ip, caller_cs, caller_ip) = record
    return (f'ms={ms} section={section} target={cs:x}:{ip:x} '
            f'caller={caller_cs:x}:{caller_ip:x}')


def describe_rtlink_sequence(left_dir, right_dir, stage):
    """Locate the first differing call identity (or timestamp), without guest bytes.

    This is a diagnostic only: target/caller timelines may vary even when
    the strict RAM and original-save oracles match. Do not normalize them away.
    """
    if not left_dir or not right_dir:
        return ''
    paths = [(root / stage / 'native.rtlink-sequence')
             for root in (left_dir, right_dir)]
    if not any(path.exists() for path in paths):
        return ''
    if not all(path.is_file() for path in paths):
        return 'RTLink call sequence unavailable (recorded on only one side)'
    first_time_drift = None
    previous = deque(maxlen=2)
    count = 0
    with paths[0].open(encoding='ascii') as a, paths[1].open(encoding='ascii') as b:
        for count, (line_a, line_b) in enumerate(zip_longest(a, b), 1):
            left = rtlink_sequence_record(line_a, count) if line_a is not None else None
            right = rtlink_sequence_record(line_b, count) if line_b is not None else None
            if left is None or right is None or left[1] != right[1]:
                context = (f'; preceding calls {", ".join(map(str, previous))}'
                           if previous else '')
                time_context = (f'; first time drift at call {first_time_drift[0]} '
                                f'ms={first_time_drift[1]}/{first_time_drift[2]}'
                                if first_time_drift else '')
                return (f'RTLink first code-call difference at #{count}: '
                        f'a {format_rtlink_record(left)}; b {format_rtlink_record(right)}'
                        f'{context}{time_context}')
            if first_time_drift is None and left[0] != right[0]:
                first_time_drift = (count, left[0], right[0])
            previous.append(f'#{count} {format_rtlink_record(left)}')
    if first_time_drift:
        index, a_ms, b_ms = first_time_drift
        return (f'RTLink code-call order identical ({count} calls); '
                f'first guest-ms difference at #{index}: {a_ms}/{b_ms}')
    return f'RTLink call sequences identical ({count} calls)'


def with_stage_save_hashes(record, campaign_dir):
    """Enrich old hash-only manifests from their existing local run metadata."""
    if not campaign_dir:
        return record
    stages = []
    for stage in record['stages']:
        result = dict(stage)
        if 'save_sha256' not in result:
            path = campaign_dir / stage['name'] / 'stage-meta.json'
            if path.exists():
                metadata = json.loads(path.read_text(encoding='utf-8'))
                digest = metadata.get('save_sha256')
                if not isinstance(digest, str) or not SHA256.fullmatch(digest):
                    raise ValueError(f'malformed native saved-game digest: {path}')
                result['save_sha256'] = digest
        stages.append(result)
    return {**record, 'stages': stages}


def explain_difference(a, b, left_dir, right_dir, stage, label):
    """Optional local diagnostic: print differences, never guest-owned bytes.

    Timed snapshots can catch *different* stack depths. Use the UNION of
    the two stack exclusions when counting changed retained bytes, otherwise
    an extra live stack word appears to be an unrelated RAM mutation.
    """
    if not left_dir or not right_dir or label == '-':
        return ''
    try:
        left_prefix = snapshot_prefix(left_dir, stage, label)
        right_prefix = snapshot_prefix(right_dir, stage, label)
        lregs = read_registers(left_prefix.with_suffix('.regs'))
        rregs = read_registers(right_prefix.with_suffix('.regs'))
        lraw = left_prefix.with_suffix('.bin').read_bytes()
        rraw = right_prefix.with_suffix('.bin').read_bytes()
        lmem, lranges = normalized_memory(left_prefix.with_suffix('.bin'), lregs)
        rmem, rranges = normalized_memory(right_prefix.with_suffix('.bin'), rregs)
        # This is for diagnosis only. The golden manifests still require the
        # individual exclusion ranges and exact SS:SP values to match.
        for region in lranges + rranges:
            start, end = region['start'], region['end']
            lmem[start:end] = bytes(end - start)
            rmem[start:end] = bytes(end - start)
        counts = {}
        first = None
        for index, (lvalue, rvalue) in enumerate(zip(lmem, rmem)):
            if lvalue != rvalue:
                if first is None:
                    first = index
                page = index // 4096
                counts[page] = counts.get(page, 0) + 1
        details = [describe_cpu_difference(lregs, rregs)]
        if first is None:
            details.append('retained RAM identical after shared volatile exclusions')
        else:
            hot_pages = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[:5]
            distribution = ', '.join(f'0x{page * 4096:05x}:{count}'
                                     for page, count in hot_pages)
            count = sum(counts.values())
            details.append(f'first unmasked byte at 0x{first:05x}; '
                           f'{count} changed {"byte" if count == 1 else "bytes"} in '
                           f'{len(counts)} {"page" if len(counts) == 1 else "pages"}; '
                           f'top 4-KiB pages (start:changed bytes) {distribution}')
        details.append(describe_object_difference(lraw, lregs, rraw, rregs))
        stage_save = describe_stage_save(left_dir, right_dir, stage)
        if stage_save:
            details.append(stage_save)
        rtlink_detail = describe_rtlink_sequence(left_dir, right_dir, stage)
        if rtlink_detail:
            details.append(rtlink_detail)
        return '; ' + '; '.join(details)
    except (OSError, ValueError):
        return ''


def check_pair(left, right, left_dir=None, right_dir=None):
    left = with_stage_save_hashes(left, left_dir)
    right = with_stage_save_hashes(right, right_dir)
    divergence = first_divergence(left, right)
    if divergence is not None:
        stage, label, reason = divergence
        detail = explain_difference(left, right, left_dir, right_dir, stage, label)
        raise ValueError(f'first divergence: stage={stage} checkpoint={label}: {reason}{detail}')
    count = sum(len(stage['checkpoints']) for stage in left['stages'])
    print(f'Golden replay matched: {len(left["stages"])} stages, {count} RAM checkpoints')
    if left_dir and right_dir:
        for stage in left['stages']:
            detail = describe_rtlink_sequence(left_dir, right_dir, stage['name'])
            if detail:
                print(f'  {stage["name"]}: {detail}')


def report_checkpoint_drift(left, right, left_dir=None, right_dir=None):
    """Show whether a mismatch is brief or persists through later checkpoints."""
    for a, b in zip(left.get('stages', ()), right.get('stages', ())):
        if a.get('name') != b.get('name'):
            break
        ac, bc = a.get('checkpoints', ()), b.get('checkpoints', ())
        mismatches = []
        for x, y in zip(ac, bc):
            if x.get('label') != y.get('label'):
                break
            types = []
            if x.get('stack') != y.get('stack') or x.get('excluded') != y.get('excluded'):
                types.append('stack/mask')
            if x.get('sha256') != y.get('sha256'):
                types.append('RAM')
            if types:
                mismatches.append((x, types))
        stage_save = (describe_stage_save(left_dir, right_dir, a['name'])
                      if left_dir and right_dir else '')
        save_mismatch = (a.get('save_sha256') != b.get('save_sha256') or
                         stage_save == 'stage-end saves differ')
        if mismatches or save_mismatch:
            print(f'{a["name"]}: {len(mismatches)}/{len(ac)} checkpoints differ')
            if stage_save:
                print(f'  {stage_save}')
            for x, types in mismatches:
                print(f'  {x["label"]}: command={x["command_index"]} '
                      f'ms={x["time_ms"]} ({", ".join(types)})')


def seed_previous_stage(manifest, from_stage, source, target):
    """Copy one verified native save to a private run, never reuse mutable state.

    This allows repeated investigation of a late stage without replaying
    earlier stages. The previous save is checked against its original SHA-256
    and stage/script identity before being used as a seed.
    """
    stages = load_campaign(manifest)
    names = [stage.name for stage in stages]
    if from_stage not in names or names.index(from_stage) == 0:
        raise ValueError('--seed-from requires a noninitial --from-stage')
    previous = stages[names.index(from_stage) - 1]
    saved = verified_save(source, previous)
    metadata = metadata_path(source, previous)
    output = target / previous.name
    if output.exists():
        raise ValueError(f'seed destination already exists: {output}')
    state = output / 'state'
    state.mkdir(parents=True)
    shutil.copyfile(saved, state / saved.name)
    shutil.copyfile(metadata, metadata_path(target, previous))
    verified_save(target, previous)  # Assert copy integrity before launching DOS.
    print(f'Seeded independent run from verified {previous.name} native save', flush=True)


def run_campaign(manifest, probe, data, output, every, timeout, verify_overlays,
                 probe_args, through_stage=None, from_stage=None,
                 trace_rtlink_sequence=False):
    command = [sys.executable, str(Path(__file__).with_name('run_walkthrough_campaign.py')),
               str(manifest), '--probe', str(probe), '--data', str(data),
               '--output', str(output), '--golden-every', str(every),
               '--timeout', str(timeout)]
    if verify_overlays:
        command.append('--verify-overlays')
    if trace_rtlink_sequence:
        command.append('--trace-rtlink-sequence')
    if through_stage:
        command += ['--through-stage', through_stage]
    if from_stage:
        command += ['--from-stage', from_stage]
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
    capture.add_argument('--through-stage', metavar='NAME')
    capture.add_argument('--from-stage', metavar='NAME')
    capture.add_argument('--output', type=Path, required=True, help='write hashes (no guest bytes)')
    compare = commands.add_parser('compare', help='compare saved golden hash manifests')
    compare.add_argument('left', type=Path)
    compare.add_argument('right', type=Path)
    compare.add_argument('--left-campaign', type=Path)
    compare.add_argument('--right-campaign', type=Path)
    compare.add_argument('--all-checkpoints', action='store_true',
                         help='list all differing checkpoints, including later recovery')
    compare.add_argument('--common-checkpoints', action='store_true',
                         help='compare shared stage/label snapshots despite different --every')
    compare.add_argument('--objects-only', action='store_true',
                         help='compare all 734 native object records and stage saves at each '
                         'checkpoint, ignoring CPU/RAM (diagnostic only)')
    run = commands.add_parser('run', help='run independent campaigns and compare RAM checkpoints')
    run.add_argument('manifest', type=Path)
    run.add_argument('--probe', type=Path, default=Path('/tmp/gateway-port-build/gateway-probe'))
    run.add_argument('--data', type=Path, default=Path('../gateway-dos'))
    run.add_argument('--output', type=Path, default=Path('build/golden'))
    run.add_argument('--every', type=int, default=10)
    run.add_argument('--runs', type=int, default=2, metavar='N',
                     help='number of independent replays, 2–8 (default: 2)')
    run.add_argument('--through-stage', metavar='NAME',
                     help='run only through this stage while diagnosing early mismatches')
    run.add_argument('--from-stage', metavar='NAME',
                     help='start at this stage using a verified earlier native save')
    run.add_argument('--seed-from', type=Path, metavar='CAMPAIGN',
                     help='prior native campaign containing the preceding stage save')
    run.add_argument('--timeout', type=int, default=1800)
    run.add_argument('--verify-overlays', action='store_true')
    run.add_argument('--trace-rtlink-sequence', action='store_true',
                     help='trace each RTLink code-call identity and report first discrepancy')
    run.add_argument('--probe-arg', action='append', default=[], metavar='ARG',
                     help='forward backend flag; requires support in the selected probe')
    run.add_argument('--baseline', type=Path, help='compare each run against known good hashes')
    run.add_argument('--write-baseline', type=Path,
                     help='write validated hashes only after both runs agree')
    args = parser.parse_args()
    try:
        if args.mode == 'capture':
            save_manifest(args.output, capture_campaign(args.manifest, args.campaign,
                                                        args.every, args.through_stage,
                                                        args.from_stage))
            print(f'Golden hashes saved: {args.output}')
        elif args.mode == 'compare':
            left, right = read_manifest(args.left), read_manifest(args.right)
            if args.common_checkpoints:
                left, right = align_common_checkpoints(left, right)
                print(f'Comparing {sum(len(s["checkpoints"]) for s in left["stages"])} '
                      f'common checkpoints across {len(left["stages"])} shared stages')
            if args.objects_only:
                compare_object_checkpoints(left, right, args.left_campaign,
                                           args.right_campaign, args.all_checkpoints)
            else:
                if args.all_checkpoints:
                    report_checkpoint_drift(left, right, args.left_campaign,
                                            args.right_campaign)
                check_pair(left, right, args.left_campaign, args.right_campaign)
        else:
            if args.timeout <= 0:
                raise ValueError('--timeout must be positive')
            if not 2 <= args.runs <= 8:
                raise ValueError('--runs must be between 2 and 8')
            selected = selected_stages(args.manifest, args.from_stage, args.through_stage)
            begins_at_first = selected[0].name == load_campaign(args.manifest)[0].name
            if begins_at_first and args.seed_from:
                raise ValueError('--seed-from requires --from-stage after the first stage')
            if not begins_at_first and not args.seed_from:
                raise ValueError('--from-stage requires --seed-from with a verified campaign')
            if args.seed_from:
                previous_stages = load_campaign(args.manifest)
                previous = previous_stages[[x.name for x in previous_stages].index(
                    args.from_stage) - 1]
                verified_save(args.seed_from, previous)
            baseline = read_manifest(args.baseline) if args.baseline else None
            # Never reuse a stale game state directory; two independent runs
            # must start from scratch or their saved-game handoffs are invalid.
            for suffix in string.ascii_lowercase[:args.runs]:
                root = args.output / f'run-{suffix}'
                if root.exists():
                    raise ValueError(f'{root} already exists; choose a fresh --output')
            records = []
            for suffix in string.ascii_lowercase[:args.runs]:
                root = args.output / f'run-{suffix}'
                print(f'Starting independent native campaign {suffix}', flush=True)
                if args.seed_from:
                    seed_previous_stage(args.manifest, args.from_stage, args.seed_from, root)
                run_campaign(args.manifest, args.probe, args.data, root, args.every,
                             args.timeout, args.verify_overlays, args.probe_arg,
                             args.through_stage, args.from_stage,
                             args.trace_rtlink_sequence)
                hashed = capture_campaign(args.manifest, root, args.every,
                                          args.through_stage, args.from_stage)
                save_manifest(args.output / f'run-{suffix}.json', hashed)
                if baseline is not None:
                    check_pair(baseline, hashed)
                if records:
                    check_pair(records[0], hashed,
                               args.output / 'run-a', root)
                records.append(hashed)
            print(f'Validated {len(records)} independent campaign runs', flush=True)
            if args.write_baseline:
                save_manifest(args.write_baseline, records[0])
                print(f'Validated baseline written to {args.write_baseline}')
    except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError,
            subprocess.CalledProcessError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()

# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#@category Gateway
# Batch-decompile the intro, including newly discovered direct callees.
# Ghidra pseudocode, raw addresses and unknown callsites stay under build/.

import csv
import json
import os
from ghidra.app.decompiler import DecompInterface


args = getScriptArgs()
if len(args) != 1:
    raise ValueError('usage: DecompileGatewayIntro.py /absolute/intro-ghidra-plan.json')
plan = json.load(open(args[0], 'r'))
report_dir = os.path.dirname(args[0])
import_report = json.load(open(os.path.join(report_dir,
                                        'intro-ghidra-import-report.json'), 'r'))
memory = currentProgram.getMemory()
listing = currentProgram.getListing()
manager = currentProgram.getFunctionManager()
blocks = {}
for bank in plan['sections']:
    block = memory.getBlock(bank['name'])
    if block is None or not block.isOverlay():
        raise ValueError('missing linked RTL overlay: %s' % bank['name'])
    blocks[bank['segment']] = (bank, block)

resident = import_report['resident_matches']
exe_anchors = [entry for entry in plan['exe_labels']
               if resident.get(entry['name']) is not None]
known_addresses = {}
known_entries = []
for entry in exe_anchors:
    address = toAddr(resident[entry['name']])
    known_addresses[str(address)] = entry['name']
    known_entries.append((address, entry['name']))
for bank, block in blocks.values():
    for entry in bank['entries']:
        address = block.getStart().add(entry['offset'])
        known_addresses[str(address)] = entry['name']
        known_entries.append((address, entry['name']))


def source_file_at(bank, runtime_offset):
    """Resolve an original RTL identity only inside a verified file span."""
    for span in bank['source_spans']:
        offset = runtime_offset - span['offset']
        if 0 <= offset < span['size']:
            return span['file_base'] + offset
    return None


def identity(address):
    block = memory.getBlock(address)
    if block is None:
        return None
    for bank, mapped in blocks.values():
        if mapped.getName() == block.getName():
            offset = address.subtract(mapped.getStart())
            if 0 <= offset < bank['size']:
                original = source_file_at(bank, offset)
                if original is not None:
                    return dict(file='rtl', offset=original,
                                linked_segment=bank['segment'])
                return None
    if block.isOverlay() or not exe_anchors:
        return None
    # The imported MZ resident functions are in the same image as these
    # uniquely matched, unrelocated anchors.  Never guess a far alias.
    for entry in exe_anchors:
        anchor = toAddr(resident[entry['name']])
        if memory.getBlock(anchor) != block:
            continue
        candidate = entry['file_offset'] + address.subtract(anchor)
        if 0 <= candidate < os.path.getsize(plan['exe_file']):
            return dict(file='exe', offset=candidate)
    return None


def linked_callee(bytes_):
    if len(bytes_) >= 5 and bytes_[0] == 0x9a:
        offset = bytes_[1] | bytes_[2] << 8
        original_segment = bytes_[3] | bytes_[4] << 8
        physical = ((original_segment << 4) + offset) & 0xfffff
        matches = []
        for bank, block in blocks.values():
            # Guest code may use a paragraph alias in the middle of a bank:
            # 2621:030e is physically 25d7:07ae, for example.  Compare
            # physical addresses, not segment numbers alone.
            start = (plan['load_segment'] + bank['segment']) * 16
            local = physical - start
            if 0 <= local < bank['size']:
                file_offset = source_file_at(bank, local)
                if file_offset is not None:
                    matches.append((bank, block, local, file_offset))
        if len(matches) == 1:
            bank, block, local, source = matches[0]
            return dict(kind='far', segment=bank['segment'], offset=local,
                        target=block.getStart().add(local),
                        source_file_offset=source)
        return dict(kind='external-far', segment=original_segment,
                    offset=offset, target=None)
    return None


def near_callee(address, bytes_):
    """Decode a plain 8086 E8 call within the verified linked RTL bank.

    Ghidra's normal references can instead point to the non-overlay DOS
    address space.  The call's signed displacement is relative to the next
    instruction *inside the current overlay*, and is independent of Ghidra's
    current segment-register assumption.
    """
    if len(bytes_) != 3 or bytes_[0] != 0xe8:
        return None
    source = identity(address)
    if source is None or source['file'] != 'rtl':
        return None
    segment = source['linked_segment']
    bank, block = blocks[segment]
    displacement = bytes_[1] | bytes_[2] << 8
    if displacement & 0x8000:
        displacement -= 0x10000
    local = address.subtract(block.getStart()) + 3 + displacement
    file_offset = source_file_at(bank, local)
    if file_offset is not None:
        return dict(kind='near', segment=segment, offset=local,
                    target=block.getStart().add(local),
                    source_file_offset=file_offset)
    return dict(kind='out-of-bank-near', segment=segment,
                offset=local & 0xffff, target=None)


def calls(function):
    calls = []
    instructions = listing.getInstructions(function.getBody(), True)
    while instructions.hasNext():
        instruction = instructions.next()
        if not instruction.getFlowType().isCall():
            continue
        address = instruction.getAddress()
        raw = [(int(value) & 255) for value in instruction.getBytes()]
        target = linked_callee(raw)
        if target is None:
            target = near_callee(address, raw)
        if target is None:
            references = [ref for ref in instruction.getReferencesFrom()
                          if ref.getReferenceType().isCall()]
            if len(references) == 1:
                address_to = references[0].getToAddress()
                target = dict(kind='ghidra-reference', target=address_to)
            else:
                target = dict(kind='indirect-or-unresolved', target=None)
        destination = target.pop('target')
        data = dict(callsite=str(address), source=identity(address),
                    kind=target['kind'], destination=str(destination)
                    if destination is not None else None,
                    target_identity=identity(destination) if destination is not None
                    else None)
        if 'segment' in target:
            data['segment'] = target['segment']
            data['offset'] = target['offset']
        if destination is None:
            # Calls that resolve outside these verified images are not
            # automatically new functions.  Preserve their original byte
            # encoding for prioritizing the next source-bank mapping.
            data['opcode'] = ''.join('%02x' % value for value in raw)
        if destination is not None:
            data['function'] = (str(getFunctionAt(destination).getEntryPoint())
                                if getFunctionAt(destination) is not None else None)
        calls.append((data, destination))
    return calls


def ensure_function(address, name=None):
    function = getFunctionAt(address)
    if function is None:
        if getFunctionContaining(address) is not None:
            return None
        try:
            disassemble(address)
            function = createFunction(address, name)
        except Exception as error:
            printerr('intro: cannot create function at %s: %s' % (address, error))
    return function


# Seed the entry points justified by the previous source-C recovery.  Do not
# assert that Ghidra's additional functions are true original ABI boundaries.
for address, name in known_entries:
    ensure_function(address, name)

# Follow direct CALLs *only* when the target is within the independently
# verified memory blocks; external services remain explicit report entries.
# Repeated runs are idempotent because we use a fresh imported project.
processed = set()
pending = []
iterator = manager.getFunctions(True)
while iterator.hasNext():
    function = iterator.next()
    if identity(function.getEntryPoint()) is not None:
        pending.append(function)
while pending and len(processed) < 2500:
    function = pending.pop()
    address = str(function.getEntryPoint())
    if address in processed:
        continue
    processed.add(address)
    for edge, destination in calls(function):
        if destination is not None and edge['target_identity'] is not None:
            candidate = ensure_function(destination)
            if candidate is not None and str(candidate.getEntryPoint()) not in processed:
                pending.append(candidate)

functions = []
iterator = manager.getFunctions(True)
while iterator.hasNext():
    function = iterator.next()
    if identity(function.getEntryPoint()) is not None:
        functions.append(function)
functions.sort(key=lambda item: (identity(item.getEntryPoint())['file'],
                                 identity(item.getEntryPoint())['offset']))

interface = DecompInterface()
interface.setSimplificationStyle('decompile')
if not interface.openProgram(currentProgram):
    raise ValueError('Ghidra could not open the intro program for decompilation')
text = ['Gateway intro Ghidra pseudocode (GENERATED, UNVERIFIED)',
        'Function discovery is provisional. Far pointers and arguments need native traces.',
        'This is not compilable original source and must not be committed.', '']
entries = []
all_calls = []
for function in functions:
    address = function.getEntryPoint()
    source = identity(address)
    name = known_addresses.get(str(address), function.getName())
    entry = dict(name=name, address=str(address), identity=source,
                 seeded=str(address) in known_addresses, status='error')
    text.append('===== %s [%s:0x%x] at %s =====' %
                (name, source['file'], source['offset'], address))
    try:
        result = interface.decompileFunction(function, 45, monitor)
        if result.decompileCompleted() and result.getDecompiledFunction() is not None:
            text.append(result.getDecompiledFunction().getC())
            entry['status'] = 'ok'
        else:
            entry['error'] = str(result.getErrorMessage())
            text.append('Decompilation failed: %s' % entry['error'])
    except Exception as error:
        entry['error'] = str(error)
        text.append('Decompilation error: %s' % entry['error'])
    text.append('')
    edges = [item for item, _ in calls(function)]
    entry['call_count'] = len(edges)
    all_calls.append(dict(caller=source, address=str(address), calls=edges))
    entries.append(entry)
interface.dispose()

found = set(item['address'] for item in entries)
missing_seeded = [dict(name=name, address=address)
                  for address, name in sorted(known_addresses.items())
                  if address not in found]
for entry in plan['exe_labels']:
    if resident.get(entry['name']) is None:
        missing_seeded.append(dict(name=entry['name'],
                                   reason='MZ signature not uniquely resolved'))
unresolved = [dict(caller=record['caller'], **edge)
              for record in all_calls for edge in record['calls']
              if edge['target_identity'] is None]
hotspots = {}
for edge in unresolved:
    if 'segment' in edge:
        key = ('%04x:%04x' % (edge['segment'], edge['offset']), edge['kind'])
    else:
        key = (edge.get('destination') or edge.get('opcode', 'unknown')[:4],
               edge['kind'])
    if key not in hotspots:
        hotspots[key] = dict(target=key[0], kind=key[1], count=0, callers=[])
    hotspot = hotspots[key]
    hotspot['count'] += 1
    caller = edge['caller']
    if caller is not None and caller not in hotspot['callers']:
        hotspot['callers'].append(caller)
unresolved_targets = sorted(hotspots.values(),
                            key=lambda item: (-item['count'], item['target'], item['kind']))
report = dict(schema_version=1, requested=len(entries),
              completed=sum(item['status'] == 'ok' for item in entries),
              functions=entries, seeded=len(known_addresses),
              missing_seeded=missing_seeded,
              unmapped_rtl_entries=plan['unmapped_rtl_entries'],
              unresolved_calls=len(unresolved),
              unresolved_targets=unresolved_targets,
              note='Static analysis of one relocated intro snapshot, not native verification')

with open(os.path.join(report_dir, 'intro-pseudocode.txt'), 'w') as stream:
    stream.write('\n'.join(text) + '\n')
with open(os.path.join(report_dir, 'intro-ghidra-report.json'), 'w') as stream:
    json.dump(report, stream, indent=2)
with open(os.path.join(report_dir, 'intro-ghidra-callgraph.json'), 'w') as stream:
    json.dump(dict(schema_version=1, functions=all_calls,
                   unresolved=unresolved), stream, indent=2)
with open(os.path.join(report_dir, 'intro-ghidra-functions.tsv'), 'w') as stream:
    writer = csv.writer(stream, delimiter='\t')
    writer.writerow(['file', 'offset', 'name', 'status', 'seeded', 'calls'])
    for entry in entries:
        writer.writerow([entry['identity']['file'], '0x%x' % entry['identity']['offset'],
                         entry['name'], entry['status'], int(entry['seeded']),
                         entry['call_count']])
with open(os.path.join(report_dir, 'intro-ghidra-unresolved.tsv'), 'w') as stream:
    writer = csv.writer(stream, delimiter='\t')
    writer.writerow(['target', 'kind', 'call_sites', 'distinct_callers', 'caller_identities'])
    for target in unresolved_targets:
        callers = sorted('%s:0x%x' % (caller['file'], caller['offset'])
                         for caller in target['callers'])
        writer.writerow([target['target'], target['kind'], target['count'],
                         len(callers), ','.join(callers)])
println('Gateway intro: decompiled %d/%d functions; %d unresolved static calls; '
        '%d missing original seed entries' %
        (report['completed'], report['requested'], len(unresolved),
         len(missing_seeded)))

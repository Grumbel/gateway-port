# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#@category Gateway
# PyGhidra/Ghidra post-analysis export. Arguments: absolute plan, output JSON.
# Per-function static facts only; no executable bytes, decompiled C, or game text.
# Identifiers are ORIGINAL EXE/OVL file offsets, not transient segments.

import json
import re

args = getScriptArgs()
if len(args) != 2:
    raise ValueError('usage: ExportGatewayFacts.py plan.json facts.json')
plan = json.load(open(args[0], 'r'))
if plan.get('schema_version') != 1:
    raise ValueError('unsupported Ghidra plan')
sections = dict((section['id'], section) for section in plan['sections'])
memory = currentProgram.getMemory()
listing = currentProgram.getListing()
exe = bytearray(open(plan['exe_file'], 'rb').read())
ovl = bytearray(open(plan['ovl_file'], 'rb').read())
header_size = (exe[8] | (exe[9] << 8)) * 16
resident_blocks = [block for block in memory.getBlocks()
                   if block.isInitialized() and not block.isOverlay() and
                   not block.getName().startswith('GW_RT_')]
if not resident_blocks:
    raise ValueError('no resident image')
image_base = min(block.getStart().getOffset() for block in resident_blocks)
overlay_blocks = dict(('GW_RT_%03d' % section['id'], section)
                      for section in plan['sections'])


def origin(address):
    if address is None:
        return None
    block = memory.getBlock(address)
    if block is None:
        return None
    section = overlay_blocks.get(block.getName())
    if section is not None:
        relative = address.subtract(block.getStart())
        if 0 <= relative < section['file_bytes']:
            return dict(file='ovl', offset=section['original_file_offset'] + relative,
                        section=section['id'])
        return None
    if block in resident_blocks:
        offset = address.getOffset() - image_base + header_size
        if header_size <= offset < len(exe):
            return dict(file='exe', offset=offset)
    return None


# A verified ten-byte RTLink trampoline consists of CALL near, JMP far and
# an appended section word. The word disambiguates overlaid bank addresses.
def parse_trampoline(data):
    if len(data) < 10:
        return None
    if data[0] != 0xe8 or data[3] != 0xea:
        return None
    section = data[8] | (data[9] << 8)
    if section not in sections:
        return None
    target_offset = data[4] | (data[5] << 8)
    target_segment = data[6] | (data[7] << 8)
    candidate = sections[section]
    # The bank may have been relocated by the MZ load segment or may still
    # contain its original linked segment. Check both, with no guessing.
    for segment in (target_segment, target_segment - plan['relocation']):
        delta = (segment - candidate['linked_segment']) * 16 + target_offset
        if 0 <= delta < candidate['file_bytes']:
            return dict(section=section,
                        target=dict(file='ovl', section=section,
                                    offset=candidate['original_file_offset'] + delta))
    return None


def trampoline(address):
    block = memory.getBlock(address)
    if block is None or not block.isInitialized():
        return None
    try:
        data = [memory.getByte(address.add(i)) & 0xff for i in range(10)]
    except Exception:
        return None
    return parse_trampoline(data)


def original_call_trampoline(instruction):
    """Resolve far CALL through the RTLink manager using original image bytes.

    RTLink's relocated far segment can lose Ghidra's code reference. The
    native far CALL still names manager linked segment 0x1c24. Two manager
    offset bases occur in this fingerprinted executable; accept a candidate
    only after independently validating all ten stub bytes and section.
    """
    if not plan.get('fingerprinted_gateway'):
        return None
    source = origin(instruction.getAddress())
    if source is None:
        return None
    image = exe if source['file'] == 'exe' else ovl
    start = source['offset']
    if start + 5 > len(image) or image[start] != 0x9a:
        return None
    offset = image[start+1] | (image[start+2] << 8)
    segment = image[start+3] | (image[start+4] << 8)
    if segment != 0x1c24:
        return None
    for base in (0xa0da, 0xa020):
        stub_offset = base + offset
        stub = parse_trampoline(exe[stub_offset:stub_offset+10])
        if stub is not None:
            stub['stub'] = dict(file='exe', offset=stub_offset)
            return stub
    return None


# Ghidra labels can change the displayed operand. Only the explicit address
# forms count as recovered direct references; unresolved ones stay unknown.
MEMORY = re.compile(r'\[([^\]]+)\]')
NUMBER = re.compile(r'(?<![A-Za-z0-9_])(?:0x([0-9a-fA-F]+)|([0-9]+)h)(?![A-Za-z0-9_])',
                    re.I)
INDEX = re.compile(r'\b(?:BX|SI|DI)\b', re.I)
STACK = re.compile(r'\b(?:BP|SP)\b', re.I)


def memory_facts(instruction):
    direct = []
    indexed = False
    for index in range(instruction.getNumOperands()):
        part = instruction.getDefaultOperandRepresentation(index).upper()
        m = MEMORY.search(part)
        if m is None:
            continue
        expression = m.group(1)
        segment = 'ES' if 'ES:' in part else ('SS' if 'SS:' in part else 'DS')
        if segment == 'SS' or (segment != 'ES' and STACK.search(expression)):
            continue
        if INDEX.search(expression):
            indexed = True
        # An absolute scalar reference is a candidate global. Indexed memory
        # remains a pointer-arithmetic fact even if it has a displacement.
        if INDEX.search(expression) or STACK.search(expression):
            continue
        scalar = NUMBER.search(expression)
        if scalar is None:
            continue
        offset = int(scalar.group(1) or scalar.group(2), 16)
        if offset > 0xffff:
            continue
        # Ghidra's 16-bit operands often lack READ/WRITE flags; don't claim
        # access direction until source-level dataflow or a trace proves it.
        direct.append(dict(segment=segment, offset=offset))
    return direct, indexed


facts = []
for function in currentProgram.getFunctionManager().getFunctions(True):
    entry = function.getEntryPoint()
    identity = origin(entry)
    if identity is None or function.isExternal():
        continue
    result = dict(identity=identity, instruction_count=0, calls=[], call_sites=[],
                  far_trampolines=[], direct_memory=[], has_loop=False,
                  indexed_memory=False, computed_calls=0, computed_jumps=0,
                  unresolved_calls=0)
    seen_calls = set()
    seen_trampolines = set()
    seen_memory = set()
    instructions = listing.getInstructions(function.getBody(), True)
    while instructions.hasNext():
        instruction = instructions.next()
        result['instruction_count'] += 1
        flow = instruction.getFlowType()
        if flow.isCall():
            site = origin(instruction.getAddress())
            if site is not None:
                result['call_sites'].append(site)
        if flow.isJump():
            if flow.isComputed():
                result['computed_jumps'] += 1
            for destination in instruction.getFlows():
                if (destination.getAddressSpace() == entry.getAddressSpace() and
                        function.getBody().contains(destination) and
                        destination.compareTo(instruction.getAddress()) <= 0):
                    result['has_loop'] = True
        if flow.isCall() and flow.isComputed():
            result['computed_calls'] += 1
        references = [ref for ref in instruction.getReferencesFrom()
                      if ref.getReferenceType().isCall()]
        if flow.isCall() and not references:
            result['unresolved_calls'] += 1
        for ref in references:
            target = ref.getToAddress()
            dest = origin(target)
            if dest is not None:
                key = (dest['file'], dest['offset'])
                if key not in seen_calls:
                    seen_calls.add(key)
                    result['calls'].append(dest)
            stub = trampoline(target)
            if stub is not None:
                key = (stub['section'], stub['target']['offset'])
                if key not in seen_trampolines:
                    seen_trampolines.add(key)
                    result['far_trampolines'].append(stub)
                    if (stub['target']['file'], stub['target']['offset']) not in seen_calls:
                        seen_calls.add((stub['target']['file'], stub['target']['offset']))
                        result['calls'].append(stub['target'])
        stub = original_call_trampoline(instruction) if flow.isCall() else None
        if stub is not None:
            key = (stub['section'], stub['target']['offset'])
            if key not in seen_trampolines:
                seen_trampolines.add(key)
                result['far_trampolines'].append(stub)
                if (stub['target']['file'], stub['target']['offset']) not in seen_calls:
                    seen_calls.add((stub['target']['file'], stub['target']['offset']))
                    result['calls'].append(stub['target'])
        direct, indexed = memory_facts(instruction)
        result['indexed_memory'] |= indexed
        for item in direct:
            key = (item['segment'], item['offset'])
            if key not in seen_memory:
                seen_memory.add(key)
                result['direct_memory'].append(item)
    result['calls'].sort(key=lambda item: (item['file'], item['offset']))
    result['far_trampolines'].sort(key=lambda item: (item['section'], item['target']['offset']))
    result['direct_memory'].sort(key=lambda item: (item['segment'], item['offset']))
    facts.append(result)

facts.sort(key=lambda item: (item['identity']['file'], item['identity']['offset']))
with open(args[1], 'w') as output:
    json.dump(dict(schema_version=1, functions=facts), output, indent=2)
print('Gateway facts: %d native functions, %d instruction records, %d trampoline targets' % (
    len(facts), sum(item['instruction_count'] for item in facts),
    sum(len(item['far_trampolines']) for item in facts)))

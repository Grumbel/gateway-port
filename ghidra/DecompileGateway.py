# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#@category Gateway
# Ghidra Jython 2.7 post-analysis script.
# Arguments: absolute ghidra-plan.json, absolute pseudocode output path.
# Confirmed native entries are attempted as functions. Internal dispatcher
# branches and user-selected *candidate* offsets inside a larger analyzed
# function decompile that containing function, without inventing new entries.

import json
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.program.model.symbol import SourceType


args = getScriptArgs()
if len(args) != 2:
    raise ValueError('usage: DecompileGateway.py /absolute/ghidra-plan.json /absolute/output.txt')
plan = json.load(open(args[0], 'r'))
if plan['schema_version'] != 1:
    raise ValueError('unsupported Ghidra plan')
report = json.load(open(os.path.join(os.path.dirname(args[0]),
                                     'ghidra-import-report.json'), 'r'))
targets = []
memory = currentProgram.getMemory()
for section in plan['sections']:
    block = memory.getBlock('GW_RT_%03d' % section['id'])
    if block is None:
        raise ValueError('overlay section %d was not imported' % section['id'])
    for label in section['labels']:
        if label['role'] in ('function', 'callback-entry', 'object-entry-candidate',
                             'overlay-entry-candidate'):
            targets.append((label['name'], block.getStart().add(label['offset']),
                            'GATEMAIN.OVL:0x%x' % label['file_offset'], label['role']))
for label in plan['resident_labels']:
    address = report['resident_matches'].get(label['name'])
    if address is not None:
        targets.append((label['name'], toAddr(address),
                        'GATEMAIN.EXE:0x%x' % label['file_offset'], label['role']))

interface = DecompInterface()
interface.setSimplificationStyle('decompile')
if not interface.openProgram(currentProgram):
    raise ValueError('Ghidra decompiler failed to open the MZ program')
lines = [
    'Gateway native code: Ghidra pseudocode (UNVERIFIED)',
    'Overlay banks can overwrite one another. Calls between banks, far-pointer',
    'segment values, state semantics, and function extents need native traces.',
    '',
]
results = []
callgraph_targets = []
section_blocks = {}
for section in plan['sections']:
    section_blocks['GW_RT_%03d' % section['id']] = section
known_entries = dict((str(address), name) for name, address, origin, role in targets)
callgraph_cache = {}


def source_offset(address):
    block = memory.getBlock(address)
    if block is None or block.getName() not in section_blocks:
        return None
    section = section_blocks[block.getName()]
    offset = address.subtract(block.getStart())
    if offset < 0 or offset >= section['file_bytes']:
        return None
    return section['original_file_offset'] + offset


def static_calls(function):
    """References Ghidra resolved statically; not an executed RTLink call graph."""
    key = str(function.getEntryPoint())
    if key in callgraph_cache:
        return callgraph_cache[key]
    edges = []
    unresolved = []
    instructions = currentProgram.getListing().getInstructions(function.getBody(), True)
    while instructions.hasNext():
        instruction = instructions.next()
        from_address = instruction.getAddress()
        references = [ref for ref in instruction.getReferencesFrom()
                      if ref.getReferenceType().isCall()]
        for ref in references:
            target = ref.getToAddress()
            destination = getFunctionAt(target)
            if destination is None:
                destination = getFunctionContaining(target)
            edges.append(dict(callsite=str(from_address),
                              source_ovl_file_offset=source_offset(from_address),
                              destination=str(target),
                              destination_ovl_file_offset=source_offset(target),
                              known_target=known_entries.get(str(target)),
                              containing_function=str(destination.getEntryPoint())
                              if destination is not None else None,
                              ref_type=str(ref.getReferenceType())))
        if instruction.getFlowType().isCall() and not references:
            unresolved.append(str(from_address))
    value = dict(function_entry=key, edges=edges, unresolved_calls=unresolved)
    callgraph_cache[key] = value
    return value


for name, address, origin, role in targets:
    lines.append('===== %s at %s (%s; %s) =====' % (name, address, origin, role))
    item = dict(name=name, origin=origin, address=str(address), role=role,
                function_entry=None, status='missing')
    results.append(item)
    graph_item = dict(name=name, origin=origin, role=role,
                      requested_address=str(address), function_entry=None,
                      edges=[], unresolved_calls=[])
    callgraph_targets.append(graph_item)
    if role == 'instruction':
        function = getFunctionContaining(address)
        if function is None:
            item['error'] = 'no analyzed function contains this internal branch'
            lines.append(item['error'])
            lines.append('')
            continue
        lines.append('INTERNAL BRANCH; decompiling Ghidra containing function at %s. '
                     'This is NOT an independently verified function entry.' %
                     function.getEntryPoint())
        # Ghidra can carve a common jump target or a shared RETF tail out as
        # another function, even though original 16-bit conditional jumps
        # reach it from the verified 048e:0284 entry. Do not quietly present
        # that auto-analysis artifact as a separate native routine.
        if name.startswith('gw_room_transfer_'):
            parent = report['resident_matches'].get('gw_room_transfer')
            parent_function = getFunctionAt(toAddr(parent)) if parent else None
            if (parent_function is not None and
                    function.getEntryPoint() != parent_function.getEntryPoint()):
                item['context'] = 'ghidra-split-tail'
                lines.append('GHIDRA SPLIT TAIL: static jumps enter this code from '
                             'resident 048e:0284; this separate containing function '
                             'is an analyzer boundary, NOT a verified native ABI.')
    else:
        function = getFunctionAt(address)
        if function is None:
            containing = getFunctionContaining(address)
            if containing is not None:
                if role == 'overlay-entry-candidate':
                    function = containing
                    item['context'] = 'inside-containing-function'
                    lines.append('OFFSET CANDIDATE is inside Ghidra function at %s. '
                                 'Decompiling containing function, NOT asserting '
                                 'an independent native entry.' %
                                 containing.getEntryPoint())
                else:
                    item['error'] = 'Ghidra placed entry inside function at %s' % (
                        containing.getEntryPoint())
                    lines.append(item['error'])
                    lines.append('')
                    continue
            else:
                try:
                    disassemble(address)
                    function = createFunction(address, name)
                except Exception as error:
                    item['error'] = 'cannot create function: %s' % error
                    lines.append(item['error'])
                    lines.append('')
                    continue
        if function is None:
            item['error'] = 'Ghidra did not establish a function boundary'
            lines.append(item['error'])
            lines.append('')
            continue
    item['function_entry'] = str(function.getEntryPoint())
    graph = static_calls(function)
    graph_item.update(graph)
    try:
        if role in ('function', 'callback-entry', 'object-entry-candidate',
                    'overlay-entry-candidate'):
            # ImportGatewayRtlink creates a user label first. When analysis
            # chooses a different primary symbol for the function, Ghidra
            # rejects renaming it to that still-existing secondary label
            # with DuplicateNameException. The address and report name are
            # authoritative; a cosmetic rename is not worth aborting export.
            existing_names = [symbol.getName() for symbol in
                              currentProgram.getSymbolTable().getSymbols(address)]
            if (function.getEntryPoint() == address and
                    name not in existing_names and function.getName() != name):
                function.setName(name, SourceType.USER_DEFINED)
        result = interface.decompileFunction(function, 120, monitor)
        if result.decompileCompleted() and result.getDecompiledFunction() is not None:
            lines.append(result.getDecompiledFunction().getC())
            item['status'] = 'ok'
        else:
            item['status'] = 'failed'
            item['error'] = str(result.getErrorMessage())
            lines.append('Decompilation failed: %s' % item['error'])
    except Exception as error:
        item['status'] = 'failed'
        item['error'] = str(error)
        lines.append('Decompilation error: %s' % item['error'])
    lines.append('')
interface.dispose()
with open(args[1], 'w') as output:
    output.write('\n'.join(lines) + '\n')
success = sum(item['status'] == 'ok' for item in results)
summary = dict(schema_version=1, targets=results, completed=success,
               requested=len(results), failed=len(results)-success)
report_path = os.path.join(os.path.dirname(args[0]), 'ghidra-decompile-report.json')
with open(report_path, 'w') as output:
    json.dump(summary, output, indent=2)
callgraph_path = os.path.join(os.path.dirname(args[0]), 'ghidra-callgraph.json')
with open(callgraph_path, 'w') as output:
    json.dump(dict(schema_version=1, targets=callgraph_targets,
                   unique_containing_functions=len(callgraph_cache),
                   unresolved_call_instructions=sum(len(v['unresolved_calls'])
                                                    for v in callgraph_cache.values()),
                   static_call_references=sum(len(v['edges'])
                                              for v in callgraph_cache.values())), output, indent=2)
println('Gateway Ghidra: pseudocode for %d/%d entry/branch targets: %s' % (
    success, len(results), args[1]))

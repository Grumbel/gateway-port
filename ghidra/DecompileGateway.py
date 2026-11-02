#@category Gateway
# Ghidra Jython 2.7 post-analysis script.
# Arguments: absolute ghidra-plan.json, absolute pseudocode output path.
# Confirmed native entries are attempted as functions. Internal dispatcher
# branch labels instead decompile their *containing* Ghidra functions, without
# creating false function boundaries at jump targets.

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
for name, address, origin, role in targets:
    lines.append('===== %s at %s (%s; %s) =====' % (name, address, origin, role))
    item = dict(name=name, origin=origin, address=str(address), role=role,
                function_entry=None, status='missing')
    results.append(item)
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
    else:
        function = getFunctionAt(address)
        if function is None:
            containing = getFunctionContaining(address)
            if containing is not None:
                item['error'] = 'Ghidra placed entry inside function at %s' % (
                    containing.getEntryPoint())
                lines.append(item['error'])
                lines.append('')
                continue
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
            if name not in existing_names and function.getName() != name:
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
println('Gateway Ghidra: pseudocode for %d/%d entry/branch targets: %s' % (
    success, len(results), args[1]))

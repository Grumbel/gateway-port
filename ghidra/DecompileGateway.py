#@category Gateway
# Ghidra Jython 2.7 post-analysis script.
# Arguments: absolute ghidra-plan.json, absolute pseudocode output path.
# Only *confirmed* native entry points are attempted as functions. Ghidra's C
# output is a research aid, not a verified port or source-language recovery.

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
        if label['role'] == 'function':
            targets.append((label['name'], block.getStart().add(label['offset']),
                            'GATEMAIN.OVL:0x%x' % label['file_offset']))
for label in plan['resident_labels']:
    if label['role'] != 'function':
        continue
    address = report['resident_matches'].get(label['name'])
    if address is not None:
        targets.append((label['name'], toAddr(address),
                        'GATEMAIN.EXE:0x%x' % label['file_offset']))

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
success = 0
for name, address, origin in targets:
    lines.append('===== %s at %s (%s) =====' % (name, address, origin))
    function = getFunctionAt(address)
    if function is None:
        try:
            disassemble(address)
            function = createFunction(address, name)
        except Exception as error:
            lines.append('No function boundary: %s' % error)
            lines.append('')
            continue
    if function is None:
        lines.append('No function boundary; inspect the surrounding disassembly.')
        lines.append('')
        continue
    try:
        try:
            # Pre-analysis already created the named label. A Ghidra analyzer
            # may have promoted a different label to the function's symbol.
            function.setName(name, SourceType.USER_DEFINED)
        except Exception as rename_error:
            lines.append('Function rename skipped: %s' % rename_error)
        result = interface.decompileFunction(function, 120, monitor)
        if result.decompileCompleted() and result.getDecompiledFunction() is not None:
            lines.append(result.getDecompiledFunction().getC())
            success += 1
        else:
            lines.append('Decompilation failed: %s' % result.getErrorMessage())
    except Exception as error:
        lines.append('Decompilation error: %s' % error)
    lines.append('')
interface.dispose()
with open(args[1], 'w') as output:
    output.write('\n'.join(lines) + '\n')
println('Gateway Ghidra: pseudocode for %d/%d verified entries: %s' % (
    success, len(targets), args[1]))

#@category Gateway
# Ghidra Jython 2.7 pre-analysis script. Argument: absolute ghidra-plan.json.
# Run on the *original* GATEMAIN.EXE imported with the MZ/DOS loader.
# Each section gets its own overlay address space even when DOS banks overlap.

import hashlib
import json
import os
from java.io import FileInputStream
from ghidra.program.model.data import (CategoryPath, DataTypeConflictHandler,
                                      StructureDataType, UnsignedCharDataType,
                                      UnsignedShortDataType)
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.symbol import RefType, SourceType
from jarray import array as byte_array


def require(test, message):
    if not test:
        raise ValueError(message)


def register_types():
    cat = CategoryPath('/Gateway')
    byte = UnsignedCharDataType.dataType
    word = UnsignedShortDataType.dataType
    exits = StructureDataType(cat, 'GatewayRoomExit', 0)
    exits.add(byte, 'direction', '0x03 EAST and 0x07 WEST observed')
    exits.add(byte, 'kind', '1=direct, 2=flag gate, 4=far callback')
    exits.add(word, 'argument', 'room ID or table/callback argument')
    conditional = StructureDataType(cat, 'GatewayConditionalExit', 0)
    conditional.add(word, 'destination', 'room object ID')
    conditional.add(word, 'guardObject', 'object whose flags 0x0c/0x10 are tested')
    index = StructureDataType(cat, 'GatewayObjectDirectoryEntry', 0)
    index.add(byte, 'kind', 'native type 1..8')
    index.add(byte, 'unknown', 'not yet classified')
    index.add(word, 'recordOffset', 'far pointer offset')
    index.add(word, 'recordSegment', 'far pointer segment')
    manager = currentProgram.getDataTypeManager()
    for dtype in (exits, conditional, index):
        manager.addDataType(dtype, DataTypeConflictHandler.KEEP_HANDLER)


def note(address, message):
    listing = currentProgram.getListing()
    existing = listing.getComment(CodeUnit.PRE_COMMENT, address)
    if existing:
        message = existing + '\n' + message
    listing.setComment(address, CodeUnit.PRE_COMMENT, message)


def annotate(address, label):
    name = label['name']
    # Object names are aliases, not a promise of separate functions. Shared
    # handler pointers can have several object aliases at one entry point.
    currentProgram.getSymbolTable().createLabel(
        address, name, currentProgram.getGlobalNamespace(), SourceType.USER_DEFINED)
    explanation = label['explanation']
    if label['role'] == 'callsite':
        explanation += ' (callsite; function entry NOT established)'
    note(address, '%s; original file offset 0x%x; %s' % (
        name, label['file_offset'], explanation))


def find_original_signature(blocks, signature):
    raw = byte_array([byte-256 if byte >= 128 else byte for byte in
                      bytearray.fromhex(signature)], 'b')
    found = []
    memory = currentProgram.getMemory()
    for block in blocks:
        if not block.isInitialized() or block.isOverlay():
            continue
        start = block.getStart()
        while start.compareTo(block.getEnd()) <= 0:
            match = memory.findBytes(start, block.getEnd(), raw, None, True, monitor)
            if match is None:
                break
            found.append(match)
            start = match.add(1)
    return found


def annotate_study_event_table(section, start):
    """Expose verified internal branch words without inventing C functions.

    Section 51's original Study object handler (OVL 0x6b521) uses a near
    CS:[BX+0x0b82] jump, with event IDs 1..23. Ghidra's segmented decompiler
    currently tries ram:4000:0b82 and cannot follow the switch. The actual
    RTLink *overlay address space* can still hold correct data references.
    """
    if section['id'] != 51:
        return 0
    # These exact offsets are from the known, SHA-256-verified Gateway OVL.
    # Never apply them to an arbitrary section-51-like synthetic image.
    require(section['original_file_offset'] == 0x6acd0,
            'Study jump table expected original section-51 file base')
    original_targets = (
        0x6b692, 0x6b5ba, 0x6b5ba, 0x6b5ba, 0x6b5ba,
        0x6b82d, 0x6b5ba, 0x6b56b, 0x6b56b, 0x6b5ba,
        0x6b56b, 0x6b5ba, 0x6b5ba, 0x6b5bf, 0x6b5ba,
        0x6b5ba, 0x6b5ba, 0x6b5ba, 0x6b5ba, 0x6b5ba,
        0x6b5ba, 0x6b53e, 0x6b555,
    )
    listing = currentProgram.getListing()
    refs = currentProgram.getReferenceManager()
    table = start.add(0x0b82)
    require(0x0b82 + 2 * len(original_targets) <= section['file_bytes'],
            'Study jump table is not within file-backed image')
    currentProgram.getSymbolTable().createLabel(
        table, 'gw_study_event_jump_table',
        currentProgram.getGlobalNamespace(), SourceType.USER_DEFINED)
    note(table, 'OVL:0x6b852, 23 original Study event branches (1-based); '
         'CS-relative near offsets, NOT separate function entries')
    for index, file_target in enumerate(original_targets):
        slot = table.add(2 * index)
        relative_target = file_target - section['original_file_offset']
        require((int(memory.getShort(slot)) & 0xffff) == relative_target,
                'Study event %d jump-table bytes differ from original' % (index + 1))
        require(0 <= relative_target < section['file_bytes'],
                'Study event %d targets non-file-backed bytes' % (index + 1))
        listing.createData(slot, UnsignedShortDataType.dataType)
        destination = start.add(relative_target)
        refs.addMemoryReference(slot, destination, RefType.DATA, SourceType.USER_DEFINED, 0)
        note(slot, 'Study event %d -> original OVL:0x%x; internal jump target' %
             (index + 1, file_target))
    return len(original_targets)


args = getScriptArgs()
require(len(args) == 1, 'usage: ImportGatewayRtlink.py /absolute/ghidra-plan.json')
plan_path = os.path.abspath(args[0])
plan = json.load(open(plan_path, 'r'))
require(plan['schema_version'] == 1, 'unsupported Ghidra import plan')
require('x86:LE:16' in str(currentProgram.getLanguage().getLanguageID()),
        'import GATEMAIN.EXE using the 16-bit x86 DOS/MZ loader first')
require(hashlib.sha256(open(plan['exe_file'], 'rb').read()).hexdigest() ==
        plan['exe_sha256'], 'original EXE changed since preparation')
require(hashlib.sha256(open(plan['ovl_file'], 'rb').read()).hexdigest() ==
        plan['ovl_sha256'], 'original OVL changed since preparation')

memory = currentProgram.getMemory()
original_blocks = list(memory.getBlocks())
register_types()
imported = 0
imported_labels = 0
study_event_table_words = 0
for section in plan['sections']:
    sid = section['id']
    prefix = 'GW_RT_%03d' % sid
    require(memory.getBlock(prefix) is None,
            'section %d already imported: use a fresh Ghidra project' % sid)
    data_path = section['relocated_file']
    require(hashlib.sha256(open(data_path, 'rb').read()).hexdigest() ==
            section['relocated_sha256'], 'section %d image changed' % sid)
    require(section['file_bytes'] > 0, 'section %d has no file-backed image' % sid)
    # SegmentedAddressSpace uses the original DOS address notation; it does
    # not mean all of these sections can coexist at runtime.
    address = toAddr('%04x:0000' % section['runtime_segment'])
    require(address is not None, 'cannot make a 16-bit segment address')
    stream = FileInputStream(data_path)
    try:
        block = memory.createInitializedBlock(
            prefix, address, stream, section['file_bytes'], monitor, True)
    finally:
        stream.close()
    block.setRead(True)
    block.setWrite(False)
    block.setExecute(True)
    block.setComment('RTLink section %d; original OVL file 0x%x; %d fixups; '
                     'parent=%s; overlapping sections=%s' % (
                         sid, section['original_file_offset'], section['fixup_count'],
                         section['parent'], section['overlaps']))
    start = block.getStart()  # overlay address, NOT the underlying physical bank
    tail = section['missing_paragraph_tail']
    bss_size = section['memory_size'] - section['initialized_size']
    if tail:
        unknown = memory.createUninitializedBlock(
            prefix + '_UNKNOWN_TAIL', start.add(section['file_bytes']), tail, False)
        unknown.setComment('Native DOS read was short: not file-backed and not known to be zero')
    if bss_size:
        zero = memory.createInitializedBlock(
            prefix + '_ZERO_BSS', start.add(section['initialized_size']),
            bss_size, 0, monitor, False)
        zero.setComment('RTLink zeroes this BSS interval on each load')
    if plan.get('fingerprinted_gateway', False):
        # The preparer has fingerprinted these original assets. Keep the
        # fixed table mapping out of unknown-image synthetic fixtures.
        study_event_table_words += annotate_study_event_table(section, start)
    for label in section['labels']:
        address = start.add(label['offset'])
        annotate(address, label)
        if label['role'] in ('function', 'callback-entry', 'object-entry-candidate',
                             'overlay-entry-candidate'):
            # Seeding disassembly BEFORE the headless auto-analysis ensures
            # these verified entries are explored, without pretending that
            # all object/event pointers identify distinct C functions.
            disassemble(address)
        imported_labels += 1
    imported += 1

resident_matches = {}
for label in plan['resident_labels']:
    matches = find_original_signature(original_blocks, label['signature'])
    if len(matches) != 1:
        printerr('Ghidra: resident %s: expected unique original-MZ byte signature, '
                 'found %d; left unlabeled' % (label['name'], len(matches)))
        resident_matches[label['name']] = None
        continue
    address = matches[0]
    annotate(address, label)
    if label['role'] == 'function':
        disassemble(address)
    resident_matches[label['name']] = str(address)

report = dict(imported_sections=imported, labeled_overlay_entries=imported_labels,
              study_event_table_words=study_event_table_words,
              resident_matches=resident_matches)
report_path = os.path.join(os.path.dirname(plan_path), 'ghidra-import-report.json')
with open(report_path, 'w') as output:
    json.dump(report, output, indent=2)
println('Gateway RTLink: imported %d isolated banks, %d entry labels. Report: %s' % (
    imported, imported_labels, report_path))

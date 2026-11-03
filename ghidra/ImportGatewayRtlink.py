# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

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
        return (0, 0)
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
    computed_jump = start.add(0x6b539 - section['original_file_offset'])
    require(0x0b82 + 2 * len(original_targets) <= section['file_bytes'],
            'Study jump table is not within file-backed image')
    require((int(memory.getByte(computed_jump)) & 0xff) == 0x2e,
            'Study event dispatcher CS-relative JMP opcode differs')
    currentProgram.getSymbolTable().createLabel(
        table, 'gw_study_event_jump_table',
        currentProgram.getGlobalNamespace(), SourceType.USER_DEFINED)
    note(table, 'OVL:0x6b852, 23 original Study event branches (1-based); '
         'CS-relative near offsets, NOT separate function entries')
    branch_targets = set()
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
        if relative_target not in branch_targets:
            # A reference from the actual CS:[BX+0b82] opcode, not just
            # from the table slot, may allow Ghidra to follow its verified
            # computed edges during analysis. The original table has only
            # seven *unique* branch destinations for 23 event numbers.
            refs.addMemoryReference(computed_jump, destination, RefType.COMPUTED_JUMP,
                                    SourceType.USER_DEFINED, 0)
            branch_targets.add(relative_target)
        note(slot, 'Study event %d -> original OVL:0x%x; internal jump target' %
             (index + 1, file_target))
    return (len(original_targets), len(branch_targets))


def annotate_study_matcher_path(section, start):
    """Name verified *interior instructions* of Study event 1, not functions.

    These labels make the action/object table comparisons navigable in the
    overlay bank even while Ghidra cannot recover the handler's switch.
    Byte anchors also ensure a mismatched original section cannot be marked.
    """
    if section['id'] != 51:
        return 0
    require(section['original_file_offset'] == 0x6acd0,
            'Study matcher expected original section-51 file base')
    anchors = (
        (0x6b6c0, 0x8e, 'gw_study_actor_guard',
         'event 1: actor at [DS:965e]:35c8 must be player 0x0d3'),
        (0x6b6ec, 0x26, 'gw_study_target_lookup',
         'expected object u16 from [DS:9652]:02aa + 4*progress; zero is any'),
        (0x6b71c, 0x8e, 'gw_study_action_lookup',
         'native parser action byte at [DS:9654]:e664'),
        (0x6b735, 0x26, 'gw_study_primary_compare',
         'compare action byte with [DS:9652]:02a8 + 4*progress'),
        (0x6b73c, 0x8e, 'gw_study_primary_match',
         'primary match writes [DS:9662]:7909 = 1 at original 0x6b740'),
        (0x6b740, 0x26, 'gw_study_primary_flag_write',
         'native :7909 write proven by unmodified SCREAM and FEEL COUCH'),
        (0x6b786, 0xc7, 'gw_study_alternative_loop',
         'scan five {stage:u16, action1:u8, action2:u8} records at :02fc'),
        (0x6b7b3, 0x8e, 'gw_study_alternative_match',
         'alternative action match writes [DS:9662]:7909 = 1'),
        (0x6b7dc, 0x26, 'gw_study_alternative_stage_compare',
         'compare current signed stage with each :02fc alternative record'),
    )
    for file_offset, byte, name, detail in anchors:
        relative = file_offset - section['original_file_offset']
        require(0 <= relative < section['file_bytes'] and
                (int(memory.getByte(start.add(relative))) & 0xff) == byte,
                '%s original x86 anchor differs' % name)
        currentProgram.getSymbolTable().createLabel(
            start.add(relative), name, currentProgram.getGlobalNamespace(),
            SourceType.USER_DEFINED)
        note(start.add(relative), '%s; OVL:0x%x; %s; interior code, NOT a function entry' %
             (name, file_offset, detail))
    return len(anchors)


def annotate_study_response_path(section, start):
    """Expose verified interior branches of the native Study dialogue state machine."""
    if section['id'] != 51:
        return 0
    require(section['original_file_offset'] == 0x6acd0,
            'Study response expected original section-51 file base')
    anchors = (
        (0x6ce97, 0x26, 'gw_study_initialize_progress', 'initialize :7904 from 0 to 1'),
        (0x6ceb4, 0xb8, 'gw_study_stage3_couch_check',
         'native direct membership query for player and couch 0x2be'),
        (0x6ced4, 0xb8, 'gw_study_stage13_desk_check',
         'native direct membership query for player and desk 0x2bf'),
        (0x6ceec, 0x26, 'gw_study_posture_match_write',
         'set :7909 after successful UP/DOWN no longer on old parent'),
        (0x6cef6, 0x26, 'gw_study_response_match_gate',
         'branch to acceptance when :7909 is nonzero'),
        (0x6cf0d, 0x26, 'gw_study_previous_prompt_check',
         'compare last prompted stage :7906 with current :7904'),
        (0x6cf20, 0x8e, 'gw_study_repeat_instruction',
         'select prompt far pointer at :03a2 when :7908 is zero'),
        (0x6cf4c, 0x26, 'gw_study_reminder_flag_write', 'set :7908 to 1'),
        (0x6cf55, 0xb8, 'gw_study_insistence_text',
         'F000:70ae and auxiliary handler on repeated refusal'),
        (0x6cf66, 0xe8, 'gw_study_insistence_auxiliary_call',
         'near call to original OVL:0x6d564'),
        (0x6cf6f, 0x8e, 'gw_study_new_instruction',
         'select :03a2 prompt, print F000:70af'),
        (0x6cfd4, 0x26, 'gw_study_record_prompt_stage', 'store :7906 = :7904'),
        (0x6cfda, 0x8e, 'gw_study_acceptance_branch',
         'accepted action tests progress signed >19 before response'),
        (0x6cfe6, 0xb8, 'gw_study_completion_text',
         'F000:70b0 on matched stage >19'),
        (0x6cff1, 0x26, 'gw_study_progress_increment',
         'increment :7904 before reading response far pointer at :03f2'),
        (0x6d01c, 0x26, 'gw_study_clear_match_flag', 'reset :7909 to 0'),
        (0x6d026, 0x26, 'gw_study_clear_previous_prompt', 'reset :7906 to 0'),
        (0x6d031, 0x26, 'gw_study_clear_reminder', 'reset :7908 to 0'),
    )
    for file_offset, byte, name, detail in anchors:
        relative = file_offset - section['original_file_offset']
        require(0 <= relative < section['file_bytes'] and
                (int(memory.getByte(start.add(relative))) & 0xff) == byte,
                '%s original x86 anchor differs' % name)
        currentProgram.getSymbolTable().createLabel(
            start.add(relative), name, currentProgram.getGlobalNamespace(),
            SourceType.USER_DEFINED)
        note(start.add(relative), '%s; OVL:0x%x; %s; interior code, NOT a new function' %
             (name, file_offset, detail))
    return len(anchors)


def annotate_vr_return_path(section, start):
    """Name 24 byte-verified instructions in the native VR-return dispatcher.

    OVL 0x6d564 is reached both by Study and by the Beach scanner. The
    branches are not separate function entries. Resident 048e:0284 can
    suspend the return frame; a later input resumes at the next instruction.
    """
    if section['id'] != 51:
        return 0
    require(section['original_file_offset'] == 0x6acd0,
            'VR return expected original section-51 file base')
    anchors = (
        (0x6d579, 0xb8, 'gw_vr_return_technician_guard', 'recursive relation: technician 0x2a3 reaches Terminal 0x29b'),
        (0x6d590, 0x8e, 'gw_vr_return_beach_success_guard', 'check guest byte :78fa for Beach success'),
        (0x6d59c, 0xb8, 'gw_vr_return_pin_guard', 'direct relation: pin 0x2a9 belongs to technician'),
        (0x6d5b0, 0xb8, 'gw_vr_return_pin_text', 'award pin, F000:70b9'),
        (0x6d5c0, 0x2b, 'gw_vr_return_pin_move', 'move pin 0x2a9 to Terminal 0x29b'),
        (0x6d5d3, 0xb8, 'gw_vr_return_pin_unhide', 'clear pin flag 0x08'),
        (0x6d5e3, 0x8e, 'gw_vr_return_result_guard', 'branch on raw guest result word :7920'),
        (0x6d5f2, 0xb8, 'gw_vr_return_result_text', 'nonzero result: F000:70ba'),
        (0x6d602, 0x8e, 'gw_vr_return_result_seven_guard', 'test raw result word :7920 == 7'),
        (0x6d60e, 0x8e, 'gw_vr_return_other_repeat_guard', 'other result: test :7927 for F000:70bb/70bc'),
        (0x6d62e, 0x8e, 'gw_vr_return_seven_repeat_guard', 'result 7: test :7927 for F000:70bb/70bd'),
        (0x6d65b, 0xb8, 'gw_vr_return_seven_first_service', 'on first result 7 only: 1c24:434b(3)'),
        (0x6d667, 0x8e, 'gw_vr_return_seven_clear_word', 'result 7 clears guest word :6b1a'),
        (0x6d672, 0x8e, 'gw_vr_return_seven_mark_seen', 'result 7 sets guest byte :7927 to 1'),
        (0x6d67e, 0xb8, 'gw_vr_return_no_result_text', 'zero result: F000:70be'),
        (0x6d68e, 0x8e, 'gw_vr_return_no_result_timer', 'zero result sets guest word :44e4 = 0x38'),
        (0x6d699, 0xb8, 'gw_vr_return_no_result_event', 'zero result calls 02e3:002b(0x2b,1)'),
        (0x6d6a9, 0x2b, 'gw_vr_return_cleanup_start', 'shared cleanup after all result branches'),
        (0x6d6b9, 0x8e, 'gw_vr_return_cleanup_flag', 'set :3942 to 1 after removing object 0xe4'),
        (0x6d6c3, 0xb8, 'gw_vr_return_cleanup_wall', 'set flag 0x0b on wall object 0x165'),
        (0x6d70f, 0x2b, 'gw_vr_return_cleanup_unseat', 'move player to native current room :35c6'),
        (0x6d727, 0xb8, 'gw_vr_return_room_transfer', 'call 048e:0284(Terminal 0x29b); may yield to later input'),
        (0x6d73b, 0xb8, 'gw_vr_return_couch_resume', 'after deferred room-transfer return: place player in couch 0x2a1'),
        (0x6d747, 0xcb, 'gw_vr_return_retf', 'far return after deferred couch move'),
    )
    for file_offset, byte, name, detail in anchors:
        relative = file_offset - section['original_file_offset']
        require(0 <= relative < section['file_bytes'] and
                (int(memory.getByte(start.add(relative))) & 0xff) == byte,
                '%s original x86 anchor differs' % name)
        currentProgram.getSymbolTable().createLabel(
            start.add(relative), name, currentProgram.getGlobalNamespace(),
            SourceType.USER_DEFINED)
        note(start.add(relative), '%s; OVL:0x%x; %s; interior code, NOT a function entry' %
             (name, file_offset, detail))
    return len(anchors)


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
study_event_flow_edges = 0
study_matcher_path_labels = 0
study_response_path_labels = 0
vr_return_path_labels = 0
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
        words, edges = annotate_study_event_table(section, start)
        study_event_table_words += words
        study_event_flow_edges += edges
        study_matcher_path_labels += annotate_study_matcher_path(section, start)
        study_response_path_labels += annotate_study_response_path(section, start)
        vr_return_path_labels += annotate_vr_return_path(section, start)
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
              study_event_flow_edges=study_event_flow_edges,
              study_matcher_path_labels=study_matcher_path_labels,
              study_response_path_labels=study_response_path_labels,
              vr_return_path_labels=vr_return_path_labels,
              resident_matches=resident_matches)
report_path = os.path.join(os.path.dirname(plan_path), 'ghidra-import-report.json')
with open(report_path, 'w') as output:
    json.dump(report, output, indent=2)
println('Gateway RTLink: imported %d isolated banks, %d entry labels. Report: %s' % (
    imported, imported_labels, report_path))

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"
#include <stdlib.h>

/* Nested RTL:6eb8..6f45, linked from the first control-key CRT close.
 * On EVERY return the original writes the descriptor base FAR pointer
 * into current (+0/+2) and resets its pending count (+4), even if no
 * write was needed. Only its nonwriting routes are source-reconstructed.
 * The mode-2 path without bit 08h indexes a separately relocated DOS
 * descriptor flag table. Its bit-zero-clear exit also skips the writer.
 * Positive pending data calls the CRT writer at RTL:7d64, so abort
 * before changing the descriptor in that still-unrecovered case. */
uint16_t gate_linked_flush_intro_stream(GateFarPtr stream)
{
    uint16_t field;
    uint8_t flags;
    uint16_t base_offset;
    uint16_t base_segment;

    if (stream.segment == 0)
        abort();
    field = (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET);
    flags = gate_far_byte(stream.segment, field);
    if ((flags & GATE_CONTROL_STREAM_MODE_MASK) == GATE_CONTROL_STREAM_WRITE_MODE) {
        uint8_t needs_buffer_check = 1;
        if ((flags & GATE_CONTROL_STREAM_FORCE_WRITE_BIT) == 0) {
            /* RTL:6ed5..6eed: original signed IDIV by 12 truncates toward
             * zero, even for an unaligned or negative descriptor offset.
             * Convert the 16-bit quotient back to the 16-bit BX index
             * before testing the first byte of its six-byte file record. */
            int16_t delta = (int16_t)(uint16_t)(stream.offset - GATE_DS_CRT_STREAMS);
            int16_t index = (int16_t)(delta / GATE_CRT_STREAM_STRIDE);
            uint16_t flag_address = (uint16_t)(GATE_DS_CRT_DESCRIPTOR_FLAGS +
                     (uint16_t)(index * GATE_CRT_FILE_STRIDE));
            if ((gate_far_byte(gate_ds_segment(), flag_address) &
                 GATE_CRT_FILE_WRITE_BIT) == 0)
                needs_buffer_check = 0;
        }
        if (needs_buffer_check) {
            uint16_t cursor = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                            GATE_CONTROL_STREAM_CURRENT_OFFSET));
            uint16_t base = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                          GATE_CONTROL_STREAM_BASE_OFFSET));
            if ((int16_t)(uint16_t)(cursor - base) > 0)
                abort();
        }
    }

    base_offset = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                GATE_CONTROL_STREAM_BASE_OFFSET));
    base_segment = gate_far_word(stream.segment, (uint16_t)(stream.offset +
                                 GATE_CONTROL_STREAM_BASE_SEGMENT));
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_CURRENT_OFFSET), base_offset);
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_CURRENT_SEGMENT), base_segment);
    gate_far_set_word(stream.segment, (uint16_t)(stream.offset +
                      GATE_CONTROL_STREAM_PENDING_COUNT), 0);
    return 0;
}

/* First Ctrl-Break FAR callee, RTL:15706..15729, linked 221b:021f.
 * DS:156e/1570 hold a signed open flag and companion word; DS:6150/6152
 * hold the stream descriptor FAR pointer (a distinct relocation group).
 * The nested RTL:6640 close clears descriptor byte +0a without any DOS/CRT
 * operations if (flags & 83h) == 0 or (flags & 40h) != 0. Only those two
 * original quiet exits are supported here. An active stream would enter
 * RTL:6eb8's buffered flush and a much larger DOS CRT close path; refusing
 * it prevents a silently missing flush or leaked file descriptor. */
void gate_linked_shutdown_intro_stream(void)
{
    GateFarPtr stream;
    uint8_t flags;
    if ((int16_t)gate_ds_word(GATE_DS_CONTROL_STREAM_OPEN) < 0)
        return;

    stream.offset = gate_ds_word(GATE_DS_CONTROL_STREAM_OFFSET);
    stream.segment = gate_ds_word(GATE_DS_CONTROL_STREAM_SEGMENT);
    if (stream.segment == 0)
        abort();
    flags = gate_far_byte(stream.segment,
                          (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET));
    if ((flags & GATE_CONTROL_STREAM_ACTIVE_MASK) != 0 &&
        (flags & GATE_CONTROL_STREAM_BYPASS_BIT) == 0)
        abort();

    gate_far_set_byte(stream.segment,
                      (uint16_t)(stream.offset + GATE_CONTROL_STREAM_FLAGS_OFFSET), 0);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0xffffu);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_AUX, 0);
}

/* GATE.RTL:0xe4ff..0xe510, 0c33:000e.  Keep the source's order of both
 * DS global writes.  The matching original caller may set this base once. */
void gate_rtl_set_resource_path_base(GateFarPtr base)
{
    gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET, base.offset);
    gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT, base.segment);
}

/* GATE.RTL:0xe511..0xe530, 0c33:0020.  This is specifically an unsigned
 * AX < 1 test, i.e. one only when the lower-level result is zero. */
uint16_t gate_rtl_check_resource_path(GateFarPtr path)
{
    return gate_linked_open_runtime_path(path, GATE_RESOURCE_PATH_PROBE_CODE) == 0 ? 1u : 0u;
}

/* GATE.RTL:0xe531..0xe581, 0c33:0040.  Original stack layout is an
 * 80-byte pathname and a separate result WORD; the formatter's nine pushed
 * WORDs are preserved in source-argument order.  Input `type` is multiplied
 * by four with 16-bit truncation before indexing the original DS table. */
uint16_t gate_rtl_open_resource_file(uint16_t type, uint16_t resource)
{
    uint8_t path[GATE_RESOURCE_PATH_STACK_BYTES];
    uint16_t entry = (uint16_t)(GATE_DS_RESOURCE_PATH_TABLE + (uint16_t)(type << GATE_RESOURCE_PATH_TABLE_ENTRY_SHIFT));
    uint16_t ds = gate_ds_segment();
    GateFarPtr table;
    GateFarPtr base;
    GateFarPtr format;
    GateFarPtr pathname;

    table.segment = gate_ds_word((uint16_t)(entry + GATE_RESOURCE_PATH_TABLE_SEGMENT_WORD_OFFSET));
    table.offset = gate_ds_word(entry);
    base.segment = gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT);
    base.offset = gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET);
    format.segment = ds;
    format.offset = GATE_DS_RESOURCE_PATH_FORMAT;
    pathname = gate_stack_pointer(path);

    gate_linked_format_resource_filename(pathname, format, base, resource, table);
    return gate_linked_dos_file_open(pathname, type == GATE_RESOURCE_FILE_CREATE_TYPE ? GATE_DOS_FILE_CREATE_FLAG : 0u);
}

/* GATE.RTL:0xe582..0xe59a, 0c33:0091.  The guest pushes the offset's high
 * WORD, low WORD, zero origin, then handle.  The return value is ignored. */
void gate_rtl_seek_resource_file(uint16_t handle, uint32_t offset)
{
    (void)gate_linked_dos_file_seek(handle, 0, offset);
}

/* GATE.RTL:0xe59b..0xe5c4, 0c33:00aa.  The byte is read into a WORD-sized
 * stack slot, so the top byte can remain from the original entry state.
 * If the underlying call returns zero, the result is explicitly -1. */
uint16_t gate_rtl_read_resource_byte(uint16_t handle)
{
#ifdef GATE_MSC51_ORIGINAL_FILE_ABI
    /* RTL:e59e..e5b9 allocates one WORD at BP-2 and passes SS:BP-2 to
     * the one-byte DOS read without initializing the other byte.  In the
     * real /Od DOS build, keep the automatic slot exactly as it was; the
     * host oracle instead seeds that unknown stack word explicitly. */
    uint16_t value;
#else
    uint16_t value = gate_rtl_file_stack_word();
#endif
    if (gate_linked_dos_file_read(handle, gate_stack_pointer(&value), 1) != 0)
        return value;
    return GATE_RESOURCE_FILE_BYTE_READ_FAILURE;
}

/* GATE.RTL:0xe5c5..0xe5e2, 0c33:00d4.  Unlike the byte reader above this
 * returns the original stack WORD unconditionally, even if read fails. */
uint16_t gate_rtl_read_resource_word(uint16_t handle)
{
#ifdef GATE_MSC51_ORIGINAL_FILE_ABI
    /* RTL:e5c8..e5dc uses the same uninitialized BP-2 slot, and returns
     * it even if the DOS read does not fill both bytes. */
    uint16_t value;
#else
    uint16_t value = gate_rtl_file_stack_word();
#endif
    (void)gate_linked_dos_file_read(handle, gate_stack_pointer(&value), 2);
    return value;
}

/* GATE.RTL:0xe5e3..0xe5fb, 0c33:00f2. */
uint16_t gate_rtl_read_resource_bytes(uint16_t handle, GateFarPtr destination, uint16_t count)
{
    return gate_linked_dos_file_read(handle, destination, count);
}

/* GATE.RTL:0xe5fc..0xe61d, 0c33:010b.  The original MUL discards the high
 * product word before invoking the byte reader.  The returned AX byte
 * count is then divided by the original element size, not clamped to the
 * requested count.  A zero divisor raises the guest's DIV exception. */
uint16_t gate_rtl_read_resource_elements(uint16_t handle, GateFarPtr destination,
                       uint16_t element_size, uint16_t count)
{
    uint16_t bytes = (uint16_t)(element_size * count);
    uint16_t result = gate_linked_dos_file_read(handle, destination, bytes);
    /* RTL:e617..e61c executes DIV with the original divisor directly.
     * In real mode this naturally raises INT 0 when it is zero.  The
     * host comparator keeps its explicit exception callback instead. */
#ifndef GATE_MSC51_ORIGINAL_FILE_ABI
    if (element_size == 0)
        gate_rtl_file_divide_fault();
#endif
    return (uint16_t)(result / element_size);
}

/* GATE.RTL:0xe61e..0xe636 and 0xe637..0xe64f.  Two distinct original
 * entry points contain identical one-WORD-on-stack write wrappers; both
 * pass a ONE-byte count, so only the low byte is consumed by the callee. */
uint16_t gate_rtl_write_resource_low_byte(uint16_t handle, uint16_t word)
{
    return gate_linked_dos_file_write(handle, gate_stack_pointer(&word), 1);
}

uint16_t gate_rtl_write_resource_low_byte_alternate(uint16_t handle, uint16_t word)
{
    return gate_linked_dos_file_write(handle, gate_stack_pointer(&word), 1);
}

/* GATE.RTL:0xe650..0xe668, 0c33:015f. */
uint16_t gate_rtl_write_resource_bytes(uint16_t handle, GateFarPtr source, uint16_t count)
{
    return gate_linked_dos_file_write(handle, source, count);
}

/* GATE.RTL:0xe669..0xe68a, 0c33:0178.  Matching element-sized write path. */
uint16_t gate_rtl_write_resource_elements(uint16_t handle, GateFarPtr source,
                       uint16_t element_size, uint16_t count)
{
    uint16_t bytes = (uint16_t)(element_size * count);
    uint16_t result = gate_linked_dos_file_write(handle, source, bytes);
    /* RTL:e684..e689 uses the same native 8086 DIV fault semantics. */
#ifndef GATE_MSC51_ORIGINAL_FILE_ABI
    if (element_size == 0)
        gate_rtl_file_divide_fault();
#endif
    return (uint16_t)(result / element_size);
}

/* GATE.RTL:0xe68b..0xe69a, 0c33:019a.  AX from the close call is not used
 * by the original resource-selector caller. */
void gate_rtl_close_resource_file(uint16_t handle)
{
    (void)gate_linked_dos_file_close(handle);
}

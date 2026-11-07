/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"

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
    GateFarPtr table = {gate_ds_word((uint16_t)(entry + GATE_RESOURCE_PATH_TABLE_SEGMENT_WORD_OFFSET)),
                        gate_ds_word(entry)};
    GateFarPtr base = {gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT), gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET)};
    GateFarPtr format = {ds, GATE_DS_RESOURCE_PATH_FORMAT};
    GateFarPtr pathname = gate_stack_pointer(path);

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
    uint16_t value = gate_rtl_file_stack_word();
    if (gate_linked_dos_file_read(handle, gate_stack_pointer(&value), 1) != 0)
        return value;
    return GATE_RESOURCE_FILE_BYTE_READ_FAILURE;
}

/* GATE.RTL:0xe5c5..0xe5e2, 0c33:00d4.  Unlike the byte reader above this
 * returns the original stack WORD unconditionally, even if read fails. */
uint16_t gate_rtl_read_resource_word(uint16_t handle)
{
    uint16_t value = gate_rtl_file_stack_word();
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
    if (element_size == 0)
        gate_rtl_file_divide_fault();
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
    if (element_size == 0)
        gate_rtl_file_divide_fault();
    return (uint16_t)(result / element_size);
}

/* GATE.RTL:0xe68b..0xe69a, 0c33:019a.  AX from the close call is not used
 * by the original resource-selector caller. */
void gate_rtl_close_resource_file(uint16_t handle)
{
    (void)gate_linked_dos_file_close(handle);
}

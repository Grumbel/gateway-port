/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_file.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static uint16_t ds_words[0x10000];
static void *stack_address;
static int stack_calls;
static int formatter_calls;
static int open_calls;
static int close_calls;
static int write_calls;
static int read_calls;
static int seek_calls;
static uint16_t expected_read_word = 0x5678;
static uint16_t stack_initial = 0xbeefu;
static uint16_t read_result = 1;
static uint16_t write_result = 2;
static uint16_t open_result = 17;
static uint16_t path_check_result = 0;
static uint16_t last_handle, last_origin, last_length, last_flags;
static uint32_t last_offset;
static GateFarPtr last_pointer;
static jmp_buf divide_fault_jump;
static int divide_faults;

static uint16_t read_zero_element_size(void)
{
    GateFarPtr destination = {0x1111, 0x2222};
    return gate_rtl_read_resource_elements(21, destination, 0, 77);
}

static uint16_t write_zero_element_size(void)
{
    GateFarPtr source = {0x2222, 0x3333};
    return gate_rtl_write_resource_elements(17, source, 0, 4);
}

uint16_t gate_ds_word(uint16_t offset) { return ds_words[offset]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds_words[offset] = value; }
uint16_t gate_ds_segment(void) { return 0x4567; }
GateFarPtr gate_stack_pointer(void *pointer)
{
    stack_address = pointer;
    ++stack_calls;
    return (GateFarPtr){0xf000, 0x0001};
}
uint16_t gate_rtl_file_stack_word(void) { return stack_initial; }
_Noreturn void gate_rtl_file_divide_fault(void)
{
    ++divide_faults;
    longjmp(divide_fault_jump, 1);
}

uint16_t gate_linked_open_runtime_path(GateFarPtr path, uint16_t code)
{
    assert(path.segment == 0x1234 && path.offset == 0x4567);
    assert(code == 4);
    return path_check_result;
}

void gate_linked_format_resource_filename(GateFarPtr output, GateFarPtr format,
                        GateFarPtr base, uint16_t resource, GateFarPtr table)
{
    ++formatter_calls;
    assert(output.segment == 0xf000 && output.offset == 1);
    assert(format.segment == 0x4567 && format.offset == 0x0faa);
    assert(base.segment == 0x4321 && base.offset == 0x8765);
    assert(resource == 42);
    assert(table.segment == 0x5566 && table.offset == 0x7788);
    assert(stack_address != NULL);
    memcpy(stack_address, "test", 5);
}

uint16_t gate_linked_dos_file_open(GateFarPtr filename, uint16_t flags)
{
    ++open_calls;
    assert(filename.segment == 0xf000 && filename.offset == 1);
    assert(strcmp((const char *)stack_address, "test") == 0);
    last_flags = flags;
    return open_result;
}

uint16_t gate_linked_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset)
{
    ++seek_calls;
    last_handle = handle;
    last_origin = origin;
    last_offset = offset;
    return 0xdead;
}

uint16_t gate_linked_dos_file_read(uint16_t handle, GateFarPtr destination,
                            uint16_t count)
{
    ++read_calls;
    last_handle = handle;
    last_pointer = destination;
    last_length = count;
    if (destination.segment == 0xf000 && read_result != 0) {
        if (count == 1) {
            *(unsigned char *)stack_address = (unsigned char)expected_read_word;
        } else if (count == 2) {
            memcpy(stack_address, &expected_read_word, 2);
        } else {
            assert(!"unsupported test write length");
        }
    }
    return read_result;
}

uint16_t gate_linked_dos_file_write(uint16_t handle, GateFarPtr source,
                            uint16_t count)
{
    ++write_calls;
    last_handle = handle;
    last_pointer = source;
    last_length = count;
    if (source.segment == 0xf000)
        assert(*(unsigned char *)stack_address == 0xab);
    return write_result;
}

uint16_t gate_linked_dos_file_close(uint16_t handle)
{
    ++close_calls;
    last_handle = handle;
    return 0x1234;
}

static void test_base_and_path(void)
{
    GateFarPtr path = {0x1234, 0x4567};
    gate_rtl_set_resource_path_base((GateFarPtr){0x4321, 0x8765});
    assert(ds_words[0x54c2] == 0x8765);
    assert(ds_words[0x54c4] == 0x4321);
    assert(gate_rtl_check_resource_path(path) == 1);
    path_check_result = 1;
    assert(gate_rtl_check_resource_path(path) == 0);
    path_check_result = 0xffff;
    assert(gate_rtl_check_resource_path(path) == 0);

    ds_words[0x0fb6 + 4 * 5] = 0x7788;
    ds_words[0x0fb8 + 4 * 5] = 0x5566;
    assert(gate_rtl_open_resource_file(5, 42) == 17);
    assert(formatter_calls == 1 && open_calls == 1);
    assert(last_flags == 0x80);
    ds_words[0x0fb6 + 4 * 4] = 0x7788;
    ds_words[0x0fb8 + 4 * 4] = 0x5566;
    open_result = 0;
    assert(gate_rtl_open_resource_file(4, 42) == 0);
    assert(formatter_calls == 2 && open_calls == 2);
    assert(last_flags == 0);
}

static void test_reads(void)
{
    gate_rtl_seek_resource_file(41, 0x12345678u);
    assert(seek_calls == 1 && last_handle == 41 && last_origin == 0);
    assert(last_offset == 0x12345678u);

    read_result = 1;
    assert(gate_rtl_read_resource_byte(6) == 0xbe78); /* byte read preserves stack high byte */
    assert(last_handle == 6 && last_length == 1);
    read_result = 0;
    assert(gate_rtl_read_resource_byte(6) == 0xffff); /* checked read failure */

    read_result = 2;
    assert(gate_rtl_read_resource_word(8) == expected_read_word);
    assert(last_handle == 8 && last_length == 2);
    read_result = 0;
    assert(gate_rtl_read_resource_word(8) == stack_initial); /* UNCHECKED read failure */

    assert(gate_rtl_read_resource_bytes(9, (GateFarPtr){0x2345, 0xabcd}, 0x1122) == 0);
    assert(last_handle == 9 && last_length == 0x1122);
    assert(last_pointer.segment == 0x2345 && last_pointer.offset == 0xabcd);
    assert(read_calls == 5);

    read_result = 19;
    assert(gate_rtl_read_resource_elements(21, (GateFarPtr){0x1111, 0x2222}, 4, 0x4001) == 4);
    assert(last_handle == 21 && last_length == 4); /* 32-bit product truncated */
    assert(last_pointer.segment == 0x1111 && last_pointer.offset == 0x2222);
    if (setjmp(divide_fault_jump) == 0) {
        (void)read_zero_element_size();
        assert(!"original DIV-by-zero did not fault");
    }
    assert(divide_faults == 1 && last_length == 0);
}

static void test_writes_and_close(void)
{
    assert(gate_rtl_write_resource_low_byte(13, 0x12ab) == 2);
    assert(write_calls == 1 && last_length == 1 && last_handle == 13);
    assert(gate_rtl_write_resource_low_byte_alternate(14, 0xffab) == 2);
    assert(write_calls == 2 && last_length == 1 && last_handle == 14);
    assert(gate_rtl_write_resource_bytes(15, (GateFarPtr){0x7654, 0x3210}, 67) == 2);
    assert(write_calls == 3 && last_length == 67 && last_handle == 15);
    assert(last_pointer.segment == 0x7654 && last_pointer.offset == 0x3210);
    write_result = 24;
    assert(gate_rtl_write_resource_elements(17, (GateFarPtr){0x2222, 0x3333}, 6, 0x2aab) == 4);
    assert(last_handle == 17 && last_length == 2);
    if (setjmp(divide_fault_jump) == 0) {
        (void)write_zero_element_size();
        assert(!"original DIV-by-zero did not fault");
    }
    assert(divide_faults == 2 && last_length == 0);
    assert(write_calls == 5);
    gate_rtl_close_resource_file(16);
    assert(close_calls == 1 && last_handle == 16);
    assert(stack_calls >= 6);
}

int main(void)
{
    test_base_and_path();
    test_reads();
    test_writes_and_close();
    puts("intro RTL file wrappers: ok");
    return 0;
}

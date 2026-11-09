/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_file.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static uint16_t ds_words[0x10000];
static uint8_t ds_bytes[0x10000];
static uint8_t stream_bytes[0x10000];
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
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    if (segment == 0x4567u)
        return ds_bytes[offset];
    assert(segment == 0x9876u);
    return stream_bytes[offset];
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    if (segment == 0x4567u) {
        ds_bytes[offset] = value;
        return;
    }
    assert(segment == 0x9876u);
    stream_bytes[offset] = value;
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
           ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
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

static void test_first_ctrl_break_stream_cleanup_quiet_branches(void)
{
    uint16_t offset = 0xfffdu; /* descriptor +0ah wraps to 0007h */
    uint16_t flag_offset = (uint16_t)(offset + GATE_CONTROL_STREAM_FLAGS_OFFSET);
    int prior_closes = close_calls;
    int prior_writes = write_calls;

    /* A negative signed flag takes RTL:1570b directly to RETF, without
     * even loading the stream descriptor. */
    ds_words[GATE_DS_CONTROL_STREAM_OPEN] = 0xffffu;
    ds_words[GATE_DS_CONTROL_STREAM_AUX] = 0xbeefu;
    ds_words[GATE_DS_CONTROL_STREAM_SEGMENT] = 0;
    gate_linked_shutdown_intro_stream();
    assert(ds_words[GATE_DS_CONTROL_STREAM_OPEN] == 0xffffu);
    assert(ds_words[GATE_DS_CONTROL_STREAM_AUX] == 0xbeefu);

    ds_words[GATE_DS_CONTROL_STREAM_SEGMENT] = 0x9876u;
    ds_words[GATE_DS_CONTROL_STREAM_OFFSET] = offset;
    ds_words[GATE_DS_CONTROL_STREAM_OPEN] = 0u;
    stream_bytes[flag_offset] = 0x04u; /* flags & 83h == 0 */
    gate_linked_shutdown_intro_stream();
    assert(stream_bytes[flag_offset] == 0u);
    assert(ds_words[GATE_DS_CONTROL_STREAM_OPEN] == 0xffffu);
    assert(ds_words[GATE_DS_CONTROL_STREAM_AUX] == 0u);

    ds_words[GATE_DS_CONTROL_STREAM_OPEN] = 0x7fffu;
    ds_words[GATE_DS_CONTROL_STREAM_AUX] = 0xabceu;
    stream_bytes[flag_offset] = 0xc3u; /* 40h bit bypasses active close */
    gate_linked_shutdown_intro_stream();
    assert(stream_bytes[flag_offset] == 0u);
    assert(ds_words[GATE_DS_CONTROL_STREAM_OPEN] == 0xffffu);
    assert(ds_words[GATE_DS_CONTROL_STREAM_AUX] == 0u);
    assert(close_calls == prior_closes && write_calls == prior_writes);

#ifndef _WIN32
    /* The busy descriptor would enter original CRT flushing and writing.
     * Never claim that the quiet-path wrapper handled it. */
    ds_words[GATE_DS_CONTROL_STREAM_OPEN] = 0u;
    stream_bytes[flag_offset] = 0x81u;
    {
        pid_t child = fork();
        int status = 0;
        assert(child >= 0);
        if (child == 0) {
            struct rlimit limit = {0, 0};
            (void)setrlimit(RLIMIT_CORE, &limit);
            gate_linked_shutdown_intro_stream();
            _exit(0);
        }
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
#endif
}

static void test_original_stream_flush_without_writer(void)
{
    GateFarPtr stream = {0x9876u, 0x0110u};
    uint16_t f = stream.offset;
    int old_writes = write_calls;
    int old_closes = close_calls;

    /* RTL:6eca skips the CRT writer for descriptor modes other than 2,
     * but still restores current FAR pointer and zeroes the count. */
    gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au), 0x01u);
    gate_far_set_word(stream.segment, f, 0xabcd);
    gate_far_set_word(stream.segment, (uint16_t)(f + 2u), 0x1000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 0x4321u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 6u), 0x4567u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 8u), 0x3456u);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x4567u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 2u)) == 0x3456u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);
    assert(gate_far_byte(stream.segment, (uint16_t)(f + 0x0au)) == 0x01u);

    /* Mode 2 with bit 08h set computes signed 16-bit cursor-base.
     * Equal and negative differences skip the CRT writer. */
    gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au), 0x0au);
    gate_far_set_word(stream.segment, f, 0x4567u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 0x7777u);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);

    gate_far_set_word(stream.segment, f, 0x4565u); /* signed -2 */
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 3u);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x4567u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);

    /* The two independently relocated operands are DS:3244/3334.
     * A 12-byte stream index selects the corresponding 6-byte file
     * record's first byte. Bit zero clear skips a write regardless of
     * the cursor-base difference. */
    stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS + GATE_CRT_STREAM_STRIDE);
    f = stream.offset;
    gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au), 0x02u);
    gate_far_set_word(stream.segment, f, 0x6000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 2u), 0x1111u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 0x4444u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 6u), 0x5000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 8u), 0x2222u);
    ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS + GATE_CRT_FILE_STRIDE] = 0u;
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x5000u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 2u)) == 0x2222u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);

    /* Bit zero set must consult pending data, including signed deltas. */
    ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS + GATE_CRT_FILE_STRIDE] = 1u;
    gate_far_set_word(stream.segment, f, 0x4ffeu);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x5000u);

    /* Original signed IDIV truncates a -4 descriptor displacement to
     * index zero, NOT index -1; that selects DS:3334 directly. */
    stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS - 4u);
    f = stream.offset;
    ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS] = 0u;
    gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au), 0x02u);
    gate_far_set_word(stream.segment, f, 0x7000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 6u), 0x5000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 8u), 0x3333u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 11u);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x5000u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);

    /* With -13 the same signed IDIV produces -1, selecting the record
     * preceding DS:3334; unsigned division would select another index. */
    stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS - 13u);
    f = stream.offset;
    ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS - GATE_CRT_FILE_STRIDE] = 0u;
    ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS] = 1u;
    gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au), 0x02u);
    gate_far_set_word(stream.segment, f, 0x7000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 6u), 0x5000u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 8u), 0x3333u);
    gate_far_set_word(stream.segment, (uint16_t)(f + 4u), 11u);
    assert(gate_linked_flush_intro_stream(stream) == 0u);
    assert(gate_far_word(stream.segment, f) == 0x5000u);
    assert(gate_far_word(stream.segment, (uint16_t)(f + 4u)) == 0u);
    assert(write_calls == old_writes && close_calls == old_closes);

#ifndef _WIN32
    /* Both write-needed branches fail closed.
     * Child failures must leave parent fixture intact. */
    {
        int branch;
        stream.offset = (uint16_t)(GATE_DS_CRT_STREAMS + GATE_CRT_STREAM_STRIDE);
        f = stream.offset;
        ds_bytes[GATE_DS_CRT_DESCRIPTOR_FLAGS + GATE_CRT_FILE_STRIDE] = 1u;
        for (branch = 0; branch < 2; ++branch) {
            pid_t child = fork();
            int status = 0;
            assert(child >= 0);
            if (child == 0) {
                struct rlimit limit = {0, 0};
                (void)setrlimit(RLIMIT_CORE, &limit);
                gate_far_set_byte(stream.segment, (uint16_t)(f + 0x0au),
                                  branch ? 0x02u : 0x0au);
                gate_far_set_word(stream.segment, f, 0x5001u);
                (void)gate_linked_flush_intro_stream(stream);
                _exit(0);
            }
            assert(waitpid(child, &status, 0) == child);
            assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        }
    }
#endif
}

int main(void)
{
    test_base_and_path();
    test_reads();
    test_writes_and_close();
    test_first_ctrl_break_stream_cleanup_quiet_branches();
    test_original_stream_flush_without_writer();
    puts("intro RTL file wrappers: ok");
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_image.h"
/* These tests must execute calls nested in assert in Release/Nix builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODE_SEG = 0x1234, HEADER_SEG = 0x2345, HANDLE_SEG = 0x3456,
    TABLE_SEG = 0x4567, PALETTE_SEG = 0x5678,
};
static uint8_t data[0x80], mode_data[0x100], header[0x100];
static uint8_t handle_data[0x100], table_data[0x100];
static uint16_t descriptor_result, open_result;
static unsigned word_reads;
static struct Event {
    char kind;
    uint16_t a, b;
    uint32_t dword;
} events[32];
static unsigned nevents;

static void record(char kind, uint16_t a, uint16_t b, uint32_t dword)
{
    struct Event *event;
    assert(nevents < sizeof events / sizeof events[0]);
    event = &events[nevents++];
    event->kind = kind;
    event->a = a;
    event->b = b;
    event->dword = dword;
}

static uint8_t *segment(uint16_t seg)
{
    switch (seg) {
    case MODE_SEG: return mode_data;
    case HEADER_SEG: return header;
    case HANDLE_SEG: return handle_data;
    case TABLE_SEG: return table_data;
    default: assert(!"unexpected guest segment"); return 0;
    }
}

uint8_t gate_far_byte(uint16_t seg, uint16_t offset)
{
    assert(offset < 0x100);
    return segment(seg)[offset];
}

uint16_t gate_far_word(uint16_t seg, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(seg, offset) |
                      ((uint16_t)gate_far_byte(seg, (uint16_t)(offset + 1u)) << 8));
}

void gate_far_set_byte(uint16_t seg, uint16_t offset, uint8_t value)
{
    assert(offset < 0x100);
    segment(seg)[offset] = value;
}

void gate_far_set_word(uint16_t seg, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(seg, offset, (uint8_t)value);
    gate_far_set_byte(seg, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}

uint16_t gate_rtl_resource_ds_word(uint16_t offset)
{
    assert(offset + 1u < sizeof data);
    return (uint16_t)(data[offset] | ((uint16_t)data[offset + 1u] << 8));
}

void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value)
{
    assert(offset + 1u < sizeof data);
    data[offset] = (uint8_t)value;
    data[offset + 1u] = (uint8_t)(value >> 8);
}

uint16_t gate_rtl_image_mode_segment(void)
{
    return gate_rtl_resource_ds_word(0x0a);
}

uint16_t gate_rtl_image_header_segment(void)
{
    return HEADER_SEG;
}

uint16_t gate_rtl_resource_handle_segment(void)
{
    return HANDLE_SEG;
}

uint16_t gate_rtl_resource_size_table_segment(void)
{
    return TABLE_SEG;
}

void gate_far_0c33_019a(uint16_t handle)
{
    record('C', handle, 0, 0);
}

uint16_t gate_far_0c33_0040(uint16_t zero, uint16_t file)
{
    assert(zero == 0);
    record('O', zero, file, 0);
    return open_result;
}

void gate_far_0c33_0091(uint16_t handle, uint32_t offset)
{
    record('S', handle, 0, offset);
}

uint16_t gate_far_0c33_00f2(uint16_t handle, uint32_t value, uint16_t count)
{
    assert(value == 0);
    record('R', handle, count, value);
    return descriptor_result;
}

uint16_t gate_far_0c33_00d4(uint16_t handle)
{
    record('W', handle, 0, 0);
    return word_reads++ == 0 ? MODE_SEG : PALETTE_SEG;
}

static void reset_case(uint8_t count, uint8_t flags, uint16_t video_mode)
{
    memset(data, 0, sizeof data);
    memset(mode_data, 0, sizeof mode_data);
    memset(header, 0, sizeof header);
    memset(handle_data, 0, sizeof handle_data);
    memset(table_data, 0, sizeof table_data);
    memset(events, 0, sizeof events);
    nevents = word_reads = 0;
    descriptor_result = 12;
    open_result = 0x44;
    gate_rtl_resource_ds_set_word(8, 0xffff);
    gate_rtl_resource_ds_set_word(0x0a, MODE_SEG);
    gate_far_set_word(MODE_SEG, 0, video_mode);
    gate_far_set_word(HEADER_SEG, 0, 0xfff0);
    gate_far_set_word(HEADER_SEG, 2, 0x1234);
    gate_far_set_word(HEADER_SEG, 6, 320);
    gate_far_set_word(HEADER_SEG, 8, 200);
    gate_far_set_byte(HEADER_SEG, 4, count);
    gate_far_set_byte(HEADER_SEG, 5, flags);
    gate_far_set_word(TABLE_SEG, 6, 0x5a);
}

static void expect_event(unsigned i, char kind, uint16_t a, uint16_t b,
                         uint32_t dword)
{
    assert(i < nevents);
    assert(events[i].kind == kind);
    assert(events[i].a == a && events[i].b == b);
    assert(events[i].dword == dword);
}

static void test_basic_resource_selection(void)
{
    reset_case(3, 0, 3);
    assert(gate_rtl_11ecb(0x0123, 2) == 1);
    assert(gate_rtl_resource_ds_word(8) == 101);
    assert(gate_far_word(HANDLE_SEG, 0) == 0x44);
    assert(gate_rtl_resource_ds_word(0x0a) == MODE_SEG);
    assert(gate_rtl_resource_ds_word(0x0c) == PALETTE_SEG);
    assert(nevents == 7);
    expect_event(0, 'O', 0, 101, 0);
    expect_event(1, 'S', 0x44, 0, 0x23u * 12u);
    expect_event(2, 'R', 0x44, 12, 0);
    expect_event(3, 'S', 0x44, 0, 0x1234fff0u + 4u);
    expect_event(4, 'W', 0x44, 0, 0);
    expect_event(5, 'W', 0x44, 0, 0);
    expect_event(6, 'S', 0x44, 0, 0x1234fff0u + 12u);
}

static void test_cached_handle_and_changed_file(void)
{
    reset_case(3, 0, 1);
    assert(gate_rtl_11ecb(0x0123, 1) == 1);
    nevents = 0;
    word_reads = 0;
    assert(gate_rtl_11ecb(0x0124, 1) == 1);
    assert(nevents == 6); /* same resource file; no close or open */
    assert(events[0].kind == 'S');
    assert(gate_rtl_resource_ds_word(8) == 101);

    /* Different resource-file id triggers close followed by reopen. */
    nevents = 0;
    assert(gate_rtl_11ecb(0x0324, 0) == 1);
    assert(events[0].kind == 'C' && events[0].a == 0x44);
    assert(events[1].kind == 'O' && events[1].b == 103);
    assert(gate_rtl_resource_ds_word(8) == 103);
}

static void test_open_failure_latches(void)
{
    reset_case(2, 0, 0);
    open_result = 0;
    assert(gate_rtl_11ecb(0x0123, 1) == 0);
    assert(nevents == 1 && events[0].kind == 'O' && events[0].b == 1);
    assert(gate_rtl_resource_ds_word(0) == 1);
    assert(gate_rtl_resource_ds_word(8) == 0xffff);
    assert(gate_rtl_11ecb(0x0123, 1) == 0 && nevents == 1);
}

static void test_descriptor_failures(void)
{
    reset_case(2, 0, 2);
    descriptor_result = 0;
    assert(gate_rtl_11ecb(0x0123, 2) == 0);
    assert(nevents == 3);

    reset_case(2, 0, 2);
    gate_far_set_word(HEADER_SEG, 0, 0);
    gate_far_set_word(HEADER_SEG, 2, 0);
    assert(gate_rtl_11ecb(0x0123, 2) == 0);
    assert(nevents == 3);

    reset_case(2, 0, 2);
    gate_far_set_word(HEADER_SEG, 6, 0);
    assert(gate_rtl_11ecb(0x0123, 2) == 0);
    assert(nevents == 7);
    reset_case(2, 0, 2);
    gate_far_set_word(HEADER_SEG, 8, 0);
    assert(gate_rtl_11ecb(0x0123, 2) == 0);
}

static void test_negative_and_capped_modes(void)
{
    reset_case(2, 0, 2);
    assert(gate_rtl_11ecb(0x0123, 16) == 1);
    expect_event(3, 'S', 0x44, 0, 0x1234fff0u + 4u);

    reset_case(2, 0, 2);
    assert(gate_rtl_11ecb(0x0123, 0xffff) == 1);
    assert(nevents == 4);
    assert(gate_rtl_resource_ds_word(0x0a) == 0);
    assert(gate_rtl_resource_ds_word(0x0c) == 0);

    reset_case(0xfe, 0, 2); /* signed header mode count -2 */
    assert(gate_rtl_11ecb(0x0123, 2) == 1);
    assert(nevents == 4);
    expect_event(3, 'S', 0x44, 0, 0x1234ffe8u);
}

static void test_compressed_format(void)
{
    reset_case(1, 0, 3);
    assert(gate_rtl_11ecb(0x8123, 1) == 1);
    assert(events[0].kind == 'O' && events[0].b == 301);

    reset_case(1, 0x13, 1);
    assert(gate_rtl_11ecb(0x8123, 1) == 1);
    assert(events[0].kind == 'O' && events[0].b == 101);
    assert(nevents == 8 && events[7].kind == 'R');
    assert(events[7].b == 0x5a);
    assert(gate_rtl_resource_ds_word(6) == 1);

    reset_case(1, 0x13, 1);
    gate_rtl_resource_ds_set_word(0x0e, 1);
    assert(gate_rtl_11ecb(0x8123, 1) == 0);
    assert(nevents == 3 && gate_rtl_resource_ds_word(6) == 0);

    reset_case(1, 0x13, 1);
    gate_rtl_resource_ds_set_word(0x10, 1);
    assert(gate_rtl_11ecb(0x8123, 1) == 1);
    assert(nevents == 6 && gate_rtl_resource_ds_word(6) == 0);
}

int main(void)
{
    test_basic_resource_selection();
    test_cached_handle_and_changed_file();
    test_open_failure_latches();
    test_descriptor_failures();
    test_negative_and_capped_modes();
    test_compressed_format();
    puts("original RTL resource cache, descriptor, formats, and 16-bit offsets: OK");
    return 0;
}

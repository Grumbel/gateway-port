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

enum { WORK_SEG = 0x4567, MODE_SEG = 0x1234, HEADER_SEG = 0x2345 };
static uint8_t work_bytes[0x10000], mode_bytes[0x10000], header_bytes[0x10000];
static GateFarPtr work = {WORK_SEG, 0xffed};
static char calls[8];
static unsigned ncalls;
static uint16_t resource_status, allocate_status, validate_status;
static uint16_t expected_resource, expected_mode, expected_width, expected_height;
static uint16_t expected_video_mode;

static void record(char c)
{
    assert(ncalls < sizeof calls);
    calls[ncalls++] = c;
}

static uint8_t *memory(uint16_t segment)
{
    switch (segment) {
    case WORK_SEG: return work_bytes;
    case MODE_SEG: return mode_bytes;
    case HEADER_SEG: return header_bytes;
    default: assert(!"unexpected segment"); return 0;
    }
}

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return memory(segment)[offset];
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
                      (uint16_t)(gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}

void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    memory(segment)[offset] = value;
}

void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}

static uint16_t work_word(uint16_t offset)
{
    return gate_far_word(work.segment, (uint16_t)(work.offset + offset));
}

static uint8_t work_byte(uint16_t offset)
{
    return gate_far_byte(work.segment, (uint16_t)(work.offset + offset));
}

static void set_work_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(work.segment, (uint16_t)(work.offset + offset), value);
}

uint16_t gate_rtl_image_mode_segment(void) { return MODE_SEG; }
uint16_t gate_rtl_image_palette_segment(void) { return 0x6789; }
uint16_t gate_rtl_image_header_segment(void) { return HEADER_SEG; }

uint16_t gate_rtl_11ecb(uint16_t resource, uint16_t mode)
{
    record('R');
    assert(resource == expected_resource && mode == expected_mode);
    return resource_status;
}

uint16_t gate_far_145d_00f7(uint16_t header_word8, uint16_t header_word6,
                             uint16_t video_mode, GateFarPtr destination,
                             uint16_t zero_flag)
{
    record('A');
    assert(header_word8 == expected_height);
    assert(header_word6 == expected_width);
    assert(video_mode == expected_video_mode && zero_flag == 0);
    assert(destination.segment == work.segment);
    assert(destination.offset == (uint16_t)(work.offset + 8u));
    /* The actual allocator fills these two words used by the later FAR
     * validation call; deliberately use recognizable test-only values. */
    set_work_word(0x20, 0xabcd);
    set_work_word(0x22, 0x4321);
    return allocate_status;
}

void gate_far_1479_0002(uint16_t zero_flag, GateFarPtr destination)
{
    record('I');
    assert(zero_flag == 0);
    assert(destination.segment == work.segment);
    assert(destination.offset == (uint16_t)(work.offset + 8u));
}

uint16_t gate_far_0eec_018a(uint16_t first, uint16_t second)
{
    record('S');
    assert(first == 0xabcd && second == 0x4321);
    assert(work_byte(0) == 1);
    return validate_status;
}

void gate_rtl_12205(GateFarPtr pointer)
{
    record('P');
    assert(pointer.segment == work.segment && pointer.offset == work.offset);
    assert(work_byte(0) == 1);
}

void gate_rtl_131cb(GateFarPtr pointer)
{
    record('C');
    assert(pointer.segment == work.segment && pointer.offset == work.offset);
    assert(work_byte(0) == 1);
    gate_far_set_byte(work.segment, work.offset, 0);
}

static void reset_case(uint16_t video_mode, uint16_t width, uint16_t height,
                       uint8_t flags)
{
    memset(work_bytes, 0xa5, sizeof work_bytes);
    memset(mode_bytes, 0, sizeof mode_bytes);
    memset(header_bytes, 0, sizeof header_bytes);
    memset(calls, 0, sizeof calls);
    ncalls = 0;
    expected_resource = 0xfedc;
    expected_mode = 0x8000;
    expected_width = width;
    expected_height = height;
    expected_video_mode = video_mode;
    resource_status = 1;
    allocate_status = 0;
    validate_status = 0;
    gate_far_set_word(MODE_SEG, 0, video_mode);
    gate_far_set_word(HEADER_SEG, 6, width);
    gate_far_set_word(HEADER_SEG, 8, height);
    gate_far_set_byte(HEADER_SEG, 5, flags);
    gate_far_set_byte(HEADER_SEG, 10, 0xd3);
    gate_far_set_byte(HEADER_SEG, 11, 0xe4);
    gate_far_set_byte(work.segment, work.offset, 0);
}

static void test_success_layout(uint16_t video_mode, uint16_t width,
                                uint8_t flags, uint16_t expected_stride)
{
    reset_case(video_mode, width, 0x2468, flags);
    assert(gate_rtl_12c33(work, expected_resource, expected_mode) == 1);
    assert(ncalls == 5 && memcmp(calls, "RAISP", 5) == 0);
    assert(work_byte(0) == 1);
    assert(work_byte(1) == ((flags & 0x40u) ? 3 : 0));
    assert(work_word(2) == MODE_SEG);
    assert(work_word(4) == 0x6789);
    assert(work_byte(6) == 0xd3 && work_byte(7) == 0xe4);
    assert(work_word(0x1c) == expected_stride);
    assert(work_word(0x20) == 0xabcd && work_word(0x22) == 0x4321);
}

static void test_previously_active(void)
{
    reset_case(3, 16, 12, 0x40);
    gate_far_set_byte(work.segment, work.offset, 0xff);
    assert(gate_rtl_12c33(work, expected_resource, expected_mode) == 0);
    assert(ncalls == 0 && work_byte(0) == 0xff);
    assert(work_byte(6) == 0xa5 && work_byte(7) == 0xa5);
}

static void test_failed_resource(void)
{
    reset_case(3, 16, 12, 0);
    resource_status = 0;
    assert(gate_rtl_12c33(work, expected_resource, expected_mode) == 0);
    assert(ncalls == 1 && memcmp(calls, "R", 1) == 0);
    assert(work_byte(0) == 0 && work_byte(6) == 0 && work_byte(7) == 0);
    assert(work_byte(1) == 0xa5);
}

static void test_failed_allocation(void)
{
    reset_case(1, 21, 12, 0);
    allocate_status = 0x100;
    assert(gate_rtl_12c33(work, expected_resource, expected_mode) == 0);
    assert(ncalls == 2 && memcmp(calls, "RA", 2) == 0);
    assert(work_byte(0) == 0 && work_byte(6) == 0 && work_byte(7) == 0);
}

static void test_failed_validation(void)
{
    reset_case(3, 16, 12, 0);
    validate_status = 0xffff;
    assert(gate_rtl_12c33(work, expected_resource, expected_mode) == 0);
    assert(ncalls == 5 && memcmp(calls, "RAISC", 5) == 0);
    assert(work_byte(0) == 0); /* cleanup */
    assert(work_word(0x1c) == 16);
}

int main(void)
{
    /* Offsets cross FFFF:0000 inside one segment. A host flat-pointer
     * implementation would fail these unmodified 8086 access patterns. */
    test_previously_active();
    test_failed_resource();
    test_failed_allocation();
    test_failed_validation();
    test_success_layout(3, 0x1234, 0x40, 0x1234);
    test_success_layout(1, 17, 0, 3);
    test_success_layout(2, 0xffff, 0x40, 0);
    test_success_layout(2, 0x7ff9, 0, 4096); /* 8086 -32768 overflow */
    test_success_layout(2, 0xeffa, 0, (uint16_t)-511);
    puts("original RTL image resource loader, failures, mode stride and FAR wrapping: OK");
    return 0;
}

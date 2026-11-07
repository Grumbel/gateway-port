/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const GateFarPtr work_address = {0x1234, 0xfffcu};
static uint8_t *native_record;
static uint8_t guest_record[0x10000];
static char events[128];
static unsigned event_count;
static uint16_t loaded[16], loaded_mode[16];
static unsigned load_count, draw_count, cleanup_count, prepare_count;
static uint32_t clocks[16];
static unsigned clock_count, clock_index;
static uint16_t polls[16];
static unsigned poll_count, poll_index;
static uint16_t random_result;
static uint16_t fail_at_resource;
static unsigned splash_fades;

static void note(char id)
{
    assert(event_count < sizeof events);
    events[event_count++] = id;
}

static void reset(void)
{
    memset(events, 0, sizeof events);
    memset(guest_record, 0, sizeof guest_record);
    memset(loaded, 0, sizeof loaded);
    memset(loaded_mode, 0, sizeof loaded_mode);
    memset(clocks, 0, sizeof clocks);
    memset(polls, 0, sizeof polls);
    event_count = load_count = draw_count = cleanup_count = 0;
    prepare_count = clock_count = clock_index = poll_count = poll_index = 0;
    splash_fades = 0;
    native_record = 0;
    random_result = 0;
    fail_at_resource = 0xdead;
}

GateFarPtr gate_stack_pointer(void *ptr)
{
    native_record = ptr;
    return work_address;
}

uint16_t gate_rtl_anim_segment_22(void) { return 0x0101; }
uint16_t gate_rtl_anim_segment_24(void) { return 0x0102; }
uint16_t gate_rtl_anim_segment_26(void) { return 0x0103; }

uint16_t gate_far_word(uint16_t seg, uint16_t offset)
{
    if (seg == 0x0101) { assert(offset == 0x4c); return 0x2233; }
    if (seg == 0x0102) { assert(offset == 0x42); return 0x4455; }
    assert(seg == 0x0103 && offset == 0x3e);
    return 0x6677;
}

uint8_t gate_far_byte(uint16_t seg, uint16_t offset)
{
    assert(seg == work_address.segment);
    return guest_record[offset];
}

void gate_rtl_fade_palette_out(void) { note('f'); }
void gate_rtl_fade_palette_in(void) { note('I'); ++splash_fades; }

void gate_linked_set_intro_pen_color(uint16_t value)
{
    note('A');
    assert(value == 0x2233);
}

void gate_linked_fill_intro_rectangle(uint16_t two, uint16_t zero_a, uint16_t zero_b,
                        uint16_t value_b, uint16_t value_a)
{
    note('B');
    assert(two == 2 && zero_a == 0 && zero_b == 0);
    assert(value_b == 0x6677 && value_a == 0x4455);
}

uint16_t gate_rtl_load_picture_record(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    note('L');
    assert(work.segment == work_address.segment &&
           work.offset == work_address.offset);
    assert(native_record);
    assert(load_count < sizeof loaded / sizeof loaded[0]);
    loaded[load_count] = resource;
    loaded_mode[load_count] = mode;
    ++load_count;
    if (resource == fail_at_resource)
        return 0;
    native_record[0] = 1;
    guest_record[(uint16_t)(work.offset + 6u)] = 1;
    guest_record[(uint16_t)(work.offset + 7u)] = 0;
    return 1;
}

void gate_rtl_draw_picture_record(GateFarPtr work)
{
    note('D');
    assert(work.segment == work_address.segment &&
           work.offset == work_address.offset);
    ++draw_count;
}

void gate_rtl_release_picture_record(GateFarPtr work)
{
    note('C');
    assert(work.segment == work_address.segment &&
           work.offset == work_address.offset);
    assert(native_record);
    native_record[0] = 0;
    ++cleanup_count;
}

void gate_rtl_prepare_picture_display(void) { note('P'); ++prepare_count; }

uint32_t gate_linked_animation_elapsed_milliseconds(void)
{
    note('T');
    assert(clock_index < clock_count);
    return clocks[clock_index++];
}

uint16_t gate_linked_runtime_random(void)
{
    note('R');
    return random_result;
}

uint16_t gate_linked_poll_animation_input(void)
{
    note('K');
    assert(poll_index < poll_count);
    return polls[poll_index++];
}

static void test_empty_and_negative_first(void)
{
    reset();
    gate_rtl_play_picture_delta_sequence(0xffff, 0x3456, -1);
    assert(event_count == 4 && memcmp(events, "fABC", 4) == 0);
    assert(load_count == 0 && cleanup_count == 1);
    reset();
    gate_rtl_play_picture_delta_sequence(0x8000, 0x3456, 0);
    assert(event_count == 4 && memcmp(events, "fABC", 4) == 0);
}

static void test_two_frames_and_deadline_wait(void)
{
    static const char expected[] = "fAB" "LDIC" "LTPDCTR" "LTKTPDCTR" "C";
    reset();
    clocks[0] = 10;
    clocks[1] = 20;
    clocks[2] = 40;
    clocks[3] = 80;
    clocks[4] = 90;
    clock_count = 5;
    polls[0] = 1;
    poll_count = 1;
    random_result = 3;
    gate_rtl_play_picture_delta_sequence(0x3001, 0x3221, 2);
    assert(load_count == 3);
    assert(loaded[0] == 0x3001 && loaded_mode[0] == 0);
    assert(loaded[1] == 0x3221 && loaded_mode[1] == 1);
    assert(loaded[2] == 0x3222 && loaded_mode[2] == 1);
    assert(splash_fades == 1 && prepare_count == 2);
    assert(draw_count == 3 && cleanup_count == 4);
    assert(clock_index == 5 && poll_index == 1);
    assert(event_count == sizeof expected - 1);
    assert(memcmp(events, expected, event_count) == 0);
}

static void test_input_early_exit(void)
{
    static const char expected[] = "fAB" "LTPDCTR" "LTKC";
    reset();
    clocks[0] = 10;
    clocks[1] = 20;
    clocks[2] = 40;
    clock_count = 3;
    polls[0] = 0;
    poll_count = 1;
    gate_rtl_play_picture_delta_sequence(0xffff, 0xffff, 2);
    assert(load_count == 2 && loaded[0] == 0xffff && loaded[1] == 0);
    assert(draw_count == 1 && cleanup_count == 2 && prepare_count == 1);
    assert(clock_index == 3 && poll_index == 1);
    assert(event_count == sizeof expected - 1);
    assert(memcmp(events, expected, event_count) == 0);
}

static void test_signed_high_deadline_and_failed_load(void)
{
    reset();
    clocks[0] = 0x80000000u; /* signed high negative, deadline high 0 */
    clock_count = 1;
    polls[0] = 0;
    poll_count = 1;
    gate_rtl_play_picture_delta_sequence(0xffff, 0x3300, 1);
    assert(event_count == 7 && memcmp(events, "fABLTKC", 7) == 0);
    assert(draw_count == 0 && cleanup_count == 1);

    reset();
    fail_at_resource = 0x3456;
    gate_rtl_play_picture_delta_sequence(0xffff, 0x3456, 4);
    assert(event_count == 5 && memcmp(events, "fABLC", 5) == 0);
    assert(load_count == 1 && draw_count == 0 && cleanup_count == 1);

    reset();
    fail_at_resource = 0x3001;
    gate_rtl_play_picture_delta_sequence(0x3001, 0x3456, 0);
    assert(event_count == 8 && memcmp(events, "fABLDICC", 8) == 0);
    assert(splash_fades == 1 && draw_count == 1 && cleanup_count == 2);
}

int main(void)
{
    test_empty_and_negative_first();
    test_two_frames_and_deadline_wait();
    test_input_early_exit();
    test_signed_high_deadline_and_failed_load();
    puts("original RTL intro sequence, clock, input and resource order: OK");
    return 0;
}

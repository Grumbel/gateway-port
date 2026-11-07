/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t ram[0x10000];
static const uint16_t ram_segment = 0x2345;
static uint16_t origin_x, origin_y;
static uint16_t saved_return, clock_value;
static uint16_t saved_arg, draw_zero, draw_y, draw_x, draw_mode;
static GateFarPtr drawn_pixels;
static uint32_t last_delay;
static uint32_t delays[8];
static GateFarPtr released[8];
static unsigned draw_count, delay_count, release_count;
static char events[64];
static unsigned event_count;

static void record(char id)
{
    assert(event_count < sizeof events);
    events[event_count++] = id;
}

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == ram_segment);
    return ram[offset];
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    assert(segment == ram_segment);
    return (uint16_t)(ram[offset] |
        ((uint16_t)ram[(uint16_t)(offset + 1u)] << 8));
}

void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == ram_segment);
    ram[offset] = value;
}

static void set_word(uint16_t offset, uint16_t value)
{
    ram[offset] = (uint8_t)value;
    ram[(uint16_t)(offset + 1u)] = (uint8_t)(value >> 8);
}

uint16_t gate_rtl_resource_ds_word(uint16_t offset)
{
    assert(offset == 2 || offset == 4);
    return offset == 2 ? origin_x : origin_y;
}

uint16_t gate_linked_save_vfx_draw_state(void)
{
    record('S');
    return saved_return;
}

void gate_linked_blit_vfx_picture(uint16_t zero, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr pixels)
{
    record('D');
    ++draw_count;
    draw_zero = zero;
    draw_y = y;
    draw_x = x;
    draw_mode = signed_mode;
    drawn_pixels = pixels;
}

void gate_linked_restore_vfx_draw_state(uint16_t saved)
{
    record('R');
    saved_arg = saved;
}

uint16_t gate_linked_runtime_random(void)
{
    record('C');
    return clock_value;
}

void gate_linked_wait_animation_delay(uint32_t delay)
{
    record('W');
    assert(delay_count < sizeof delays / sizeof delays[0]);
    delays[delay_count] = delay;
    ++delay_count;
    last_delay = delay;
}

void gate_rtl_release_picture_record(GateFarPtr work)
{
    assert(release_count < sizeof released / sizeof released[0]);
    record('F');
    released[release_count++] = work;
}

static void reset(void)
{
    memset(ram, 0, sizeof ram);
    memset(events, 0, sizeof events);
    event_count = draw_count = delay_count = release_count = 0;
    origin_x = origin_y = 0;
    saved_return = clock_value = 0;
    last_delay = 0;
}

static void test_draw_inactive_and_active_wrap(void)
{
    GateFarPtr work = {ram_segment, 0xfffcu};
    reset();
    gate_rtl_draw_picture_record(work);
    assert(event_count == 0);

    ram[0xfffc] = 1;
    ram[0xfffd] = 0xfe;
    set_word(0xfffe, 0xffff);
    set_word(0x0000, 0x8001);
    origin_x = 2;
    origin_y = 0x8000;
    saved_return = 0xabcd;
    gate_rtl_draw_picture_record(work);
    assert(event_count == 3 && memcmp(events, "SDR", 3) == 0);
    assert(draw_count == 1);
    assert(draw_zero == 0 && draw_x == 1 && draw_y == 1);
    assert(draw_mode == 0xfffe);
    assert(drawn_pixels.segment == ram_segment && drawn_pixels.offset == 4);
    assert(saved_arg == 0xabcd);
}

static void test_display_array_signed_count_and_wrap(void)
{
    GateFarPtr work = {ram_segment, 0xff80};
    reset();
    ram[0xff80] = ram[0x0008] = 1;
    ram[0x0090] = 0;
    gate_rtl_draw_picture_record_sequence(work, (int16_t)0xffff);
    gate_rtl_draw_picture_record_sequence(work, 0);
    assert(event_count == 0);
    gate_rtl_draw_picture_record_sequence(work, 3);
    assert(event_count == 6 && memcmp(events, "SDRSDR", 6) == 0);
    assert(draw_count == 2);
    assert(drawn_pixels.offset == 0x0010 && drawn_pixels.segment == ram_segment);
}

static void test_timed_array(void)
{
    GateFarPtr work = {ram_segment, 0xff80};
    reset();
    gate_rtl_play_timed_picture_records(work, -1);
    assert(event_count == 0);
    ram[0xff80] = 1;
    ram[0xff86] = 255;
    ram[0xff87] = 255;
    ram[0x0008] = 0; /* inactive records still trigger the clock + wait */
    ram[0x000e] = 10;
    ram[0x000f] = 0;
    clock_value = 65535;
    gate_rtl_play_timed_picture_records(work, 2);
    assert(event_count == 7 && memcmp(events, "SDRCW", 5) == 0);
    assert(memcmp(events + 5, "CW", 2) == 0);
    assert(draw_count == 1 && delay_count == 2);
    assert(delays[0] == (65535u % 12751u) + 12750u);
    assert(last_delay == 500);
}

static void test_clear_and_release_array(void)
{
    GateFarPtr work = {ram_segment, 0xff80};
    reset();
    ram[0xff80] = 4;
    ram[0x0008] = 5;
    ram[0x0090] = 6;
    gate_rtl_initialize_picture_record_array(work, -2);
    assert(ram[0xff80] == 4);
    gate_rtl_initialize_picture_record_array(work, 3);
    assert(ram[0xff80] == 0 && ram[0x0008] == 0 && ram[0x0090] == 0);
    assert(event_count == 0);

    gate_rtl_release_picture_record_array((GateFarPtr){0, 0}, 2);
    gate_rtl_release_picture_record_array(work, -1);
    assert(release_count == 0);
    gate_rtl_release_picture_record_array(work, 3);
    assert(event_count == 3 && memcmp(events, "FFF", 3) == 0);
    assert(release_count == 3);
    assert(released[0].offset == 0xff80);
    assert(released[1].offset == 0x0008);
    assert(released[2].offset == 0x0090);
    for (unsigned i = 0; i < release_count; ++i)
        assert(released[i].segment == ram_segment);
}

int main(void)
{
    test_draw_inactive_and_active_wrap();
    test_display_array_signed_count_and_wrap();
    test_timed_array();
    test_clear_and_release_array();
    puts("original RTL image processor and record-array helpers: OK");
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_exe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t ds[65536];
static uint8_t palette[65536];
static unsigned palette_writes, palette_uploads, key_reads, keyboard_flushes;
static unsigned message_calls, clear_calls, center_calls, print_calls, special_calls;
static uint16_t keyboard_pending, keyboard_key, mouse_flags;
static uint32_t clock_now, clock_step;
static int16_t center_x, center_y;

static void reset(void)
{
    memset(ds, 0, sizeof ds);
    memset(palette, 0, sizeof palette);
    palette_writes = palette_uploads = key_reads = keyboard_flushes = 0;
    message_calls = clear_calls = center_calls = print_calls = special_calls = 0;
    keyboard_pending = keyboard_key = mouse_flags = 0;
    clock_now = 0;
    clock_step = 1000;
}

uint16_t gate_ds_word(uint16_t offset) { return ds[offset]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds[offset] = value; }
uint16_t gate_ds_segment(void) { return 0x1234; }
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x2222 && offset == 0x54) return 12;
    if (segment == 0x3333 && offset == 0x3e) return 80;
    if (segment == 0x4444 && offset == 0x42) return 25;
    if (segment == 0x5555 && offset == 0x0fce) return 0;
    assert(!"unexpected far memory read");
    return 0;
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == 0x6666);
    palette[offset] = value;
    ++palette_writes;
}
uint16_t gate_far_0df2_000a(void) { return keyboard_pending; }
uint16_t gate_far_0df2_0030(void) { ++key_reads; return keyboard_key; }
void gate_far_0df2_00f8(void) { ++keyboard_flushes; }
uint16_t gate_far_0e48_07fe(void) { return mouse_flags; }
uint32_t gate_far_02a5_38c4(void) { clock_now += clock_step; return clock_now; }
void gate_far_123f_0004(void) { ++special_calls; }
void gate_far_0c4d_018d(uint16_t value) { assert(value == 1); ++clear_calls; }
void gate_far_0c4d_061b(uint16_t handle, uint16_t mode)
{
    assert(handle == 12 && mode == 0xffff);
    ++message_calls;
}
uint16_t gate_far_0c4d_05be(GateFarPtr string)
{
    assert(string.segment == 0x1234 && string.offset == 0x35fe);
    return 16;
}
uint16_t gate_far_0c4d_0511(void) { return 5; }
void gate_far_0c4d_0653(int16_t x, int16_t y)
{
    center_x = x;
    center_y = y;
    ++center_calls;
}
void gate_far_0c4d_0698(GateFarPtr string)
{
    assert(string.segment == 0x1234 && string.offset == 0x35fe);
    ++print_calls;
}
void gate_far_0cdc_04d2(GateFarPtr ptr)
{
    assert(ptr.segment == 0x1a7a && ptr.offset == 0x0fdc);
    ++palette_uploads;
}

static void test_wait(void)
{
    reset();
    clock_step = 1;
    gate_exe_0f18(3);
    assert(clock_now == 4);

    reset();
    keyboard_pending = 1;
    clock_step = 1;
    gate_exe_0f18(2000);
    assert(clock_now == 2);

    reset();
    mouse_flags = 1; /* wait treats any nonzero mouse status as input */
    clock_step = 1;
    gate_exe_0f18(2000);
    assert(clock_now == 2);

    reset();
    clock_now = 0x7ffffffd;
    clock_step = 1;
    gate_exe_0f18(3);
    /* Deliberately preserve the ORIGINAL signed-high-word comparison. */
    assert(clock_now == 0x7fffffff);
}

static void test_bar_fade(void)
{
    reset();
    ds[0x4e42] = 0x6666;
    gate_exe_1c11(6);
    assert(palette_uploads == 64 && palette_writes == 64);
    assert(palette[0x0fdc + 45 + 3 * 6] == 63);
    assert(clock_now >= 500);

    reset();
    keyboard_pending = 1;
    gate_exe_1c11(6);
    assert(palette_uploads == 0 && palette_writes == 0 && clock_now == 0);
}

static void test_credit_fade(void)
{
    reset();
    ds[0x4e42] = 0x6666;
    gate_exe_1c6c();
    assert(palette_writes == 192 && palette_uploads == 64);
    assert(palette[0x1021] == 63 && palette[0x1022] == 63 &&
           palette[0x1023] == 63);
    assert(clock_now == 0);
}

static void test_input(void)
{
    reset();
    mouse_flags = 2;
    assert(gate_exe_0e37() == 1);
    mouse_flags = 1;
    assert(gate_exe_0e37() == 0);

    reset();
    keyboard_pending = 1;
    keyboard_key = 27;
    assert(gate_exe_0e37() == 1 && ds[0x35fc] == 0);
    keyboard_key = 3;
    assert(gate_exe_0e37() == 0 && special_calls == 1);

    reset();
    keyboard_pending = 1;
    keyboard_key = 'x';
    ds[0x4e40] = 0x2222;
    ds[0x4e30] = 0x3333;
    ds[0x4e2e] = 0x4444;
    ds[0x4e2a] = 0x5555;
    assert(gate_exe_0e37() == 0);
    assert(ds[0x35fc] == 1);
    assert(center_x == 32 && center_y == 19);
    assert(message_calls == 1 && clear_calls == 1 &&
           center_calls == 1 && print_calls == 1 && keyboard_flushes == 1);
    assert(gate_exe_0e37() == 0);
    assert(message_calls == 1 && keyboard_flushes == 1);
}

int main(void)
{
    test_wait();
    test_bar_fade();
    test_credit_fade();
    test_input();
    puts("GATE.EXE C decompilation: four source-shaped routines verified by mocks");
    return 0;
}

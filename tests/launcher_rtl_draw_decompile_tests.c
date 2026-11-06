/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_draw.h"
#include "gate_rtl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t ds[0x10000];
static uint16_t pen;
static unsigned pen_calls, setup_calls, rectangle_calls;
static uint16_t fill_args[3], rectangle_args[5];

uint16_t gate_ds_word(uint16_t offset) { return ds[offset]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds[offset] = value; }
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x4321 && offset == 0x0054);
    return ds[0x1000];
}
void gate_rtl_resident_set_pen_color(uint16_t color)
{
    pen = color;
    ++pen_calls;
}
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    fill_args[0] = pattern;
    fill_args[1] = color;
    fill_args[2] = transparent;
    ++setup_calls;
}
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left)
{
    rectangle_args[0] = style;
    rectangle_args[1] = bottom;
    rectangle_args[2] = right;
    rectangle_args[3] = top;
    rectangle_args[4] = left;
    ++rectangle_calls;
}

static void check_color(void)
{
    ds[0x4da8] = 0x4321;
    ds[0x1000] = 15;
    ds[0x5512] = 3;
    assert(gate_rtl_fbf5(12) == 3);
    assert(ds[0x5512] == 12 && pen == 12 && pen_calls == 1);
    assert(gate_rtl_fbf5(31) == 12);
    assert(ds[0x5512] == 15 && pen == 15 && pen_calls == 2);
    /* Original CMP/JLE is signed, so ffff is left unchanged. */
    assert(gate_rtl_fbf5(0xffffu) == 15);
    assert(ds[0x5512] == 0xffffu && pen == 0xffffu);
    ds[0x1000] = 0x00ff;
    assert(gate_rtl_fbf5(0x0100) == 0xffffu);
    assert(pen == 0x00ff && pen_calls == 4);
}

static void check_pattern_and_rectangle(void)
{
    gate_rtl_fc23(0);
    assert(ds[0x12de] == 0 && ds[0x12e0] == 0);
    gate_rtl_fc23(3);
    assert(ds[0x12de] == 3 && ds[0x12e0] == 1);
    gate_rtl_fc23(0xffffu);
    assert(ds[0x12de] == 0xffffu && ds[0x12e0] == 1);

    ds[0x5512] = 7;
    gate_rtl_fc23(2);
    gate_rtl_fc37(2, 5, 3, 20, 30);
    assert(setup_calls == 1 && rectangle_calls == 1);
    assert(fill_args[0] == 2 && fill_args[1] == 7 && fill_args[2] == 1);
    assert(memcmp(rectangle_args, (const uint16_t[]){2, 30, 20, 3, 5},
                  sizeof rectangle_args) == 0);

    /* Test bit 7 of the low style byte and signed top clamp to -1. */
    gate_rtl_fc37(0x0182, 12, 0xfffcu, 15, 19);
    assert(setup_calls == 2 && rectangle_calls == 2);
    assert(memcmp(fill_args, (const uint16_t[]){10, 7, 1},
                  sizeof fill_args) == 0);
    assert(memcmp(rectangle_args,
                  (const uint16_t[]){0x0182, 19, 15, 0xffffu, 12},
                  sizeof rectangle_args) == 0);
    /* Bit 15 alone does not activate the special fill branch. */
    gate_rtl_fc37(0x8002, 3, 0xffffu, 7, 8);
    assert(setup_calls == 3 && rectangle_calls == 3);
    assert(fill_args[0] == 2 && fill_args[1] == 7 && fill_args[2] == 1);
    assert(rectangle_args[3] == 0xffffu);
}

int main(void)
{
    check_color();
    check_pattern_and_rectangle();
    puts("original RTL draw wrappers: passed");
    return 0;
}

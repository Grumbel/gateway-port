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
struct DrawOperation { char kind; uint16_t a, b; };
static struct DrawOperation draw_ops[96];
static unsigned draw_count;

static void record(char kind, uint16_t a, uint16_t b)
{
    assert(draw_count < sizeof draw_ops / sizeof draw_ops[0]);
    draw_ops[draw_count++] = (struct DrawOperation){kind, a, b};
}

uint16_t gate_ds_word(uint16_t offset) { return ds[offset]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds[offset] = value; }
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x4321);
    assert(offset == 0x54 || offset == 0x4c || offset == 0x4e ||
           offset == 0x50 || offset == 0x52);
    return ds[0x1000 + offset];
}
void gate_rtl_resident_set_pen_color(uint16_t color)
{
    pen = color;
    ++pen_calls;
    record('C', color, 0);
}
void gate_rtl_resident_move_to(uint16_t x, uint16_t y)
{
    record('M', x, y);
}
void gate_rtl_resident_line_to(uint16_t x, uint16_t y)
{
    record('L', x, y);
}
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent)
{
    fill_args[0] = pattern;
    fill_args[1] = color;
    fill_args[2] = transparent;
    ++setup_calls;
    record('S', pattern, color);
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
    record('R', style, left);
}

static void check_color(void)
{
    ds[0x4da8] = 0x4321;
    ds[0x1054] = 15;
    ds[0x5512] = 3;
    assert(gate_rtl_set_pen_color(12) == 3);
    assert(ds[0x5512] == 12 && pen == 12 && pen_calls == 1);
    assert(gate_rtl_set_pen_color(31) == 12);
    assert(ds[0x5512] == 15 && pen == 15 && pen_calls == 2);
    /* Original CMP/JLE is signed, so ffff is left unchanged. */
    assert(gate_rtl_set_pen_color(0xffffu) == 15);
    assert(ds[0x5512] == 0xffffu && pen == 0xffffu);
    ds[0x1054] = 0x00ff;
    assert(gate_rtl_set_pen_color(0x0100) == 0xffffu);
    assert(pen == 0x00ff && pen_calls == 4);
}

static void clear_events(void)
{
    draw_count = 0;
    memset(draw_ops, 0, sizeof draw_ops);
}

static void check_events(const struct DrawOperation *events, size_t count)
{
    size_t i;
    assert(draw_count == count);
    for (i = 0; i < count; ++i) {
        assert(draw_ops[i].kind == events[i].kind);
        assert(draw_ops[i].a == events[i].a);
        assert(draw_ops[i].b == events[i].b);
    }
}

static void check_frame(void)
{
    static const struct DrawOperation mode3_bevel[] = {
        {'C', 5, 0}, {'S', 0, 5}, {'R', 1, 20},
        {'C', 6, 0}, {'S', 0, 6}, {'R', 2, 21},
        {'C', 9, 0},
        {'M', 21, 58}, {'L', 21, 31}, {'L', 48, 31},
        {'M', 22, 57}, {'L', 22, 32}, {'L', 47, 32},
        {'C', 2, 0},
        {'M', 21, 59}, {'L', 49, 59}, {'L', 49, 31},
        {'M', 22, 58}, {'L', 48, 58}, {'L', 48, 32},
    };
    static const struct DrawOperation mode0_bevel[] = {
        {'C', 9, 0}, {'S', 0, 9}, {'R', 1, 20},
        {'M', 21, 30}, {'L', 21, 60},
        {'M', 49, 30}, {'L', 49, 60},
    };
    static const struct DrawOperation plain[] = {
        {'C', 5, 0},
        {'M', 21, 30}, {'L', 49, 30},
        {'M', 21, 60}, {'L', 49, 60},
        {'M', 50, 31}, {'L', 50, 59},
        {'M', 20, 31}, {'L', 20, 59},
    };
    static const struct DrawOperation mode3_shadow[] = {
        {'C', 5, 0}, {'S', 0, 5}, {'R', 1, 20},
        {'C', 2, 0},
        {'M', 19, 61}, {'L', 19, 29}, {'L', 51, 29},
        {'M', 18, 62}, {'L', 18, 29},
        {'M', 18, 28}, {'L', 52, 28},
        {'C', 9, 0},
        {'M', 20, 61}, {'L', 51, 61}, {'L', 51, 30},
        {'M', 19, 62}, {'L', 52, 62}, {'L', 52, 29},
    };
    static const struct DrawOperation mode1_bevel[] = {
        {'C', 5, 0}, {'S', 0, 5}, {'R', 1, 20},
        {'C', 6, 0}, {'S', 0, 6}, {'R', 2, 21},
        {'C', 9, 0},
        {'M', 21, 58}, {'L', 21, 31}, {'L', 48, 31},
        {'M', 22, 57}, {'L', 22, 32},
        {'C', 2, 0},
        {'M', 21, 59}, {'L', 49, 59}, {'L', 49, 31},
        {'L', 48, 58}, {'L', 48, 32},
    };
    ds[0x4da0] = ds[0x4da2] = ds[0x4da4] = ds[0x4da6] = 0x4321;
    ds[0x1054] = 15;
    ds[0x104c] = 5;
    ds[0x104e] = 2;
    ds[0x1050] = 6;
    ds[0x1052] = 9;
    gate_rtl_set_fill_pattern(0);
    ds[0x0fce] = 3;
    clear_events();
    gate_rtl_draw_caption_frame(20, 30, 50, 60, 4);
    check_events(mode3_bevel, sizeof mode3_bevel / sizeof mode3_bevel[0]);

    ds[0x0fce] = 0;
    clear_events();
    gate_rtl_draw_caption_frame(20, 30, 50, 60, 4);
    check_events(mode0_bevel, sizeof mode0_bevel / sizeof mode0_bevel[0]);

    ds[0x0fce] = 3;
    clear_events();
    gate_rtl_draw_caption_frame(20, 30, 50, 60, 2);
    check_events(plain, sizeof plain / sizeof plain[0]);

    clear_events();
    gate_rtl_draw_caption_frame(20, 30, 50, 60, 8);
    check_events(mode3_shadow,
                 sizeof mode3_shadow / sizeof mode3_shadow[0]);

    ds[0x0fce] = 1;
    clear_events();
    gate_rtl_draw_caption_frame(20, 30, 50, 60, 4);
    check_events(mode1_bevel,
                 sizeof mode1_bevel / sizeof mode1_bevel[0]);
}

static void check_pattern_and_rectangle(void)
{
    gate_rtl_set_fill_pattern(0);
    assert(ds[0x12de] == 0 && ds[0x12e0] == 0);
    gate_rtl_set_fill_pattern(3);
    assert(ds[0x12de] == 3 && ds[0x12e0] == 1);
    gate_rtl_set_fill_pattern(0xffffu);
    assert(ds[0x12de] == 0xffffu && ds[0x12e0] == 1);

    ds[0x5512] = 7;
    gate_rtl_set_fill_pattern(2);
    gate_rtl_fill_rectangle(2, 5, 3, 20, 30);
    assert(setup_calls == 1 && rectangle_calls == 1);
    assert(fill_args[0] == 2 && fill_args[1] == 7 && fill_args[2] == 1);
    assert(memcmp(rectangle_args, (const uint16_t[]){2, 30, 20, 3, 5},
                  sizeof rectangle_args) == 0);

    /* Test bit 7 of the low style byte and signed top clamp to -1. */
    gate_rtl_fill_rectangle(0x0182, 12, 0xfffcu, 15, 19);
    assert(setup_calls == 2 && rectangle_calls == 2);
    assert(memcmp(fill_args, (const uint16_t[]){10, 7, 1},
                  sizeof fill_args) == 0);
    assert(memcmp(rectangle_args,
                  (const uint16_t[]){0x0182, 19, 15, 0xffffu, 12},
                  sizeof rectangle_args) == 0);
    /* Bit 15 alone does not activate the special fill branch. */
    gate_rtl_fill_rectangle(0x8002, 3, 0xffffu, 7, 8);
    assert(setup_calls == 3 && rectangle_calls == 3);
    assert(fill_args[0] == 2 && fill_args[1] == 7 && fill_args[2] == 1);
    assert(rectangle_args[3] == 0xffffu);
}

int main(void)
{
    check_color();
    check_pattern_and_rectangle();
    check_frame();
    puts("original RTL draw wrappers: passed");
    return 0;
}

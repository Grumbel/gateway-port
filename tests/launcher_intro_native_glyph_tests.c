/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original video/text-mode and opaque rectangle services are separate
 * link boundaries. The tested graphics-font cases have background=-1 and
 * therefore cannot call a rectangle service. */
void gate_rtl_background_setup(uint16_t a, uint16_t b, uint16_t c)
{
    (void)a; (void)b; (void)c;
    abort();
}
void gate_rtl_background_rect(uint16_t a, uint16_t b, uint16_t c,
                              uint16_t d, uint16_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    abort();
}
void gate_rtl_f997(uint16_t x, uint16_t y)
{
    (void)x; (void)y;
    abort();
}
void gate_rtl_fa45(GateFarPtr text) { (void)text; abort(); }
void gate_rtl_f959(uint16_t *x, uint16_t *y)
{
    (void)x; (void)y;
    abort();
}

static void set_far_pointer(uint16_t segment, uint16_t offset,
                            GateFarPtr target)
{
    gate_far_set_word(segment, offset, target.offset);
    gate_far_set_word(segment, (uint16_t)(offset + 2), target.segment);
}

static GateFarPtr read_far_pointer(uint16_t segment, uint16_t offset)
{
    GateFarPtr result;
    result.offset = gate_far_word(segment, offset);
    result.segment = gate_far_word(segment, (uint16_t)(offset + 2));
    return result;
}

static void configure_video(uint16_t mode, uint16_t width, uint16_t height)
{
    gate_ds_set_word(0x15cc, mode);
    gate_ds_set_word(0x29de + 0x16, 0);
    gate_ds_set_word(0x29de + 0x18, 1);
    gate_ds_set_word(0x29de + 0x1a, 0);
    gate_ds_set_word(0x29de + 0x1c, 0);
    gate_ds_set_word(0x29de + 0x1e, (uint16_t)(width - 1));
    gate_ds_set_word(0x29de + 0x20, (uint16_t)(height - 1));
}

static void check_video_pixel_modes(void)
{
    configure_video(2, 640, 480);
    gate_rtl_plot_pixel(0x0d, 5, 7);
    gate_rtl_plot_pixel(0x06, 5, 8);
    assert(gate_intro_vga16_pixel(7, 5) == 13);
    assert(gate_intro_vga16_pixel(8, 5) == 6);
    gate_ds_set_word(0x29de + 0x16, 3);
    gate_rtl_plot_pixel(0x0f, 5, 7);
    assert(gate_intro_vga16_pixel(7, 5) == (13 ^ 15));
    gate_ds_set_word(0x29de + 0x16, 1);
    gate_rtl_plot_pixel(0x03, 5, 8);
    assert(gate_intro_vga16_pixel(8, 5) == (6 & 3));
    gate_rtl_plot_pixel(0x0f, 65535u, 7);
    gate_rtl_plot_pixel(0x0f, 5, 640);
    assert(gate_intro_vga16_pixel(7, 5) == (13 ^ 15));
    assert(gate_intro_vga16_pixel(639, 5) == 0);

    configure_video(3, 320, 200);
    gate_rtl_plot_pixel(0x4f, 5, 7);
    assert(gate_far_byte(0xa000, 5 * 320 + 7) == 0x4f);
    gate_ds_set_word(0x29de + 0x16, 3);
    gate_rtl_plot_pixel(0x33, 5, 7);
    assert(gate_far_byte(0xa000, 5 * 320 + 7) == (0x4f ^ 0x33));
    gate_ds_set_word(0x29de + 0x16, 2);
    gate_rtl_plot_pixel(0x80, 5, 7);
    assert(gate_far_byte(0xa000, 5 * 320 + 7) ==
           ((0x4f ^ 0x33) | 0x80));
    gate_rtl_plot_pixel(0xff, 200, 7);
    assert(gate_far_byte(0xa000, 5 * 320 + 7) ==
           ((0x4f ^ 0x33) | 0x80));
}

static void check_relocated_text_scalars(void)
{
    const uint16_t descriptor = 0x2a70;
    gate_ds_set_word(0x4d82, descriptor);
    gate_ds_set_word(0x4d86, descriptor);
    gate_ds_set_word(0x4d8a, descriptor);
    gate_ds_set_word(0x4d90, descriptor);
    gate_far_set_word(descriptor, 0x34, 11);
    gate_far_set_word(descriptor, 0x3a, 24);
    gate_far_set_word(descriptor, 0x3c, 8);
    gate_far_set_word(descriptor, 0x3e, 319);
    assert(gate_rtl_tab_advance() == 24);
    assert(gate_rtl_tab_pitch() == 24);
    assert(gate_rtl_text_mode_pitch() == 8);
    assert(gate_rtl_tab_limit() == 319);
    assert(gate_rtl_newline_pitch() == 11);
}

static void check_synthetic_glyph(void)
{
    const uint16_t font_segment = 0x2400;
    const uint16_t font_offset = 0x100;
    const GateFarPtr widths = {0x2500, 0x200};
    const GateFarPtr spacing = {0x2600, 0x300};
    const GateFarPtr bitmap = {0x2700, 0x400};
    static const uint8_t rows[] = {0xa8, 0x50, 0xf8};
    unsigned row, column;
    configure_video(2, 640, 480);
    gate_ds_set_word(0x54ca, font_offset);
    gate_ds_set_word(0x54cc, font_segment);
    gate_far_set_byte(font_segment, font_offset + 3, 5);
    gate_far_set_byte(font_segment, font_offset + 6, 1);
    gate_far_set_byte(font_segment, font_offset + 7, 'A');
    gate_far_set_byte(font_segment, font_offset + 8, 'A');
    gate_far_set_byte(font_segment, font_offset + 9, 1);
    gate_far_set_byte(font_segment, font_offset + 10, 3);
    gate_far_set_byte(font_segment, font_offset + 11, 0xff);
    gate_far_set_byte(font_segment, font_offset + 12, 0xff);
    set_far_pointer(font_segment, font_offset + 14, widths);
    set_far_pointer(font_segment, font_offset + 18, spacing);
    set_far_pointer(font_segment, font_offset + 26, bitmap);
    gate_far_set_byte(widths.segment, widths.offset + 'A', 5);
    gate_far_set_byte(spacing.segment, spacing.offset + 'A', 0x12);
    for (row = 0; row < 3; ++row)
        gate_far_set_byte(bitmap.segment, bitmap.offset + row, rows[row]);

    gate_rtl_set_cursor(20, 30);
    gate_rtl_set_colors(0x0d, -1);
    gate_rtl_edff('A');
    assert(gate_rtl_cursor_x() == 28 && gate_rtl_cursor_y() == 30);
    for (row = 0; row < 3; ++row)
        for (column = 0; column < 7; ++column) {
            const uint8_t expected = column >= 1 && column <= 5 &&
                (rows[row] & (0x80u >> (column - 1))) ? 13 : 0;
            assert(gate_intro_vga16_pixel((uint16_t)(20 + column),
                                         (uint16_t)(30 + row)) == expected);
        }
}

/* Every character below exercises the *real* live-linked font descriptors
 * and compressed bitmaps resident in the 22s DOS capture. Expected pixels
 * are computed independently from font header and bitmask data. */
static void check_original_glyphs(const char *capture)
{
    static const unsigned chars[] = {'A', 'Z', 'a', 'g', '0', '?', '!'};
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    uint8_t expected[320 * 200] = {0};
    GateFarPtr font, width_table, spacing_table, bitmap;
    uint8_t first, last, height, row_bytes;
    FILE *file;
    unsigned i, row, col;

    assert(memory);
    file = fopen(capture, "rb");
    assert(file);
    assert(fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, file) ==
           GATE_INTRO_RAM_BYTES);
    assert(fclose(file) == 0);
    memory->ds_segment = 0x2a7a; /* capture's original recorded DS */
    gate_intro_memory_attach(memory);
    assert(gate_ds_word(0x15cc) == 3);
    font = gate_rtl_active_font();
    assert(gate_far_byte(font.segment, font.offset + 3) != 0);
    assert((int8_t)gate_far_byte(font.segment, font.offset + 11) < 0);
    assert((int8_t)gate_far_byte(font.segment, font.offset + 12) < 0);
    first = gate_far_byte(font.segment, font.offset + 7);
    last = gate_far_byte(font.segment, font.offset + 8);
    height = gate_far_byte(font.segment, font.offset + 10);
    row_bytes = gate_far_byte(font.segment, font.offset + 6);
    width_table = read_far_pointer(font.segment, font.offset + 14);
    spacing_table = read_far_pointer(font.segment, font.offset + 18);
    bitmap = read_far_pointer(font.segment, font.offset + 26);
    assert(first <= '!' && last >= 'g' && height > 0 && row_bytes > 0);

    memset(memory->bytes + 0xa0000, 0, sizeof expected);
    gate_rtl_set_colors(15, -1);
    gate_ds_set_word(0x29de + 0x16, 0);
    for (i = 0; i < sizeof chars / sizeof chars[0]; ++i) {
        uint16_t x = (uint16_t)(10 + i * 42);
        uint16_t y = 40;
        uint8_t ch = chars[i];
        uint8_t width = gate_far_byte(width_table.segment,
                                      (uint16_t)(width_table.offset + ch));
        uint8_t spacing = gate_far_byte(spacing_table.segment,
                                        (uint16_t)(spacing_table.offset + ch));
        uint16_t advance = (uint16_t)(width + (spacing >> 4) + (spacing & 15));
        uint16_t glyph_offset = (uint16_t)((ch - first) * row_bytes * height);

        assert(x + advance < 320 && y + height < 200);
        gate_rtl_set_cursor((int16_t)x, (int16_t)y);
        gate_rtl_edff(ch);
        assert(gate_rtl_cursor_x() == (int16_t)(x + advance));
        for (row = 0; row < height; ++row)
            for (col = 0; col < width; ++col) {
                uint16_t source = (uint16_t)(bitmap.offset + glyph_offset +
                                            row * row_bytes + col / 8u);
                uint8_t bits = gate_far_byte(bitmap.segment, source);
                if ((bits & (0x80u >> (col & 7u))) != 0)
                    expected[(y + row) * 320 + x + (spacing >> 4) + col] = 15;
            }
    }
    for (i = 0; i < sizeof expected; ++i)
        assert(memory->bytes[0xa0000 + i] == expected[i]);
    gate_intro_memory_detach();
    free(memory);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    assert(memory);
    memory->ds_segment = 0x2a7a;
    gate_intro_memory_attach(memory);
    check_video_pixel_modes();
    check_relocated_text_scalars();
    check_synthetic_glyph();
    gate_intro_memory_detach();
    free(memory);
    if (argc > 1)
        check_original_glyphs(argv[1]);
    puts("native VGA pixel modes, relocated text scalars, and original glyphs: OK");
    return 0;
}

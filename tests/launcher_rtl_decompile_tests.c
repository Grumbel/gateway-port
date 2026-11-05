/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { FONT_SEG = 0x1111, WIDTH_SEG = 0x2222,
       SPACING_SEG = 0x3333, TEXT_SEG = 0x4444, BITMAP_SEG = 0x5555 };

static uint8_t font[65536], widths[65536], spacing[65536], text[65536], bitmap[65536];
static uint16_t tab_advance;
static uint16_t video_set_x, video_set_y, video_print_count;
static int16_t cursor_x, cursor_y, video_cursor_x, video_cursor_y, video_pitch;
static int16_t tab_limit, newline_pitch, background_color;
static uint16_t ink_color;
static uint16_t background_setup_args[3], background_rect_args[5];
static unsigned background_setups, background_rects, pixel_count;
struct Pixel { uint16_t color, y, x; };
static struct Pixel pixels[1024];

static void put16(uint8_t *mem, uint16_t off, uint16_t value)
{
    mem[off] = (uint8_t)value;
    mem[(uint16_t)(off + 1)] = (uint8_t)(value >> 8);
}

static void initialize(void)
{
    memset(font, 0, sizeof font);
    memset(widths, 0, sizeof widths);
    memset(spacing, 0, sizeof spacing);
    memset(text, 0, sizeof text);
    memset(bitmap, 0, sizeof bitmap);
    video_print_count = 0;
    background_setups = background_rects = pixel_count = 0;
    tab_advance = 6;
    tab_limit = 99;
    newline_pitch = 11;
    background_color = -1;
    ink_color = 0x23;
    cursor_x = cursor_y = video_cursor_x = video_cursor_y = 0;
    video_set_x = video_set_y = 0;
    video_pitch = 10;
    /* Font descriptor +14/+18 are FAR pointers, not direct byte arrays. */
    put16(font, 14, 0x0100);
    put16(font, 16, WIDTH_SEG);
    put16(font, 18, 0x0200);
    put16(font, 20, SPACING_SEG);
    put16(font, 26, 0x1000);
    put16(font, 28, BITMAP_SEG);
    font[3] = 5;
    font[6] = 1;
    font[7] = 33;
    font[8] = 127;
    font[9] = 1;
    font[10] = 10;
    font[11] = 0xff;
    font[12] = 0xff;
}

static uint8_t *memory(uint16_t segment)
{
    switch (segment) {
    case FONT_SEG: return font;
    case WIDTH_SEG: return widths;
    case SPACING_SEG: return spacing;
    case TEXT_SEG: return text;
    case BITMAP_SEG: return bitmap;
    }
    assert(!"unknown segment");
    return font;
}

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return memory(segment)[offset];
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    uint8_t *bytes = memory(segment);
    return (uint16_t)(bytes[offset] | ((uint16_t)bytes[(uint16_t)(offset+1)] << 8));
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    put16(memory(segment), offset, value);
}

GateFarPtr gate_rtl_active_font(void)
{
    GateFarPtr ptr = { FONT_SEG, 0 };
    return ptr;
}

uint16_t gate_rtl_tab_advance(void) { return tab_advance; }
void gate_rtl_set_cursor(int16_t x, int16_t y)
{
    cursor_x = x;
    cursor_y = y;
}

int16_t gate_rtl_cursor_x(void) { return cursor_x; }
int16_t gate_rtl_cursor_y(void) { return cursor_y; }
int16_t gate_rtl_text_mode_pitch(void) { return video_pitch; }
void gate_rtl_video_command(uint16_t function, uint8_t *input, uint8_t *output)
{
    assert(function == 0x10 && input == output);
    switch (input[1]) {
    case 2:
        video_set_x = input[6];
        video_set_y = input[7];
        video_cursor_x = (int16_t)video_set_x;
        video_cursor_y = (int16_t)video_set_y;
        break;
    case 3:
        output[6] = (uint8_t)video_cursor_x;
        output[7] = (uint8_t)video_cursor_y;
        break;
    case 9:
        ++video_print_count;
        break;
    default:
        assert(!"unknown original video command");
    }
}
uint16_t gate_rtl_video_block_mode(void) { return 0; }
void gate_rtl_video_set_block_mode(uint16_t mode) { assert(mode == 0); }
uint16_t gate_rtl_video_inversion(void) { return 0; }
uint8_t gate_rtl_video_attribute(void) { return 0x07; }
int16_t gate_rtl_video_columns(void) { return 80; }
int16_t gate_rtl_video_rows(void) { return 25; }
int16_t gate_rtl_video_tab_base(void) { return 60; }

int16_t gate_rtl_tab_pitch(void) { return 8; }
int16_t gate_rtl_tab_limit(void) { return tab_limit; }
int16_t gate_rtl_newline_pitch(void) { return newline_pitch; }
int16_t gate_rtl_background_color(void) { return background_color; }
uint16_t gate_rtl_ink_color(void) { return ink_color; }
void gate_rtl_set_colors(uint16_t ink, int16_t background)
{
    ink_color = ink;
    background_color = background;
}
void gate_rtl_background_setup(uint16_t arg0, uint16_t arg1, uint16_t arg2)
{
    background_setup_args[0] = arg0;
    background_setup_args[1] = arg1;
    background_setup_args[2] = arg2;
    ++background_setups;
}
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left)
{
    background_rect_args[0] = style;
    background_rect_args[1] = bottom;
    background_rect_args[2] = right;
    background_rect_args[3] = top;
    background_rect_args[4] = left;
    ++background_rects;
}
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x)
{
    assert(pixel_count < sizeof pixels / sizeof pixels[0]);
    pixels[pixel_count].color = color;
    pixels[pixel_count].y = y;
    pixels[pixel_count].x = x;
    ++pixel_count;
}

static void test_font_advances(void)
{
    initialize();
    widths[0x0100 + 'A'] = 3;
    spacing[0x0200 + 'A'] = 0x21;
    assert(gate_rtl_ebf7() == 11);
    assert(gate_rtl_ec0a('A') == 6);
    spacing[0x0200 + 'A'] = 0xf1;
    assert(gate_rtl_ec0a('A') == 19);
    assert(gate_rtl_ec0a(9) == 6 && gate_rtl_ec0a(10) == 0);

    /* CBW in the string-width caller sign-extends high-bit characters. */
    widths[0x00ff] = 2;
    spacing[0x01ff] = 0x10;
    assert(gate_rtl_ec0a(0xffff) == 3);

    /* Fixed spacing with per-character widths (font+11 signed negative). */
    font[12] = 2;
    assert(gate_rtl_ec0a('A') == 5);
    font[11] = 0xfd;
    /* Neither signed byte is negative only when both flags are nonnegative. */
    font[11] = 4;
    assert(gate_rtl_ec0a('A') == 6);
    font[9] = 0xfe;
    font[10] = 4;
    assert(gate_rtl_ebf7() == 2);
}

static void test_original_color_and_cursor_accessors(void)
{
    GateFarPtr first = { TEXT_SEG, 0x0100 };
    GateFarPtr second = { TEXT_SEG, 0x0104 };
    GateFarPtr null = { 0, 0 };
    initialize();
    gate_rtl_ed01(0x03ab, -1);
    assert(gate_rtl_ink_color() == 0x03ab && gate_rtl_background_color() == -1);
    gate_rtl_ecea(first, second);
    assert(gate_far_word(TEXT_SEG, 0x0100) == 0x03ab);
    assert(gate_far_word(TEXT_SEG, 0x0104) == 0xffff);
    gate_rtl_ed39(-17, 276);
    gate_rtl_ed12(first, null);
    assert(gate_far_word(TEXT_SEG, 0x0100) == (uint16_t)-17);
    assert(gate_far_word(TEXT_SEG, 0x0104) == 0xffff);
    gate_rtl_ed12(null, second);
    assert(gate_far_word(TEXT_SEG, 0x0104) == 276);
}

static void test_string_width_and_offset_wrap(void)
{
    GateFarPtr ptr = { TEXT_SEG, 0xfffe };
    initialize();
    font[11] = 2;
    font[12] = 3;
    text[0xfffe] = 'A';
    text[0xffff] = 'B';
    text[0] = 10;   /* width 0; same segment after 16-bit offset wrap */
    text[1] = 9;    /* width 6, not the ordinary font's 5 */
    text[2] = 0;
    assert(gate_rtl_eca4(ptr) == 16);
    assert(gate_exe_0bb8(ptr).segment == TEXT_SEG);
    assert(gate_exe_0bb8(ptr).offset == 0xfffe);

    /* Advance adds in AX, not an arbitrarily wide host integer. */
    font[11] = 0x80;
    font[12] = 1;
    widths[0x0100 + 'A'] = 0x80;
    widths[0x0100 + 'B'] = 0x80;
    text[0] = 0;
    assert(gate_rtl_eca4(ptr) == 0xff02);
}

static void test_text_cursor_and_printing(void)
{
    GateFarPtr ptr = { TEXT_SEG, 0x0100 };
    initialize();
    text[0x0100] = 'A';
    text[0x0101] = 0x80;
    text[0x0102] = 0;
    font[10] = 2;
    widths[0x0100 + 'A'] = 1;
    spacing[0x0200 + 'A'] = 0x10;
    bitmap[0x1000 + ('A' - 33) * 2] = 0x80;
    bitmap[0x1000 + ('A' - 33) * 2 + 1] = 0x80;
    gate_rtl_ed39(-9, 40);
    assert(cursor_x == -9 && cursor_y == 40 && video_set_x == 0);
    gate_rtl_ed7e(ptr);
    assert(video_print_count == 0);
    assert(pixel_count == 2 && pixels[0].x == (uint16_t)-8 &&
           pixels[0].y == 40 && pixels[1].x == (uint16_t)-8 &&
           pixels[1].y == 41 && pixels[1].color == 0x23);
    assert(cursor_x == -7 && cursor_y == 40);

    /* In text-mode fonts, y uses IDIV, x uses the original sign/SAR. */
    font[3] = 0;
    gate_rtl_ed39(-9, -25);
    assert(cursor_x == -9 && cursor_y == -25);
    assert(video_set_x == 0xff && video_set_y == 0xfe);
    gate_rtl_ed39((int16_t)0x8000, 0);
    assert(video_set_x == 0 && video_set_y == 0);
    video_cursor_x = 2;
    video_cursor_y = 3;
    gate_rtl_ed7e(ptr);
    assert(video_print_count == 2 && pixel_count == 2);
    assert(cursor_x == 32 && cursor_y == 30);
}

static void test_original_glyph_bit_scan(void)
{
    initialize();
    font[10] = 2;
    widths[0x0100 + 'A'] = 5;
    spacing[0x0200 + 'A'] = 0x21;
    bitmap[0x1000 + ('A' - 33) * 2] = 0xa8;
    bitmap[0x1000 + ('A' - 33) * 2 + 1] = 0x50;
    gate_rtl_set_cursor(8, 9);
    gate_rtl_edff('A');
    assert(pixel_count == 5 && cursor_x == 16 && cursor_y == 9);
    assert(pixels[0].x == 10 && pixels[0].y == 9);
    assert(pixels[1].x == 12 && pixels[1].y == 9);
    assert(pixels[2].x == 14 && pixels[2].y == 9);
    assert(pixels[3].x == 11 && pixels[3].y == 10);
    assert(pixels[4].x == 13 && pixels[4].y == 10);
    for (unsigned i = 0; i < pixel_count; ++i)
        assert(pixels[i].color == ink_color);

    /* No bitmap for an out-of-range character, but its width/spacing
     * advance is still applied. Background rect precedes bearing. */
    pixel_count = 0;
    background_color = 4;
    widths[0x0100 + 'A'] = 2;
    gate_rtl_set_cursor(8, 9);
    font[7] = 'B';
    gate_rtl_edff('A');
    assert(pixel_count == 0 && cursor_x == 13 && cursor_y == 9);
    assert(background_setups == 1 && background_rects == 1);
    assert(background_setup_args[0] == 0 && background_setup_args[1] == 4 &&
           background_setup_args[2] == 0);
    assert(background_rect_args[0] == 2 && background_rect_args[1] == 19 &&
           background_rect_args[2] == 12 && background_rect_args[3] == 9 &&
           background_rect_args[4] == 8);
}

static void test_tab_newline_and_bitmap_offset_wrap(void)
{
    initialize();
    gate_rtl_set_cursor(13, 7);
    gate_rtl_edff(9);
    assert(cursor_x == 16 && cursor_y == 7);
    gate_rtl_edff(10);
    assert(cursor_x == 0 && cursor_y == 18);
    gate_rtl_set_cursor(96, 7);
    gate_rtl_edff(9);
    assert(cursor_x == 0 && cursor_y == 18); /* tab exceeds width => newline */

    put16(font, 26, 0xffff);
    font[7] = 'A';
    font[10] = 2;
    widths[0x0100 + 'A'] = 1;
    bitmap[0xffff] = 0x80;
    bitmap[0] = 0x80;
    gate_rtl_set_cursor(3, 4);
    gate_rtl_edff('A');
    assert(pixel_count == 2 && pixels[0].x == 3 && pixels[1].x == 3 &&
           pixels[0].y == 4 && pixels[1].y == 5);
    assert(cursor_x == 4 && cursor_y == 4);
}

static void test_original_intro_font(const char *directory)
{
    char filename[4096];
    uint8_t file[4096];
    size_t count;
    FILE *input;

    if (!directory) return;
    assert(snprintf(filename, sizeof filename, "%s/GATE_305.FNT", directory) > 0);
    input = fopen(filename, "rb");
    if (!input) return; /* original assets are optional in host builds */
    count = fread(file, 1, sizeof file, input);
    assert(!ferror(input));
    fclose(input);
    assert(count > 266 && file[5] == 1 && file[6] == 10 &&
           file[7] == 0xff && file[8] == 0xff);

    initialize();
    font[9] = file[5];
    font[10] = file[6];
    font[11] = file[7];
    font[12] = file[8];
    memcpy(widths + 0x0100, file + 10, 128);
    memcpy(spacing + 0x0200, file + 138, 128);
    memcpy(bitmap + 0x1000, file + 266, count - 266);
    assert(gate_rtl_ebf7() == 11);
    for (unsigned ch = 33; ch <= 126; ++ch) {
        unsigned expected = (unsigned)widths[0x0100 + ch] +
                            (unsigned)(spacing[0x0200 + ch] & 15) +
                            (unsigned)(spacing[0x0200 + ch] >> 4);
        assert(gate_rtl_ec0a((uint16_t)ch) == expected);

        /* Independent expected bit positions from GATE_305.FNT, not the
         * reconstructed glyph routine's host pixel callbacks. */
        pixel_count = 0;
        gate_rtl_set_cursor(20, 40);
        gate_rtl_edff((uint16_t)ch);
        assert(cursor_x == (int16_t)(20 + expected) && cursor_y == 40);
        unsigned total = 0;
        for (unsigned row = 0; row < file[6]; ++row) {
            for (unsigned col = 0; col < file[10 + ch]; ++col) {
                unsigned glyph_offset = 266 + (ch - file[3]) *
                                        (unsigned)file[2] * file[6] +
                                        row * file[2] + col / 8;
                assert(glyph_offset < count);
                if (file[glyph_offset] & (0x80u >> (col % 8))) {
                    assert(total < pixel_count);
                    assert(pixels[total].color == ink_color);
                    assert(pixels[total].x == 20 + (spacing[0x0200 + ch] >> 4) + col);
                    assert(pixels[total].y == 40 + row);
                    ++total;
                }
            }
        }
        assert(pixel_count == total);
    }
}

int main(int argc, char **argv)
{
    test_font_advances();
    test_original_color_and_cursor_accessors();
    test_string_width_and_offset_wrap();
    test_text_cursor_and_printing();
    test_original_glyph_bit_scan();
    test_tab_newline_and_bitmap_offset_wrap();
    test_original_intro_font(argc > 1 ? argv[1] : NULL);
    puts("launcher RTL source C: metrics, string width, cursor, print pass passed");
    return 0;
}

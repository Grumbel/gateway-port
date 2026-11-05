/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { FONT_SEG = 0x1111, WIDTH_SEG = 0x2222,
       SPACING_SEG = 0x3333, TEXT_SEG = 0x4444 };

static uint8_t font[65536], widths[65536], spacing[65536], text[65536];
static uint16_t normalize_count, glyph_count, tab_advance;
static uint16_t glyphs[16];
static uint16_t video_set_x, video_set_y, video_print_count;
static int16_t cursor_x, cursor_y, video_cursor_x, video_cursor_y, video_pitch;

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
    glyph_count = normalize_count = video_print_count = 0;
    tab_advance = 6;
    cursor_x = cursor_y = video_cursor_x = video_cursor_y = 0;
    video_set_x = video_set_y = 0;
    video_pitch = 10;
    /* Font descriptor +14/+18 are FAR pointers, not direct byte arrays. */
    put16(font, 14, 0x0100);
    put16(font, 16, WIDTH_SEG);
    put16(font, 18, 0x0200);
    put16(font, 20, SPACING_SEG);
    font[3] = 5;
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

GateFarPtr gate_rtl_active_font(void)
{
    GateFarPtr ptr = { FONT_SEG, 0 };
    return ptr;
}

uint16_t gate_rtl_tab_advance(void) { return tab_advance; }
GateFarPtr gate_rtl_normalize_pointer(GateFarPtr ptr)
{
    ++normalize_count;
    return ptr;
}

void gate_rtl_set_cursor(int16_t x, int16_t y)
{
    cursor_x = x;
    cursor_y = y;
}

int16_t gate_rtl_cursor_x(void) { return cursor_x; }
int16_t gate_rtl_cursor_y(void) { return cursor_y; }
int16_t gate_rtl_text_mode_pitch(void) { return video_pitch; }
void gate_rtl_text_mode_set_cursor(uint16_t x, uint16_t y)
{
    video_set_x = x;
    video_set_y = y;
}
void gate_rtl_text_mode_get_cursor(int16_t *x, int16_t *y)
{
    *x = video_cursor_x;
    *y = video_cursor_y;
}
void gate_rtl_text_mode_print(GateFarPtr ptr)
{
    assert(ptr.segment == TEXT_SEG);
    ++video_print_count;
}

void gate_rtl_edff(uint16_t character)
{
    assert(glyph_count < sizeof glyphs / sizeof glyphs[0]);
    glyphs[glyph_count++] = character;
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
    assert(gate_rtl_eca4(ptr) == 16 && normalize_count == 1);

    /* Advance adds in AX, not an arbitrarily wide host integer. */
    font[11] = 0x80;
    font[12] = 1;
    widths[0x0100 + 'A'] = 0x80;
    widths[0x0100 + 'B'] = 0x80;
    text[0] = 0;
    assert(gate_rtl_eca4(ptr) == 0xff02 && normalize_count == 2);
}

static void test_text_cursor_and_printing(void)
{
    GateFarPtr ptr = { TEXT_SEG, 0x0100 };
    initialize();
    text[0x0100] = 'A';
    text[0x0101] = 0x80;
    text[0x0102] = 0;
    gate_rtl_ed39(-9, 40);
    assert(cursor_x == -9 && cursor_y == 40 && video_set_x == 0);
    gate_rtl_ed7e(ptr);
    assert(normalize_count == 1 && glyph_count == 2 && video_print_count == 0);
    assert(glyphs[0] == 'A' && glyphs[1] == 0xff80);

    /* In text-mode fonts, y uses IDIV, x uses the original sign/SAR. */
    font[3] = 0;
    gate_rtl_ed39(-9, -25);
    assert(cursor_x == -9 && cursor_y == -25);
    assert(video_set_x == 0 && video_set_y == 0xffff);
    gate_rtl_ed39((int16_t)0x8000, 0);
    assert(video_set_x == 0x1001 && video_set_y == 1);
    video_cursor_x = 3;
    video_cursor_y = 4;
    gate_rtl_ed7e(ptr);
    assert(video_print_count == 1 && glyph_count == 2);
    assert(cursor_x == 16 && cursor_y == 30 && normalize_count == 2);
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
    assert(gate_rtl_ebf7() == 11);
    for (unsigned ch = 33; ch <= 126; ++ch) {
        unsigned expected = (unsigned)widths[0x0100 + ch] +
                            (unsigned)(spacing[0x0200 + ch] & 15) +
                            (unsigned)(spacing[0x0200 + ch] >> 4);
        assert(gate_rtl_ec0a((uint16_t)ch) == expected);
    }
}

int main(int argc, char **argv)
{
    test_font_advances();
    test_string_width_and_offset_wrap();
    test_text_cursor_and_printing();
    test_original_intro_font(argc > 1 ? argv[1] : NULL);
    puts("launcher RTL source C: metrics, string width, cursor, print pass passed");
    return 0;
}

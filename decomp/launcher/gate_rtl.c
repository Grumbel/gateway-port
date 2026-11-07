/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl.h"

static uint8_t font_byte(GateFarPtr font, uint16_t offset)
{
    return gate_far_byte(font.segment, (uint16_t)(font.offset + offset));
}

static GateFarPtr font_pointer(GateFarPtr font, uint16_t offset)
{
    GateFarPtr result;
    result.offset = gate_far_word(font.segment, (uint16_t)(font.offset + offset));
    result.segment = gate_far_word(font.segment, (uint16_t)(font.offset + offset + 2));
    return result;
}

/* GATE.RTL file 0xebf7..0xec09: both font-header bytes are sign-extended
 * BEFORE addition. For GATE_305.FNT, they are 1 and 10, yielding 11. */
uint16_t gate_rtl_font_line_advance(void)
{
    GateFarPtr font;
    font = gate_rtl_active_font();
    return (uint16_t)((int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_LINE_GAP) +
                      (int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_GLYPH_HEIGHT));
}

/* GATE.RTL file 0xec0a..0xeca3: a char-width switch with three font modes.
 * The field at +12 selects per-character spacing if negative; the field
 * at +11 independently selects per-character widths if negative. The
 * per-character spacing byte at [font+18] contributes both four-bit nibbles.
 * The index is the complete original 16-bit argument, not a clamped char.
 */
uint16_t gate_rtl_font_character_advance(uint16_t character)
{
    GateFarPtr font;
    uint16_t width;

    if (character == GATE_TEXT_CHARACTER_TAB)
        return gate_rtl_tab_advance();
    if (character == GATE_TEXT_CHARACTER_NEWLINE)
        return 0;

    font = gate_rtl_active_font();
    if ((int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) < 0) {
        GateFarPtr spacing;
        GateFarPtr widths;
        int16_t packed;
        spacing = font_pointer(font, GATE_FONT_SLOT_SPACING_TABLE_OFFSET);
        widths = font_pointer(font, GATE_FONT_SLOT_WIDTH_TABLE_OFFSET);
        packed = (int8_t)gate_far_byte(spacing.segment,
                                       (uint16_t)(spacing.offset + character));
        width = (uint16_t)(int16_t)(int8_t)gate_far_byte(
            widths.segment, (uint16_t)(widths.offset + character));
        width = (uint16_t)(width + (((uint16_t)(packed >> GATE_TEXT_FONT_SPACING_NIBBLE_SHIFT)) & GATE_TEXT_FONT_SPACING_NIBBLE_MASK));
        width = (uint16_t)(width + (packed & GATE_TEXT_FONT_SPACING_NIBBLE_MASK));
    } else if ((int8_t)font_byte(font, GATE_FONT_SLOT_WIDTH_MODE) < 0) {
        GateFarPtr widths;
        widths = font_pointer(font, GATE_FONT_SLOT_WIDTH_TABLE_OFFSET);
        width = (uint16_t)((int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) +
                           (int16_t)(int8_t)gate_far_byte(
                               widths.segment, (uint16_t)(widths.offset + character)));
    } else {
        width = (uint16_t)((int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) +
                           (int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_WIDTH_MODE));
    }
    return width;
}

/* GATE.RTL file 0xeca4..0xece9: EXE:0x0bb8 returns the original pointer
 * unchanged in DX:AX; the caller then mutates only the OFFSET half of its
 * stack pointer. Signed bytes are passed through CBW;
 * the total accumulates in a wrapping 16-bit local. */
uint16_t gate_rtl_measure_text_width(GateFarPtr text)
{
    uint16_t total = 0;
    text = gate_exe_identity_text_pointer(text);
    while (gate_far_byte(text.segment, text.offset) != 0) {
        uint16_t ch = (uint16_t)(int16_t)(int8_t)gate_far_byte(text.segment,
                                                               text.offset);
        text.offset = (uint16_t)(text.offset + 1);
        total = (uint16_t)(total + gate_rtl_font_character_advance(ch));
    }
    return total;
}

/* GATE.RTL file 0xecea..0xed00: unconditionally store two DS globals into
 * caller-provided FAR word pointers, without checking for NULL. */
void gate_rtl_get_text_colors(GateFarPtr ink, GateFarPtr background)
{
    gate_far_set_word(ink.segment, ink.offset, gate_rtl_ink_color());
    gate_far_set_word(background.segment, background.offset,
                      (uint16_t)gate_rtl_background_color());
}

/* GATE.RTL file 0xed01..0xed11: exact two-word color setting, including
 * the sign bit of the background flag used at RTL:0xeea7. */
void gate_rtl_set_text_colors(uint16_t ink, int16_t background)
{
    gate_rtl_set_colors(ink, background);
}

/* GATE.RTL file 0xed12..0xed38: optional FAR output pointers. The original
 * checks offset OR segment for zero (not either half independently). */
void gate_rtl_get_text_cursor(GateFarPtr x, GateFarPtr y)
{
    if ((uint16_t)(x.offset | x.segment) != 0)
        gate_far_set_word(x.segment, x.offset, (uint16_t)gate_rtl_cursor_x());
    if ((uint16_t)(y.offset | y.segment) != 0)
        gate_far_set_word(y.segment, y.offset, (uint16_t)gate_rtl_cursor_y());
}

/* GATE.RTL file 0xed39..0xed7d: always store x/y; text-mode fonts additionally
 * convert the pixel coordinates into character-cell coordinates. The two
 * linked video call is now decompiled at RTL:0xf997; its underlying video
 * dispatcher and the pitch global are unresolved. The signed IDIV,
 * sign/magnitude SAR and +1 conversion come directly from x86. */
void gate_rtl_set_text_cursor(int16_t x, int16_t y)
{
    GateFarPtr font;
    font = gate_rtl_active_font();
    gate_rtl_set_cursor(x, y);
    if (font_byte(font, GATE_FONT_SLOT_RESOURCE_ID) == 0) {
        int16_t pitch = gate_rtl_text_mode_pitch();
        uint16_t abs_x = (uint16_t)(x < 0 ? -(int32_t)x : x);
        /* Arithmetic SAR 3, including the original INT16_MIN wrap case. */
        uint16_t shifted = (uint16_t)((abs_x >> GATE_TEXT_CELL_WIDTH_SHIFT) |
                                       ((abs_x & GATE_TEXT_SIGNED_WORD_SIGN_BIT) ? GATE_TEXT_CELL_SAR_SIGN_FILL : 0));
        if (x < 0)
            shifted = (uint16_t)(-shifted);
        gate_rtl_set_text_cell_cursor((uint16_t)(shifted + 1),
                       (uint16_t)((int16_t)(y / pitch) + 1));
    }
}

/* GATE.RTL file 0xed7e..0xedfe: normalized far pointer; a special text-mode
 * branch delegates printing and rescales the saved coordinates. In the
 * graphics-font branch it calls RTL:0xedff once per SIGNED input character.
 * Text-mode calls now resolve to RTL:0xfa45 and RTL:0xf959. */
void gate_rtl_draw_text_string(GateFarPtr text)
{
    GateFarPtr font;
    text = gate_exe_identity_text_pointer(text);
    font = gate_rtl_active_font();
    if (font_byte(font, GATE_FONT_SLOT_RESOURCE_ID) == 0) {
        uint16_t x, y;
        gate_rtl_print_text_mode_string(text);
        gate_rtl_get_text_cell_cursor(&x, &y);
        gate_rtl_set_cursor((int16_t)(uint16_t)(x * GATE_TEXT_CELL_WIDTH_PIXELS - GATE_TEXT_CELL_WIDTH_PIXELS),
                            (int16_t)(uint16_t)((uint16_t)(y - 1) *
                                                 (uint16_t)gate_rtl_text_mode_pitch()));
    } else {
        uint16_t ch;
        while ((ch = (uint16_t)(int16_t)(int8_t)gate_far_byte(
                   text.segment, text.offset)) != 0) {
            text.offset = (uint16_t)(text.offset + 1);
            gate_rtl_draw_font_glyph(ch);
        }
    }
}

/* GATE.RTL file 0xedff..0xefd8: original graphics-font character printer.
 * The font header and its three FAR pointers are accessed exactly as in the
 * compiled code. In particular:
 *  - raw width is a SIGNED byte and DEC/JNS draws exactly width bits;
 *  - both signed IMULs retain only AX when calculating a glyph's byte offset;
 *  - rows increment the bitmap pointer's 16-bit OFFSET, never its segment;
 *  - a character outside font +7..+8 still consumes its calculated advance;
 *  - background setup precedes the left-bearing cursor adjustment;
 *  - bit order is MSB first, with one far pixel call per set bit.
 * The original linked background/pixel functions remain named callbacks.
 */
void gate_rtl_draw_font_glyph(uint16_t character)
{
    GateFarPtr font;
    int16_t glyph_width;
    uint16_t advance;

    if (character == GATE_TEXT_CHARACTER_TAB) {
        int16_t pitch = gate_rtl_tab_pitch();
        int16_t quotient = (int16_t)(gate_rtl_cursor_x() / pitch);
        int16_t next = (int16_t)(uint16_t)((uint32_t)(uint16_t)quotient *
                                            (uint16_t)pitch + (uint16_t)pitch);
        gate_rtl_set_cursor(next, gate_rtl_cursor_y());
        if (gate_rtl_tab_limit() <= next)
            goto newline;
        return;
    }
    if (character == GATE_TEXT_CHARACTER_NEWLINE) {
    newline:
        gate_rtl_set_cursor(0, (int16_t)(uint16_t)(
            (uint16_t)gate_rtl_cursor_y() + (uint16_t)gate_rtl_newline_pitch()));
        return;
    }

    font = gate_rtl_active_font();
    if ((int8_t)font_byte(font, GATE_FONT_SLOT_WIDTH_MODE) < 0) {
        GateFarPtr widths;
        widths = font_pointer(font, GATE_FONT_SLOT_WIDTH_TABLE_OFFSET);
        glyph_width = (int8_t)gate_far_byte(widths.segment,
                                            (uint16_t)(widths.offset + character));
    } else {
        glyph_width = (int8_t)font_byte(font, GATE_FONT_SLOT_WIDTH_MODE);
    }

    if ((int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) < 0) {
        GateFarPtr spacing;
        uint16_t packed;
        spacing = font_pointer(font, GATE_FONT_SLOT_SPACING_TABLE_OFFSET);
        packed = (uint16_t)(int16_t)(int8_t)gate_far_byte(
            spacing.segment, (uint16_t)(spacing.offset + character));
        advance = (uint16_t)(glyph_width +
                             ((packed >> GATE_TEXT_FONT_SPACING_NIBBLE_SHIFT) & GATE_TEXT_FONT_SPACING_NIBBLE_MASK) +
                             (packed & GATE_TEXT_FONT_SPACING_NIBBLE_MASK));
    } else {
        advance = (uint16_t)(glyph_width + (int16_t)(int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE));
    }

    if (gate_rtl_background_color() >= 0) {
        uint16_t x = (uint16_t)gate_rtl_cursor_x();
        uint16_t y = (uint16_t)gate_rtl_cursor_y();
        gate_rtl_background_setup(0, (uint16_t)gate_rtl_background_color(), 0);
        gate_rtl_background_rect(GATE_TEXT_GLYPH_BACKGROUND_RECT_STYLE,
            (uint16_t)(y + (uint16_t)gate_rtl_newline_pitch() - 1),
            (uint16_t)(x + advance - 1), y, x);
    }

    if ((int8_t)font_byte(font, GATE_FONT_SLOT_SPACING_MODE) < 0) {
        GateFarPtr spacing;
        uint16_t packed;
        uint16_t bearing;
        spacing = font_pointer(font, GATE_FONT_SLOT_SPACING_TABLE_OFFSET);
        packed = (uint16_t)(int16_t)(int8_t)gate_far_byte(
            spacing.segment, (uint16_t)(spacing.offset + character));
        bearing = (packed >> GATE_TEXT_FONT_SPACING_NIBBLE_SHIFT) & GATE_TEXT_FONT_SPACING_NIBBLE_MASK;
        gate_rtl_set_cursor((int16_t)(uint16_t)((uint16_t)gate_rtl_cursor_x() + bearing),
                            gate_rtl_cursor_y());
        advance = (uint16_t)(advance - bearing);
    }

    if ((int16_t)character >= (int8_t)font_byte(font, GATE_FONT_SLOT_FIRST_CHARACTER) &&
        (int16_t)character <= (int8_t)font_byte(font, GATE_FONT_SLOT_LAST_CHARACTER)) {
        int16_t row_bytes = (int8_t)font_byte(font, GATE_FONT_SLOT_GLYPH_ROW_BYTES);
        int16_t height = (int8_t)font_byte(font, GATE_FONT_SLOT_GLYPH_HEIGHT);
        int16_t first = (int8_t)font_byte(font, GATE_FONT_SLOT_FIRST_CHARACTER);
        uint16_t row_index = (uint16_t)((uint32_t)(uint16_t)(character - first) *
                                         (uint16_t)row_bytes);
        uint16_t offset = (uint16_t)((uint32_t)row_index * (uint16_t)height);
        GateFarPtr bitmap;
        uint16_t remaining_rows;
        uint16_t y;
        bitmap = font_pointer(font, GATE_FONT_SLOT_BITMAP_OFFSET);
        remaining_rows = (uint16_t)height;
        y = (uint16_t)gate_rtl_cursor_y();
        bitmap.offset = (uint16_t)(bitmap.offset + offset);

        for (;;) {
            uint16_t remaining_columns;
            uint16_t x, byte_offset, mask;

            remaining_rows = (uint16_t)(remaining_rows - 1);
            if ((int16_t)remaining_rows < 0)
                break;
            remaining_columns = (uint16_t)glyph_width;
            x = (uint16_t)gate_rtl_cursor_x();
            byte_offset = 0;
            mask = GATE_TEXT_GLYPH_HIGH_BIT_MASK;
            for (;;) {
                remaining_columns = (uint16_t)(remaining_columns - 1);
                if ((int16_t)remaining_columns < 0)
                    break;
                if ((uint16_t)(int16_t)(int8_t)gate_far_byte(
                        bitmap.segment, (uint16_t)(bitmap.offset + byte_offset)) & mask)
                    gate_rtl_plot_pixel(gate_rtl_ink_color(), y, x);
                mask >>= 1;
                if (mask == 0) {
                    mask = GATE_TEXT_GLYPH_HIGH_BIT_MASK;
                    byte_offset = (uint16_t)(byte_offset + 1);
                }
                x = (uint16_t)(x + 1);
            }
            bitmap.offset = (uint16_t)(bitmap.offset + (uint16_t)row_bytes);
            y = (uint16_t)(y + 1);
        }
    }
    gate_rtl_set_cursor((int16_t)(uint16_t)((uint16_t)gate_rtl_cursor_x() + advance),
                        gate_rtl_cursor_y());
}

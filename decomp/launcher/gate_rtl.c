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
uint16_t gate_rtl_ebf7(void)
{
    GateFarPtr font = gate_rtl_active_font();
    return (uint16_t)((int16_t)(int8_t)font_byte(font, 9) +
                      (int16_t)(int8_t)font_byte(font, 10));
}

/* GATE.RTL file 0xec0a..0xeca3: a char-width switch with three font modes.
 * The field at +12 selects per-character spacing if negative; the field
 * at +11 independently selects per-character widths if negative. The
 * per-character spacing byte at [font+18] contributes both four-bit nibbles.
 * The index is the complete original 16-bit argument, not a clamped char.
 */
uint16_t gate_rtl_ec0a(uint16_t character)
{
    GateFarPtr font;
    uint16_t width;

    if (character == 9)
        return gate_rtl_tab_advance();
    if (character == 10)
        return 0;

    font = gate_rtl_active_font();
    if ((int8_t)font_byte(font, 12) < 0) {
        GateFarPtr spacing = font_pointer(font, 18);
        GateFarPtr widths = font_pointer(font, 14);
        int16_t packed = (int8_t)gate_far_byte(spacing.segment,
                                                (uint16_t)(spacing.offset + character));
        width = (uint16_t)(int16_t)(int8_t)gate_far_byte(
            widths.segment, (uint16_t)(widths.offset + character));
        width = (uint16_t)(width + (((uint16_t)(packed >> 4)) & 15));
        width = (uint16_t)(width + (packed & 15));
    } else if ((int8_t)font_byte(font, 11) < 0) {
        GateFarPtr widths = font_pointer(font, 14);
        width = (uint16_t)((int16_t)(int8_t)font_byte(font, 12) +
                           (int16_t)(int8_t)gate_far_byte(
                               widths.segment, (uint16_t)(widths.offset + character)));
    } else {
        width = (uint16_t)((int16_t)(int8_t)font_byte(font, 12) +
                           (int16_t)(int8_t)font_byte(font, 11));
    }
    return width;
}

/* GATE.RTL file 0xeca4..0xece9: the external far-pointer helper at
 * original 0xecb0 returns DX:AX and the caller then mutates only the
 * OFFSET half of its stack pointer. Signed bytes are passed through CBW;
 * the total accumulates in a wrapping 16-bit local. */
uint16_t gate_rtl_eca4(GateFarPtr text)
{
    uint16_t total = 0;
    text = gate_rtl_normalize_pointer(text);
    while (gate_far_byte(text.segment, text.offset) != 0) {
        uint16_t ch = (uint16_t)(int16_t)(int8_t)gate_far_byte(text.segment,
                                                               text.offset);
        text.offset = (uint16_t)(text.offset + 1);
        total = (uint16_t)(total + gate_rtl_ec0a(ch));
    }
    return total;
}

/* GATE.RTL file 0xed39..0xed7d: always store x/y; text-mode fonts additionally
 * convert the pixel coordinates into character-cell coordinates. The two
 * linked video calls and the pitch global are unresolved, but the signed
 * IDIV, sign/magnitude SAR and +1 conversion come directly from x86. */
void gate_rtl_ed39(int16_t x, int16_t y)
{
    GateFarPtr font = gate_rtl_active_font();
    gate_rtl_set_cursor(x, y);
    if (font_byte(font, 3) == 0) {
        int16_t pitch = gate_rtl_text_mode_pitch();
        uint16_t abs_x = (uint16_t)(x < 0 ? -(int32_t)x : x);
        /* Arithmetic SAR 3, including the original INT16_MIN wrap case. */
        uint16_t shifted = (uint16_t)((abs_x >> 3) |
                                       ((abs_x & 0x8000) ? 0xe000 : 0));
        if (x < 0)
            shifted = (uint16_t)(-shifted);
        gate_rtl_text_mode_set_cursor((uint16_t)(shifted + 1),
                                      (uint16_t)((int16_t)(y / pitch) + 1));
    }
}

/* GATE.RTL file 0xed7e..0xedfe: normalized far pointer; a special text-mode
 * branch delegates printing and rescales the saved coordinates. In the
 * graphics-font branch it calls RTL:0xedff once per SIGNED input character.
 * The glyph renderer's full body is a separate pending reconstruction. */
void gate_rtl_ed7e(GateFarPtr text)
{
    GateFarPtr font;
    text = gate_rtl_normalize_pointer(text);
    font = gate_rtl_active_font();
    if (font_byte(font, 3) == 0) {
        int16_t x, y;
        gate_rtl_text_mode_print(text);
        gate_rtl_text_mode_get_cursor(&x, &y);
        gate_rtl_set_cursor((int16_t)(uint16_t)((uint16_t)x * 8u - 8u),
                            (int16_t)(uint16_t)((uint16_t)(y - 1) *
                                                 (uint16_t)gate_rtl_text_mode_pitch()));
    } else {
        uint16_t ch;
        while ((ch = (uint16_t)(int16_t)(int8_t)gate_far_byte(
                   text.segment, text.offset)) != 0) {
            text.offset = (uint16_t)(text.offset + 1);
            gate_rtl_edff(ch);
        }
    }
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_draw.h"
#include "gate_rtl.h"

/* GATE.RTL:fbf5..fc22. The requested color is compared SIGNED against
 * the selected mode descriptor's maximum at ES:0054, but negative colors
 * are not clamped. The resident pen-color call is a distinct side effect
 * from the original C's DS:0004 write (relocated to DS:5512). */
uint16_t gate_rtl_set_pen_color(uint16_t color)
{
    uint16_t previous = gate_ds_word(GATE_DS_PEN_COLOR);
    uint16_t maximum = gate_far_word(gate_ds_word(GATE_DS_MAX_PEN_COLOR_SEGMENT), GATE_MODE_DESC_MAX_PEN_COLOR);
    if ((int16_t)color > (int16_t)maximum)
        color = maximum;
    gate_ds_set_word(GATE_DS_PEN_COLOR, color);
    gate_rtl_resident_set_pen_color(color);
    return previous;
}

/* GATE.RTL:fc23..fc36. SBB/INC consumes the UNSIGNED carry flag left by
 * CMP AX,1, producing one for every nonzero 16-bit pattern (even ffff). */
void gate_rtl_set_fill_pattern(uint16_t pattern)
{
    gate_ds_set_word(GATE_DS_FILL_PATTERN, pattern);
    gate_ds_set_word(GATE_DS_FILL_PATTERN_ACTIVE, pattern != 0 ? 1u : 0u);
}

/* GATE.RTL:fc37..fc80. This builds the original resident 25a3:0002
 * fill-state call, then calls resident 2698:0006. Note that the top
 * coordinate (third C parameter) is clamped to -1, not zero. The style
 * test checks bit 7 of its LOW byte, not bit 15 of its WORD. */
void gate_rtl_fill_rectangle(uint16_t style, uint16_t left, uint16_t top,
                   uint16_t right, uint16_t bottom)
{
    if ((style & 0x0080u) != 0)
        gate_rtl_background_setup(10, gate_ds_word(GATE_DS_PEN_COLOR), 1);
    else
        gate_rtl_background_setup(gate_ds_word(GATE_DS_FILL_PATTERN),
                                  gate_ds_word(GATE_DS_PEN_COLOR),
                                  gate_ds_word(GATE_DS_FILL_PATTERN_ACTIVE));

    if ((int16_t)top < -1)
        top = 0xffffu;
    gate_rtl_background_rect(style, bottom, right, top, left);
}

/* GATE.RTL:ff99..1029f. The original frame painter uses graphics mode
 * (relocated DS:0fce) to choose four words in the current mode descriptor.
 * Segment selectors DS:4da0/4da2/4da4/4da6 are separately relocated,
 * although all point at the same mode descriptor in the 22-second image. */
static uint16_t frame_color(uint16_t segment_word, uint16_t field)
{
    return gate_far_word(gate_ds_word(segment_word), field);
}

void gate_rtl_draw_caption_frame(uint16_t left, uint16_t top, uint16_t right,
                   uint16_t bottom, uint16_t style)
{
    const uint16_t mode = gate_ds_word(GATE_DS_GRAPHICS_MODE);
    uint16_t x, y;

    (void)gate_rtl_set_pen_color(mode == 0
        ? frame_color(0x4da6, 0x52)
        : frame_color(0x4da0, 0x4c));

    if ((style & 2u) != 0) {
        x = (uint16_t)(left + 1u);
        gate_rtl_resident_move_to(x, top);
        gate_rtl_resident_line_to((uint16_t)(right - 1u), top);
        gate_rtl_resident_move_to(x, bottom);
        gate_rtl_resident_line_to((uint16_t)(right - 1u), bottom);
        y = (uint16_t)(top + 1u);
        gate_rtl_resident_move_to(right, y);
        gate_rtl_resident_line_to(right, (uint16_t)(bottom - 1u));
        gate_rtl_resident_move_to(left, y);
        gate_rtl_resident_line_to(left, (uint16_t)(bottom - 1u));
    } else {
        gate_rtl_fill_rectangle(1, left, top, right, bottom);
    }

    if ((style & 4u) != 0) {
        if (mode == 0) {
            x = (uint16_t)(left + 1u);
            gate_rtl_resident_move_to(x, top);
            gate_rtl_resident_line_to(x, bottom);
            x = (uint16_t)(right - 1u);
            gate_rtl_resident_move_to(x, top);
            gate_rtl_resident_line_to(x, bottom);
            return;
        }

        (void)gate_rtl_set_pen_color(frame_color(0x4da4, 0x50));
        gate_rtl_fill_rectangle(2, (uint16_t)(left + 1u),
                       (uint16_t)(top + 1u), (uint16_t)(right - 1u),
                       (uint16_t)(bottom - 1u));
        (void)gate_rtl_set_pen_color(frame_color(0x4da6, 0x52));

        x = (uint16_t)(left + 1u);
        y = (uint16_t)(top + 1u);
        gate_rtl_resident_move_to(x, (uint16_t)(bottom - 2u));
        gate_rtl_resident_line_to(x, y);
        gate_rtl_resident_line_to((uint16_t)(right - 2u), y);
        x = (uint16_t)(left + 2u);
        gate_rtl_resident_move_to(x, (uint16_t)(bottom - 3u));
        y = (uint16_t)(top + 2u);
        gate_rtl_resident_line_to(x, y);
        if (mode != 1)
            gate_rtl_resident_line_to((uint16_t)(right - 3u), y);

        (void)gate_rtl_set_pen_color(frame_color(0x4da2, 0x4e));
        y = (uint16_t)(bottom - 1u);
        gate_rtl_resident_move_to((uint16_t)(left + 1u), y);
        x = (uint16_t)(right - 1u);
        gate_rtl_resident_line_to(x, y);
        gate_rtl_resident_line_to(x, (uint16_t)(top + 1u));
        if (mode == 1) {
            gate_rtl_resident_line_to((uint16_t)(right - 2u),
                                      (uint16_t)(bottom - 2u));
        } else {
            y = (uint16_t)(bottom - 2u);
            gate_rtl_resident_move_to((uint16_t)(left + 2u), y);
            gate_rtl_resident_line_to((uint16_t)(right - 2u), y);
        }
        gate_rtl_resident_line_to((uint16_t)(right - 2u),
                                  (uint16_t)(top + 2u));
    } else if ((style & 8u) != 0) {
        if (mode == 0) {
            x = (uint16_t)(left - 1u);
            gate_rtl_resident_move_to(x, top);
            gate_rtl_resident_line_to(x, bottom);
            x = (uint16_t)(right + 1u);
            gate_rtl_resident_move_to(x, top);
            gate_rtl_resident_line_to(x, bottom);
            return;
        }

        (void)gate_rtl_set_pen_color(frame_color(0x4da2, 0x4e));
        x = (uint16_t)(left - 1u);
        gate_rtl_resident_move_to(x, (uint16_t)(bottom + 1u));
        gate_rtl_resident_line_to(x, (uint16_t)(top - 1u));
        gate_rtl_resident_line_to((uint16_t)(right + 1u),
                                  (uint16_t)(top - 1u));
        x = (uint16_t)(left - 2u);
        y = (uint16_t)(bottom + 2u);
        gate_rtl_resident_move_to(x, y);
        gate_rtl_resident_line_to(x, (uint16_t)(top - 1u));
        if (mode != 1) {
            gate_rtl_resident_move_to(x, (uint16_t)(top - 2u));
            gate_rtl_resident_line_to((uint16_t)(right + 2u),
                                      (uint16_t)(top - 2u));
        }

        (void)gate_rtl_set_pen_color(frame_color(0x4da6, 0x52));
        y = (uint16_t)(bottom + 1u);
        gate_rtl_resident_move_to(left, y);
        x = (uint16_t)(right + 1u);
        gate_rtl_resident_line_to(x, y);
        gate_rtl_resident_line_to(x, top);
        if (mode == 1) {
            gate_rtl_resident_line_to((uint16_t)(right + 2u),
                                      (uint16_t)(bottom + 1u));
        } else {
            y = (uint16_t)(bottom + 2u);
            gate_rtl_resident_move_to((uint16_t)(left - 1u), y);
            gate_rtl_resident_line_to((uint16_t)(right + 2u), y);
        }
        gate_rtl_resident_line_to((uint16_t)(right + 2u),
                                  (uint16_t)(top - 1u));
    }
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_draw.h"
#include "gate_rtl.h"

/* GATE.RTL:fbf5..fc22. The requested color is compared SIGNED against
 * the selected mode descriptor's maximum at ES:0054, but negative colors
 * are not clamped. The resident pen-color call is a distinct side effect
 * from the original C's DS:0004 write (relocated to DS:5512). */
uint16_t gate_rtl_fbf5(uint16_t color)
{
    uint16_t previous = gate_ds_word(0x5512);
    uint16_t maximum = gate_far_word(gate_ds_word(0x4da8), 0x0054);
    if ((int16_t)color > (int16_t)maximum)
        color = maximum;
    gate_ds_set_word(0x5512, color);
    gate_rtl_resident_set_pen_color(color);
    return previous;
}

/* GATE.RTL:fc23..fc36. SBB/INC consumes the UNSIGNED carry flag left by
 * CMP AX,1, producing one for every nonzero 16-bit pattern (even ffff). */
void gate_rtl_fc23(uint16_t pattern)
{
    gate_ds_set_word(0x12de, pattern);
    gate_ds_set_word(0x12e0, pattern != 0 ? 1u : 0u);
}

/* GATE.RTL:fc37..fc80. This builds the original resident 25a3:0002
 * fill-state call, then calls resident 2698:0006. Note that the top
 * coordinate (third C parameter) is clamped to -1, not zero. The style
 * test checks bit 7 of its LOW byte, not bit 15 of its WORD. */
void gate_rtl_fc37(uint16_t style, uint16_t left, uint16_t top,
                   uint16_t right, uint16_t bottom)
{
    if ((style & 0x0080u) != 0)
        gate_rtl_background_setup(10, gate_ds_word(0x5512), 1);
    else
        gate_rtl_background_setup(gate_ds_word(0x12de),
                                  gate_ds_word(0x5512),
                                  gate_ds_word(0x12e0));

    if ((int16_t)top < -1)
        top = 0xffffu;
    gate_rtl_background_rect(style, bottom, right, top, left);
}

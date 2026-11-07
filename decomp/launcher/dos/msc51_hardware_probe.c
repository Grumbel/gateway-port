/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* An actual DOS executable probes the relocated clip state. The read-only
 * VGA status sample checks that IN is reachable under DOSBox, without
 * assuming a stable retrace bit. Port OUT is linked but not invoked here:
 * writing VGA registers for test purposes would alter display hardware. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

uint8_t gate_rtl_palette_in(uint16_t port);
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left);
void gate_rtl_mode_pen(uint16_t enabled);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Hardware probe: %s got %04x expected %04x\n",
               label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int segment = 0;
    uint8_t status;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Hardware probe: DOS segment allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);

    gate_rtl_mode_clip(199u, 319u, 12u, 7u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_LEFT), 7u, "clip left");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_TOP), 12u, "clip top");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_RIGHT), 319u, "clip right");
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIP_BOTTOM), 199u, "clip bottom");
    gate_rtl_mode_pen(1u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED), 1u, "clip enabled");
    gate_rtl_mode_pen(2u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_CLIPPING_ENABLED), 0u,
                   "non-one clip mode disabled");

    status = gate_rtl_palette_in(0x03dau); /* VGA color status, dynamic */
    printf("Hardware probe: VGA status port 03da read %02x\n", status);
    if (_dos_freemem(segment) != 0) {
        puts("Hardware probe: DOS segment free failed");
        return 1;
    }
    puts(valid ? "Hardware probe: PASS" : "Hardware probe: FAIL");
    return valid ? 0 : 1;
}

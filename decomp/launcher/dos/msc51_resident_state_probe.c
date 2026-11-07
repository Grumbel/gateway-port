/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Read and write genuine guest DS fields; query BIOS mode under DOSBox. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

void gate_rtl_resident_set_pen_color(uint16_t color);
void gate_rtl_resident_move_to(uint16_t x, uint16_t y);
void gate_rtl_background_setup(uint16_t pattern, uint16_t color, uint16_t transparent);
uint16_t gate_rtl_mode_set_bios(uint16_t mode);
uint16_t gate_rtl_mode_start(uint16_t enabled);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Resident state probe: %s got %04x expected %04x\n",
               label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int segment = 0;
    int valid = 1;
    union REGS in, out;
    uint8_t original_bios_mode;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Resident state probe: DOS memory allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_rtl_resident_set_pen_color(0xabcdu);
    valid &= check(gate_ds_word(GATE_DS_DRAW_COLOR), 0xabcdu, "pen color");
    gate_rtl_resident_move_to(300u, 99u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_X), 300u, "pen x");
    valid &= check(gate_ds_word(GATE_DS_DRAW_Y), 99u, "pen y");
    gate_rtl_background_setup(11u, 15u, 1u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_PATTERN), 11u, "fill pattern");
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_COLOR), 15u, "fill color");
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_TRANSPARENT), 1u, "fill alpha");
    gate_rtl_background_setup(0u, 4u, 2u);
    valid &= check(gate_ds_word(GATE_DS_DRAW_FILL_TRANSPARENT), 0u,
                   "non-one transparency disabled");

    valid &= check(gate_rtl_mode_set_bios(8u), 0xfffau, "reject invalid mode");
    valid &= check(gate_rtl_mode_set_bios(0u), 0u, "accept text descriptor");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_MODE), 0u, "descriptor selected");
    valid &= check(gate_rtl_mode_start(2u), 0xfff9u, "reject invalid enable");
    in.h.ah = 0x0fu;
    int86(0x10, &in, &out);
    original_bios_mode = out.h.al;
    gate_far_set_byte((uint16_t)segment,
        (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE + GATE_RESIDENT_MODE_BIOS_NUMBER),
        original_bios_mode);
    valid &= check(gate_rtl_mode_start(1u), 0u, "start with current BIOS mode");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_ENABLED), 1u, "video enabled");
    valid &= check(gate_ds_word(GATE_DS_RENDERER_STATUS), 0u, "video status cleared");
    in.h.ah = 0x0fu;
    int86(0x10, &in, &out);
    valid &= check(out.h.al, original_bios_mode, "BIOS mode unchanged");

    if (_dos_freemem(segment) != 0) {
        puts("Resident state probe: DOS memory free failed");
        return 1;
    }
    puts(valid ? "Resident state probe: PASS" : "Resident state probe: FAIL");
    return valid ? 0 : 1;
}

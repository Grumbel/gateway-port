/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_draw.h"
#include "gate_intro_host_memory.h"
#include "gate_rtl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void configure(uint16_t mode, uint16_t max_color)
{
    gate_ds_set_word(0x15cc, mode);
    gate_ds_set_word(0x4da8, 0x3100);
    gate_far_set_word(0x3100, 0x54, max_color);
    gate_ds_set_word(0x29de + 0x16, 0);
    gate_ds_set_word(0x29de + 0x18, 1);
    gate_ds_set_word(0x29de + 0x1a, 0);
    gate_ds_set_word(0x29de + 0x1c, 0);
    gate_ds_set_word(0x29de + 0x1e, mode == 3 ? 319 : 639);
    gate_ds_set_word(0x29de + 0x20,
                     mode == 1 ? 349 : (mode == 2 ? 479 : 199));
    gate_ds_set_word(0x5512, 0);
    gate_rtl_fc23(0);
}

static void check_native_mode(uint16_t mode)
{
    uint16_t x, y;
    configure(mode, mode == 3 ? 255 : 15);
    assert(gate_rtl_fbf5(mode == 3 ? 23 : 100) == 0);
    assert(gate_ds_word(0x5512) == (mode == 3 ? 23 : 15));
    assert(gate_ds_word(0x29de + 0x0c) == (mode == 3 ? 23 : 15));
    gate_rtl_fc37(2, 7, 11, 10, 14);
    assert(gate_ds_word(0x29de + 0x22) == 0);
    assert(gate_ds_word(0x29de + 0x24) == (mode == 3 ? 23 : 15));
    assert(gate_ds_word(0x29de + 0x26) == 0);
    assert(gate_ds_word(0x29de + 0x0c) == (mode == 3 ? 23 : 15));
    for (y = 10; y <= 15; ++y)
        for (x = 6; x <= 11; ++x) {
            uint8_t pixel = mode != 3 ? gate_intro_vga16_pixel(x, y) :
                gate_far_byte(0xa000, (uint16_t)(320u * y + x));
            assert(pixel == ((x >= 7 && x <= 10 && y >= 11 && y <= 14)
                             ? (mode == 3 ? 23 : 15) : 0));
        }
    if (mode == 1) {
        gate_rtl_plot_pixel(7, 349, 15);
        gate_rtl_plot_pixel(7, 350, 16);
        assert(gate_intro_vga16_pixel(15, 349) == 7);
        assert(gate_intro_vga16_pixel(16, 350) == 0);
    }
    gate_rtl_background_setup(10, 7, 2);
    assert(gate_ds_word(0x29de + 0x22) == 10);
    assert(gate_ds_word(0x29de + 0x26) == 0); /* resident normalization */
    gate_rtl_background_setup(0, 7, 0);
}

static void check_original_snapshot(const char *path)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    FILE *file;
    assert(ram);
    file = fopen(path, "rb");
    assert(file);
    assert(fread(ram->bytes, 1, sizeof ram->bytes, file) ==
           sizeof ram->bytes);
    assert(fclose(file) == 0);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    /* At 22s: GATE.RTL:fbf5 gets max ink from DS:4da8 -> ES:0054,
     * with actual mode 13h chosen in the captured original launcher. */
    assert(gate_ds_word(0x4da8) == 0x2a70);
    assert(gate_far_word(0x2a70, 0x54) == 255);
    assert(gate_ds_word(0x12de) == 0 && gate_ds_word(0x12e0) == 0);
    assert(gate_ds_word(0x5512) == 2);
    assert(gate_rtl_fbf5(999) == 2);
    assert(gate_ds_word(0x5512) == 255);
    assert(gate_ds_word(0x29de + 0x0c) == 255);
    gate_rtl_fc37(2, 10, 12, 13, 14);
    for (uint16_t y = 12; y <= 14; ++y)
        for (uint16_t x = 10; x <= 13; ++x)
            assert(gate_far_byte(0xa000, (uint16_t)(320u * y + x)) == 255);
    gate_intro_memory_detach();
    free(ram);
}

int main(int argc, char **argv)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    assert(ram);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    check_native_mode(2);
    check_native_mode(3);
    check_native_mode(1);
    gate_intro_memory_detach();
    free(ram);
    if (argc > 1)
        check_original_snapshot(argv[1]);
    puts("native original RTL draw wrappers: passed");
    return 0;
}

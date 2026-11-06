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

int main(void)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    GateFarPtr font;
    assert(ram);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    gate_ds_set_word(0x54ca, 0x1200);
    gate_ds_set_word(0x54cc, 0x2b70);
    font = gate_rtl_active_font();
    assert(font.segment == 0x2b70 && font.offset == 0x1200);
    gate_rtl_set_cursor(-9, 157);
    assert(gate_rtl_cursor_x() == -9);
    assert(gate_rtl_cursor_y() == 157);
    assert(gate_ds_word(0x550a) == (uint16_t)-9);
    assert(gate_ds_word(0x550c) == 157);
    gate_rtl_set_colors(0x024f, -1);
    assert(gate_rtl_ink_color() == 0x024f);
    assert(gate_rtl_background_color() == -1);
    assert(gate_ds_word(0x54c8) == 0x024f);
    assert(gate_ds_word(0x54c6) == 0xffff);
    gate_intro_memory_detach();
    free(ram);
    puts("original RTL font, text cursor and foreground/background DS: OK");
    return 0;
}

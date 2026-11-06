/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_graphics.h"
#include "gate_rtl_image.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    assert(ram);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    gate_ds_set_word(0x4dce, 0x2200); /* video mode */
    gate_ds_set_word(0x4dd4, 0x2201); /* video state */
    gate_ds_set_word(0x4dda, 0x2202); /* size table */
    gate_ds_set_word(0x4ddc, 0x2203); /* palette */
    gate_far_set_word(0x2200, GATE_RTL_VIDEO_MODE_OFFSET, 3);
    gate_far_set_word(0x2202, GATE_RTL_SIZE_TABLE_OFFSET + 6, 0x0300);
    gate_far_set_byte(0x2203, GATE_RTL_TRANSFORM_OFFSET + 13, 0x53);
    assert(gate_rtl_graphics_mode() == 3);
    assert(gate_rtl_graphics_byte_count(6) == 0x0300);
    assert(gate_rtl_graphics_byte(13) == 0x53);
    gate_rtl_graphics_set_byte(13, 0xa7);
    assert(gate_far_byte(0x2203, GATE_RTL_TRANSFORM_OFFSET + 13) == 0xa7);
    gate_rtl_graphics_set_index(0x3c);
    assert(gate_far_word(0x2201, 0x70) == 0x3c);
    gate_intro_memory_detach();
    free(ram);
    puts("original RTL mode, size table, status and palette globals: OK");
    return 0;
}

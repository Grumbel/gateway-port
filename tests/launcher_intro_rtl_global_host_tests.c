/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gate_rtl_image.h"
#include "gate_intro_host_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GateIntroMemory *ram = calloc(1, sizeof *ram);
    GateFarPtr palette;
    uint16_t offsets[] = {0x4dce, 0x4dd0, 0x4dd2, 0x4dd4, 0x4dd6,
                          0x4dd8, 0x4dda, 0x4ddc, 0x4de2,
                          0x4de6, 0x4de8, 0x4dea};
    unsigned i;
    assert(ram);
    ram->ds_segment = 0x2a7a;
    gate_intro_memory_attach(ram);
    for (i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
        gate_ds_set_word(offsets[i], (uint16_t)(0x1100u + i));
    assert(gate_rtl_image_mode_segment() == 0x1100);
    assert(gate_rtl_image_palette_segment() == 0x1101);
    assert(gate_rtl_preparer_flag_source_segment() == 0x1102);
    assert(gate_rtl_preparer_status_segment() == 0x1103);
    assert(gate_rtl_resource_handle_segment() == 0x1104);
    assert(gate_rtl_image_header_segment() == 0x1105);
    assert(gate_rtl_resource_size_table_segment() == 0x1106);
    assert(gate_rtl_image_transform_segment() == 0x1107);
    assert(gate_rtl_preparer_word_dest_segment() == 0x1108);
    assert(gate_rtl_anim_segment_22() == 0x1109);
    assert(gate_rtl_anim_segment_24() == 0x110a);
    assert(gate_rtl_anim_segment_26() == 0x110b);
    assert(gate_rtl_image_data_segment() == ram->ds_segment);
    palette = gate_rtl_image_palette_source();
    assert(palette.segment == 0x1107);
    assert(palette.offset == GATE_RTL_TRANSFORM_OFFSET);

    gate_ds_set_word(0x1416, 0xffff);
    gate_rtl_resource_ds_set_word(0x08, 217);
    assert(gate_rtl_resource_ds_word(0) == 0xffff);
    assert(gate_rtl_resource_ds_word(8) == 217);
    assert(gate_ds_word(0x141e) == 217);
    gate_far_set_byte(ram->ds_segment, 0x1480, 0x7d);
    assert(gate_rtl_image_data_byte(0x1480) == 0x7d);

    gate_intro_memory_detach();
    free(ram);
    puts("original RTL relocated FAR globals and scalar DS state: OK");
    return 0;
}

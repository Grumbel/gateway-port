/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* GATE.RTL:0x1266f..0x12771, original pre-display video preparation.
 * The GATE.RTL file contains relocatable placeholder zeros for the FAR
 * globals and direct memory addresses. This source uses the independently
 * verified live offsets, with only the original segment words external.
 * In particular DS:4dd4 here names a relocated FAR pointer, NOT the
 * resource selector's scalar data word at relocated DS:1426. */
void gate_rtl_prepare_picture_display(void)
{
    uint16_t video_mode = gate_far_word(gate_rtl_image_mode_segment(),
                                        GATE_RTL_VIDEO_MODE_OFFSET);
    uint16_t status_segment;
    uint16_t original_data_segment;
    uint16_t palette_segment;
    uint16_t flag_source_segment;
    uint16_t active = 0;
    uint16_t i;

    if (video_mode == 0)
        return;

    status_segment = gate_rtl_preparer_status_segment();
    gate_far_set_word(status_segment, 0x70, 0);

    if (gate_rtl_resource_ds_word(6) != 0) {
        GateFarPtr palette = gate_rtl_image_palette_source();
        gate_rtl_resource_ds_set_word(6, 0);
        gate_linked_upload_intro_palette(palette);
        original_data_segment = gate_rtl_image_data_segment();
        for (i = 0; i < 0x300; ++i)
            gate_far_set_byte(original_data_segment, (uint16_t)(0x56ceu + i),
                gate_far_byte(palette.segment, (uint16_t)(palette.offset + i)));
    }

    original_data_segment = gate_rtl_image_data_segment();
    for (i = 0; i < 8; ++i)
        gate_far_set_byte(original_data_segment, (uint16_t)(0x72u + i), 0);
    for (i = 0; i < 4; ++i)
        gate_far_set_byte(original_data_segment, (uint16_t)(0x7eu + i),
            gate_far_byte(original_data_segment, (uint16_t)(0x86u + i)));

    gate_far_set_word(gate_rtl_preparer_word_dest_segment(), 0x7c,
                      gate_far_word(original_data_segment, 0x84));

    palette_segment = gate_rtl_image_palette_segment();
    flag_source_segment = gate_rtl_preparer_flag_source_segment();
    for (i = 0; i < 2; ++i) {
        uint8_t value = gate_far_byte(flag_source_segment, (uint16_t)(0x82u + i));
        gate_far_set_byte(palette_segment, (uint16_t)(0x7au + i), value);
        gate_far_set_byte(flag_source_segment, (uint16_t)(0x82u + i), 0);
        if (value != 0)
            active = 1;
    }
    gate_far_set_word(status_segment, 0x70, active);
}

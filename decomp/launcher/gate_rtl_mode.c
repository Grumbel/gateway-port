/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_mode.h"
#include "gate_rtl_palette.h"

#include <stdlib.h>

/* GATE.RTL:f49b..f4de. This is a near-called C function rather than a
 * generated 8086 thunk. Only VGA adapters (type 5) and modes 1, 2, 4
 * take this branch. The clear/set retrace wait is the original f473. */
void gate_rtl_configure_vga_palette(void)
{
    uint16_t i;
    uint16_t mode = gate_ds_word(GATE_DS_GRAPHICS_MODE);
    if (gate_ds_word(GATE_DS_VIDEO_ADAPTER_TYPE) != GATE_VGA_ADAPTER_VIDEO_TYPE ||
        mode == GATE_RTL_VIDEO_MODE_TEXT || mode == GATE_RTL_VIDEO_MODE_VGA_256)
        return;
    gate_rtl_wait_vertical_retrace();
    (void)gate_rtl_palette_in(gate_ds_word(GATE_DS_VGA_STATUS_PORT));
    for (i = 0; i < GATE_VGA_ATTRIBUTE_PALETTE_COLORS; ++i) {
        gate_rtl_palette_out(GATE_VGA_PORT_ATTRIBUTE_CONTROLLER, (uint8_t)i);
        gate_rtl_palette_out(GATE_VGA_PORT_ATTRIBUTE_CONTROLLER, (uint8_t)i);
    }
    gate_rtl_palette_out(GATE_VGA_PORT_ATTRIBUTE_CONTROLLER, GATE_VGA_ATTRIBUTE_DISPLAY_ENABLE);
}

/* Original RTL mode-data table references are relocated near DS words in
 * the captured 0cdc bank, not segment selectors: table starts 12e2/12ee,
 * width/height at 12f8/1302, pitch at 130c, grouping at 1316 and final
 * descriptor at 1320. The *destinations* are far via DS:4d92..4da8.
 * The caller's requested mode and the BIOS mode may differ for SVGA 4.
 * The original C stores ffff into the current-mode word on *entry*, so
 * even a rejected request deliberately destroys the preceding mode. */
uint16_t gate_rtl_set_video_mode(int16_t requested_mode)
{
    uint8_t adapter[GATE_RTL_ADAPTER_QUERY_BYTES] = {0};
    uint16_t video_type;
    uint16_t monitor;
    uint16_t bios_mode;
    uint16_t svga;
    uint16_t width, height, pitch, result;
    uint16_t index;
    uint16_t final_mask;
    int16_t rows;

    gate_ds_set_word(GATE_DS_GRAPHICS_MODE, GATE_RTL_VIDEO_INVALID_MODE);
    if (requested_mode < 0 || requested_mode > GATE_RTL_VIDEO_MAX_MODE)
        return 0;

    gate_rtl_mode_query_adapter(adapter);
    video_type = adapter[GATE_RTL_ADAPTER_QUERY_VIDEO_TYPE];
    monitor = adapter[GATE_RTL_ADAPTER_QUERY_MONITOR_TYPE];
    gate_ds_set_word(GATE_DS_VIDEO_ADAPTER_TYPE, video_type);
    bios_mode = (uint16_t)requested_mode;

    if (requested_mode == GATE_RTL_VIDEO_MODE_SVGA &&
        video_type == GATE_VGA_ADAPTER_VIDEO_TYPE &&
        monitor >= GATE_RTL_VIDEO_SVGA_MONITOR_MINIMUM) {
        switch (gate_rtl_mode_svga_variant()) {
        case 1: case 5: bios_mode = 4; break;
        case 2: bios_mode = 5; break;
        case 3: bios_mode = 6; break;
        case 4: bios_mode = 7; break;
        default: return 0;
        }
    }

    svga = ((video_type == GATE_RTL_VIDEO_OTHER_SVGA_ADAPTER_TYPE ||
             video_type == GATE_VGA_ADAPTER_VIDEO_TYPE) &&
            monitor >= GATE_RTL_VIDEO_SVGA_MONITOR_MINIMUM)
        ? 1u : 0u;
    gate_ds_set_word(GATE_DS_SVGA_ENABLED, svga);
    if (requested_mode == GATE_RTL_VIDEO_MODE_VGA_256) {
        if (svga == 0)
            return 0;
    } else {
        if ((int16_t)gate_ds_word((uint16_t)(GATE_DS_MODE_ADAPTER_LIMIT_TABLE + 2u * video_type)) <
              requested_mode ||
            (int16_t)gate_ds_word((uint16_t)(GATE_DS_MODE_MONITOR_LIMIT_TABLE + 2u * monitor)) <
              requested_mode)
            return 0;
    }

    gate_ds_set_word(GATE_DS_VGA_STATUS_PORT,
                     monitor == GATE_RTL_VIDEO_MONO_STATUS_MONITOR_TYPE ?
                     GATE_VGA_PORT_MONO_STATUS : GATE_VGA_PORT_COLOR_STATUS);
    if (gate_rtl_mode_set_bios(bios_mode) != 0 ||
        gate_rtl_mode_start(1) != 0)
        return 0;

    gate_ds_set_word(GATE_DS_GRAPHICS_MODE, (uint16_t)requested_mode);
    gate_rtl_configure_vga_palette();
    index = (uint16_t)(2u * (uint16_t)requested_mode);
    width = gate_ds_word((uint16_t)(GATE_DS_MODE_WIDTH_TABLE + index));
    height = gate_ds_word((uint16_t)(GATE_DS_MODE_HEIGHT_TABLE + index));
    pitch = gate_ds_word((uint16_t)(GATE_DS_MODE_GROUPING_TABLE + index));

    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_WIDTH_SEGMENT), GATE_MODE_DESC_SCREEN_WIDTH, width);
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_HEIGHT_SEGMENT), GATE_MODE_DESC_SCREEN_HEIGHT, height);
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_STRIDE_SEGMENT), GATE_MODE_DESC_SCREEN_STRIDE,
        gate_ds_word((uint16_t)(GATE_DS_MODE_STRIDE_TABLE + index)));
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_GROUPING_SEGMENT), GATE_MODE_DESC_LINE_GROUPING, pitch);

    /* CWD / IDIV, then CWD / SUB / SAR: signed quotient with original
     * rounding. A zero pitch would have faulted the 8086 division. */
    if (pitch == 0)
        abort();
    rows = (int16_t)((int16_t)(uint16_t)(height + 1u) / (int16_t)pitch);
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_ROW_COUNT_SEGMENT), GATE_MODE_DESC_SCREEN_ROW_COUNT, (uint16_t)rows);
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_HALF_ROW_COUNT_SEGMENT), GATE_MODE_DESC_HALF_ROW_COUNT, (uint16_t)(rows / 2));
    gate_far_set_word(gate_ds_word(GATE_DS_VIDEO_COLOR_LAYOUT_SEGMENT), GATE_MODE_DESC_COLOR_LAYOUT,
        gate_ds_word((uint16_t)(GATE_DS_MODE_COLOR_LAYOUT_TABLE + index)));

    /* Exactly the five far WORD writes selected by the original three
     * branch targets f2d0/f311/f38b, including the unusual 00ff mask. */
    if (requested_mode == GATE_RTL_VIDEO_MODE_TEXT) {
        static const uint16_t values[5] = {0, 0, 1, 1, 1};
        for (index = 0; index < GATE_MODE_DESC_FRAME_COLOR_WORDS; ++index)
            gate_far_set_word(gate_ds_word((uint16_t)(GATE_DS_FRAME_COLOR_SEGMENTS_BASE + 2u * index)),
                              (uint16_t)(GATE_MODE_DESC_FRAME_COLOR_PRIMARY + 2u * index), values[index]);
    } else if (requested_mode == GATE_RTL_VIDEO_MODE_VGA_256) {
        static const uint16_t values[5] = {0, 2, 1, GATE_RTL_VIDEO_8BIT_PEN_MASK,
                                           GATE_RTL_VIDEO_8BIT_PEN_MASK};
        for (index = 0; index < GATE_MODE_DESC_FRAME_COLOR_WORDS; ++index)
            gate_far_set_word(gate_ds_word((uint16_t)(GATE_DS_FRAME_COLOR_SEGMENTS_BASE + 2u * index)),
                              (uint16_t)(GATE_MODE_DESC_FRAME_COLOR_PRIMARY + 2u * index), values[index]);
    } else {
        static const uint16_t values[5] = {GATE_RTL_VIDEO_4BIT_PEN_MASK, 2, 1, 0,
                                           GATE_RTL_VIDEO_4BIT_PEN_MASK};
        for (index = 0; index < GATE_MODE_DESC_FRAME_COLOR_WORDS; ++index)
            gate_far_set_word(gate_ds_word((uint16_t)(GATE_DS_FRAME_COLOR_SEGMENTS_BASE + 2u * index)),
                              (uint16_t)(GATE_MODE_DESC_FRAME_COLOR_PRIMARY + 2u * index), values[index]);
    }
    final_mask = gate_far_word(gate_ds_word(GATE_DS_MAX_PEN_COLOR_SEGMENT), GATE_MODE_DESC_MAX_PEN_COLOR);
    gate_ds_set_word(GATE_DS_PEN_COLOR, final_mask);
    gate_ds_set_word(GATE_DS_PEN_MASK, final_mask);
    gate_ds_set_word(GATE_DS_PEN_AUX_STATE, 0);
    gate_ds_set_word(GATE_DS_FILL_AUX_STATE, 0);
    gate_rtl_mode_clip(height, width, 0, 0);
    gate_rtl_mode_pen(1);
    result = 1;
    return result;
}

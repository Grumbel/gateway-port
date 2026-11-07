/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

/* GATE.RTL:0x12c33..0x12d66. The original BP+6 FAR work pointer addresses
 * the 136-byte local record created by 0x12b19. Preserve the exact read,
 * write, and failure order: the record is NOT reset if already active, and
 * the early resource/allocator failures do NOT call the cleanup helper.
 *
 * All memory access uses original segment:offset arithmetic. The three
 * relocated RTL DS globals and the three linked FAR services remain narrow
 * external dependencies, not newly designed host-image abstractions. */
uint16_t gate_rtl_load_picture_record(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    uint16_t mode_segment;
    uint16_t header_segment;
    uint16_t video_mode;
    uint16_t width;
    GateFarPtr destination;

    if (gate_far_byte(work.segment, work.offset) != 0)
        return 0;

    gate_far_set_byte(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_BASE), 0);
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_SPREAD), 0);

    if (gate_rtl_select_picture_resource(resource, mode) == 0)
        return 0;

    destination.segment = work.segment;
    destination.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE);
    mode_segment = gate_rtl_image_mode_segment();
    header_segment = gate_rtl_image_header_segment();
    video_mode = gate_far_word(mode_segment, GATE_RTL_VIDEO_MODE_OFFSET);
    if (gate_linked_allocate_vfx_image(gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_HEIGHT),
                           gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_WIDTH),
                           video_mode, destination, 0) != 0)
        return 0;

    gate_linked_initialize_vfx_image(0, destination);

    /* The original linked A1 20 14 / A1 22 14 read relocated *near DS
     * scalar values* (RTL DS:000a/000c -> linked DS:1420/1422): picture
     * x/y from the resource selector. They are NOT segment selectors.
     * Confusing them with DS:4dce/4dd0 put pictures far off-screen. */
    gate_far_set_word(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_X),
                      gate_rtl_resource_ds_word(GATE_RESOURCE_PICTURE_X));
    gate_far_set_word(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_Y),
                      gate_rtl_resource_ds_word(GATE_RESOURCE_PICTURE_Y));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_BASE),
                      gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_DELAY_BASE));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_DELAY_SPREAD),
                      gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_DELAY_SPREAD));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_BLIT_MODE),
                      (gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FLAGS) &
                       GATE_PICTURE_HEADER_SPECIAL_BLIT) != 0 ? 3 : 0);
    gate_far_set_byte(work.segment, work.offset, 1);

    width = gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_WIDTH);
    if (gate_far_word(mode_segment, GATE_RTL_VIDEO_MODE_OFFSET) != 3) {
        /* Match ADD/CWD/XOR/SUB/SAR/XOR/SUB as WORD operations. Ordinarily
         * this means signed division by eight toward zero. The original
         * 8086 also retains the two's-complement -32768 overflow in its
         * intermediate negation, giving +4096 for the 0x8000 input. */
        uint16_t sum = (uint16_t)(width + 7u);
        uint16_t sign = (sum & 0x8000u) ? 0xffffu : 0;
        uint16_t magnitude = (uint16_t)((sum ^ sign) - sign);
        uint16_t shifted = (uint16_t)(magnitude >> 3);
        if ((magnitude & 0x8000u) != 0)
            shifted |= 0xe000u; /* original signed SAR by 3 */
        width = (uint16_t)((shifted ^ sign) - sign);
    }
    gate_far_set_word(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_ROW_STRIDE), width);

    if (gate_linked_decompress_picture_pixels(
            gate_far_word(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_PIXELS_OFFSET)),
            gate_far_word(work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_PIXELS_SEGMENT))) != 0) {
        gate_rtl_release_picture_record(work);
        return 0;
    }
    gate_rtl_postprocess_picture(work);
    return 1;
}

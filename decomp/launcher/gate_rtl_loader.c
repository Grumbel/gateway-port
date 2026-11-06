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
uint16_t gate_rtl_12c33(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    uint16_t mode_segment;
    uint16_t header_segment;
    uint16_t video_mode;
    uint16_t width;
    GateFarPtr destination;

    if (gate_far_byte(work.segment, work.offset) != 0)
        return 0;

    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 6u), 0);
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 7u), 0);

    if (gate_rtl_11ecb(resource, mode) == 0)
        return 0;

    destination.segment = work.segment;
    destination.offset = (uint16_t)(work.offset + 8u);
    mode_segment = gate_rtl_image_mode_segment();
    header_segment = gate_rtl_image_header_segment();
    video_mode = gate_far_word(mode_segment, GATE_RTL_VIDEO_MODE_OFFSET);
    if (gate_far_145d_00f7(gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + 8),
                           gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + 6),
                           video_mode, destination, 0) != 0)
        return 0;

    gate_far_1479_0002(0, destination);

    /* The original linked A1 20 14 / A1 22 14 read relocated *near DS
     * scalar values* (RTL DS:000a/000c -> linked DS:1420/1422): picture
     * x/y from the resource selector. They are NOT segment selectors.
     * Confusing them with DS:4dce/4dd0 put pictures far off-screen. */
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 2u),
                      gate_rtl_resource_ds_word(0x0au));
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 4u),
                      gate_rtl_resource_ds_word(0x0cu));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 6u),
                      gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + 10));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 7u),
                      gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + 11));
    gate_far_set_byte(work.segment, (uint16_t)(work.offset + 1u),
                      (gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + 5) & 0x40u) != 0 ? 3 : 0);
    gate_far_set_byte(work.segment, work.offset, 1);

    width = gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + 6);
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
    gate_far_set_word(work.segment, (uint16_t)(work.offset + 0x1cu), width);

    if (gate_far_0eec_018a(
            gate_far_word(work.segment, (uint16_t)(work.offset + 0x20u)),
            gate_far_word(work.segment, (uint16_t)(work.offset + 0x22u))) != 0) {
        gate_rtl_131cb(work);
        return 0;
    }
    gate_rtl_12205(work);
    return 1;
}

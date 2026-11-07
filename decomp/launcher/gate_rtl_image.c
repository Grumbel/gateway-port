/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

/* GATE.RTL:0x12b19..0x12b66. Original 136-byte stack-local work record:
 * initialize byte zero, load one source resource, prepare output only when
 * loading succeeded, then always release the loaded work area. The return
 * is the 16-bit 0/1 status, not a host success/exception abstraction. */
uint16_t gate_rtl_show_picture(uint16_t resource, uint16_t mode)
{
    uint8_t work[0x88];
    GateFarPtr pointer;

    work[0] = 0;
    pointer = gate_stack_pointer(work);
    if (gate_rtl_load_picture_record(pointer, resource, mode) != 0) {
        gate_rtl_prepare_picture_display();
        gate_rtl_draw_picture_record(pointer);
        gate_rtl_release_picture_record(pointer);
        return 1;
    }
    return 0;
}

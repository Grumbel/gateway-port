/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* GATE.RTL:0x12d67..0x12dda (linked 0f10:1262).
 * Load consecutive resource IDs into consecutive 0x88-byte work records.
 * The original writes the relocated DS:1424 flag only for a nonpositive
 * count, replacing that count with 256. It clears the flag unconditionally
 * on return, including when the first resource fails. The caller must have
 * sufficient work records: this routine performs no bounds check. */
uint16_t gate_rtl_load_picture_sequence(GateFarPtr work, int16_t count,
                        uint16_t first_resource, uint16_t mode)
{
    int16_t loaded = 0;

    if (count < 1) {
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_SEQUENCE_FILTER, 1);
        count = GATE_PICTURE_SEQUENCE_NONPOSITIVE_COUNT;
    }

    while (loaded < count) {
        if (gate_rtl_load_picture_record(work, first_resource, mode) == 0)
            break;
        first_resource = (uint16_t)(first_resource + 1u);
        work.offset = (uint16_t)(work.offset + GATE_PICTURE_RECORD_BYTES);
        ++loaded;
    }

    gate_rtl_resource_ds_set_word(GATE_RESOURCE_SEQUENCE_FILTER, 0);
    return (uint16_t)loaded;
}

/* GATE.RTL:0x130ab..0x13114 (linked 0f10:15a6).
 * 32 local image records occupy the 0x1100-byte stack frame. The frame
 * initializer and load/draw/release helpers are reconstructed separately.
 * The greater-than-32 diagnostic is a FAR call with a relocated DS:14b2
 * string; it must return without allocating or releasing any records.
 * Count 0 and negative counts retain the original loader's 256-record
 * fallback, even though that can exceed the local stack allocation. */
void gate_rtl_play_preloaded_picture_sequence(uint16_t first_resource, int16_t count)
{
    uint8_t records[GATE_PICTURE_SEQUENCE_CAPACITY * GATE_PICTURE_RECORD_BYTES];
    GateFarPtr work;
    uint16_t loaded;

    if (count > GATE_PICTURE_SEQUENCE_CAPACITY) {
        gate_linked_print_intro_diagnostic((GateFarPtr){gate_rtl_image_data_segment(),
                                       GATE_RTL_SEQUENCE_LIMIT_MESSAGE_OFFSET});
        return;
    }

    work = gate_stack_pointer(records);
    gate_rtl_initialize_picture_record_array(work, count);
    loaded = gate_rtl_load_picture_sequence(work, count, first_resource, 1);
    gate_rtl_play_timed_picture_records(work, (int16_t)loaded);
    gate_rtl_release_picture_record_array(work, (int16_t)loaded);
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* GATE.RTL:0x11ecb..0x120f1. The original 16-bit C resource selector,
 * called by the image loader. It selects a cached resource file from the
 * resource's high bits and video mode, reads its 12-byte descriptor into
 * the original header storage, optionally reads two display-mode WORDs,
 * and conditionally processes a format table. Read/write order and early
 * exits match the 8086; original linked I/O calls are retained as externs.
 */
uint16_t gate_rtl_select_picture_resource(uint16_t resource, uint16_t mode)
{
    uint16_t file_group;
    uint16_t resource_file;
    uint16_t file_handle;
    uint16_t handle_segment;
    uint16_t header_segment;
    uint16_t flags;
    uint16_t format;
    int16_t selected_mode;
    int16_t max_mode;
    uint32_t header_offset;

    if (gate_rtl_resource_ds_word(GATE_RESOURCE_ERROR_LATCH) != 0)
        return 0;

    if ((resource & GATE_RESOURCE_USE_GRAPHICS_MODE_BIT) != 0) {
        file_group = gate_far_word(gate_rtl_image_mode_segment(), GATE_RTL_VIDEO_MODE_OFFSET);
    } else {
        file_group = (uint16_t)((int16_t)resource >> GATE_RESOURCE_GROUP_SHIFT);
        if (file_group == 0 &&
            gate_far_word(gate_rtl_image_mode_segment(), GATE_RTL_VIDEO_MODE_OFFSET) != 0)
            file_group = 1;
    }

    /* IMUL 100 uses only AX; the original signed SAR and AND select one of
     * sixteen resource IDs within each group. All sums wrap at 16 bits. */
    resource_file = (uint16_t)(file_group * GATE_RESOURCE_FILENAME_GROUP_MULTIPLIER +
                               ((resource >> 8) & GATE_RESOURCE_FILENAME_SUFFIX_MASK));
    handle_segment = gate_rtl_resource_handle_segment();
    if (resource_file != gate_rtl_resource_ds_word(GATE_RESOURCE_CACHED_FILE_ID)) {
        if (gate_rtl_resource_ds_word(GATE_RESOURCE_CACHED_FILE_ID) != GATE_RESOURCE_NO_CACHED_FILE)
            gate_linked_close_resource_file(gate_far_word(handle_segment, GATE_RTL_FILE_HANDLE_OFFSET));
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_CACHED_FILE_ID, resource_file);
        file_handle = gate_linked_open_resource_file(0, resource_file);
        gate_far_set_word(handle_segment, GATE_RTL_FILE_HANDLE_OFFSET, file_handle);
        if (file_handle == 0) {
            gate_rtl_resource_ds_set_word(GATE_RESOURCE_CACHED_FILE_ID, GATE_RESOURCE_NO_CACHED_FILE);
            gate_rtl_resource_ds_set_word(GATE_RESOURCE_ERROR_LATCH, 1);
            return 0;
        }
    }

    file_handle = gate_far_word(handle_segment, GATE_RTL_FILE_HANDLE_OFFSET);
    /* The original byte-index * 12 is promoted by CWD before the seek. */
    gate_linked_seek_resource_file(file_handle,
        (uint32_t)(resource & GATE_RESOURCE_DIRECTORY_ENTRY_MASK) * GATE_PICTURE_HEADER_BYTES);
    if (gate_linked_read_resource_bytes(file_handle,
            (GateFarPtr){gate_rtl_image_header_segment(), GATE_RTL_HEADER_OFFSET},
            GATE_PICTURE_HEADER_BYTES) == 0)
        return 0;

    header_segment = gate_rtl_image_header_segment();
    header_offset = (uint32_t)gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET) |
                    ((uint32_t)gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FILE_OFFSET_HIGH) << 16);
    if (header_offset == 0)
        return 0;

    flags = gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FLAGS);
    format = flags & GATE_PICTURE_HEADER_FORMAT_MASK;
    if (gate_rtl_resource_ds_word(GATE_RESOURCE_SEQUENCE_FILTER) != 0 &&
        (flags & GATE_PICTURE_HEADER_HAS_PALETTE) != 0)
        return 0;

    max_mode = (int8_t)gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_MODE_COUNT);
    selected_mode = (int16_t)mode;
    if (selected_mode > max_mode)
        selected_mode = max_mode;
    if (selected_mode > 0) {
        uint32_t location = (uint32_t)(header_offset +
                                        (uint32_t)((int32_t)selected_mode * GATE_RESOURCE_POSITION_PAIR_BYTES -
                                                   GATE_RESOURCE_POSITION_PAIR_BYTES));
        gate_linked_seek_resource_file(file_handle, location);
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_PICTURE_X, gate_linked_read_resource_word(file_handle));
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_PICTURE_Y, gate_linked_read_resource_word(file_handle));
    } else {
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_PICTURE_X, 0);
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_PICTURE_Y, 0);
    }

    if (gate_rtl_resource_ds_word(GATE_RESOURCE_SKIP_PICTURE_PAYLOAD) != 0)
        return 1;

    /* The original header's count at +4 is SIGN-extended, even though
     * ordinary game descriptors have a small positive count. */
    gate_linked_seek_resource_file(file_handle, (uint32_t)(header_offset +
        (uint32_t)((int32_t)(int8_t)gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_MODE_COUNT) *
                   GATE_RESOURCE_POSITION_PAIR_BYTES)));
    if ((gate_far_byte(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FLAGS) &
         GATE_PICTURE_HEADER_HAS_PALETTE) != 0) {
        uint16_t table_word = gate_far_word(gate_rtl_resource_size_table_segment(),
                                           (uint16_t)(GATE_RTL_SIZE_TABLE_OFFSET + (format << 1)));
        (void)gate_linked_read_resource_bytes(file_handle,
            gate_rtl_image_palette_source(), table_word);
        gate_rtl_resource_ds_set_word(GATE_RESOURCE_PALETTE_PENDING, 1);
    }

    return gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_WIDTH) != 0 &&
           gate_far_word(header_segment, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_HEIGHT) != 0 ? 1 : 0;
}

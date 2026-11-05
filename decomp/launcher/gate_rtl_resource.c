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
uint16_t gate_rtl_11ecb(uint16_t resource, uint16_t mode)
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

    if (gate_rtl_resource_ds_word(0) != 0)
        return 0;

    if ((resource & 0x8000u) != 0) {
        file_group = gate_far_word(gate_rtl_image_mode_segment(), 0);
    } else {
        file_group = (uint16_t)((int16_t)resource >> 12);
        if (file_group == 0 &&
            gate_far_word(gate_rtl_image_mode_segment(), 0) != 0)
            file_group = 1;
    }

    /* IMUL 100 uses only AX; the original signed SAR and AND select one of
     * sixteen resource IDs within each group. All sums wrap at 16 bits. */
    resource_file = (uint16_t)(file_group * 100u +
                               ((resource >> 8) & 0x0fu));
    handle_segment = gate_rtl_resource_handle_segment();
    if (resource_file != gate_rtl_resource_ds_word(8)) {
        if (gate_rtl_resource_ds_word(8) != 0xffffu)
            gate_far_0c33_019a(gate_far_word(handle_segment, 0));
        gate_rtl_resource_ds_set_word(8, resource_file);
        file_handle = gate_far_0c33_0040(0, resource_file);
        gate_far_set_word(handle_segment, 0, file_handle);
        if (file_handle == 0) {
            gate_rtl_resource_ds_set_word(8, 0xffffu);
            gate_rtl_resource_ds_set_word(0, 1);
            return 0;
        }
    }

    file_handle = gate_far_word(handle_segment, 0);
    /* The original byte-index * 12 is promoted by CWD before the seek. */
    gate_far_0c33_0091(file_handle, (uint32_t)(resource & 0xffu) * 12u);
    if (gate_far_0c33_00f2(file_handle, 0, 12) == 0)
        return 0;

    header_segment = gate_rtl_image_header_segment();
    header_offset = (uint32_t)gate_far_word(header_segment, 0) |
                    ((uint32_t)gate_far_word(header_segment, 2) << 16);
    if (header_offset == 0)
        return 0;

    flags = gate_far_byte(header_segment, 5);
    format = flags & 0x0fu;
    if (gate_rtl_resource_ds_word(0x0e) != 0 && (flags & 0x10u) != 0)
        return 0;

    max_mode = (int8_t)gate_far_byte(header_segment, 4);
    selected_mode = (int16_t)mode;
    if (selected_mode > max_mode)
        selected_mode = max_mode;
    if (selected_mode > 0) {
        uint32_t location = (uint32_t)(header_offset +
                                        (uint32_t)((int32_t)selected_mode * 4 - 4));
        gate_far_0c33_0091(file_handle, location);
        gate_rtl_resource_ds_set_word(0x0a, gate_far_0c33_00d4(file_handle));
        gate_rtl_resource_ds_set_word(0x0c, gate_far_0c33_00d4(file_handle));
    } else {
        gate_rtl_resource_ds_set_word(0x0a, 0);
        gate_rtl_resource_ds_set_word(0x0c, 0);
    }

    if (gate_rtl_resource_ds_word(0x10) != 0)
        return 1;

    /* The original header's count at +4 is SIGN-extended, even though
     * ordinary game descriptors have a small positive count. */
    gate_far_0c33_0091(file_handle, (uint32_t)(header_offset +
        (uint32_t)((int32_t)(int8_t)gate_far_byte(header_segment, 4) * 4)));
    if ((gate_far_byte(header_segment, 5) & 0x10u) != 0) {
        uint16_t table_word = gate_far_word(gate_rtl_resource_size_table_segment(),
                                           (uint16_t)(format << 1));
        (void)gate_far_0c33_00f2(file_handle, 0, table_word);
        gate_rtl_resource_ds_set_word(6, 1);
    }

    return gate_far_word(header_segment, 6) != 0 &&
           gate_far_word(header_segment, 8) != 0 ? 1 : 0;
}

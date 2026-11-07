/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_image.h"

/* The original RTLink loader patches segment WORDs into RTL's linked DS.
 * These offsets are verified in the resident DOS snapshot and the original
 * image-loader instruction stream (see launcher_rtl_graphics_binary_tests).
 * The host reads the *linked WORD values* instead of pinning runtime
 * segment 2a70/2a7a, which differ across a DOS loading environment. */
uint16_t gate_rtl_image_mode_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_MODE_SEGMENT);
}

uint16_t gate_rtl_image_palette_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_PALETTE_SEGMENT);
}

uint16_t gate_rtl_preparer_flag_source_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_FLAGS_SEGMENT);
}

uint16_t gate_rtl_preparer_status_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_STATUS_SEGMENT);
}

uint16_t gate_rtl_resource_handle_segment(void)
{
    return gate_ds_word(GATE_DS_RESOURCE_HANDLE_SEGMENT);
}

uint16_t gate_rtl_image_header_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_HEADER_SEGMENT);
}

uint16_t gate_rtl_resource_size_table_segment(void)
{
    return gate_ds_word(GATE_DS_RESOURCE_SIZE_TABLE_SEGMENT);
}

uint16_t gate_rtl_image_transform_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_TRANSFORM_SEGMENT);
}

uint16_t gate_rtl_preparer_word_dest_segment(void)
{
    return gate_ds_word(GATE_DS_IMAGE_PREPARE_DEST_SEGMENT);
}

uint16_t gate_rtl_anim_segment_22(void)
{
    return gate_ds_word(GATE_DS_ANIMATION_FIELD_22_SEGMENT);
}

uint16_t gate_rtl_anim_segment_24(void)
{
    return gate_ds_word(GATE_DS_ANIMATION_FIELD_24_SEGMENT);
}

uint16_t gate_rtl_anim_segment_26(void)
{
    return gate_ds_word(GATE_DS_ANIMATION_FIELD_26_SEGMENT);
}

/* Unlike the relocatable FAR references above, RTL's own data and
 * resource-selector scalar words live directly in the current DS. */
uint16_t gate_rtl_image_data_segment(void)
{
    return gate_ds_segment();
}

uint8_t gate_rtl_image_data_byte(uint16_t offset)
{
    return gate_far_byte(gate_ds_segment(), offset);
}

uint16_t gate_rtl_resource_ds_word(uint16_t offset)
{
    return gate_ds_word((uint16_t)(GATE_DS_RESOURCE_SCALARS_BASE + offset));
}

void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_ds_set_word((uint16_t)(GATE_DS_RESOURCE_SCALARS_BASE + offset), value);
}

GateFarPtr gate_rtl_image_palette_source(void)
{
    return (GateFarPtr){gate_rtl_image_transform_segment(),
                        GATE_RTL_TRANSFORM_OFFSET};
}

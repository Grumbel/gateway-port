/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_rtl_graphics.h"

#include <stdint.h>
#include <stdlib.h>

/* Original far allocation services at 253a:0004/0048, supplied by the
 * host's conventional-memory arena in native builds. */
GateFarPtr gate_linked_allocate_vfx_pixels(uint32_t bytes);
uint16_t gate_linked_release_vfx_pixels(GateFarPtr pixels);

static uint16_t image_word(GateFarPtr image, uint16_t at)
{
    return gate_far_word(image.segment, (uint16_t)(image.offset + at));
}

static void set_image_word(GateFarPtr image, uint16_t at, uint16_t value)
{
    gate_far_set_word(image.segment, (uint16_t)(image.offset + at), value);
}

static void set_image_byte(GateFarPtr image, uint16_t at, uint8_t value)
{
    gate_far_set_byte(image.segment, (uint16_t)(image.offset + at), value);
}

/* Pixel memory may cross a segment boundary. Original 2479:0002 uses
 * 244c:0000 to advance the segment by 1000h on a 16-bit offset wrap.
 * Keep that behavior separate from ordinary FAR accesses, which wrap their
 * offset without adjusting the segment. */
static void set_pixel_byte(GateFarPtr pixels, uint32_t at, uint8_t value)
{
    uint32_t offset = (uint32_t)pixels.offset + at;
    gate_far_set_byte((uint16_t)(pixels.segment + ((offset >> GATE_VFX_FAR_PIXEL_OFFSET_BITS) << GATE_VFX_FAR_PIXEL_SEGMENT_ADVANCE_BITS)),
                      (uint16_t)offset, value);
}

/* 145d:00f7 -> resident 245d:00f7: VFX image allocation, backed by
 * 244c:00b9 (row-stride/plane-count size) and 245d:0006 (CA00 record
 * constructor). Mode 2 is 640x480, one bit per plane and four planes;
 * mode 3 is 320x200, eight bits per pixel and one plane. Both descriptor
 * layouts are pinned against original RAM, including their 32-bit buffer
 * lengths and even-byte alignment of each plane-row.
 * Original stack order: height, width, mode, FAR destination, flag. */
uint16_t gate_linked_allocate_vfx_image(uint16_t height, uint16_t width,
                            uint16_t mode, GateFarPtr image,
                            uint16_t zero_flag)
{
    uint32_t length;
    uint16_t row_bytes;
    uint16_t stride;
    uint16_t descriptor;
    uint16_t mode_width;
    uint16_t mode_height;
    uint16_t bpp;
    uint16_t planes;
    uint16_t remainder;
    uint8_t high_mask;
    GateFarPtr pixels;

    if (mode != GATE_VFX_PLANAR_MODE && mode != GATE_VFX_CHUNKY_MODE)
        abort(); /* remaining original VFX formats are unresolved */
    if (width == 0 || height == 0)
        return GATE_VFX_ALLOCATOR_FAILURE;

    /* Resident mode descriptors: 2409:00e8 -> DS:27ee+mode*0x3e. */
    descriptor = (uint16_t)(GATE_RESIDENT_MODE_DESCRIPTOR_BASE + mode * GATE_RESIDENT_MODE_DESCRIPTOR_BYTES);
    mode_width = gate_ds_word((uint16_t)(descriptor + GATE_RESIDENT_MODE_WIDTH));
    mode_height = gate_ds_word((uint16_t)(descriptor + GATE_RESIDENT_MODE_HEIGHT));
    bpp = gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + GATE_RESIDENT_MODE_BITS_PER_PLANE));
    planes = gate_far_byte(gate_ds_segment(), (uint16_t)(descriptor + GATE_RESIDENT_MODE_PLANE_COUNT));
    if (!((mode == GATE_VFX_PLANAR_MODE &&
           mode_width == GATE_VFX_PLANAR_WIDTH && mode_height == GATE_VFX_PLANAR_HEIGHT &&
           bpp == GATE_VFX_PLANAR_BITS_PER_PLANE && planes == GATE_VFX_PLANAR_PLANES) ||
          (mode == GATE_VFX_CHUNKY_MODE &&
           mode_width == GATE_VFX_CHUNKY_WIDTH && mode_height == GATE_VFX_CHUNKY_HEIGHT &&
           bpp == GATE_VFX_CHUNKY_BITS_PER_PLANE && planes == GATE_VFX_CHUNKY_PLANES)))
        abort(); /* original VFX mode table has not been initialized */

    row_bytes = (uint16_t)(((uint32_t)width * bpp + GATE_VFX_LAST_BIT_INDEX) / GATE_VFX_BITS_PER_BYTE);
    stride = (uint16_t)((row_bytes + (GATE_VFX_ROW_ALIGNMENT_BYTES - 1u)) & ~(GATE_VFX_ROW_ALIGNMENT_BYTES - 1u));
    length = (uint32_t)stride * height * planes;
    /* The original size calculator allows up to a megabyte through DX<16;
     * our isolated conventional-memory heap is deliberately smaller. */
    if (length > GATE_VFX_MAX_ALLOCATED_PIXEL_BYTES)
        return GATE_VFX_ALLOCATOR_FAILURE;
    pixels = gate_linked_allocate_vfx_pixels(length);
    if (pixels.segment == 0)
        return GATE_VFX_ALLOCATOR_FAILURE;

    /* Exact resident 245d:0006 header fields, including reserved words.
     * The caller owns the 0x80-byte record at image; the allocator owns
     * only its separate pixel region, released by 245d:016c. */
    set_image_word(image, GATE_VFX_IMAGE_SIGNATURE, GATE_VFX_IMAGE_SIGNATURE_VALUE);
    set_image_byte(image, GATE_VFX_IMAGE_KIND, GATE_VFX_ALLOCATOR_KIND_BYTE);
    set_image_byte(image, GATE_VFX_IMAGE_RESERVED_03, 0);
    set_image_word(image, GATE_VFX_IMAGE_VIDEO_MODE, mode);
    set_image_word(image, GATE_VFX_IMAGE_LEFT, 0);
    set_image_word(image, GATE_VFX_IMAGE_TOP, 0);
    set_image_word(image, GATE_VFX_IMAGE_RIGHT, (uint16_t)(width - 1u));
    set_image_word(image, GATE_VFX_IMAGE_BOTTOM, (uint16_t)(height - 1u));
    set_image_word(image, GATE_VFX_IMAGE_MODE_WIDTH, mode_width);
    set_image_word(image, GATE_VFX_IMAGE_MODE_HEIGHT, mode_height);
    set_image_byte(image, GATE_VFX_IMAGE_PLANES, (uint8_t)planes);
    set_image_byte(image, GATE_VFX_IMAGE_BITS_PER_PLANE, (uint8_t)bpp);
    set_image_word(image, GATE_VFX_IMAGE_ROW_STRIDE, stride);
    set_image_word(image, GATE_VFX_IMAGE_FLAGS, zero_flag);
    set_image_word(image, GATE_VFX_IMAGE_PIXELS_OFFSET, pixels.offset);
    set_image_word(image, GATE_VFX_IMAGE_PIXELS_SEGMENT, pixels.segment);
    set_image_word(image, GATE_VFX_IMAGE_LINE_LAYOUT, gate_ds_word((uint16_t)(descriptor + GATE_RESIDENT_MODE_LINE_LAYOUT)));
    set_image_word(image, GATE_VFX_IMAGE_RESERVED_1E, 0);
    set_image_word(image, GATE_VFX_IMAGE_RESERVED_20, 0);
    set_image_word(image, GATE_VFX_IMAGE_WIDTH, width);
    set_image_word(image, GATE_VFX_IMAGE_HEIGHT, height);
    set_image_word(image, GATE_VFX_IMAGE_ROW_BYTES, row_bytes);
    remainder = (uint16_t)(((uint32_t)width * bpp) & GATE_VFX_LAST_BIT_INDEX);
    high_mask = (uint8_t)(GATE_VFX_PIXEL_BYTE_ALL_BITS << ((GATE_VFX_BITS_PER_BYTE - remainder) & GATE_VFX_LAST_BIT_INDEX));
    set_image_word(image, GATE_VFX_IMAGE_PIXEL_MASK, (uint16_t)((uint16_t)(uint8_t)~high_mask << 8 |
                                           high_mask));
    set_image_word(image, GATE_VFX_IMAGE_PLANE_ROWS, (uint16_t)(height * planes));
    return 0;
}

/* 1479:0002 -> resident 2479:0002, fill the allocated image pixels.
 * Its multi-plane path repeats one 0xff or 0x00 byte per plane according
 * to the corresponding bit of the caller's WORD pattern, interleaving
 * planes within each image scanline. The single-plane 8bpp path repeats
 * the low byte. The original advances the FAR segment on offset wrap. */
void gate_linked_initialize_vfx_image(uint16_t pattern, GateFarPtr image)
{
    uint32_t row;
    uint32_t index;
    uint16_t stride;
    uint16_t height;
    uint8_t planes;
    uint8_t bpp;
    GateFarPtr data;
    if (image_word(image, GATE_VFX_IMAGE_SIGNATURE) != GATE_VFX_IMAGE_SIGNATURE_VALUE)
        return;
    planes = gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_PLANES));
    bpp = gate_far_byte(image.segment, (uint16_t)(image.offset + GATE_VFX_IMAGE_BITS_PER_PLANE));
    if (!((planes == GATE_VFX_CHUNKY_PLANES && bpp == GATE_VFX_CHUNKY_BITS_PER_PLANE) ||
          (planes == GATE_VFX_PLANAR_PLANES && bpp == GATE_VFX_PLANAR_BITS_PER_PLANE)))
        abort();
    stride = image_word(image, GATE_VFX_IMAGE_ROW_STRIDE);
    height = image_word(image, GATE_VFX_IMAGE_PLANE_ROWS);
    data.offset = image_word(image, GATE_VFX_IMAGE_PIXELS_OFFSET);
    data.segment = image_word(image, GATE_VFX_IMAGE_PIXELS_SEGMENT);
    /* +34 is already height * plane_count (245d:0006), rather than the
     * logical height in +2e. Original 2479:0002 uses it directly. */
    for (row = 0; row < height; ++row) {
        uint8_t fill = planes == GATE_VFX_CHUNKY_PLANES ? (uint8_t)pattern :
            (pattern & (1u << (row % planes))) != 0 ? GATE_VFX_PIXEL_BYTE_ALL_BITS : 0;
        for (index = 0; index < stride; ++index)
            set_pixel_byte(data, row * stride + index, fill);
    }
}

/* 145d:016c -> resident 245d:016c, original CA00 resource destructor.
 * The original clears all 128 bytes of the passed image record to 0xff
 * after the FAR heap free succeeds. */
void gate_rtl_graphics_release(GateFarPtr image)
{
    GateFarPtr data;
    uint16_t i;
    if (image_word(image, GATE_VFX_IMAGE_SIGNATURE) != GATE_VFX_IMAGE_SIGNATURE_VALUE)
        return;
    data.offset = image_word(image, GATE_VFX_IMAGE_PIXELS_OFFSET);
    data.segment = image_word(image, GATE_VFX_IMAGE_PIXELS_SEGMENT);
    if (gate_linked_release_vfx_pixels(data) != 0)
        return;
    for (i = 0; i < GATE_VFX_IMAGE_HEADER_BYTES; ++i)
        set_image_byte(image, i, GATE_VFX_RELEASED_IMAGE_FILL_BYTE);
}

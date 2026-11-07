/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_image.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    MODE_SEG = 0x1000, STATUS_SEG = 0x2000, DATA_SEG = 0x3000,
    PALETTE_SEG = 0x4000, SOURCE_SEG = 0x5000, MARKER_SEG = 0x6000,
    PALETTE_SOURCE_SEG = 0x7000,
};
static uint8_t mode_data[0x10000], status_data[0x10000];
static uint8_t data[0x10000], palette_data[0x10000], source_data[0x10000];
static uint8_t marker_data[0x10000], palette_source[0x10000];
static uint16_t rtl_ds_compressed;
static unsigned uploaded;

static uint8_t *segment(uint16_t seg)
{
    switch (seg) {
    case MODE_SEG: return mode_data;
    case STATUS_SEG: return status_data;
    case DATA_SEG: return data;
    case PALETTE_SEG: return palette_data;
    case SOURCE_SEG: return source_data;
    case MARKER_SEG: return marker_data;
    case PALETTE_SOURCE_SEG: return palette_source;
    default: assert(!"unexpected FAR segment"); return 0;
    }
}

uint8_t gate_far_byte(uint16_t seg, uint16_t offset)
{
    return segment(seg)[offset];
}

uint16_t gate_far_word(uint16_t seg, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(seg, offset) |
        (uint16_t)(gate_far_byte(seg, (uint16_t)(offset + 1u)) << 8));
}

void gate_far_set_byte(uint16_t seg, uint16_t offset, uint8_t value)
{
    segment(seg)[offset] = value;
}

void gate_far_set_word(uint16_t seg, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(seg, offset, (uint8_t)value);
    gate_far_set_byte(seg, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}

uint16_t gate_rtl_image_mode_segment(void) { return MODE_SEG; }
uint16_t gate_rtl_preparer_status_segment(void) { return STATUS_SEG; }
uint16_t gate_rtl_preparer_flag_source_segment(void) { return SOURCE_SEG; }
uint16_t gate_rtl_preparer_word_dest_segment(void) { return MARKER_SEG; }
uint16_t gate_rtl_image_data_segment(void) { return DATA_SEG; }
uint16_t gate_rtl_image_palette_segment(void) { return PALETTE_SEG; }
GateFarPtr gate_rtl_image_palette_source(void)
{
    return (GateFarPtr){PALETTE_SOURCE_SEG, GATE_RTL_TRANSFORM_OFFSET};
}

uint16_t gate_rtl_resource_ds_word(uint16_t offset)
{
    assert(offset == 6);
    return rtl_ds_compressed;
}

void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value)
{
    assert(offset == 6);
    rtl_ds_compressed = value;
}

void gate_linked_upload_intro_palette(GateFarPtr palette)
{
    assert(palette.segment == PALETTE_SOURCE_SEG &&
           palette.offset == GATE_RTL_TRANSFORM_OFFSET);
    assert(rtl_ds_compressed == 0); /* original clears first */
    ++uploaded;
}

static void reset_case(uint16_t video_mode, uint16_t compressed)
{
    unsigned i;
    memset(mode_data, 0, sizeof mode_data);
    memset(status_data, 0xa5, sizeof status_data);
    memset(data, 0xa5, sizeof data);
    memset(palette_data, 0xa5, sizeof palette_data);
    memset(source_data, 0xa5, sizeof source_data);
    memset(marker_data, 0xa5, sizeof marker_data);
    memset(palette_source, 0, sizeof palette_source);
    gate_far_set_word(MODE_SEG, GATE_RTL_VIDEO_MODE_OFFSET, video_mode);
    gate_far_set_word(DATA_SEG, 0x84, 0x1234);
    for (i = 0; i < 4; ++i)
        data[0x86 + i] = (uint8_t)(0x50 + i);
    for (i = 0; i < 0x300; ++i)
        palette_source[GATE_RTL_TRANSFORM_OFFSET + i] = (uint8_t)(i * 17u + 3u);
    source_data[0x82] = 0x51;
    source_data[0x83] = 0x00;
    rtl_ds_compressed = compressed;
    uploaded = 0;
}

static void test_video_off_does_nothing(void)
{
    reset_case(0, 1);
    gate_rtl_prepare_picture_display();
    assert(uploaded == 0 && rtl_ds_compressed == 1);
    assert(gate_far_word(STATUS_SEG, 0x70) == 0xa5a5);
    assert(source_data[0x82] == 0x51);
}

static void test_original_status_and_flags(void)
{
    unsigned i;
    reset_case(3, 0);
    gate_rtl_prepare_picture_display();
    assert(uploaded == 0);
    assert(gate_far_word(STATUS_SEG, 0x70) == 1);
    for (i = 0; i < 8; ++i)
        assert(data[0x72 + i] == 0);
    for (i = 0; i < 4; ++i)
        assert(data[0x7e + i] == (uint8_t)(0x50 + i));
    assert(gate_far_word(MARKER_SEG, 0x7c) == 0x1234);
    assert(palette_data[0x7a] == 0x51 && palette_data[0x7b] == 0);
    assert(source_data[0x82] == 0 && source_data[0x83] == 0);
    assert(data[0x56ce] == 0xa5);

    reset_case(3, 0);
    source_data[0x82] = source_data[0x83] = 0;
    gate_rtl_prepare_picture_display();
    assert(gate_far_word(STATUS_SEG, 0x70) == 0);
}

static void test_original_palette_upload_and_copy(void)
{
    unsigned i;
    reset_case(2, 1);
    gate_rtl_prepare_picture_display();
    assert(uploaded == 1 && rtl_ds_compressed == 0);
    for (i = 0; i < 0x300; ++i)
        assert(data[0x56ce + i] == (uint8_t)(i * 17u + 3u));
    assert(data[0x56ce + 0x300] == 0xa5);
    assert(gate_far_word(STATUS_SEG, 0x70) == 1);
}

int main(void)
{
    test_video_off_does_nothing();
    test_original_status_and_flags();
    test_original_palette_upload_and_copy();
    puts("original RTL image preparer, palette copying and display flags: OK");
    return 0;
}

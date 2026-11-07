/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_image.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    WORK_SEG = 0x1111, MODE_SEG = 0x2222, HEADER_SEG = 0x3333,
    DATA_SEG = 0x4444, TABLE_SEG = 0x5555, SRC16_SEG = 0x6666,
    SRC48_SEG = 0x7777, PIXEL_SEG = 0x8888, PALETTE_SEG = 0x9999
};
static uint8_t work_bytes[0x10000], mode_bytes[0x10000];
static uint8_t header_bytes[0x10000], data_bytes[0x10000];
static uint8_t table_bytes[0x10000], src16_bytes[0x10000];
static uint8_t src48_bytes[0x10000], pixel_bytes[0x10000];
static GateFarPtr work = {WORK_SEG, 0xffd9};
static unsigned src16_calls, src48_calls, print_calls;

static uint8_t *memory(uint16_t seg)
{
    switch (seg) {
    case WORK_SEG: return work_bytes;
    case MODE_SEG: return mode_bytes;
    case HEADER_SEG: return header_bytes;
    case DATA_SEG: return data_bytes;
    case TABLE_SEG: return table_bytes;
    case SRC16_SEG: return src16_bytes;
    case SRC48_SEG: return src48_bytes;
    case PIXEL_SEG: return pixel_bytes;
    default: assert(!"invalid original FAR segment"); return 0;
    }
}

uint8_t gate_far_byte(uint16_t seg, uint16_t offset)
{
    return memory(seg)[offset];
}

uint16_t gate_far_word(uint16_t seg, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(seg, offset) |
        (uint16_t)(gate_far_byte(seg, (uint16_t)(offset + 1u)) << 8));
}

void gate_far_set_byte(uint16_t seg, uint16_t offset, uint8_t val)
{
    memory(seg)[offset] = val;
}

void gate_far_set_word(uint16_t seg, uint16_t offset, uint16_t val)
{
    gate_far_set_byte(seg, offset, (uint8_t)val);
    gate_far_set_byte(seg, (uint16_t)(offset + 1u), (uint8_t)(val >> 8));
}

uint16_t gate_rtl_image_mode_segment(void) { return MODE_SEG; }
uint16_t gate_rtl_image_header_segment(void) { return HEADER_SEG; }
uint16_t gate_rtl_image_transform_segment(void) { return TABLE_SEG; }
uint16_t gate_rtl_image_data_segment(void) { return DATA_SEG; }
uint8_t gate_rtl_image_data_byte(uint16_t offset) { return data_bytes[offset]; }
GateFarPtr gate_rtl_image_palette_source(void)
{
    return (GateFarPtr){PALETTE_SEG, GATE_RTL_TRANSFORM_OFFSET};
}

GateFarPtr gate_linked_transform_picture_palette_indexed(GateFarPtr palette)
{
    assert(palette.segment == PALETTE_SEG &&
           palette.offset == GATE_RTL_TRANSFORM_OFFSET);
    ++src16_calls;
    return (GateFarPtr){SRC16_SEG, 0xfff7};
}

GateFarPtr gate_linked_transform_picture_palette_rgb(GateFarPtr palette)
{
    assert(palette.segment == PALETTE_SEG &&
           palette.offset == GATE_RTL_TRANSFORM_OFFSET);
    ++src48_calls;
    return (GateFarPtr){SRC48_SEG, 0xffe7};
}

void gate_linked_print_intro_diagnostic(GateFarPtr message)
{
    assert(message.segment == DATA_SEG && message.offset == GATE_RTL_MESSAGE_OFFSET);
    ++print_calls;
}

static uint16_t word(uint16_t offset)
{
    return gate_far_word(work.segment, (uint16_t)(work.offset + offset));
}

static void set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(work.segment, (uint16_t)(work.offset + offset), value);
}

static void reset_case(uint8_t flags, int8_t format_tag, uint16_t mode)
{
    unsigned i;
    memset(work_bytes, 0, sizeof work_bytes);
    memset(header_bytes, 0, sizeof header_bytes);
    memset(mode_bytes, 0, sizeof mode_bytes);
    memset(data_bytes, 0, sizeof data_bytes);
    memset(table_bytes, 0, sizeof table_bytes);
    memset(pixel_bytes, 0, sizeof pixel_bytes);
    src16_calls = src48_calls = print_calls = 0;
    gate_far_set_byte(HEADER_SEG, GATE_RTL_HEADER_OFFSET + 5, flags);
    gate_far_set_word(MODE_SEG, GATE_RTL_VIDEO_MODE_OFFSET, mode);
    data_bytes[GATE_RTL_FORMAT_TAGS_OFFSET + (flags & 0x0f)] = (uint8_t)format_tag;
    data_bytes[GATE_RTL_FORMAT_MODES_OFFSET + mode] = (uint8_t)format_tag;
    for (i = 0; i < 0x10000; ++i) {
        src16_bytes[i] = (uint8_t)(i * 17u + 3u);
        src48_bytes[i] = (uint8_t)(i * 53u + 7u);
    }
}

static void test_format_selection(void)
{
    reset_case(3, -1, 2);
    assert(gate_rtl_detect_picture_format() == 0xffff);
    data_bytes[GATE_RTL_FORMAT_MODES_OFFSET + 2] = 1;
    assert(gate_rtl_detect_picture_format() == 0);
    reset_case(6, 1, 3);
    assert(gate_rtl_detect_picture_format() == 1);
    reset_case(6, 0, 3);
    assert(gate_rtl_detect_picture_format() == 0);
}

static void test_original_format_table_copy(void)
{
    uint16_t i;
    reset_case(0x13, -1, 2);
    gate_rtl_prepare_picture_palette_translation(0xffff);
    assert(src16_calls == 1 && src48_calls == 0);
    for (i = 0; i < 16; ++i)
        assert(table_bytes[GATE_RTL_TRANSFORM_OFFSET + i] ==
               src16_bytes[(uint16_t)(0xfff7u + i)]);
    assert(table_bytes[GATE_RTL_TRANSFORM_OFFSET + 16] == 0);

    gate_rtl_prepare_picture_palette_translation(1);
    assert(src16_calls == 1 && src48_calls == 1);
    for (i = 0; i < 48; ++i)
        assert(table_bytes[GATE_RTL_TRANSFORM_OFFSET + i] ==
               src48_bytes[(uint16_t)(0xffe7u + i)]);
    gate_rtl_prepare_picture_palette_translation(2);
    assert(src16_calls == 1 && src48_calls == 1);

    reset_case(3, -1, 2); /* missing header bit 0x10 */
    gate_rtl_prepare_picture_palette_translation(1);
    assert(src16_calls == 0 && src48_calls == 0);
}

static void test_original_coordinate_conversion(void)
{
    assert(gate_rtl_transform_picture_coordinate(0, 0xfffe) == 0xfffe);
    assert(gate_rtl_transform_picture_coordinate(0xffff, 7) == 6);
    assert(gate_rtl_transform_picture_coordinate(0xffff, (uint16_t)-7) == (uint16_t)-6);
    assert(gate_rtl_transform_picture_coordinate(0xffff, 0x8000) == 0x6000);
    assert(gate_rtl_transform_picture_coordinate(1, 100) == 133);
    assert(gate_rtl_transform_picture_coordinate(1, 0x7fff) == (uint16_t)-1);
    assert(gate_rtl_transform_picture_coordinate(2, 100) == 100);
}

static void test_original_in_place_compaction(void)
{
    uint16_t i;
    reset_case(3, -1, 2);
    for (i = 0; i < 20; ++i)
        pixel_bytes[(uint16_t)(0xfffbu + i)] = (uint8_t)(i + 1u);
    gate_rtl_compact_picture_rows((GateFarPtr){PIXEL_SEG, 0xfffb},
                    (GateFarPtr){PIXEL_SEG, 0xfffb}, 0, 4, 4);
    for (i = 0; i < 12; ++i)
        assert(pixel_bytes[(uint16_t)(0xfffbu + i)] == (uint8_t)(i + 1u));
    for (i = 0; i < 4; ++i)
        assert(pixel_bytes[(uint16_t)(0xfffbu + 12u + i)] == (uint8_t)(17u + i));
}

static void setup_work(uint16_t first, uint16_t height, uint16_t top)
{
    set_word(4, first);
    set_word(0x0e, 0);
    set_word(0x10, top);
    set_word(0x12, height);
    set_word(0x14, height);
    set_word(0x1c, 1); /* source row length = 4 bytes */
    set_word(0x20, 0x0100); /* image pixels FAR pointer */
    set_word(0x22, PIXEL_SEG);
}

static void test_original_postload_empty(void)
{
    reset_case(3, 0, 2);
    setup_work(10, 5, 0);
    gate_rtl_postprocess_picture(work);
    assert(word(4) == 10);
    assert(src16_calls == 0 && src48_calls == 0 && print_calls == 0);
}

static void test_original_postload_negative(void)
{
    unsigned i;
    reset_case(0x13, -1, 2);
    setup_work(0, 3, 0); /* end=3; the index-3 row must be skipped */
    for (i = 0; i < 20; ++i)
        pixel_bytes[0x100 + i] = (uint8_t)(i + 1u);
    gate_rtl_postprocess_picture(work);
    assert(src16_calls == 1 && src48_calls == 0 && print_calls == 0);
    assert(word(4) == 0);
    assert(word(0x14) == 2); /* compacted count - 1 */
    assert(word(0x36) == 3 && word(0x3c) == 12);
    for (i = 0; i < 12; ++i)
        assert(pixel_bytes[0x100 + i] == (uint8_t)(i + 1u));
}

static void test_original_postload_positive(void)
{
    reset_case(0x13, 1, 2);
    setup_work(4, 5, 1); /* end=8, positive format uses message service */
    gate_rtl_postprocess_picture(work);
    assert(word(4) == 5); /* signed (4*4) / 3 */
    assert(word(0x14) == 5); /* original field remains unchanged */
    assert(print_calls == 1 && src16_calls == 0 && src48_calls == 1);
}

int main(void)
{
    test_format_selection();
    test_original_format_table_copy();
    test_original_coordinate_conversion();
    test_original_in_place_compaction();
    test_original_postload_empty();
    test_original_postload_negative();
    test_original_postload_positive();
    puts("original RTL image post-load helpers, signed transforms and row compaction: OK");
    return 0;
}

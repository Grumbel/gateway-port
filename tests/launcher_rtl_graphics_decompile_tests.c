/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_graphics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t mode, index_value, last_size_index;
static int16_t size_words;
static uint8_t buffer[0x800];
static unsigned index_writes, pixel_writes, size_reads;
static struct Upload {
    char kind;
    uint8_t bytes[0x300];
    uint8_t before_reset[16];
    uint32_t delay;
} uploads[40];
static unsigned nuploads, byte_count;
static uint8_t work_memory[0x10000];
static unsigned releases;
static GateFarPtr released;
static uint16_t active_work_offset;

uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x9999);
    return work_memory[offset];
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == 0x9999);
    work_memory[offset] = value;
}
void gate_rtl_graphics_release(GateFarPtr pointer)
{
    assert(work_memory[active_work_offset] != 0);
    released = pointer;
    ++releases;
}

uint16_t gate_rtl_graphics_mode(void) { return mode; }
void gate_rtl_graphics_set_index(uint16_t value)
{
    index_value = value;
    ++index_writes;
}
int16_t gate_rtl_graphics_byte_count(uint16_t index)
{
    last_size_index = index;
    ++size_reads;
    return size_words;
}
uint8_t gate_rtl_graphics_byte(uint16_t index) { return buffer[index]; }
void gate_rtl_graphics_set_byte(uint16_t index, uint8_t value)
{
    buffer[index] = value;
    ++pixel_writes;
}
void gate_rtl_graphics_palette_upload(const uint8_t *palette)
{
    struct Upload *entry;
    assert(nuploads < sizeof uploads / sizeof uploads[0]);
    entry = &uploads[nuploads++];
    entry->kind = palette ? 'P' : 'N';
    memcpy(entry->before_reset, buffer, sizeof entry->before_reset);
    if (palette)
        memcpy(entry->bytes, palette, byte_count);
}
void gate_rtl_graphics_delay(uint32_t ms)
{
    struct Upload *entry;
    assert(nuploads < sizeof uploads / sizeof uploads[0]);
    entry = &uploads[nuploads++];
    entry->kind = 'D';
    entry->delay = ms;
}

static void reset(uint16_t video_mode, int16_t size)
{
    unsigned i;
    mode = video_mode;
    size_words = size;
    nuploads = index_writes = pixel_writes = size_reads = 0;
    last_size_index = 0xffff;
    index_value = 1234;
    byte_count = (unsigned)(size > 0 ? size : 0);
    memset(uploads, 0, sizeof uploads);
    for (i = 0; i < sizeof buffer; ++i)
        buffer[i] = (uint8_t)(i * 113u + 128u);
}

/* Independent arithmetic reference: 8086 IMUL low 16 bits and C-style
 * truncation for negative quotients, not an unsigned right shift. */
static uint8_t expected(uint8_t sample, int factor)
{
    int32_t full_product = (int8_t)sample * factor;
    int16_t low_product = (int16_t)(uint16_t)full_product;
    return (uint8_t)(low_product / 64);
}

static void test_palette_fade_in(void)
{
    unsigned step, i;
    uint8_t original[0x300];
    reset(3, 0x300);
    memcpy(original, buffer, sizeof original);
    gate_rtl_fade_palette_in();
    assert(index_writes == 1 && index_value == 0);
    assert(nuploads == 17 && pixel_writes == 0 && size_reads == 0);
    for (step = 0; step < 17; ++step) {
        assert(uploads[step].kind == 'P');
        for (i = 0; i < sizeof original; ++i)
            assert(uploads[step].bytes[i] == expected(original[i], (int)step * 4));
    }
    assert(memcmp(original, buffer, sizeof original) == 0);

    /* All non-1/non-3 nonzero modes select the shorter, fixed 0x30 path. */
    reset(2, 0x300);
    byte_count = 0x30;
    gate_rtl_fade_palette_in();
    assert(nuploads == 17);
    for (step = 0; step < 17; ++step)
        for (i = 0; i < 0x30; ++i)
            assert(uploads[step].bytes[i] == expected(buffer[i], (int)step * 4));

    reset(1, 16);
    gate_rtl_fade_palette_in();
    assert(index_writes == 1 && nuploads == 1 && uploads[0].kind == 'N');
    reset(0, 16);
    gate_rtl_fade_palette_in();
    assert(index_writes == 0 && nuploads == 0);
}

static void test_buffer_reset(void)
{
    unsigned i;
    reset(0, 20);
    gate_rtl_clear_palette_buffer();
    assert(pixel_writes == 0 && nuploads == 0 && size_reads == 0);

    reset(3, 0x300);
    gate_rtl_clear_palette_buffer();
    assert(size_reads == 2 && last_size_index == 6);
    assert(pixel_writes == 0x300 && nuploads == 1 && uploads[0].kind == 'N');
    for (i = 0; i < 0x300; ++i)
        assert(buffer[i] == 0);
    assert(buffer[0x300] == (uint8_t)(0x300u * 113u + 128u));

    reset(0xffff, -1);
    gate_rtl_clear_palette_buffer();
    assert(last_size_index == 0xfffe && pixel_writes == 0);
    assert(nuploads == 1 && uploads[0].kind == 'N');
}

static void test_palette_fade_out(void)
{
    unsigned step, i;
    uint8_t original[8];
    reset(2, 8);
    memcpy(original, buffer, 8);
    gate_rtl_fade_palette_out();
    assert(index_writes == 1 && index_value == 0);
    assert(last_size_index == 4 && size_reads == 3);
    assert(nuploads == 18 && pixel_writes == 8);
    for (step = 0; step < 17; ++step) {
        assert(uploads[step].kind == 'P');
        for (i = 0; i < 8; ++i)
            assert(uploads[step].bytes[i] == expected(original[i], 64 - (int)step * 4));
    }
    assert(uploads[17].kind == 'N');
    for (i = 0; i < 8; ++i)
        assert(buffer[i] == 0);
    assert(buffer[8] == (uint8_t)(8u * 113u + 128u));

    reset(0, 8);
    gate_rtl_fade_palette_out();
    assert(nuploads == 0 && index_writes == 0);
}

static uint8_t mode_one_step(uint8_t sample)
{
    int16_t signed_shift = (int16_t)((int8_t)sample >> 3);
    return (uint8_t)((((uint8_t)~signed_shift & sample & 7u) << 3) |
                     (sample & (uint8_t)signed_shift & 7u));
}

static void test_mode_one_destructive_transform(void)
{
    unsigned step, i;
    uint8_t expected_bytes[16];
    reset(1, 16);
    memcpy(expected_bytes, buffer, sizeof expected_bytes);
    gate_rtl_fade_palette_out();
    assert(index_writes == 1 && nuploads == 7);
    assert(pixel_writes == 3 * 16 + 16 && size_reads == 2);
    for (step = 0; step < 3; ++step) {
        for (i = 0; i < sizeof expected_bytes; ++i)
            expected_bytes[i] = mode_one_step(expected_bytes[i]);
        assert(uploads[step * 2].kind == 'N');
        assert(memcmp(uploads[step * 2].before_reset, expected_bytes,
                      sizeof expected_bytes) == 0);
        assert(uploads[step * 2 + 1].kind == 'D');
        assert(uploads[step * 2 + 1].delay == 100);
    }
    assert(uploads[6].kind == 'N');
    for (i = 0; i < sizeof expected_bytes; ++i)
        assert(buffer[i] == 0); /* final RTL:0x12823 */
    assert(buffer[16] == (uint8_t)(16u * 113u + 128u));
}

static void test_original_far_work_record(void)
{
    GateFarPtr ptr = {0x9999, 0xfffc};
    memset(work_memory, 0, sizeof work_memory);
    work_memory[0xfffc] = 0x5a;
    active_work_offset = ptr.offset;
    releases = 0;

    gate_rtl_initialize_picture_record(ptr);
    assert(work_memory[0xfffc] == 0 && releases == 0);
    gate_rtl_release_picture_record(ptr);
    assert(releases == 0);

    work_memory[0xfffc] = 0xa5;
    gate_rtl_release_picture_record(ptr);
    assert(releases == 1 && work_memory[0xfffc] == 0);
    assert(released.segment == ptr.segment && released.offset == 4);
    gate_rtl_release_picture_record(ptr);
    assert(releases == 1); /* idempotent once first byte is clear */

    /* Original checks both pointer words together before dereferencing. */
    gate_rtl_release_picture_record((GateFarPtr){0, 0});
    assert(releases == 1);
}

int main(void)
{
    test_palette_fade_in();
    test_buffer_reset();
    test_palette_fade_out();
    test_mode_one_destructive_transform();
    test_original_far_work_record();
    puts("original RTL palette/graphics transitions: OK");
    return 0;
}

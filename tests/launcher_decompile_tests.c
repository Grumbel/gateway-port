/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_exe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t ds[65536];
static uint8_t palette[65536];
static unsigned palette_writes, palette_uploads, key_reads, keyboard_flushes;
static unsigned message_calls, clear_calls, center_calls, print_calls, special_calls;
static uint16_t keyboard_pending, keyboard_key, mouse_flags;
static uint32_t clock_now, clock_step;
static int16_t center_x, center_y;
static uint16_t intro_mode;
static unsigned intro_short_init, intro_short_special, intro_common_init;
static unsigned intro_interlude;
static unsigned intro_cleanup, intro_finished, intro_setup, intro_video, intro_credit;
static unsigned intro_fullscreen, intro_palette, intro_mode1, intro_rect;
static unsigned intro_caption_count, intro_image_count, intro_anim_count;
static unsigned intro_fade_count;
static uint16_t intro_caption_text[18], intro_caption_left[18];
static uint16_t intro_caption_top[18], intro_caption_right[18];
static uint16_t intro_images[16], intro_fades[16];
static uint16_t intro_anim_base[5], intro_anim_next[5], intro_anim_lengths[5];
static uint16_t intro_word_writes, intro_last_segment, intro_last_offset;
static uint16_t intro_last_value;
static uint8_t *stack_pointer;
static unsigned local_palette_uploads, fade_prepare_calls, fade_finish_calls;
static uint8_t local_palette_sample_zero[9 * 17];
static uint8_t local_palette_sample_one[9 * 17];

static void reset(void)
{
    memset(ds, 0, sizeof ds);
    memset(palette, 0, sizeof palette);
    palette_writes = palette_uploads = key_reads = keyboard_flushes = 0;
    message_calls = clear_calls = center_calls = print_calls = special_calls = 0;
    keyboard_pending = keyboard_key = mouse_flags = 0;
    clock_now = 0;
    clock_step = 1000;
    intro_mode = 0;
    intro_short_init = intro_short_special = intro_common_init = 0;
    intro_interlude = 0;
    intro_cleanup = intro_finished = intro_setup = intro_video = intro_credit = 0;
    intro_fullscreen = intro_palette = intro_mode1 = intro_rect = 0;
    intro_caption_count = intro_image_count = intro_anim_count = intro_fade_count = 0;
    intro_word_writes = intro_last_segment = intro_last_offset = intro_last_value = 0;
    stack_pointer = NULL;
    local_palette_uploads = fade_prepare_calls = fade_finish_calls = 0;
    memset(intro_caption_text, 0, sizeof intro_caption_text);
    memset(intro_images, 0, sizeof intro_images);
}

uint16_t gate_ds_word(uint16_t offset) { return ds[offset]; }
void gate_ds_set_word(uint16_t offset, uint16_t value) { ds[offset] = value; }
uint16_t gate_ds_segment(void) { return 0x1234; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x6666);
    return palette[offset];
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    if (segment == 0x2222 && offset == 0x54) return 12;
    if (segment == 0x3333 && offset == 0x3e) return 80;
    if (segment == 0x4444 && offset == 0x42) return 25;
    if (segment == 0x5555 && offset == 0x0fce) return intro_mode;
    assert(!"unexpected far memory read");
    return 0;
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    assert(segment == 0x6666);
    palette[offset] = value;
    ++palette_writes;
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    assert(segment >= 0x7000 && segment <= 0x7004);
    ++intro_word_writes;
    intro_last_segment = segment;
    intro_last_offset = offset;
    intro_last_value = value;
}
uint16_t gate_far_0df2_000a(void) { return keyboard_pending; }
uint16_t gate_far_0df2_0030(void) { ++key_reads; return keyboard_key; }
void gate_far_0df2_00f8(void) { ++keyboard_flushes; }
uint16_t gate_far_0e48_07fe(void) { return mouse_flags; }
uint32_t gate_far_02a5_38c4(void) { clock_now += clock_step; return clock_now; }
void gate_far_123f_0004(void) { ++special_calls; }
void gate_far_0c4d_018d(uint16_t value) { assert(value == 1); ++clear_calls; }
void gate_far_0c4d_061b(uint16_t handle, uint16_t mode)
{
    assert(handle == 12 && mode == 0xffff);
    ++message_calls;
}
uint16_t gate_far_0c4d_05be(GateFarPtr string)
{
    assert(string.segment == 0x1234 && string.offset == 0x35fe);
    return 16;
}
uint16_t gate_far_0c4d_0511(void) { return 5; }
void gate_far_0c4d_0653(int16_t x, int16_t y)
{
    center_x = x;
    center_y = y;
    ++center_calls;
}
void gate_far_0c4d_0698(GateFarPtr string)
{
    assert(string.segment == 0x1234 && string.offset == 0x35fe);
    ++print_calls;
}
void gate_far_0cdc_04d2(GateFarPtr ptr)
{
    if (ptr.segment == 0x7777) {
        assert(ptr.offset == 0x7777 && stack_pointer != NULL);
        assert(local_palette_uploads < 9 * 17);
        local_palette_sample_zero[local_palette_uploads] = stack_pointer[0];
        local_palette_sample_one[local_palette_uploads] = stack_pointer[1];
        ++local_palette_uploads;
    } else {
        assert(ptr.segment == 0x1a7a && ptr.offset == 0x0fdc);
    }
    ++palette_uploads;
}
GateFarPtr gate_stack_pointer(void *ptr)
{
    GateFarPtr result = {0x7777, 0x7777};
    stack_pointer = ptr;
    return result;
}
uint16_t gate_far_0e2c_0094(uint16_t value)
{
    assert(value == 0x1000);
    ++intro_setup;
    return 0x1337;
}
void gate_far_0a46_01ec(void) { ++intro_common_init; }
void gate_far_0f10_0d1e(void) { ++intro_short_init; }
void gate_far_0f10_0d8e(void) { ++intro_cleanup; }
void gate_far_0f10_0c6e(void) { ++intro_interlude; }
void gate_far_0f10_1014(uint16_t image, uint16_t mode)
{
    assert(intro_image_count < sizeof intro_images / sizeof intro_images[0]);
    assert(mode <= 1);
    intro_images[intro_image_count++] = image;
    if (mode == 0) ++intro_fullscreen;
}
void gate_far_0f10_15a6(uint16_t image, uint16_t value)
{
    assert(value == 26);
    assert(image == 0x3001 || image == 0x3106);
    ++intro_video;
}
void gate_far_0f10_1466(uint16_t first, uint16_t next, uint16_t count)
{
    assert(intro_anim_count < 5);
    intro_anim_base[intro_anim_count] = first;
    intro_anim_next[intro_anim_count] = next;
    intro_anim_lengths[intro_anim_count++] = count;
}
void gate_far_0f10_1680(GateFarPtr buffer)
{
    assert(buffer.segment == 0x7777 && buffer.offset == 0x7777);
    ++fade_prepare_calls;
}
void gate_far_0f10_112e(GateFarPtr buffer, uint16_t image, uint16_t mode)
{
    assert(buffer.segment == 0x7777 && buffer.offset == 0x7777 && mode == 0);
    assert(intro_fade_count < 16);
    intro_fades[intro_fade_count++] = image;
}
void gate_far_0f10_13d2(GateFarPtr buffer)
{
    assert(buffer.segment == 0x7777 && buffer.offset == 0x7777);
    ++fade_finish_calls;
}
void gate_far_0f10_16c6(GateFarPtr buffer)
{
    assert(buffer.segment == 0x7777 && buffer.offset == 0x7777);
    ++fade_finish_calls;
}
void gate_far_0cdc_07d8(void) { ++intro_palette; }
void gate_far_0cdc_0004(uint16_t value) { assert(value == 3); ++intro_mode1; }
void gate_far_0cdc_0aac(uint16_t value) { assert(value == 15); ++intro_short_special; }
void gate_far_0cdc_0aee(uint16_t a, uint16_t b, uint16_t c,
                         uint16_t d, uint16_t e)
{
    assert(a == 2 && b == 0 && c == 0 && d == 80 && e == 25);
    ++intro_rect;
}
void gate_far_0e48_011b(void) { ++intro_credit; }
void gate_far_0df2_0128(void) { ++intro_finished; }
void gate_exe_117e(uint16_t left, uint16_t top, uint16_t right, GateFarPtr text)
{
    unsigned i = intro_caption_count++;
    assert(i < 18 && text.segment == 0x1234);
    intro_caption_text[i] = text.offset;
    intro_caption_left[i] = left;
    intro_caption_top[i] = top;
    intro_caption_right[i] = right;
}

static void test_wait(void)
{
    reset();
    clock_step = 1;
    gate_exe_0f18(3);
    assert(clock_now == 4);

    reset();
    keyboard_pending = 1;
    clock_step = 1;
    gate_exe_0f18(2000);
    assert(clock_now == 2);

    reset();
    mouse_flags = 1; /* wait treats any nonzero mouse status as input */
    clock_step = 1;
    gate_exe_0f18(2000);
    assert(clock_now == 2);

    reset();
    clock_now = 0x7ffffffd;
    clock_step = 1;
    gate_exe_0f18(3);
    /* Deliberately preserve the ORIGINAL signed-high-word comparison. */
    assert(clock_now == 0x7fffffff);
}

static void test_bar_fade(void)
{
    reset();
    ds[0x4e42] = 0x6666;
    gate_exe_1c11(6);
    assert(palette_uploads == 64 && palette_writes == 64);
    assert(palette[0x0fdc + 45 + 3 * 6] == 63);
    assert(clock_now >= 500);

    reset();
    keyboard_pending = 1;
    gate_exe_1c11(6);
    assert(palette_uploads == 0 && palette_writes == 0 && clock_now == 0);
}

static void test_credit_fade(void)
{
    reset();
    ds[0x4e42] = 0x6666;
    gate_exe_1c6c();
    assert(palette_writes == 192 && palette_uploads == 64);
    assert(palette[0x1021] == 63 && palette[0x1022] == 63 &&
           palette[0x1023] == 63);
    assert(clock_now == 0);
}

static void test_input(void)
{
    reset();
    mouse_flags = 2;
    assert(gate_exe_0e37() == 1);
    mouse_flags = 1;
    assert(gate_exe_0e37() == 0);

    reset();
    keyboard_pending = 1;
    keyboard_key = 27;
    assert(gate_exe_0e37() == 1 && ds[0x35fc] == 0);
    keyboard_key = 3;
    assert(gate_exe_0e37() == 0 && special_calls == 1);

    reset();
    keyboard_pending = 1;
    keyboard_key = 'x';
    ds[0x4e40] = 0x2222;
    ds[0x4e30] = 0x3333;
    ds[0x4e2e] = 0x4444;
    ds[0x4e2a] = 0x5555;
    assert(gate_exe_0e37() == 0);
    assert(ds[0x35fc] == 1);
    assert(center_x == 32 && center_y == 19);
    assert(message_calls == 1 && clear_calls == 1 &&
           center_calls == 1 && print_calls == 1 && keyboard_flushes == 1);
    assert(gate_exe_0e37() == 0);
    assert(message_calls == 1 && keyboard_flushes == 1);
}

static void test_enclosing_intro_short_paths(void)
{
    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    ds[0x4e2e] = 0x4444;
    ds[0x4e30] = 0x3333;
    ds[0x4e46] = 0x7000;
    ds[0x4e48] = 0x7001;
    ds[0x4e4a] = 0x7002;
    ds[0x4e4c] = 0x7003;
    ds[0x4e4e] = 0x7004;

    gate_exe_1302();
    assert(intro_setup == 1 && intro_common_init == 1 && intro_word_writes == 5);
    assert(intro_last_segment == 0x7004 && intro_last_offset == 0x658 &&
           intro_last_value == 0x85);
    assert(intro_short_init == 1 && intro_short_special == 0 && intro_rect == 0);
    assert(intro_image_count == 3 && intro_images[0] == 0x8101 &&
           intro_images[1] == 0x8100 && intro_images[2] == 0x8102);
    assert(intro_caption_count == 0 && intro_anim_count == 0);
    assert(intro_cleanup == 0 && intro_finished == 1);

    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    ds[0x4e2e] = 0x4444;
    ds[0x4e30] = 0x3333;
    ds[0x4e46] = 0x7000;
    ds[0x4e48] = 0x7001;
    ds[0x4e4a] = 0x7002;
    ds[0x4e4c] = 0x7003;
    ds[0x4e4e] = 0x7004;
    intro_mode = 1;
    gate_exe_1302();
    assert(intro_interlude == 1 && intro_short_init == 1 &&
           intro_short_special == 1 && intro_rect == 1);
    assert(intro_image_count == 3 && intro_images[0] == 0x8101 &&
           intro_images[1] == 0x8100 && intro_images[2] == 0x8102);
    assert(palette_uploads == 4 && palette_writes == 49);
    assert(intro_cleanup == 0 && intro_finished == 1);

    /* Signed JGE at 0x1398: 0xffff must take the short route. */
    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    ds[0x4e46] = 0x7000;
    ds[0x4e48] = 0x7001;
    ds[0x4e4a] = 0x7002;
    ds[0x4e4c] = 0x7003;
    ds[0x4e4e] = 0x7004;
    intro_mode = 0xffff;
    gate_exe_1302();
    assert(intro_short_init == 1 && intro_short_special == 0 && intro_rect == 0);
    assert(intro_caption_count == 0 && intro_finished == 1);
}

static void test_original_interlude_fade_and_key_exit(void)
{
    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    intro_mode = 2;
    gate_exe_1a14();
    assert(intro_interlude == 1 && intro_image_count == 2);
    assert(intro_images[0] == 0x8101 && intro_images[1] == 0x8100);
    assert(palette_writes == 45 + 4 * 64 * 3);
    assert(palette_uploads == 4 * 64);
    assert(palette[0x0fdf] == 63 && palette[0x0fe0] == 63 &&
           palette[0x0fe1] == 63);
    assert(palette[0x0fe5] == 0 && palette[0x0fe6] == 0 &&
           palette[0x0fe7] == 0);

    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    intro_mode = 2;
    keyboard_pending = 1;
    gate_exe_1a14();
    assert(intro_image_count == 1 && intro_images[0] == 0x8101);
    assert(palette_writes == 45 + 64 * 3 && palette_uploads == 64);
}

static void test_original_picture_fade_signed_palette(void)
{
    reset();
    ds[0x4e42] = 0x6666;
    palette[0x0fdc] = 63;
    palette[0x0fdd] = (uint8_t)-64;
    gate_exe_0f61(0x3105);

    assert(intro_cleanup == 1 && intro_fade_count == 1);
    assert(intro_fades[0] == 0x3105);
    assert(fade_prepare_calls == 1 && fade_finish_calls == 2);
    assert(palette_uploads == 18 && local_palette_uploads == 17);
    /* Zeroed palette, factor=0, factor=4, ..., factor=60. */
    assert(local_palette_sample_zero[0] == 0 && local_palette_sample_zero[1] == 0);
    assert(local_palette_sample_zero[2] == 3);
    assert(local_palette_sample_zero[16] == 59);
    assert(local_palette_sample_one[2] == (uint8_t)-4);
    assert(local_palette_sample_one[16] == (uint8_t)-60);
    assert(palette[0x0fdc] == 63 && palette[0x0fdd] == (uint8_t)-64);
}

static void test_enclosing_intro_complete_sequence(void)
{
    static const uint16_t text[] = {
        0x363b, 0x37bf, 0x3899, 0x395a, 0x3a65, 0x3b9b,
        0x3c4f, 0x3d71, 0x3e79, 0x3f3b, 0x4086, 0x4198,
        0x42ae, 0x436c, 0x4436, 0x45c4, 0x46c5, 0x4820
    };
    static const uint16_t left[] = {
        10, 10, 10, 10, 160, 180, 180, 130, 140,
        140, 140, 10, 60, 140, 120, 10, 10, 10
    };
    static const uint16_t top[] = {
        120, 100, 10, 10, 10, 10, 10, 10, 10,
        10, 10, 10, 120, 120, 10, 100, 10, 10
    };
    static const uint16_t right[] = {
        310, 180, 160, 310, 310, 310, 310, 310, 310,
        310, 310, 140, 310, 310, 310, 160, 310, 310
    };
    static const uint16_t anim_base[] = {
        0x332a, 0x3221, 0x3200, 0x3221, 0x3300
    };
    static const uint16_t anim_next[] = {
        0x332b, 0x323d, 0x3201, 0x3222, 0x3301
    };
    static const uint16_t anim_count[] = {37, 42, 32, 27, 41};
    static const uint16_t frame_ids[] = {
        0x3105, 0x3100, 0x3101, 0x3102, 0x3103,
        0x3104, 0x3200, 0x3a04, 0x332a
    };
    static const uint16_t image_ids[] = {
        0x8101, 0x8100, 0x3000, 0x301b, 0x3105, 0x3100, 0x3102,
        0x3221, 0x3221, 0x3221, 0x3104, 0x332a
    };

    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    ds[0x4e46] = 0x7000;
    ds[0x4e48] = 0x7001;
    ds[0x4e4a] = 0x7002;
    ds[0x4e4c] = 0x7003;
    ds[0x4e4e] = 0x7004;
    intro_mode = 2;
    gate_exe_1302();
    assert(intro_short_init == 0 && intro_interlude == 1 && intro_short_special == 0);
    assert(intro_caption_count == 18 && intro_anim_count == 5);
    assert(intro_fade_count == 9 && intro_image_count == 12);
    assert(intro_video == 2 && intro_cleanup == 2 + 9 && intro_finished == 1);
    assert(ds[0x35fc] == 1 && palette_uploads == 4 * 64 + 7 * 64 + 64 + 9 * 18);
    assert(local_palette_uploads == 9 * 17 && fade_prepare_calls == 9 &&
           fade_finish_calls == 18);
    for (unsigned i = 0; i < 18; ++i) {
        assert(intro_caption_text[i] == text[i]);
        assert(intro_caption_left[i] == left[i]);
        assert(intro_caption_top[i] == top[i]);
        assert(intro_caption_right[i] == right[i]);
    }
    for (unsigned i = 0; i < 5; ++i) {
        assert(intro_anim_base[i] == anim_base[i]);
        assert(intro_anim_next[i] == anim_next[i]);
        assert(intro_anim_lengths[i] == anim_count[i]);
    }
    for (unsigned i = 0; i < 9; ++i) assert(intro_fades[i] == frame_ids[i]);
    for (unsigned i = 0; i < 12; ++i) assert(intro_images[i] == image_ids[i]);

    /* An Escape at the first 0x13fc check jumps to 0x1a09, skips screens. */
    reset();
    ds[0x4e2a] = 0x5555;
    ds[0x4e42] = 0x6666;
    ds[0x4e46] = 0x7000;
    ds[0x4e48] = 0x7001;
    ds[0x4e4a] = 0x7002;
    ds[0x4e4c] = 0x7003;
    ds[0x4e4e] = 0x7004;
    intro_mode = 2;
    keyboard_pending = 1;
    keyboard_key = 27;
    gate_exe_1302();
    assert(intro_caption_count == 0 && intro_image_count == 1);
    assert(intro_cleanup == 1 && intro_finished == 1);
}

int main(void)
{
    test_wait();
    test_bar_fade();
    test_credit_fade();
    test_input();
    test_enclosing_intro_short_paths();
    test_original_interlude_fade_and_key_exit();
    test_original_picture_fade_signed_palette();
    test_enclosing_intro_complete_sequence();
    puts("GATE.EXE C decompilation: seven source-shaped routines checked by mocks");
    return 0;
}

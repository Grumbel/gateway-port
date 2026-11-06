/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gate_rtl_mode.h"
#include "gate_rtl.h"
#include "gate_intro_host_memory.h"
#include "gate_intro_host_vga_ports.h"
#include "gate_intro_host_video_mode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setup_synthetic(void)
{
    uint16_t i;
    const uint16_t segments = 0x3100;
    for (i = 0; i < 12; ++i)
        gate_ds_set_word((uint16_t)(0x4d92 + i * 2), segments);
    for (i = 0; i < 6; ++i) {
        gate_ds_set_word((uint16_t)(0x12e2 + 2u * i), 4);
        gate_ds_set_word((uint16_t)(0x12ee + 2u * i), 4);
    }
    for (i = 0; i < 5; ++i) {
        gate_ds_set_word((uint16_t)(0x12f8 + 2u * i),
                         (uint16_t)(319 + i));
        gate_ds_set_word((uint16_t)(0x1302 + 2u * i),
                         (uint16_t)(199 + i));
        gate_ds_set_word((uint16_t)(0x130c + 2u * i), 40);
        gate_ds_set_word((uint16_t)(0x1316 + 2u * i), 10);
        gate_ds_set_word((uint16_t)(0x1320 + 2u * i), 320);
    }
}

static void check_state(uint16_t mode, GateIntroVideoModeHardware *video,
                        GateIntroVgaPorts *ports)
{
    uint16_t i;
    static const uint16_t masks[3][5] = {
        {0, 0, 1, 1, 1}, {15, 2, 1, 0, 15}, {0, 2, 1, 255, 255}
    };
    const uint16_t *expected = mode == 0 ? masks[0] :
        mode == 3 ? masks[2] : masks[1];
    const uint16_t width = (uint16_t)(319 + mode);
    const uint16_t height = (uint16_t)(199 + mode);

    assert(gate_ds_word(0x0fce) == mode);
    assert(gate_ds_word(0x15cc) == video->requested_bios_mode);
    assert(gate_ds_word(0x15ce) == 1);
    assert(gate_ds_word(0x15d0) == 0);
    assert(gate_ds_word(0x5516) == video->adapter_type);
    assert(gate_ds_word(0x0fd0) == 1);
    assert(gate_ds_word(0x5514) ==
           (video->monitor_subtype == 4 ? 0x3ba : 0x3da));
    assert(gate_far_word(0x3100, 0x003e) == width);
    assert(gate_far_word(0x3100, 0x0042) == height);
    assert(gate_far_word(0x3100, 0x0044) == 40);
    assert(gate_far_word(0x3100, 0x0046) == 10);
    assert(gate_far_word(0x3100, 0x003c) == (height + 1) / 10);
    assert(gate_far_word(0x3100, 0x0048) == (height + 1) / 20);
    assert(gate_far_word(0x3100, 0x004a) == 320);
    for (i = 0; i < 5; ++i)
        assert(gate_far_word(0x3100, (uint16_t)(0x4c + 2u * i)) ==
               expected[i]);
    assert(gate_ds_word(0x5510) == expected[4]);
    assert(gate_ds_word(0x5512) == expected[4]);
    assert(gate_ds_word(0x550e) == 0);
    assert(gate_ds_word(0x12dc) == 0);
    assert(video->clip_calls != 0 && video->clip_left == 0 &&
           video->clip_top == 0 && video->clip_right == width &&
           video->clip_bottom == height);
    assert(video->clipping_enabled && gate_ds_word(0x29de + 0x18) == 1);
    assert(gate_ds_word(0x29de + 0x1e) == width &&
           gate_ds_word(0x29de + 0x20) == height);
    if (mode == 1 || mode == 2 || mode == 4) {
        for (i = 0; i < 16; ++i)
            assert(ports->attributes[i] == i);
        assert(ports->display_enabled);
    }
}

static void check_synthetic(GateIntroVideoModeHardware *video,
                            GateIntroVgaPorts *ports)
{
    uint16_t i;
    const uint16_t expected_bios_modes[] = {4, 4, 5, 6, 7, 4};
    setup_synthetic();
    video->adapter_type = 5;
    video->monitor_subtype = 5;

    assert(gate_rtl_f14d(3) == 1);
    assert(video->requested_bios_mode == 3 && video->start_value == 1);
    assert(ports->dac_writes == 0 && ports->status_reads == 0);
    check_state(3, video, ports);
    gate_rtl_plot_pixel(23, 4, 7);
    assert(gate_far_byte(0xa000, 1287) == 23);

    assert(gate_rtl_f14d(2) == 1);
    assert(video->requested_bios_mode == 2 && ports->status_reads >= 3);
    check_state(2, video, ports);
    gate_rtl_plot_pixel(13, 22, 15);
    assert(gate_intro_vga16_pixel(15, 22) == 13);
    gate_rtl_plot_pixel(9, 22, 400); /* synthetic clip ends at x=321 */
    assert(gate_intro_vga16_pixel(400, 22) == 0);

    assert(gate_rtl_f14d(0) == 1);
    check_state(0, video, ports);

    assert(gate_rtl_f14d(1) == 1);
    check_state(1, video, ports);

    for (i = 1; i <= 5; ++i) {
        video->svga_variant = i;
        assert(gate_rtl_f14d(4) == 1);
        assert(video->requested_bios_mode == expected_bios_modes[i]);
        check_state(4, video, ports);
    }

    video->svga_variant = 6;
    assert(gate_rtl_f14d(4) == 0 && gate_ds_word(0x0fce) == 0xffff);
    video->svga_variant = 1;
    assert(gate_rtl_f14d(-1) == 0 && gate_ds_word(0x0fce) == 0xffff);
    assert(gate_rtl_f14d(5) == 0 && gate_ds_word(0x0fce) == 0xffff);

    video->fail_set_mode = 1;
    i = (uint16_t)video->start_calls;
    assert(gate_rtl_f14d(3) == 0 && gate_ds_word(0x0fce) == 0xffff);
    assert(video->start_calls == i);
    video->fail_set_mode = 0;
    video->fail_start = 1;
    assert(gate_rtl_f14d(3) == 0 && gate_ds_word(0x0fce) == 0xffff);
    video->fail_start = 0;

    video->monitor_subtype = 3;
    assert(gate_rtl_f14d(3) == 0 && gate_ds_word(0x0fce) == 0xffff);
    assert(gate_ds_word(0x0fd0) == 0);
    video->monitor_subtype = 4;
    assert(gate_rtl_f14d(2) == 1);
    check_state(2, video, ports);

    /* The mode table is signed input to IDIV, and the subsequent
     * CWD/SUB/SAR implements signed division by two toward zero. */
    video->monitor_subtype = 5;
    gate_ds_set_word(0x1308, 0xfff5); /* height -11, height+1=-10 */
    gate_ds_set_word(0x131c, 3);
    assert(gate_rtl_f14d(3) == 1);
    assert(gate_far_word(0x3100, 0x003c) == 0xfffd); /* -10/3 = -3 */
    assert(gate_far_word(0x3100, 0x0048) == 0xffff); /* -3/2 = -1 */
    gate_ds_set_word(0x1308, 202);
    gate_ds_set_word(0x131c, 10);
}

static void check_original_snapshot(const char *path)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateIntroVgaPorts *ports = calloc(1, sizeof *ports);
    GateIntroVideoModeHardware *video = calloc(1, sizeof *video);
    FILE *file;
    assert(memory && ports && video);
    file = fopen(path, "rb");
    assert(file);
    assert(fread(memory->bytes, 1, sizeof memory->bytes, file) ==
           sizeof memory->bytes);
    assert(fclose(file) == 0);
    memory->ds_segment = 0x2a7a; /* private original 22-second guest DS */
    gate_intro_memory_attach(memory);
    gate_intro_vga_ports_attach(ports);
    gate_intro_video_mode_attach(video);
    assert(gate_ds_word(0x0fce) == 3);
    assert(gate_ds_word(0x5516) == 5);
    assert(gate_ds_word(0x12fe) == 319);
    assert(gate_ds_word(0x1308) == 199);
    assert(gate_ds_word(0x131c) == 25);
    video->adapter_type = 5;
    video->monitor_subtype = 5;
    assert(gate_rtl_f14d(3) == 1);
    assert(gate_far_word(gate_ds_word(0x4d92), 0x3e) == 319);
    assert(gate_far_word(gate_ds_word(0x4d94), 0x42) == 199);
    assert(gate_far_word(gate_ds_word(0x4d9a), 0x3c) == 8);
    assert(gate_far_word(gate_ds_word(0x4d9c), 0x48) == 4);
    assert(gate_ds_word(0x5510) == 255);
    assert(gate_ds_word(0x5512) == 255);
    assert(video->clip_right == 319 && video->clip_bottom == 199);
    assert(gate_ds_word(0x15cc) == 3);
    gate_intro_video_mode_detach();
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(video);
    free(ports);
    free(memory);
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory = calloc(1, sizeof *memory);
    GateIntroVgaPorts *ports = calloc(1, sizeof *ports);
    GateIntroVideoModeHardware *video = calloc(1, sizeof *video);
    assert(memory && ports && video);
    memory->ds_segment = 0x3000;
    gate_intro_memory_attach(memory);
    gate_intro_vga_ports_attach(ports);
    gate_intro_video_mode_attach(video);
    check_synthetic(video, ports);
    gate_intro_video_mode_detach();
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(video);
    free(ports);
    free(memory);
    if (argc > 1)
        check_original_snapshot(argv[1]);
    puts("original RTL VGA mode switch and native resident services: OK");
    return 0;
}

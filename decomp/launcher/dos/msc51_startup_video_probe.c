/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Isolated original-file VGA integration probe, NOT the game entry point.
 * Unlike BOOTPREP's harmless simulated callbacks, this program links the
 * reconstructed resident video services (RESSTATE.OBJ) and deliberately
 * switches DOSBox to BIOS mode 13h. No original resident machine code runs.
 * Restore the initial BIOS video mode before releasing the two data images,
 * including on every ordinary failure path via atexit. */
#include "BOOT.H"
#include "BOOTCB.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>

static char original_exe_name[] = "E:\\GATE.EXE";
static char original_rtl_name[] = "E:\\GATE.RTL";
static char unused_exe_callback[32];
static GateDosBootImage owned_image;
static uint8_t previous_bios_mode;
static int must_restore_video;

void gate_exe_prepare_intro_globals(void);
uint16_t gate_rtl_set_video_mode(int16_t requested_mode);
uint16_t gate_rtl_mode_start(uint16_t enabled);
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x);

static uint8_t query_bios_video_mode(void)
{
    union REGS in, out;
    in.x.ax = 0x0f00u;
    int86(0x10, &in, &out);
    return out.h.al;
}

static void restore_bios_video_mode(void)
{
    union REGS in, out;
    if (!must_restore_video)
        return;
    /* Arm restoration BEFORE invoking the source-C setter. A failure after
     * its first INT 10h still restores the original console mode. */
    in.x.ax = previous_bios_mode;
    int86(0x10, &in, &out);
    /* Keep the exit handler armed if the BIOS did not honor the restore;
     * the normal-path check reports the failure, and atexit retries it. */
    if (query_bios_video_mode() == previous_bios_mode)
        must_restore_video = 0;
}

static void release_video_probe(void)
{
    restore_bios_video_mode();
    if (owned_image.lower_segment != 0u ||
        owned_image.resident_segment != 0u)
        gate_dos_boot_release(&owned_image);
}

int main(void)
{
    GateDosBootConfig config;
    uint16_t data_segment;
    int status;
    int good = 1;

    owned_image.lower_segment = owned_image.resident_segment = 0u;
    must_restore_video = 0;
    if (atexit(release_video_probe) != 0) {
        puts("DOS intro live VGA probe: FAIL cleanup registration");
        return 1;
    }
    config.exe_path = gate_stack_pointer(original_exe_name);
    config.rtl_path = gate_stack_pointer(original_rtl_name);
    config.exe_callback = gate_stack_pointer(unused_exe_callback);
    status = gate_probe_load_return_context(config.rtl_path,
                                            &config.return_callback);
    if (status != 0) {
        printf("DOS intro live VGA probe: FAIL return context=%d\n", status);
        return 1;
    }
    config.external_a_segment = 0x3333u;
    config.external_b_segment = 0x4444u;
    status = gate_dos_boot_prepare(&config, &owned_image);
    if (status != 0) {
        printf("DOS intro live VGA probe: FAIL data image=%d\n", status);
        return 1;
    }
    data_segment = (uint16_t)(owned_image.lower_segment + 10u);
    if (gate_ds_segment() != data_segment ||
        gate_ds_word(0x4e2au) != data_segment ||
        gate_ds_word(0x4d92u) != owned_image.lower_segment ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 0xffffu) {
        puts("DOS intro live VGA probe: FAIL original data binding");
        return 1;
    }

    gate_exe_prepare_intro_globals();
    previous_bios_mode = query_bios_video_mode();
    /* The following call reaches the real reconstructed 2409:003e video
     * start routine, NOT BOOTPREP's callback that only counted requests. */
    must_restore_video = 1;
    if (gate_rtl_set_video_mode(3) != 1u ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 3u ||
        gate_ds_word(GATE_DS_RENDERER_MODE) != 3u ||
        gate_ds_word(GATE_DS_RENDERER_ENABLED) != 1u ||
        query_bios_video_mode() != 0x13u)
        good = 0;

    if (good) {
        /* Prove the original RTL setter's mode-3 data reaches a working
         * resident mode-13 pixel path and the genuine A000 aperture. */
        gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 0u);
        gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
        gate_rtl_plot_pixel(0x42u, 4u, 9u);
        if (gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(4u * 320u + 9u)) != 0x42u)
            good = 0;
    }

    /* The original disabled path explicitly switches to text mode 3.
     * Finally restore the exact pre-probe mode, even if it was not 3. */
    if (good && (gate_rtl_mode_start(0u) != 0u ||
                 gate_ds_word(GATE_DS_RENDERER_ENABLED) != 0u ||
                 query_bios_video_mode() != 3u))
        good = 0;
    restore_bios_video_mode();
    if (query_bios_video_mode() != previous_bios_mode)
        good = 0;
    gate_dos_boot_release(&owned_image);
    if (owned_image.lower_segment != 0u ||
        owned_image.resident_segment != 0u)
        good = 0;

    if (!good) {
        puts("DOS intro live VGA probe: FAIL mode 13, pixel or text restore");
        return 1;
    }
    puts("DOS intro live VGA probe: PASS (RTL:f14d + resident 2409 mode 13; A000 pixel; prior BIOS mode restored)");
    return 0;
}

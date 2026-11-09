/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Isolated original-file VGA integration probe, NOT the game entry point.
 * Unlike BOOTPREP's harmless simulated callbacks, this program links the
 * reconstructed resident video and retrace services, deliberately
 * switching DOSBox to BIOS modes 12h and 13h. No original resident machine
 * code runs.
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
void gate_exe_bind_resource_pointers(void);
uint16_t gate_rtl_set_video_mode(int16_t requested_mode);
uint16_t gate_rtl_mode_start(uint16_t enabled);
void gate_rtl_plot_pixel(uint16_t color, uint16_t y, uint16_t x);
uint16_t gate_linked_clear_resident_display(uint16_t page, uint16_t color);
void gate_rtl_background_setup(uint16_t pattern, uint16_t color,
                               uint16_t transparent);
void gate_rtl_background_rect(uint16_t style, uint16_t bottom,
                              uint16_t right, uint16_t top, uint16_t left);

static uint8_t query_bios_video_mode(void)
{
    union REGS in, out;
    in.x.ax = 0x0f00u;
    int86(0x10, &in, &out);
    return out.h.al;
}

static uint8_t bios_read_pixel(uint16_t x, uint16_t y)
{
    union REGS in, out;
    in.x.ax = 0x0d00u;
    in.x.bx = 0u;
    in.x.cx = x;
    in.x.dx = y;
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

    gate_exe_bind_resource_pointers();
    if (gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET) !=
            GATE_DS_EXE_STATIC_DATA_BASE ||
        gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT) != data_segment ||
        gate_ds_word(GATE_DS_ACTIVE_FONT_OFFSET) != GATE_DS_FONT_CACHE_BASE ||
        gate_ds_word(GATE_DS_ACTIVE_FONT_SEGMENT) != data_segment) {
        puts("DOS intro live VGA probe: FAIL startup resource pointers");
        return 1;
    }
    gate_exe_prepare_intro_globals();
    previous_bios_mode = query_bios_video_mode();
    /* The original 350ms capture has mode=2, color mask=0f, VGA port=3da
     * and 639x479 inclusive descriptor bounds. This branch calls the
     * actual RTL:f473 retrace wait in M24.OBJ before BIOS mode 12h. */
    must_restore_video = 1;
    if (gate_rtl_set_video_mode(2) != 1u ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 2u ||
        gate_ds_word(GATE_DS_RENDERER_MODE) != 2u ||
        gate_ds_word(GATE_DS_RENDERER_ENABLED) != 1u ||
        gate_ds_word(GATE_DS_VIDEO_ADAPTER_TYPE) != 5u ||
        gate_ds_word(GATE_DS_VGA_STATUS_PORT) != 0x03dau ||
        gate_ds_word(GATE_DS_PEN_MASK) != 0x000fu ||
        gate_ds_word(GATE_DS_PEN_COLOR) != 0x000fu ||
        gate_far_word(gate_ds_word(GATE_DS_VIDEO_WIDTH_SEGMENT),
                      GATE_MODE_DESC_SCREEN_WIDTH) != 639u ||
        gate_far_word(gate_ds_word(GATE_DS_VIDEO_HEIGHT_SEGMENT),
                      GATE_MODE_DESC_SCREEN_HEIGHT) != 479u ||
        query_bios_video_mode() != 0x12u)
        good = 0;

    if (good) {
        /* Read back through BIOS 0Dh, independently of our source-C
         * planar pixel writer's implementation. */
        gate_ds_set_word(GATE_DS_DRAW_CLIPPING_ENABLED, 0u);
        gate_ds_set_word(GATE_DS_DRAW_OPERATION, 0u);
        gate_rtl_plot_pixel(0x0au, 4u, 9u);
        if (bios_read_pixel(9u, 4u) != 0x0au)
            good = 0;
    }

    if (good) {
        /* The planar clear writes all four A000 planes, unlike the
         * byte-addressed mode-13 clear. BIOS pixel readback checks all
         * corners including the bottom-right of the 480th scanline. */
        if (gate_linked_clear_resident_display(0u, 0x03u) != 0u ||
            bios_read_pixel(0u, 0u) != 0x03u ||
            bios_read_pixel(9u, 4u) != 0x03u ||
            bios_read_pixel(639u, 479u) != 0x03u)
            good = 0;
    }
    if (good) {
        gate_rtl_background_setup(0u, 0x0cu, 0u);
        gate_rtl_background_rect(2u, 5u, 11u, 4u, 9u);
        if (bios_read_pixel(9u, 4u) != 0x0cu ||
            bios_read_pixel(11u, 5u) != 0x0cu ||
            bios_read_pixel(8u, 4u) != 0x03u ||
            bios_read_pixel(9u, 6u) != 0x03u ||
            bios_read_pixel(639u, 479u) != 0x03u)
            good = 0;
    }

    /* The next test still exercises the genuine mode-13 resident path. */
    if (good && (gate_rtl_set_video_mode(3) != 1u ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 3u ||
        gate_ds_word(GATE_DS_RENDERER_MODE) != 3u ||
        gate_ds_word(GATE_DS_RENDERER_ENABLED) != 1u ||
        query_bios_video_mode() != 0x13u))
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

    if (good) {
        /* The first intro scene clears its display and draws rectangles.
         * Exercise these already reconstructed resident services against
         * the SAME relocated original mode descriptor, not a test-only
         * descriptor or hand-filled A000 image. Check both the final row
         * and the pixels surrounding the bounded solid rectangle. */
        if (gate_linked_clear_resident_display(0u, 0x17u) != 0u ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, 0u) != 0x17u ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(4u * 320u + 9u)) != 0x17u ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT, 63999u) != 0x17u)
            good = 0;
    }
    if (good) {
        gate_rtl_background_setup(0u, 0x2cu, 0u);
        gate_rtl_background_rect(2u, 5u, 11u, 4u, 9u);
        if (gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(4u * 320u + 9u)) != 0x2cu ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(5u * 320u + 11u)) != 0x2cu ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(4u * 320u + 8u)) != 0x17u ||
            gate_far_byte(GATE_VIDEO_GRAPHICS_APERTURE_SEGMENT,
                          (uint16_t)(6u * 320u + 9u)) != 0x17u)
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
        puts("DOS intro live VGA probe: FAIL mode 12/13, pixel, clear, rectangle or text restore");
        return 1;
    }
    puts("DOS intro live 16-color VGA: PASS (RTL:f14d mode 2; BIOS 12h, 639x479 descriptors, planar pixel 0a)");
    puts("DOS intro live 16-color drawing: PASS (BIOS 12h full clear, rectangle and planar pixel readback)");
    puts("DOS intro live VGA probe: PASS (RTL:f14d + resident 2409 mode 13; A000 pixel, clear and rectangle; prior BIOS mode restored)");
    return 0;
}

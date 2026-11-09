/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Optional original-file DOS probe: execute the *reconstructed C* prologue
 * of GATE.EXE:1309..134b, the no-music branch of RTL:c3bd..c3d8,
 * and the rejected/accepted mode-state branches of RTL:f14d,
 * not any unknown relocated resident code.
 * The five selector/target addresses are independently pinned by original
 * intro-entry RAM snapshots. No intro graphics/IRQ/RTLink callbacks run.
 * The BOOT image callback addresses here are deliberately nonexecutable.
 * The valid VGA-mode path records BIOS requests without switching modes. */
#include "BOOT.H"
#include "BOOTCB.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>

#define GATE_ENTRY_MEMORY_BIAS 0x12b2u

typedef struct GateStartupWord {
    uint16_t selector;
    uint16_t target;
    uint16_t expected;
} GateStartupWord;

static const GateStartupWord startup_words[] = {
    {0x4e46u, 0x0660u, 0x0401u},
    {0x4e48u, 0x065cu, 0x007fu},
    {0x4e4au, 0x065eu, 0x0001u},
    {0x4e4cu, 0x064eu, 0x0000u}
};

static char original_exe_name[] = "E:\\GATE.EXE";
static char original_rtl_name[] = "E:\\GATE.RTL";
static char unused_exe_callback[32];
static uint16_t bios_mode_calls;
static uint16_t video_start_calls;

/* The real reconstructed source object (Mxx.OBJ), not a test substitute,
 * supplies this function. Its original call requests 1000h DOS paragraphs. */
void gate_exe_prepare_intro_globals(void);
void gate_exe_bind_resource_pointers(void);
void gate_linked_initialize_intro_services(void);
uint16_t gate_rtl_set_video_mode(int16_t requested_mode);

/* These three callback bodies exist ONLY in BOOTPREP.EXE. The invalid-mode
 * branch must return before invoking them. The valid mode-3 path records
 * calls, preserving the hardware boundary while exercising real C writes
 * to the original loaded data and mode descriptor selectors. */
uint16_t gate_rtl_mode_set_bios(uint16_t mode)
{
    if (mode != 3u || bios_mode_calls != 0u) {
        fputs("DOS intro C video layout: invalid BIOS mode request\n", stderr);
        exit(87);
    }
    ++bios_mode_calls;
    return 0u;
}

uint16_t gate_rtl_mode_start(uint16_t enabled)
{
    if (enabled != 1u || bios_mode_calls != 1u || video_start_calls != 0u) {
        fputs("DOS intro C video layout: invalid video-start request\n", stderr);
        exit(87);
    }
    ++video_start_calls;
    return 0u;
}

void gate_rtl_wait_vertical_retrace(void)
{
    fputs("DOS intro C video guard: unexpected retrace wait\n", stderr);
    exit(87);
}

int main(void)
{
    GateDosBootConfig config;
    GateDosBootImage image;
    uint16_t data_segment;
    uint16_t fifth_segment = 0u;
    uint16_t workspace = 0u;
    uint16_t video_mode_before = 0u;
    uint16_t adapter_before = 0u;
    uint16_t svga_before = 0u;
    uint16_t status_port_before = 0u;
    uint16_t width_segment = 0u;
    uint16_t height_segment = 0u;
    uint16_t descriptor_segment = 0u;
    uint16_t i;
    int status;
    int good = 1;

    image.lower_segment = image.resident_segment = 0u;
    config.exe_path = gate_stack_pointer(original_exe_name);
    config.rtl_path = gate_stack_pointer(original_rtl_name);
    config.exe_callback = gate_stack_pointer(unused_exe_callback);
    status = gate_probe_load_return_context(config.rtl_path,
                                            &config.return_callback);
    if (status != 0) {
        printf("DOS intro C entry prologue: FAIL return context=%d\n", status);
        return 1;
    }
    config.external_a_segment = 0x3333u;
    config.external_b_segment = 0x4444u;
    status = gate_dos_boot_prepare(&config, &image);
    if (status != 0) {
        printf("DOS intro C entry prologue: FAIL image status=%d\n", status);
        return 1;
    }

    data_segment = (uint16_t)(image.lower_segment + 0x000au);
    for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i) {
        if (gate_ds_word(startup_words[i].selector) != data_segment)
            good = 0;
    }
    if (gate_ds_word(0x4e4eu) != data_segment)
        good = 0;

    if (good) {
        /* Original RAM first shows these two live FAR pointers at 350ms.
         * They are absent at 250ms and were NOT populated by the static
         * EXE/RTL data compositor. Deliberately poison all four words to
         * prove that the recovered startup binder really initializes them. */
        gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET, 0xdeadu);
        gate_ds_set_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT, 0xdeadu);
        gate_ds_set_word(GATE_DS_ACTIVE_FONT_OFFSET, 0xdeadu);
        gate_ds_set_word(GATE_DS_ACTIVE_FONT_SEGMENT, 0xdeadu);
        gate_exe_bind_resource_pointers();
        if (gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_OFFSET) !=
                GATE_DS_EXE_STATIC_DATA_BASE ||
            gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT) != data_segment ||
            gate_ds_word(GATE_DS_ACTIVE_FONT_OFFSET) != GATE_DS_FONT_CACHE_BASE ||
            gate_ds_word(GATE_DS_ACTIVE_FONT_SEGMENT) != data_segment)
            good = 0;
    }

    if (good) {
        /* A copied original value could accidentally satisfy the test.
         * Poison every destination only after both full-image CRCs pass. */
        for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i)
            gate_far_set_word(data_segment, startup_words[i].target, 0xdeadu);
        gate_far_set_word(data_segment, 0x0658u, 0xdeadu);
        gate_exe_prepare_intro_globals();
        for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i) {
            if (gate_far_word(data_segment, startup_words[i].target) !=
                startup_words[i].expected)
                good = 0;
        }
        fifth_segment = gate_ds_word(0x4e4eu);
        workspace = (uint16_t)(gate_far_word(fifth_segment, 0x0658u) +
                               GATE_ENTRY_MEMORY_BIAS);
        if (workspace == 0u || workspace == image.lower_segment ||
            workspace == image.resident_segment)
            good = 0;
        /* The prepared RTL data still contains mode FFFFh at DS:0fce.
         * Original RTL:f14d sets this invalid marker on entry and only
         * stores a valid mode after successful hardware initialization.
         * The DOS image owner has NOT run that initializer. Confirm both
         * the alias and the uninitialized word before running any more
         * source-C code; never dispatch picture/VGA routines here. */
        if (gate_ds_word(0x4e2au) != data_segment)
            good = 0;
        if (good) {
            video_mode_before = gate_far_word(data_segment, 0x0fceu);
            if (video_mode_before != 0xffffu)
                good = 0;
        }
        /* The next original FAR call at GATE.EXE:134c enters the music/IRQ
         * initializer. The first original RTL branch returns immediately
         * if bits 1 and 2 of DS:0662 are clear. Check that condition and
         * the bound DOS guest segment before executing the actual RTLHW
         * source-C object. Do NOT run the unimplemented enabled branch. */
        if (gate_ds_segment() != data_segment ||
            (gate_far_byte(data_segment, 0x0662u) & 0x06u) != 0u ||
            gate_ds_word(0x0660u) != 0x0401u)
            good = 0;
        if (good) {
            gate_linked_initialize_intro_services();
            if (gate_ds_segment() != data_segment ||
                (gate_far_byte(data_segment, 0x0662u) & 0x06u) != 0u ||
                gate_ds_word(0x0660u) != 0x0401u ||
                gate_far_word(data_segment, 0x0fceu) != video_mode_before)
                good = 0;
        }
        /* Execute the actual Mxx.OBJ compiled from gate_rtl_mode.c, not
         * a handwritten surrogate. Original RTL:f14d invalidates the mode
         * word even for a rejected request, and rejects out-of-range mode
         * indices BEFORE adapter detection, palette I/O and video setup.
         * Both requests here are intentionally invalid. */
        if (good) {
            adapter_before = gate_far_word(data_segment, 0x5516u);
            svga_before = gate_far_word(data_segment, 0x0fd0u);
            status_port_before = gate_far_word(data_segment, 0x5514u);
            gate_far_set_word(data_segment, 0x0fceu, 0x1234u);
            if (gate_rtl_set_video_mode(-1) != 0u ||
                gate_far_word(data_segment, 0x0fceu) != 0xffffu)
                good = 0;
            gate_far_set_word(data_segment, 0x0fceu, 0x4321u);
            if (gate_rtl_set_video_mode(5) != 0u ||
                gate_far_word(data_segment, 0x0fceu) != 0xffffu ||
                gate_far_word(data_segment, 0x5516u) != adapter_before ||
                gate_far_word(data_segment, 0x0fd0u) != svga_before ||
                gate_far_word(data_segment, 0x5514u) != status_port_before)
                good = 0;
        }
        /* Successful RTL:f14d setup uses the original BIOS adapter QUERY
         * (INT 10h AX=1a00); the callbacks above intentionally do NOT
         * execute the BIOS mode switch. On DOSBox's VGA color display the
         * mode-3 data tables encode 319x199 inclusive pixel coordinates,
         * 40-byte stride, 25-line grouping and an 8-row descriptor.
         * The returned C result and all descriptor writes are checked
         * against the relocated original data rather than a guessed buffer. */
        if (good) {
            width_segment = gate_ds_word(0x4d92u);
            height_segment = gate_ds_word(0x4d94u);
            descriptor_segment = gate_ds_word(0x4d98u);
            if (width_segment != image.lower_segment ||
                height_segment != image.lower_segment ||
                descriptor_segment != image.lower_segment)
                good = 0;
        }
        if (good) {
            if (gate_rtl_set_video_mode(3) != 1u ||
                bios_mode_calls != 1u || video_start_calls != 1u ||
                gate_far_word(data_segment, 0x0fceu) != 3u ||
                gate_far_word(width_segment, 0x003eu) != 319u ||
                gate_far_word(height_segment, 0x0042u) != 199u ||
                gate_far_word(descriptor_segment, 0x0046u) != 25u ||
                gate_far_word(gate_ds_word(0x4d96u), 0x0044u) != 40u ||
                gate_far_word(gate_ds_word(0x4d9au), 0x003cu) != 8u ||
                gate_far_word(gate_ds_word(0x4d9cu), 0x0048u) != 4u ||
                gate_far_word(gate_ds_word(0x4da8u), 0x0054u) != 255u)
                good = 0;
        }
        /* Do not free a guessed segment if the prologue failed. The
         * original retains the allocation; DOS owns and reclaims it when
         * this one-shot probe exits. */
    }
    gate_dos_boot_release(&image);
    if (image.lower_segment != 0u || image.resident_segment != 0u)
        good = 0;
    if (!good) {
        puts("DOS intro C entry prologue: FAIL selectors, values or allocation");
        return 1;
    }
    puts("DOS intro C entry prologue: PASS (five recovered writes, original data/CRC, workspace observed; no intro code entered)");
    puts("DOS intro C resource pointers: PASS (DS:54c2=3592 and DS:54ca=54ce; both segments rebound)");
    puts("DOS intro C audio guard: PASS (RTL:c3bd no-music return, configuration preserved; no IRQ installed)");
    puts("DOS intro C video preflight: PASS (DS:0fce=ffff invalid; hardware mode setup still required)");
    puts("DOS intro C video guard: PASS (RTL:f14d rejected -1 and 5; no BIOS mode entered)");
    puts("DOS intro C video layout: PASS (RTL:f14d mode 3; 320x200 descriptors; BIOS mode switch simulated)");
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Source-C entry for a new real-mode MS-DOS executable. Do not dispatch
 * through the copied original RTL bank: its later resident banks and EXE
 * startup callback are still unreconstructed. /CHECK tests the actual DOS
 * allocations, original files and VGA initialization without playing. */
#include "BOOT.H"
#include "RTLLAY.H"
#include "RTLIMAC.H"
#include "gate_intro_schedule.h"
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GateDosBootImage owned_image;
static uint8_t saved_bios_mode;
static uint16_t video_restore_armed;

void gate_exe_bind_resource_pointers(void);
void gate_exe_prepare_intro_globals(void);
void gate_exe_play_intro(void);
void gate_exe_play_title_interlude(void);
void gate_linked_initialize_intro_services(void);
uint16_t gate_rtl_set_video_mode(int16_t requested_mode);
void gate_rtl_clear_video_display(void);
uint16_t gate_rtl_show_picture(uint16_t resource, uint16_t placement);
uint16_t gate_rtl_load_picture_record(GateFarPtr work, uint16_t resource, uint16_t placement);
void gate_rtl_prepare_picture_display(void);
void gate_rtl_draw_picture_record(GateFarPtr work);
void gate_rtl_release_picture_record(GateFarPtr work);
uint32_t gate_linked_exe_elapsed_milliseconds(void);

/* This is a diagnostic hold, not an original EXE timing constant. The
 * palette of GATE_300.PIC #0 starts black: drawing it alone cannot prove
 * that mode 13h is visible. Show the next full-color backdrop as well. */
static void gate_entry_hold_scene_preview(void)
{
    uint32_t start = gate_linked_exe_elapsed_milliseconds();
    while ((uint32_t)(gate_linked_exe_elapsed_milliseconds() - start) < 4000ul)
        ;
}

/* DOS-only timing probe around the original RTL:12b19 call sequence.
 * In particular, RTL:1266f uploads the newly loaded palette BEFORE
 * RTL:12ed7 draws the image. Report the time when the previous pixels
 * remain visible under the new DAC without altering the original order.
 * Do not mistake the probe's diagnostics for original launcher output. */
static int gate_entry_probe_palette_transition(uint16_t resource)
{
    uint8_t record[GATE_PICTURE_RECORD_BYTES];
    GateFarPtr work;
    uint32_t begin, prepared, drawn;

    record[GATE_PICTURE_RECORD_ACTIVE] = 0u;
    work = gate_stack_pointer(record);
    if (gate_rtl_load_picture_record(work, resource,
                                     GATE_PICTURE_POSITION_FIRST_PAIR) == 0u)
        return 0;
    begin = gate_linked_exe_elapsed_milliseconds();
    gate_rtl_prepare_picture_display();
    prepared = gate_linked_exe_elapsed_milliseconds();
    gate_rtl_draw_picture_record(work);
    drawn = gate_linked_exe_elapsed_milliseconds();
    gate_rtl_release_picture_record(work);
    printf("Gateway DOS palette boundary %04x: prepare=%lu ms draw=%lu ms\n",
           resource, (unsigned long)(prepared - begin),
           (unsigned long)(drawn - prepared));
    return 1;
}

/* The original RTL:4bc9 callback only RETFs. A far C function performs
 * that same stack operation on return. Call it once before installing its
 * executable address into the new data image; never pass a stack buffer as
 * a callback in an executable launcher. */
static void far gate_intro_source_retf(void)
{
}

/* The original EXE:0bca callback is NOT yet reconstructed. This genuine FAR
 * code pointer fails loudly if future original-bank execution reaches it,
 * instead of jumping to nonexecutable data or silently skipping behavior. */
static void far gate_intro_unrecovered_exe_callback(void)
{
    fputs("Gateway DOS: unsupported original EXE callback\n", stderr);
    exit(86);
}

static GateFarPtr gate_entry_code_pointer(void (far *function)(void))
{
    GateFarPtr result;
    result.offset = (uint16_t)FP_OFF(function);
    result.segment = (uint16_t)FP_SEG(function);
    return result;
}

static uint8_t gate_entry_bios_mode(void)
{
    union REGS in, out;
    in.x.ax = 0x0f00u;
    int86(0x10, &in, &out);
    return out.h.al;
}

static void gate_entry_cleanup(void)
{
    union REGS in, out;
    if (video_restore_armed) {
        in.x.ax = saved_bios_mode;
        int86(0x10, &in, &out);
        video_restore_armed = 0u;
    }
    if (owned_image.lower_segment != 0u || owned_image.resident_segment != 0u)
        gate_dos_boot_release(&owned_image);
}

int main(int argc, char **argv)
{
    GateDosBootConfig config;
    GateFarPtr source_retf;
    GateFarPtr exe_callback;
    static char exe_name[] = "GATE.EXE";
    static char rtl_name[] = "GATE.RTL";
    uint16_t data_segment;
    int status;
    int check_only = argc > 1 && strcmp(argv[1], "/CHECK") == 0;
    int picture_only = argc > 1 && strcmp(argv[1], "/PICTURE") == 0;
    int second_only = argc > 1 && strcmp(argv[1], "/SECOND") == 0;
    int title_only = argc > 1 && strcmp(argv[1], "/TITLE") == 0;
    int palette_timing = argc > 1 && strcmp(argv[1], "/PALTIME") == 0;
    int transition_only = argc > 1 && strcmp(argv[1], "/TRANS") == 0;
    int scene_only = argc > 1 && strcmp(argv[1], "/SCENE") == 0;

    owned_image.lower_segment = 0u;
    owned_image.resident_segment = 0u;
    video_restore_armed = 0u;
    if (argc > 1 && !check_only && !picture_only && !second_only &&
        !title_only && !palette_timing && !transition_only && !scene_only) {
        if (strcmp(argv[1], "/INTRO") != 0) {
            fputs("Usage: GATEWAY [/CHECK | /PICTURE | /SECOND | /TITLE | /PALTIME | /TRANS | /SCENE | /INTRO]\n", stderr);
            return 2;
        }
    }
    if (atexit(gate_entry_cleanup) != 0) {
        fputs("Gateway DOS: cannot register cleanup\n", stderr);
        return 2;
    }

    /* These two pointers really address far-callable C text, not the
     * diagnostic BOOTCB.H's original-file bytes. The source-C path never
     * jumps through the external tables in the original resident image. */
    gate_intro_source_retf();
    source_retf = gate_entry_code_pointer(gate_intro_source_retf);
    exe_callback = gate_entry_code_pointer(gate_intro_unrecovered_exe_callback);
    if (source_retf.segment == 0u || source_retf.offset == 0u ||
        exe_callback.segment == 0u || exe_callback.offset == 0u) {
        fputs("Gateway DOS: invalid source-C callback\n", stderr);
        return 2;
    }

    config.exe_path = gate_stack_pointer(exe_name);
    config.rtl_path = gate_stack_pointer(rtl_name);
    config.exe_callback = exe_callback;
    config.return_callback = source_retf;
    config.external_a_segment = exe_callback.segment;
    config.external_b_segment = exe_callback.segment;
    config.source_c_return = 1u;
    status = gate_dos_boot_prepare(&config, &owned_image);
    if (status != 0) {
        printf("Gateway DOS: original image preparation failed (%d)\n", status);
        return 1;
    }
    data_segment = (uint16_t)(owned_image.lower_segment + 10u);
    if (gate_ds_segment() != data_segment ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 0xffffu) {
        fputs("Gateway DOS: wrong initial guest data state\n", stderr);
        return 1;
    }
    gate_exe_bind_resource_pointers();
    if (gate_ds_word(GATE_DS_RESOURCE_PATH_BASE_SEGMENT) != data_segment ||
        gate_ds_word(GATE_DS_ACTIVE_FONT_SEGMENT) != data_segment) {
        fputs("Gateway DOS: failed to bind resource pointers\n", stderr);
        return 1;
    }

    saved_bios_mode = gate_entry_bios_mode();
    video_restore_armed = 1u;
    if (gate_rtl_set_video_mode(2) != 1u ||
        gate_ds_word(GATE_DS_GRAPHICS_MODE) != 2u ||
        gate_entry_bios_mode() != 0x12u) {
        fputs("Gateway DOS: VGA mode-12 initialization failed\n", stderr);
        return 1;
    }
    if (check_only) {
        puts("Gateway DOS source-C entry: PASS (images, FAR return, resources, VGA mode 12)");
        return 0;
    }
    if (transition_only || scene_only) {
        if (gate_rtl_set_video_mode(3) != 1u ||
            gate_ds_word(GATE_DS_GRAPHICS_MODE) != 3u ||
            gate_entry_bios_mode() != 0x13u) {
            fputs("Gateway DOS: VGA mode-13 transition failed\n", stderr);
            return 1;
        }
        gate_rtl_clear_video_display();
        if (scene_only) {
            if (gate_rtl_show_picture(GATE_INTRO_PICTURE_RED_BAR_TITLE, 1u) != 1u) {
                fputs("Gateway DOS: first mode-13 picture failed to load/draw\n", stderr);
                return 1;
            }
            if (gate_rtl_show_picture(GATE_INTRO_PICTURE_CAPTIONS_01_02, 0u) != 1u) {
                fputs("Gateway DOS: first full-color mode-13 picture failed to load/draw\n", stderr);
                return 1;
            }
            puts("Gateway DOS first 320x200 picture: PASS (mode 13, clear, black title, color backdrop)");
            fflush(stdout);
            gate_entry_hold_scene_preview();
            return 0;
        }
        puts("Gateway DOS transition: PASS (VGA mode 13 and display clear)");
        return 0;
    }
    if (picture_only) {
        if (gate_rtl_show_picture(GATE_INTRO_PICTURE_FIRST_SPLASH, 1u) != 1u) {
            fputs("Gateway DOS: first splash picture failed to load/draw\n", stderr);
            return 1;
        }
        puts("Gateway DOS first splash: PASS (source-C picture load and drawing)");
        return 0;
    }
    if (second_only) {
        if (gate_rtl_show_picture(GATE_INTRO_PICTURE_SECOND_SPLASH, 1u) != 1u) {
            fputs("Gateway DOS: second splash picture failed to load/draw\n", stderr);
            return 1;
        }
        puts("Gateway DOS second splash: PASS (source-C picture load and drawing)");
        return 0;
    }
    if (palette_timing) {
        if (!gate_entry_probe_palette_transition(GATE_INTRO_PICTURE_FIRST_SPLASH) ||
            !gate_entry_probe_palette_transition(GATE_INTRO_PICTURE_SECOND_SPLASH)) {
            fputs("Gateway DOS palette boundary: picture load failed\n", stderr);
            return 1;
        }
        puts("Gateway DOS palette boundary: PASS");
        return 0;
    }
    if (title_only) {
        gate_exe_prepare_intro_globals();
        gate_linked_initialize_intro_services();
        puts("Gateway DOS: entering source-C title interlude");
        fflush(stdout);
        gate_exe_play_title_interlude();
        puts("Gateway DOS: title interlude returned");
        return 0;
    }

    puts("Gateway DOS: entering source-C intro");
    fflush(stdout);
    gate_exe_play_intro();
    puts("Gateway DOS: source-C intro returned");
    return 0;
}

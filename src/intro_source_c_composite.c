/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Host for the original source-C EXE:1302 sequence, with native DOS/VGA
 * boundaries. The composite CLI also retains its single-image diagnostic. */
#include "intro_source_c_full.h"
#include "gate_exe.h"
#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"
#include "gate_intro_host_input.h"
#include "gate_intro_host_vga_ports.h"
#include "gate_rtl.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Clock {
    uint32_t milliseconds;
    const GateSourceCFullHooks *hooks;
    GateIntroMemory *memory;
    GateIntroVgaPorts *ports;
} Clock;

/* Private replay format: "GVR1" followed by timestamp (little-endian
 * milliseconds), 64,000 mode-13h pixel indices, and 768 six-bit DAC bytes
 * per changed frame. No original game resources are stored in the repo. */
typedef struct Recording {
    FILE *file;
    GateIntroMemory *memory;
    GateIntroVgaPorts *ports;
    Clock *clock;
    uint8_t last_pixels[320u * 200u];
    uint8_t last_palette[256u * 3u];
    uint32_t frames;
} Recording;

static Recording *active_recording;
static Clock *active_clock;

static void record_changed(void *userdata)
{
    Recording *record = userdata;
    const uint8_t *pixels = record->memory->bytes + 0xa0000;
    const uint8_t *palette = &record->ports->dac[0][0];
    uint32_t when = record->clock->milliseconds;
    uint8_t timestamp[4];
    unsigned i;
    if (!record->file)
        return;
    if (record->frames &&
        memcmp(record->last_pixels, pixels, sizeof record->last_pixels) == 0 &&
        memcmp(record->last_palette, palette, sizeof record->last_palette) == 0)
        return;
    for (i = 0; i < 4; ++i)
        timestamp[i] = (uint8_t)(when >> (8u * i));
    if (fwrite(timestamp, 1, sizeof timestamp, record->file) != sizeof timestamp ||
        fwrite(pixels, 1, sizeof record->last_pixels, record->file) !=
            sizeof record->last_pixels ||
        fwrite(palette, 1, sizeof record->last_palette, record->file) !=
            sizeof record->last_palette)
        abort();
    memcpy(record->last_pixels, pixels, sizeof record->last_pixels);
    memcpy(record->last_palette, palette, sizeof record->last_palette);
    ++record->frames;
}

static void frame_update(void *userdata)
{
    Clock *clock = userdata;
    if (active_recording)
        record_changed(active_recording);
    if (clock->hooks && clock->hooks->frame_ready)
        clock->hooks->frame_ready(clock->hooks->userdata,
                                 clock->memory->bytes + 0xa0000,
                                 (const uint8_t (*)[3])clock->ports->dac);
}

static void full_palette_ready(void *userdata)
{
    Clock *clock = userdata;
    /* Each 256-color upload follows the original VGA retrace wait. The
     * 16ms pacing is host-side, not part of the recovered source-C logic. */
    if (clock->hooks && clock->hooks->delay_ms) {
        clock->hooks->delay_ms(clock->hooks->userdata, 16);
        if (clock->hooks->clock_ms)
            clock->milliseconds = clock->hooks->clock_ms(clock->hooks->userdata);
    } else {
        clock->milliseconds += 16;
    }
    frame_update(clock);
}

static uint32_t sample_time(void *context)
{
    Clock *clock = context;
    if (clock->hooks && clock->hooks->clock_ms)
        clock->milliseconds = clock->hooks->clock_ms(clock->hooks->userdata);
    else
        clock->milliseconds += 50;
    frame_update(clock);
    return clock->milliseconds;
}
static uint16_t key_pending(void *context)
{
    Clock *clock = context;
    if (clock->hooks && clock->hooks->key_pending)
        return clock->hooks->key_pending(clock->hooks->userdata);
    return 0;
}
static uint16_t read_key(void *context)
{
    Clock *clock = context;
    if (clock->hooks && clock->hooks->read_key)
        return clock->hooks->read_key(clock->hooks->userdata);
    abort();
}
static uint16_t no_mouse(void *context) { (void)context; return 0; }

/* Host boundaries for services not yet represented by recovered C. The
 * source-C original sequence remains in gate_exe_1302; no scene schedule is
 * recreated in this driver. Audio is intentionally silent. */
uint16_t gate_far_0e2c_0094(uint16_t size)
{
    if (size != 0x1000) abort();
    return 0x6500; /* pinned original EXE:1302 entry/134c RAM diff */
}
void gate_far_0a46_01ec(void) { /* audio startup: silent host */ }
void gate_far_123f_0004(void) { abort(); /* unimplemented key-3 branch */ }
void gate_far_0e48_011b(void) { /* mouse cursor setup: headless */ }
uint16_t gate_far_2488_000a(uint16_t a, uint16_t b)
{
    (void)a; (void)b;
    /* Clear mode 13h's active video page. */
    unsigned i;
    for (i = 0; i < 320u * 200u; ++i)
        gate_far_set_byte(0xa000, (uint16_t)i, 0);
    return 1;
}
void gate_rtl_f921(void) { gate_far_2488_000a(0, 0); }
uint16_t gate_rtl_f14d(int16_t mode)
{
    if (mode != 3) abort();
    return 1; /* already configured in the pinned DOS guest RAM */
}
uint16_t gate_far_1000_0008(void)
{
    return active_clock && active_clock->hooks &&
           active_clock->hooks->key_pending &&
           active_clock->hooks->key_pending(active_clock->hooks->userdata)
        ? 0 : 1;
}
void gate_far_223f_00b6(uint32_t delay)
{
    if (!active_clock)
        return;
    frame_update(active_clock);
    if (active_clock->hooks && active_clock->hooks->delay_ms) {
        active_clock->hooks->delay_ms(active_clock->hooks->userdata, delay);
        if (active_clock->hooks->clock_ms)
            active_clock->milliseconds = active_clock->hooks->clock_ms(
                active_clock->hooks->userdata);
    } else {
        active_clock->milliseconds += delay;
    }
}
void gate_rtl_graphics_delay(uint32_t delay) { gate_far_223f_00b6(delay); }

/* Original C RTL text-mode services cannot be reached in captured mode 3. */
void gate_rtl_f997(uint16_t x, uint16_t y) { (void)x; (void)y; abort(); }
void gate_rtl_fa45(GateFarPtr text) { (void)text; abort(); }
void gate_rtl_f959(uint16_t *x, uint16_t *y) { (void)x; (void)y; abort(); }
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }
uint16_t gate_far_1e48_04d9(void) { return 0; }
void gate_far_1e48_03c5(uint16_t saved) { (void)saved; }
GateFarPtr gate_far_0cdc_0430(GateFarPtr palette) { return palette; }
GateFarPtr gate_far_0cdc_0396(GateFarPtr palette) { return palette; }
void gate_far_1258_172e(GateFarPtr message) { (void)message; abort(); }

static int configure(GateIntroMemory *memory)
{
    unsigned i;
    if (gate_ds_word(0x0fce) != 3 || gate_ds_word(0x15cc) != 3)
        return 0;
    for (i = 0; i < 2; ++i) {
        uint16_t slot = (uint16_t)(0x54ce + i * 30u);
        gate_ds_set_word(slot, 0);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 2u), 0xff);
        gate_far_set_byte(memory->ds_segment, (uint16_t)(slot + 3u), 0xff);
    }
    gate_ds_set_word(0x29de + 0x16, 0);
    gate_ds_set_word(0x29de + 0x18, 1);
    gate_ds_set_word(0x29de + 0x1a, 0);
    gate_ds_set_word(0x29de + 0x1c, 0);
    gate_ds_set_word(0x29de + 0x1e, 319);
    gate_ds_set_word(0x29de + 0x20, 199);
    gate_ds_set_word(0x29de + 0x28, 0xffffu);
    gate_ds_set_word(0x29de + 0x2a, 1);
    gate_ds_set_word(0x12de, 0);
    gate_ds_set_word(0x12e0, 0);
    gate_rtl_resource_ds_set_word(0, 0);
    gate_rtl_resource_ds_set_word(6, 0);
    gate_rtl_resource_ds_set_word(8, 0xffffu);
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    return 1;
}

static int run_intro(const char *snapshot, const char *dos_dir,
                     const char *ppm_output, int full,
                     const char *record_output, const GateSourceCFullHooks *hooks)
{
    GateIntroMemory *memory;
    GateDosHostFiles files = {0};
    GateIntroVgaPorts ports = {0};
    Clock clock = {0};
    Recording recording = {0};
    GateIntroHostInput input = {&clock, sample_time, key_pending, read_key, no_mouse};
    FILE *in, *out;
    unsigned i, text_pixels = 0, background_pixels = 0, final_pixels = 0;
    uint8_t background[320u * 200u] = {0};
    clock.hooks = hooks;
    clock.ports = &ports;
    memory = calloc(1, sizeof *memory);
    if (!memory)
        return 1;
    clock.memory = memory;
    in = fopen(snapshot, "rb");
    if (!in) {
        perror(snapshot);
        free(memory);
        return 1;
    }
    if (fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, in) !=
        GATE_INTRO_RAM_BYTES) {
        fprintf(stderr, "invalid 1 MiB DOS RAM snapshot: %s\n", snapshot);
        fclose(in);
        free(memory);
        return 1;
    }
    fclose(in);
    memory->ds_segment = 0x2a7a;
    files.directory = dos_dir;
    gate_intro_memory_attach(memory);
    gate_dos_host_files_attach(&files);
    gate_intro_input_attach(&input);
    gate_intro_vga_ports_attach(&ports);
    if (!configure(memory))
        abort();

    if (full) {
        active_clock = &clock;
        ports.palette_ready = full_palette_ready;
        ports.palette_userdata = &clock;
        memory->frame_ready = frame_update;
        memory->frame_userdata = &clock;
        if (record_output) {
            recording.file = fopen(record_output, "wb");
            if (!recording.file || fwrite("GVR1", 1, 4, recording.file) != 4)
                abort();
            recording.memory = memory;
            recording.ports = &ports;
            recording.clock = &clock;
            active_recording = &recording;
        }
        frame_update(&clock);
        gate_exe_1302();
        frame_update(&clock);
    } else {
        if (gate_rtl_12b19(0x3105, 0) != 1)
            abort();
        memcpy(background, memory->bytes + 0xa0000, sizeof background);
        /* First source-C EXE:117e caption, original call arguments and text
         * pointer, drawn over the image in the same host memory. */
        gate_exe_117e(10, 120, 310,
                      (GateFarPtr){memory->ds_segment, 0x363b});
    }

    if (ppm_output) {
        out = fopen(ppm_output, "wb");
        if (!out || fprintf(out, "P6\n320 200\n255\n") < 0)
            abort();
        for (i = 0; i < 320u * 200u; ++i) {
            uint8_t pixel = memory->bytes[0xa0000 + i];
            uint8_t rgb[3];
            unsigned channel;
            background_pixels += background[i] != 0;
            text_pixels += pixel != background[i];
            final_pixels += pixel != 0;
            for (channel = 0; channel < 3; ++channel) {
                uint8_t value = ports.dac[pixel][channel];
                rgb[channel] = (uint8_t)((value << 2) | (value >> 4));
            }
            if (fwrite(rgb, 1, 3, out) != 3)
                abort();
        }
        if (fclose(out) != 0)
            abort();
    }
    gate_rtl_e771();
    if (recording.file && fclose(recording.file) != 0)
        abort();
    active_recording = 0;
    active_clock = 0;
    gate_intro_vga_ports_detach();
    gate_intro_input_detach();
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    if (ppm_output) {
        if (full) {
            printf("original source-C EXE:1302 completed: %u nonzero pixels, %u virtual ms\n",
                   final_pixels, clock.milliseconds);
            if (record_output)
                printf("original EXE:1302 captured %u replay frames\n", recording.frames);
        } else {
            printf("source-C shared image+caption: background=%u changed=%u, clock=%u ms\n",
                   background_pixels, text_pixels, clock.milliseconds);
        }
    }
    return (full || text_pixels > 100) ? 0 : 1;
}

int gate_source_c_full_run(const char *snapshot, const char *dos_directory,
                           const GateSourceCFullHooks *hooks)
{
    if (!snapshot || !dos_directory)
        return 2;
    return run_intro(snapshot, dos_directory, NULL, 1, NULL, hooks);
}

#ifndef GATE_SOURCE_C_LIBRARY
int main(int argc, char **argv)
{
    if (argc != 4 && !(argc == 5 && strcmp(argv[4], "--full") == 0) &&
        !(argc == 7 && strcmp(argv[4], "--full") == 0 &&
          strcmp(argv[5], "--record") == 0)) {
        fputs("usage: gateway-intro-source-c-composite guest-22s.bin DOS-DIR OUTPUT.ppm [--full [--record FILE.gvr]]\n",
              stderr);
        return 2;
    }
    return run_intro(argv[1], argv[2], argv[3], argc >= 5,
                     argc == 7 ? argv[6] : NULL, NULL);
}
#endif

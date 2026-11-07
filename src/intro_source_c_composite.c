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
#include "gate_intro_host_video_mode.h"
#include "gate_rtl.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_audio_startup.h"
#include "gate_rtl_image.h"
#include "intro_source_c_bootstrap.h"

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
 * recreated in this driver. MIDIPlay synthesis lives in the native SDL host. */
uint16_t gate_far_0e2c_0094(uint16_t size)
{
    if (size != 0x1000) abort();
    return 0x6500; /* pinned original EXE:1302 entry/134c RAM diff */
}
void gate_far_0a46_01ec(void)
{
    /* At the genuine EXE:134c startup boundary, consult the recovered
     * original RTL:c3bd device predicates before notifying the host.
     * The IRQ8-driven loader itself is still deliberately not emulated. */
    if (active_clock && active_clock->hooks &&
        active_clock->hooks->music_start &&
        gate_rtl_c3bd_music_initialization_required())
        active_clock->hooks->music_start(active_clock->hooks->userdata);
}
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
    /* Source-C RTL:f14d configures +18..+20 itself before drawing. */
    gate_ds_set_word(0x29de + 0x28, 0xffffu);
    gate_ds_set_word(0x29de + 0x2a, 1);
    gate_ds_set_word(0x12de, 0);
    gate_ds_set_word(0x12e0, 0);
    gate_rtl_resource_ds_set_word(0, 0);
    gate_rtl_resource_ds_set_word(6, 0);
    gate_rtl_resource_ds_set_word(8, 0xffffu);
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    /* Direct host bulk writes bypass the FAR accessor; the dependency
     * profiler must not treat subsequent pixel reads as snapshot inputs. */
    if (memory->written_bytes)
        memset(memory->written_bytes + 0xa0000, 1, 320u * 200u);
    return 1;
}

/* This is a dependency-location oracle, not a memory dump: only physical
 * page offsets and the number of bytes first read before a FAR write are
 * exported. It never includes the original proprietary byte values. */
static void write_input_dependencies(const char *path, const uint8_t *reads)
{
    FILE *out = fopen(path, "wbx");
    unsigned page, count, pages = 0, ranges = 0;
    uint32_t total = 0;
    if (!out) {
        perror(path);
        abort();
    }
    if (fprintf(out, "# physical-page first-read-bytes (4096-byte pages)\n") < 0)
        abort();
    for (page = 0; page < GATE_INTRO_RAM_BYTES / 4096u; ++page) {
        unsigned i;
        count = 0;
        for (i = 0; i < 4096u; ++i)
            count += reads[page * 4096u + i] != 0;
        if (!count)
            continue;
        if (fprintf(out, "%05x %u\n", page * 4096u, count) < 0)
            abort();
        total += count;
        ++pages;
    }
    if (fprintf(out, "# first-read physical byte ranges (end exclusive)\n") < 0)
        abort();
    for (uint32_t offset = 0; offset < GATE_INTRO_RAM_BYTES;) {
        uint32_t start;
        if (!reads[offset]) {
            ++offset;
            continue;
        }
        start = offset;
        do {
            ++offset;
        } while (offset < GATE_INTRO_RAM_BYTES && reads[offset]);
        if (fprintf(out, "range %05x %05x\n", (unsigned)start,
                    (unsigned)offset) < 0)
            abort();
        ++ranges;
    }
    if (fclose(out) != 0)
        abort();
    printf("original source-C first-read profile: %u guest bytes in %u pages, %u ranges\n",
           (unsigned)total, pages, ranges);
}

static int run_intro(const char *snapshot, const char *dos_dir,
                     const char *ppm_output, int full,
                     const char *record_output, const char *memory_output,
                     const char *dependency_output, int restore_exe_data,
                     int restore_rtl_data,
                     const GateSourceCFullHooks *hooks)
{
    GateIntroMemory *memory;
    GateDosHostFiles files = {0};
    GateIntroVgaPorts ports = {0};
    GateIntroVideoModeHardware video = {0};
    Clock clock = {0};
    Recording recording = {0};
    uint8_t *dependency_maps = NULL;
    GateIntroHostInput input = {&clock, sample_time, key_pending, read_key, no_mouse};
    FILE *in, *out;
    unsigned i, text_pixels = 0, background_pixels = 0, final_pixels = 0;
    uint8_t background[320u * 200u] = {0};
    clock.hooks = hooks;
    clock.ports = &ports;
    memory = calloc(1, sizeof *memory);
    if (!memory)
        return 1;
    if (dependency_output) {
        dependency_maps = calloc(2u * GATE_INTRO_RAM_BYTES, 1);
        if (!dependency_maps) {
            free(memory);
            return 1;
        }
        memory->input_reads = dependency_maps;
        memory->written_bytes = dependency_maps + GATE_INTRO_RAM_BYTES;
    }
    clock.memory = memory;
    in = fopen(snapshot, "rb");
    if (!in) {
        perror(snapshot);
        free(dependency_maps);
        free(memory);
        return 1;
    }
    if (fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, in) !=
        GATE_INTRO_RAM_BYTES) {
        fprintf(stderr, "invalid 1 MiB DOS RAM snapshot: %s\n", snapshot);
        fclose(in);
        free(dependency_maps);
        free(memory);
        return 1;
    }
    fclose(in);
    if (restore_exe_data &&
        !gate_intro_restore_static_exe_data(memory, dos_dir)) {
        fprintf(stderr, "cannot restore verified GATE.EXE static data from %s\n",
                dos_dir);
        free(dependency_maps);
        free(memory);
        return 1;
    }
    if (restore_rtl_data &&
        !gate_intro_restore_static_rtl_data(memory, dos_dir)) {
        fprintf(stderr, "cannot restore verified GATE.RTL static data from %s\n",
                dos_dir);
        free(dependency_maps);
        free(memory);
        return 1;
    }
    memory->ds_segment = 0x2a7a;
    files.directory = dos_dir;
    gate_intro_memory_attach(memory);
    gate_dos_host_files_attach(&files);
    gate_intro_input_attach(&input);
    gate_intro_vga_ports_attach(&ports);
    /* Adapter type 5 and active mode 13h are present in the captured RAM.
     * Monitor subtype 5 models the default VGA capability required by
     * RTL:f14d's mode-3 path; it is a host setting, not guest data. */
    video.adapter_type = 5;
    video.monitor_subtype = 5;
    video.bios_mode = 0x13;
    video.bios_mode_valid = 1;
    gate_intro_video_mode_attach(&video);
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
        /* The real EXE:1302 must invoke the relocated RTL:f14d C routine
         * once through its linked FAR alias, rather than silently relying
         * on a preselected host display mode. */
        if (video.mode_changes != 1 || video.start_calls != 1 ||
            video.clip_calls != 1 || video.requested_bios_mode != 3 ||
            video.bios_mode != 0x13 || !video.clipping_enabled)
            abort();
    } else {
        if (gate_rtl_12b19(0x3105, 0) != 1)
            abort();
        memcpy(background, memory->bytes + 0xa0000, sizeof background);
        /* First source-C EXE:117e caption, original call arguments and text
         * pointer, drawn over the image in the same host memory. */
        gate_exe_117e(10, 120, 310,
                      (GateFarPtr){memory->ds_segment, 0x363b});
    }

    if (memory_output) {
        /* x: do not replace the input snapshot or any existing DOS file. */
        FILE *dump = fopen(memory_output, "wbx");
        int ok = dump != NULL;
        if (dump) {
            ok = fwrite(memory->bytes, 1, GATE_INTRO_RAM_BYTES, dump) ==
                 GATE_INTRO_RAM_BYTES;
            if (fclose(dump) != 0)
                ok = 0;
        }
        if (!ok) {
            fprintf(stderr, "cannot write private source-C RAM dump: %s\n", memory_output);
            abort();
        }
    }
    if (dependency_output)
        write_input_dependencies(dependency_output, memory->input_reads);

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
    gate_intro_video_mode_detach();
    gate_intro_vga_ports_detach();
    gate_intro_input_detach();
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(dependency_maps);
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
    return run_intro(snapshot, dos_directory, NULL, 1, NULL, NULL, NULL, 0, 0,
                     hooks);
}

#ifndef GATE_SOURCE_C_LIBRARY
int main(int argc, char **argv)
{
    const char *record_output = NULL;
    const char *memory_output = NULL;
    const char *dependency_output = NULL;
    int restore_exe_data = 0;
    int restore_rtl_data = 0;
    int full = argc >= 5 && strcmp(argv[4], "--full") == 0;
    int i;
    if (argc < 4 || (argc > 4 && !full))
        goto usage;
    for (i = 5; i < argc;) {
        if (strcmp(argv[i], "--restore-exe-data") == 0 && !restore_exe_data) {
            restore_exe_data = 1;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--restore-rtl-data") == 0 && !restore_rtl_data) {
            restore_rtl_data = 1;
            ++i;
            continue;
        }
        if (i + 1 >= argc)
            goto usage;
        if (strcmp(argv[i], "--record") == 0 && !record_output)
            record_output = argv[i + 1];
        else if (strcmp(argv[i], "--dump-memory") == 0 && !memory_output)
            memory_output = argv[i + 1];
        else if (strcmp(argv[i], "--read-deps") == 0 && !dependency_output)
            dependency_output = argv[i + 1];
        else
            goto usage;
        i += 2;
    }
    return run_intro(argv[1], argv[2], argv[3], full, record_output,
                     memory_output, dependency_output, restore_exe_data,
                     restore_rtl_data, NULL);
usage:
    fputs("usage: gateway-intro-source-c-composite guest-22s.bin DOS-DIR OUTPUT.ppm [--full [--record FILE.gvr] [--dump-memory FILE.bin] [--read-deps FILE.txt] [--restore-exe-data] [--restore-rtl-data]]\n",
          stderr);
    return 2;
}
#endif

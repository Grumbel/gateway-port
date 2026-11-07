/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"
#include "gate_intro_host_vga_ports.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_image.h"
#include "gate_rtl_palette.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original RTL:12b19 display entry, using native DCL/VGA host services.
 * Not a complete reconstruction of the intro's surrounding sequence. */
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }
uint16_t gate_linked_save_vfx_draw_state(void) { return 0; }
void gate_linked_restore_vfx_draw_state(uint16_t saved) { (void)saved; }
GateFarPtr gate_linked_transform_picture_palette_indexed(GateFarPtr palette) { return palette; }
GateFarPtr gate_linked_transform_picture_palette_rgb(GateFarPtr palette) { return palette; }
void gate_linked_upload_intro_palette(GateFarPtr palette) { gate_rtl_upload_vga_palette(palette); }
void gate_linked_print_intro_diagnostic(GateFarPtr message)
{
    (void)message;
    abort();
}
/* Original sequence-cleanup FAR edge; only the active-record release is
 * reached by the tested single-image resource. */
void gate_rtl_release_picture_record(GateFarPtr work)
{
    if (gate_far_byte(work.segment, work.offset) != 0) {
        gate_rtl_graphics_release((GateFarPtr){work.segment,
            (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE)});
        gate_far_set_byte(work.segment, work.offset, 0);
    }
}

typedef struct IntroPictureCall {
    uint16_t resource;
    uint16_t mode;
} IntroPictureCall;

/* These are only the original EXE:1302/1a14 single-picture calls in
 * introduction order. The original interleaves fades, captions, input
 * checks and animation calls between them; this diagnostic does not. */
static const IntroPictureCall picture_calls[] = {
    {0x8101, 1}, {0x8100, 1}, {0x8102, 1},
    {0x3000, 1}, {0x301b, 1},
    {0x3105, 0}, {0x3100, 0}, {0x3102, 0},
    {0x3221, 0}, {0x3221, 0}, {0x3221, 0},
    {0x3104, 0}, {0x332a, 0}
};

static int export_frame(const char *path, const GateIntroMemory *memory,
                        const GateIntroVgaPorts *ports, int rgb_output)
{
    FILE *out = fopen(path, "wb");
    unsigned i, nonzero = 0;
    if (!out) {
        perror(path);
        return -1;
    }
    if (fprintf(out, "%s\n320 200\n255\n", rgb_output ? "P6" : "P5") < 0)
        abort();
    for (i = 0; i < 320u * 200u; ++i) {
        uint8_t pixel = memory->bytes[0xa0000 + i];
        nonzero += pixel != 0;
        if (rgb_output) {
            uint8_t rgb[3];
            unsigned component;
            for (component = 0; component < 3; ++component) {
                uint8_t six_bit = ports->dac[pixel][component];
                rgb[component] = (uint8_t)((six_bit << 2) | (six_bit >> 4));
            }
            if (fwrite(rgb, 1, 3, out) != 3)
                abort();
        } else if (fwrite(&pixel, 1, 1, out) != 1) {
            abort();
        }
    }
    if (fclose(out) != 0)
        abort();
    return (int)nonzero;
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory;
    GateDosHostFiles files = {0};
    GateIntroVgaPorts ports = {0};
    FILE *in;
    uint16_t resource = 0x3001, mode = 1;
    int rgb_output = 0;
    int sequence = 0, nonzero;
    if (argc == 5 && strcmp(argv[4], "--sequence") == 0) {
        sequence = 1;
        --argc;
    } else if (argc == 5 || argc == 8) {
        if (strcmp(argv[argc - 1], "--rgb") != 0)
            return 2;
        rgb_output = 1;
        --argc;
    }
    if (argc != 4 && argc != 7) {
        fputs("usage: gateway-intro-source-c-picture-dump guest-22s.bin DOS-DIR OUTPUT.ppm [--resource ID MODE] [--rgb]\n       gateway-intro-source-c-picture-dump guest-22s.bin DOS-DIR OUTPUT-DIR --sequence\n",
              stderr);
        return 2;
    }
    if (argc == 7) {
        char *end;
        unsigned long id, display_mode;
        if (strcmp(argv[4], "--resource") != 0)
            return 2;
        id = strtoul(argv[5], &end, 0);
        if (end == argv[5] || *end || id > 65535)
            return 2;
        display_mode = strtoul(argv[6], &end, 0);
        if (end == argv[6] || *end || display_mode > 65535)
            return 2;
        resource = (uint16_t)id;
        mode = (uint16_t)display_mode;
    }
    memory = calloc(1, sizeof *memory);
    if (!memory)
        return 1;
    in = fopen(argv[1], "rb");
    if (!in || fread(memory->bytes, 1, GATE_INTRO_RAM_BYTES, in) !=
               GATE_INTRO_RAM_BYTES) {
        if (in) fclose(in);
        free(memory);
        return 1;
    }
    fclose(in);
    memory->ds_segment = 0x2a7a; /* pinned 22s original guest RAM */
    files.directory = argv[2];
    gate_intro_memory_attach(memory);
    gate_dos_host_files_attach(&files);
    gate_intro_vga_ports_attach(&ports);
    if (gate_ds_word(GATE_DS_RENDERER_MODE) != 3) {
        fputs("expected original VGA mode-13h snapshot\n", stderr);
        gate_dos_host_files_close_all(&files);
        gate_intro_vga_ports_detach();
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    gate_rtl_resource_ds_set_word(GATE_RESOURCE_ERROR_LATCH, 0);
    gate_rtl_resource_ds_set_word(GATE_RESOURCE_PALETTE_PENDING, 0); /* discard prior snapshot image's pending DAC */
    gate_rtl_resource_ds_set_word(GATE_RESOURCE_CACHED_FILE_ID, 0xffffu);
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    if (sequence) {
        unsigned step;
        for (step = 0; step < sizeof picture_calls / sizeof picture_calls[0]; ++step) {
            char filename[4096];
            IntroPictureCall call = picture_calls[step];
            if (snprintf(filename, sizeof filename,
                         "%s/source-c-%02u-%04x.ppm", argv[3], step,
                         call.resource) >= (int)sizeof filename)
                abort();
            if (gate_rtl_show_picture(call.resource, call.mode) != 1) {
                fprintf(stderr, "source-C picture call %u failed: %04x/%u\n",
                        step, call.resource, call.mode);
                abort();
            }
            nonzero = export_frame(filename, memory, &ports, 1);
            if (nonzero < 0)
                abort();
            printf("stage %u: %04x/%u, %d pixels, DAC writes %u\n",
                   step, call.resource, call.mode, nonzero, ports.dac_writes);
        }
        gate_dos_host_files_close_all(&files);
        gate_intro_vga_ports_detach();
        gate_intro_memory_detach();
        free(memory);
        return 0;
    }
    if (gate_rtl_show_picture(resource, mode) != 1) {
        fprintf(stderr, "original C image loader failed on resource 0x%04x mode %u\n",
                resource, mode);
        gate_dos_host_files_close_all(&files);
        gate_intro_vga_ports_detach();
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    nonzero = export_frame(argv[3], memory, &ports, rgb_output);
    if (nonzero < 0)
        abort();
    gate_dos_host_files_close_all(&files);
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(memory);
    printf("source-C resource 0x%04x mode %u: %u nonzero VGA pixels, %u DAC component writes\n",
           resource, mode, nonzero, ports.dac_writes);
    return nonzero != 0 ? 0 : 1;
}

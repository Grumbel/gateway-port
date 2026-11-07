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
uint16_t gate_far_1e48_04d9(void) { return 0; }
void gate_far_1e48_03c5(uint16_t saved) { (void)saved; }
GateFarPtr gate_far_0cdc_0430(GateFarPtr palette) { return palette; }
GateFarPtr gate_far_0cdc_0396(GateFarPtr palette) { return palette; }
void gate_far_0cdc_04d2(GateFarPtr palette) { gate_rtl_f61b(palette); }
void gate_far_1258_172e(GateFarPtr message)
{
    (void)message;
    abort();
}
/* Original sequence-cleanup FAR edge; only the active-record release is
 * reached by the tested single-image resource. */
void gate_rtl_131cb(GateFarPtr work)
{
    if (gate_far_byte(work.segment, work.offset) != 0) {
        gate_rtl_graphics_release((GateFarPtr){work.segment,
            (uint16_t)(work.offset + 8u)});
        gate_far_set_byte(work.segment, work.offset, 0);
    }
}

int main(int argc, char **argv)
{
    GateIntroMemory *memory;
    GateDosHostFiles files = {0};
    GateIntroVgaPorts ports = {0};
    FILE *in, *out;
    uint16_t resource = 0x3001, mode = 1;
    int rgb_output = 0;
    unsigned i, nonzero = 0;
    if (argc == 5 || argc == 8) {
        if (strcmp(argv[argc - 1], "--rgb") != 0)
            return 2;
        rgb_output = 1;
        --argc;
    }
    if (argc != 4 && argc != 7) {
        fputs("usage: gateway-intro-source-c-picture-dump guest-22s.bin DOS-DIR OUTPUT.ppm [--resource ID MODE] [--rgb]\n",
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
    if (gate_ds_word(0x15cc) != 3) {
        fputs("expected original VGA mode-13h snapshot\n", stderr);
        gate_dos_host_files_close_all(&files);
        gate_intro_vga_ports_detach();
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    gate_rtl_resource_ds_set_word(0, 0);
    gate_rtl_resource_ds_set_word(6, 0); /* discard prior snapshot image's pending DAC */
    gate_rtl_resource_ds_set_word(8, 0xffffu);
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    if (gate_rtl_12b19(resource, mode) != 1) {
        fprintf(stderr, "original C image loader failed on resource 0x%04x mode %u\n",
                resource, mode);
        gate_dos_host_files_close_all(&files);
        gate_intro_vga_ports_detach();
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    out = fopen(argv[3], "wb");
    if (!out) {
        perror(argv[3]);
        abort();
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
                uint8_t six_bit = ports.dac[pixel][component];
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
    gate_dos_host_files_close_all(&files);
    gate_intro_vga_ports_detach();
    gate_intro_memory_detach();
    free(memory);
    printf("source-C resource 0x%04x mode %u: %u nonzero VGA pixels, %u DAC component writes\n",
           resource, mode, nonzero, ports.dac_writes);
    return nonzero != 0 ? 0 : 1;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_dos_host_files.h"
#include "gate_intro_host_memory.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The first playable mode-13h image bridge is deliberately scoped to the
 * original 12c33 load / 12ed7 draw path, not the complete 12b19 caller.
 * These resident services are still HLE; verify against the guest run. */
uint16_t gate_rtl_file_stack_word(void) { return 0; }
_Noreturn void gate_rtl_file_divide_fault(void) { abort(); }
uint16_t gate_far_1e48_04d9(void) { return 0; }
void gate_far_1e48_03c5(uint16_t saved) { (void)saved; }
GateFarPtr gate_far_0cdc_0430(GateFarPtr palette) { return palette; }
GateFarPtr gate_far_0cdc_0396(GateFarPtr palette) { return palette; }
void gate_far_0cdc_04d2(GateFarPtr palette)
{
    /* Original resource #0x3001 has no palette update. If another header
     * reaches this edge, its source-C DAC service must be connected. */
    (void)palette;
    abort();
}
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
    FILE *in, *out;
    unsigned i, nonzero = 0;
    if (argc != 4) {
        fputs("usage: gateway-intro-source-c-picture-dump guest-22s.bin DOS-DIR OUTPUT.pgm\n",
              stderr);
        return 2;
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
    if (gate_ds_word(0x15cc) != 3) {
        fputs("expected original VGA mode-13h snapshot\n", stderr);
        gate_dos_host_files_close_all(&files);
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    gate_rtl_resource_ds_set_word(0, 0);
    gate_rtl_resource_ds_set_word(6, 0); /* discard prior snapshot image's pending DAC */
    gate_rtl_resource_ds_set_word(8, 0xffffu);
    memset(memory->bytes + 0xa0000, 0, 320u * 200u);
    if (gate_rtl_12b19(0x3001, 1) != 1) {
        fputs("original C image loader failed on GATE_300.PIC #1\n", stderr);
        gate_dos_host_files_close_all(&files);
        gate_intro_memory_detach();
        free(memory);
        return 1;
    }
    out = fopen(argv[3], "wb");
    if (!out) {
        perror(argv[3]);
        abort();
    }
    if (fprintf(out, "P5\n320 200\n255\n") < 0)
        abort();
    for (i = 0; i < 320u * 200u; ++i) {
        uint8_t pixel = memory->bytes[0xa0000 + i];
        nonzero += pixel != 0;
        if (fwrite(&pixel, 1, 1, out) != 1)
            abort();
    }
    if (fclose(out) != 0)
        abort();
    gate_dos_host_files_close_all(&files);
    gate_intro_memory_detach();
    free(memory);
    printf("source-C GATE_300.PIC #1: %u nonzero VGA pixels\n", nonzero);
    return nonzero > 100 ? 0 : 1;
}

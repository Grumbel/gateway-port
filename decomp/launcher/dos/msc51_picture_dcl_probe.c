/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Self-contained MS C 5.1 DOSBox probe of both native picture formats.
 * Uses the public blast.c reference sample (13 bytes: AIAIAIAIAIAIA). */
#include "GATEDOS.H"
#include "RTLIMAC.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

uint16_t gate_linked_decompress_picture_pixels(uint16_t offset,
                                                uint16_t segment);

static const uint8_t sample[8] = {
    0x00,0x04,0x82,0x24,0x25,0x8f,0x80,0x7f
};

static int check(uint16_t got, uint16_t want, const char *label)
{
    if (got != want) {
        printf("DCL probe %s: %04x != %04x\n", label, got, want);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int ds = 0;
    uint16_t handle, i, plane;
    GateFarPtr filename;
    GateDosFileResult result;
    FILE *file;
    int valid = 1;

    if (_dos_allocmem(0x1000u, &ds) != 0)
        return 1;
    gate_dos_bind_ds((uint16_t)ds);
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, (uint16_t)ds);
    gate_ds_set_word(GATE_DS_RESOURCE_HANDLE_SEGMENT, (uint16_t)ds);
    gate_ds_set_word(GATE_DS_IMAGE_HEADER_SEGMENT, (uint16_t)ds);
    for (i = 0; i < 11u; ++i)
        gate_far_set_byte((uint16_t)ds, (uint16_t)(0x0200u + i),
                          (uint8_t)"DCLCHK.PAK"[i]);
    filename.segment = (uint16_t)ds;
    filename.offset = 0x0200u;
    file = fopen("DCLCHK.PAK", "wb");
    if (!file)
        return 1;
    if (fwrite(sample, 1u, sizeof sample, file) != sizeof sample ||
        fwrite("NEXT", 1u, 4u, file) != 4u) {
        fclose(file);
        return 1;
    }
    fclose(file);
    result = gate_dos_file_open(filename, 0u, 0u);
    if (result.carry)
        return 1;
    handle = result.ax;
    gate_far_set_word((uint16_t)ds, GATE_RTL_FILE_HANDLE_OFFSET, handle);
    gate_far_set_word((uint16_t)ds, GATE_RTL_VIDEO_MODE_OFFSET, 3u);
    gate_far_set_word((uint16_t)ds, GATE_RTL_HEADER_OFFSET + 6u, 13u);
    gate_far_set_word((uint16_t)ds, GATE_RTL_HEADER_OFFSET + 8u, 1u);
    valid &= check(gate_linked_decompress_picture_pixels(0x3000u,
                   (uint16_t)ds), 0u, "mode-13 decode");
    for (i = 0; i < 13u; ++i)
        valid &= check(gate_far_byte((uint16_t)ds, (uint16_t)(0x3000u + i)),
                       i & 1u ? 'I' : 'A', "mode-13 byte");
    /* Probe read-ahead rollback using the current seek position. */
    result = gate_dos_file_seek(handle, 1u, 0u);
    valid &= check(result.carry, 0u, "file position query");
    valid &= check(result.ax, 8u, "read-ahead restored position");
    valid &= check(result.dx, 0u, "read-ahead position high");

    result = gate_dos_file_seek(handle, 0u, 0u);
    valid &= check(result.carry, 0u, "rewind");
    gate_far_set_word((uint16_t)ds, GATE_RTL_VIDEO_MODE_OFFSET, 2u);
    gate_far_set_word((uint16_t)ds, GATE_RTL_HEADER_OFFSET + 6u, 26u);
    valid &= check(gate_linked_decompress_picture_pixels(0x7000u,
                   (uint16_t)ds), 0u, "mode-12 decode");
    for (i = 0; i < 26u; ++i) {
        uint16_t expected = i & 1u ? 4u : ((i & 2u) ? 9u : 1u);
        uint16_t color = 0u;
        for (plane = 0; plane < 4u; ++plane) {
            uint8_t byte = gate_far_byte((uint16_t)ds,
                (uint16_t)(0x7000u + plane * 4u + i / 8u));
            if (byte & (0x80u >> (i & 7u)))
                color |= (uint16_t)(1u << plane);
        }
        valid &= check(color, expected, "mode-12 plane color");
    }
    result = gate_dos_file_close(handle);
    valid &= check(result.carry, 0u, "file close");
    remove("DCLCHK.PAK");
    _dos_freemem(ds);
    puts(valid ? "DOS DCL probe: PASS" : "DOS DCL probe: FAIL");
    return valid ? 0 : 1;
}

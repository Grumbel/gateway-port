/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Deliberately generated test bytes, not redistributed game data.
 * The only main() is this isolated MS C 5.1/DOSBox probe. */
#include "GATEDOS.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs);

/* MS C 5.1 rejects automatic array initialization in a function (C2073).
 * Keep this filename in static storage; gate_stack_pointer() obtains its
 * actual segment and offset, regardless of the storage segment. */
static char source_name[] = "DATASRC.BIN";

static uint8_t pattern(unsigned int index)
{
    return (uint8_t)(((index & 0xffu) ^ ((index >> 8) * 17u)) & 0xffu);
}

int main(void)
{
    unsigned int allocated = 0u;
    uint8_t block[128];
    unsigned int i;
    FILE *file;
    GateDosFileResult opened;
    GateDosFileResult closed;
    int good = 1;

    file = fopen(source_name, "wb");
    if (file == 0) {
        puts("Static EXE data probe: FAIL cannot create fixture");
        return 1;
    }
    for (i = 0u; i < 0x593eu; i += (unsigned int)sizeof block) {
        unsigned int j;
        unsigned int length = 0x593eu - i;
        if (length > sizeof block)
            length = sizeof block;
        for (j = 0u; j < length; ++j)
            block[j] = pattern(i + j);
        if (fwrite(block, 1u, length, file) != length) {
            good = 0;
            break;
        }
    }
    if (fclose(file) != 0)
        good = 0;
    if (!good || _dos_allocmem(0x100au, &allocated) != 0) {
        remove(source_name);
        puts("Static EXE data probe: FAIL fixture or memory allocation");
        return 1;
    }
    good &= gate_dos_bind_intro_aliases((uint16_t)allocated, 0x100au) == 0;
    gate_ds_set_word(0x3590u, 0x1133u);
    gate_ds_set_word(0x4ceau, 0x7799u);

    opened = gate_dos_file_open(gate_stack_pointer(source_name), 0u, 0u);
    if (opened.carry)
        good = 0;
    else {
        good &= gate_dos_load_intro_static_data(opened.ax,
                     (uint16_t)allocated, 0x1009u) == -1;
        good &= gate_dos_load_intro_static_data(opened.ax,
                     (uint16_t)allocated, 0x100au) == 0;
        for (i = 0u; i < 0x1758u; ++i) {
            if (gate_ds_segment() != (uint16_t)(allocated + 10u) ||
                gate_far_byte(gate_ds_segment(), (uint16_t)(0x3592u + i)) !=
                pattern((unsigned int)(0x41e6u + i))) {
                good = 0;
                break;
            }
        }
        closed = gate_dos_file_close(opened.ax);
        good &= closed.carry == 0u;
    }
    good &= gate_ds_word(0x3590u) == 0x1133u;
    good &= gate_ds_word(0x4ceau) == 0x7799u;
    good &= gate_ds_word(0x4d82u) == (uint16_t)allocated;
    good &= gate_ds_word(0x4d84u) == (uint16_t)(allocated + 10u);
    gate_dos_bind_ds(0u);
    if (_dos_freemem(allocated) != 0)
        good = 0;
    if (remove(source_name) != 0)
        good = 0;
    puts(good ? "Static EXE data probe: PASS (5976 bytes at DS:3592h)"
              : "Static EXE data probe: FAIL");
    return good ? 0 : 1;
}

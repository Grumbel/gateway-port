/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* The only main() in this experiment belongs to the standalone SELCHK.EXE.
 * It is not substituted for the missing launcher _main. */
#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);

int main(void)
{
    unsigned int allocated = 0;
    uint16_t lower, data;
    int good = 1;

    if (_dos_allocmem(0x100au, &allocated) != 0) {
        puts("Selector alias probe: FAIL DOS could not allocate 100Ah paragraphs");
        return 1;
    }
    lower = (uint16_t)allocated;
    data = (uint16_t)(lower + 0x000au);
    good &= gate_dos_bind_intro_aliases(lower, 0x1009u) != 0;
    good &= gate_dos_bind_intro_aliases(0u, 0x100au) != 0;
    good &= gate_dos_bind_intro_aliases(lower, 0x100au) == 0;
    good &= gate_ds_segment() == data;

    /* Representative members from both independently observed groups. */
    good &= gate_ds_word(0x4d82u) == lower;
    good &= gate_ds_word(0x4d84u) == data;
    good &= gate_ds_word(0x4dd0u) == lower;
    good &= gate_ds_word(0x4ddau) == data;
    good &= gate_ds_word(0x4e40u) == lower;
    good &= gate_ds_word(0x4e4eu) == data;

    /* lower:00A0 and data:0000 are the exact same physical location. */
    gate_far_set_word(lower, 0x00a0u, 0x7e35u);
    good &= gate_far_word(data, 0x0000u) == 0x7e35u;
    gate_far_set_word(data, 0x0002u, 0x4912u);
    good &= gate_far_word(lower, 0x00a2u) == 0x4912u;

    gate_dos_bind_ds(0u);
    if (_dos_freemem(allocated) != 0)
        good = 0;
    puts(good ? "Selector alias probe: PASS (12 data, 33 lower, 160-byte overlap)"
              : "Selector alias probe: FAIL");
    return good ? 0 : 1;
}

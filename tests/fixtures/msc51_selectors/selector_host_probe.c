/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Deterministic host checks for the DOS segment alias setup. The real
 * Microsoft C ABI and DOS conventional-memory allocation are checked
 * separately by SELCHK.EXE, not by this fixture. */
#include "GATEMEM.H"
#include <stdio.h>
#include <stdlib.h>

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);

static uint16_t current_segment;
static uint16_t words[0x2800];
static unsigned char written[0x2800];
static unsigned int writes;

void gate_dos_bind_ds(uint16_t segment)
{
    current_segment = segment;
}

uint16_t gate_ds_segment(void)
{
    return current_segment;
}

uint16_t gate_ds_word(uint16_t offset)
{
    return words[offset / 2u];
}

void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    if (current_segment != 0x2a7au || (offset & 1u) != 0u ||
        offset / 2u >= sizeof words / sizeof words[0] ||
        written[offset / 2u] != 0u ||
        (value != 0x2a70u && value != 0x2a7au))
        exit(2);
    written[offset / 2u] = 1u;
    words[offset / 2u] = value;
    ++writes;
}

int main(void)
{
    unsigned int i, lower = 0u, data = 0u;
    if (gate_dos_bind_intro_aliases(0u, 0x100au) == 0 ||
        gate_dos_bind_intro_aliases(0x2a70u, 0x1009u) == 0 ||
        current_segment != 0u || writes != 0u)
        return 1;
    if (gate_dos_bind_intro_aliases(0x2a70u, 0x100au) != 0 ||
        current_segment != 0x2a7au || writes != 45u)
        return 1;
    for (i = 0u; i < sizeof words / sizeof words[0]; ++i) {
        if (written[i] == 0u)
            continue;
        if (words[i] == 0x2a70u)
            ++lower;
        else if (words[i] == 0x2a7au)
            ++data;
    }
    if (lower != 33u || data != 12u)
        return 1;
    puts("startup selector alias host: PASS");
    return 0;
}

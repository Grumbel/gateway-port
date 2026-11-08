/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Synthetic conventional memory for the actual staged C intro prologue.
 * The real DOS original-file test uses BOOT.OBJ and HEAP.OBJ instead. */
#include "BOOT.H"
#include <stdio.h>
#include <stdlib.h>

#define MOCK_LOWER_SEGMENT 0x2306u
#define MOCK_DATA_SEGMENT 0x2310u
#define MOCK_RESIDENT_SEGMENT 0x4000u

static uint16_t words[0x8000];
static unsigned startup_allocations;

static void require(int condition)
{
    if (!condition) {
        fputs("source-C startup test violated memory contract\n", stderr);
        abort();
    }
}

uint16_t gate_ds_word(uint16_t offset)
{
    return gate_far_word(MOCK_DATA_SEGMENT, offset);
}

uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    require(segment == MOCK_DATA_SEGMENT && (offset & 1u) == 0u);
    return words[offset / 2u];
}

void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    require(segment == MOCK_DATA_SEGMENT && (offset & 1u) == 0u);
    words[offset / 2u] = value;
}

GateFarPtr gate_stack_pointer(void *source)
{
    GateFarPtr result;
    require(source != 0);
    result.segment = 0x5000u;
    result.offset = 0u;
    return result;
}

int gate_dos_boot_prepare(const GateDosBootConfig *config,
                          GateDosBootImage *image)
{
    static const uint16_t selectors[] = {
        0x4e46u, 0x4e48u, 0x4e4au, 0x4e4cu, 0x4e4eu
    };
    unsigned i;
    require(config != 0 && image != 0);
    require(image->lower_segment == 0u && image->resident_segment == 0u);
    image->lower_segment = MOCK_LOWER_SEGMENT;
    image->resident_segment = MOCK_RESIDENT_SEGMENT;
    for (i = 0u; i < sizeof selectors / sizeof selectors[0]; ++i)
        words[selectors[i] / 2u] = MOCK_DATA_SEGMENT;
    if (getenv("TEST_BAD_SELECTOR") != 0)
        words[0x4e46u / 2u] = 0x3333u;
    return 0;
}

void gate_dos_boot_release(GateDosBootImage *image)
{
    require(image != 0);
    image->lower_segment = image->resident_segment = 0u;
}

unsigned int _dos_allocmem(unsigned int paragraphs, unsigned int *segment)
{
    require(paragraphs == 0x1000u && segment != 0);
    ++startup_allocations;
    if (getenv("TEST_ALLOC_FAIL") != 0)
        return 8u;
    *segment = 0x6500u;
    return 0u;
}

unsigned int _dos_freemem(unsigned int segment)
{
    require(segment == 0x6500u);
    return 0u;
}

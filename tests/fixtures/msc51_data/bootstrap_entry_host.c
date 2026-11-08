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
static unsigned callback_opens, callback_seeks, callback_reads, callback_closes;

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

/* BOOTPREP now reads the same 40-byte original-file return context as
 * DATAREAL. Stub only the DOS transport here; the guest preflight and
 * authentic RTL bytes are verified by the original-file tests. */
GateDosFileResult gate_dos_file_open(GateFarPtr path, uint16_t access,
                                     uint16_t create)
{
    GateDosFileResult result = {0u, 0u, 0u};
    require(path.segment == 0x5000u && access == 0u && create == 0u);
    ++callback_opens;
    result.ax = 0x0020u;
    if (getenv("TEST_CALLBACK_OPEN_FAIL") != 0)
        result.carry = 1u;
    return result;
}

GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset)
{
    GateDosFileResult result = {0u, 0u, 0u};
    require(handle == 0x0020u && origin == 0u && offset == 0x4bc8ul);
    ++callback_seeks;
    result.ax = 0x4bc8u;
    if (getenv("TEST_CALLBACK_SEEK_FAIL") != 0)
        result.carry = 1u;
    return result;
}

GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr destination,
                                     uint16_t count)
{
    GateDosFileResult result = {0u, 0u, 0u};
    require(handle == 0x0020u && destination.segment == 0x5000u &&
            count == 40u);
    ++callback_reads;
    result.ax = count;
    if (getenv("TEST_CALLBACK_READ_FAIL") != 0)
        result.carry = 1u;
    return result;
}

GateDosFileResult gate_dos_file_close(uint16_t handle)
{
    GateDosFileResult result = {0u, 0u, 0u};
    require(handle == 0x0020u);
    ++callback_closes;
    if (getenv("TEST_CALLBACK_CLOSE_FAIL") != 0)
        result.carry = 1u;
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
    require(callback_opens == 1u && callback_seeks == 1u &&
            callback_reads == 1u && callback_closes == 1u);
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

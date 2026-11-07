/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Real DOS conventional-memory boundary for linked 1e2c and 253a. The
 * original callers treat returned far pointers as paragraph-aligned blocks,
 * and VFX images may exceed a single 64KiB segment. */
#include "GATEMEM.H"
#include <dos.h>

static GateFarPtr far_heap_allocate(uint32_t size)
{
    uint32_t paragraphs;
    unsigned int segment = 0;
    GateFarPtr pointer;
    pointer.segment = 0;
    pointer.offset = 0;
    /* A zero-byte request still occupies one DOS memory paragraph. */
    if (size > 0xffff0ul)
        return pointer;
    paragraphs = ((size + 15ul) >> 4);
    if (paragraphs == 0ul)
        paragraphs = 1ul;
    if (paragraphs > 0xfffful || _dos_allocmem((unsigned int)paragraphs, &segment) != 0)
        return pointer;
    pointer.segment = (uint16_t)segment;
    return pointer;
}

/* Original font allocator 1e2c:0008, return DX:AX FAR address. */
GateFarPtr gate_linked_allocate_far_heap(uint16_t bytes)
{
    return far_heap_allocate((uint32_t)bytes);
}

/* Original font release 1e2c:0138 ignores the DOS release status. */
void gate_linked_free_far_heap(GateFarPtr pointer)
{
    if (pointer.segment != 0 && pointer.offset == 0)
        (void)_dos_freemem(pointer.segment);
}

/* 253a:0004 uses a DWORD byte count, so accept allocations >64KiB. */
GateFarPtr gate_linked_allocate_vfx_pixels(uint32_t bytes)
{
    return far_heap_allocate(bytes);
}

/* Native boundary and observed linked free distinguish invalid pointers
 * with the original -28 return; never call DOS free on segment zero. */
uint16_t gate_linked_release_vfx_pixels(GateFarPtr pointer)
{
    if (pointer.segment == 0 || pointer.offset != 0)
        return 0xffe4u;
    return _dos_freemem(pointer.segment) == 0 ? 0u : 0xffe4u;
}

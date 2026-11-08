/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include <dos.h>
#include <stdio.h>

GateFarPtr gate_linked_allocate_far_heap(uint16_t bytes);
void gate_linked_free_far_heap(GateFarPtr pointer);
GateFarPtr gate_linked_allocate_vfx_pixels(uint32_t bytes);
uint16_t gate_linked_release_vfx_pixels(GateFarPtr pointer);
uint16_t gate_linked_allocate_intro_memory(uint16_t paragraphs);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Heap probe: %s got %04x expected %04x\n", label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    GateFarPtr font;
    GateFarPtr pixels;
    GateFarPtr invalid;
    int valid = 1;

    font = gate_linked_allocate_far_heap(0x0123u);
    if (font.segment == 0 || font.offset != 0) {
        puts("Heap probe: DOS font heap allocation failed");
        return 1;
    }
    gate_far_set_word(font.segment, 0u, 0x5aa5u);
    valid &= check(gate_far_word(font.segment, 0u), 0x5aa5u, "font word round-trip");
    gate_linked_free_far_heap(font);

    /* The original VFX pixel handler can address past the first 64KiB
     * boundary by incrementing the segment by 1000h paragraphs. */
    pixels = gate_linked_allocate_vfx_pixels(0x10010ul);
    if (pixels.segment == 0 || pixels.offset != 0) {
        puts("Heap probe: DOS VFX allocation across 64KiB failed");
        return 1;
    }
    gate_far_set_byte(pixels.segment, 0xffffu, 0xadu);
    gate_far_set_byte((uint16_t)(pixels.segment + 0x1000u), 0u, 0xbcu);
    valid &= check(gate_far_byte(pixels.segment, 0xffffu), 0xadu, "last byte before boundary");
    valid &= check(gate_far_byte((uint16_t)(pixels.segment + 0x1000u), 0u),
                   0xbcu, "first byte past boundary");
    /* Original free loads ES from the FAR pointer, but does not inspect DI:
     * the block is freed even if the passed offset is nonzero. */
    pixels.offset = 1;
    valid &= check(gate_linked_release_vfx_pixels(pixels), 0u,
                   "release VFX block with nonzero offset");

    invalid.segment = 0;
    invalid.offset = 0;
    valid &= check(gate_linked_release_vfx_pixels(invalid), 0xffe7u,
                   "reject null VFX pointer");
    {
        uint16_t startup = gate_linked_allocate_intro_memory(0x1000u);
        if (startup == 0u) {
            puts("Heap probe: startup paragraph allocation failed");
            return 1;
        }
        gate_far_set_word(startup, 0u, 0xfaceu);
        valid &= check(gate_far_word(startup, 0u), 0xfaceu,
                       "startup paragraph base");
        valid &= check((uint16_t)_dos_freemem(startup), 0u,
                       "release startup paragraphs");
    }
    puts(valid ? "Heap probe: PASS" : "Heap probe: FAIL");
    return valid ? 0 : 1;
}

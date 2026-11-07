/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_intro_host_memory.h"
#include "gate_rtl_video.h"

#include <stdlib.h>

/* Hardware adapter for the original linked 2488:000a, as observed at
 * physical 0x2488a in the 22-second GATE.EXE intro capture.
 *
 * 2488:00a2 initializes the eight descriptor callbacks; 2409:00b9 and
 * 2409:00e8 select and validate the active descriptor at DS:27ee + mode*3e.
 * 2488:011a clears a text aperture, 2488:0169 broadcasts a color over all
 * VGA planes, and 2488:01d7 uses REP STOSW for the 8bpp linear mode.
 * These are native display-memory effects, not an 8086 emulator. */
uint16_t gate_far_2488_000a(uint16_t page, uint16_t color)
{
    uint16_t mode, descriptor, ds, height, pitch, aperture, page_offset;
    uint32_t size;
    unsigned i;

    ds = gate_ds_segment();
    if (gate_far_byte(ds, 0x27ed) != 1) {
        for (i = 0; i < 8; ++i) {
            uint16_t entry = (uint16_t)(0x27eeu + i * 0x3eu);
            gate_far_set_word(ds, (uint16_t)(entry + 0x3a),
                              i == 0 ? 0x011a : i == 3 ? 0x01d7 : 0x0169);
            gate_far_set_word(ds, (uint16_t)(entry + 0x3c), 0x2488);
        }
        gate_far_set_byte(ds, 0x27ed, 1);
    }

    mode = gate_ds_word(0x15cc);
    if (mode > 7)
        return 0xfffau; /* original 2409:00b9 invalid mode */
    descriptor = (uint16_t)(0x27eeu + mode * 0x3eu);
    if (gate_far_byte(ds, descriptor) != mode)
        return 0xfc19u; /* original 2409:00e8 descriptor mismatch */

    /* The original also compares INT 10h/AH=0fh against descriptor +16h
     * when DS:15d2 == 1. That BIOS state is not exposed in this host;
     * the video-mode host owns mode selection. */
    height = gate_far_word(ds, (uint16_t)(descriptor + 0x1a));
    pitch = gate_far_word(ds, (uint16_t)(descriptor + 0x1c));
    aperture = gate_far_word(ds, (uint16_t)(descriptor + 0x20));
    page_offset = (uint16_t)(page * (uint32_t)gate_far_word(
        ds, (uint16_t)(descriptor + 0x22)));

    if (mode == 0) {
        /* 2488:011a writes two 4096-word blocks to B800 (16 KiB).
         * At selector zero an odd fill argument produces FF, even 00. */
        if (aperture != 0xb800 || (uint32_t)page_offset + 16384u > 65536u)
            abort();
        for (i = 0; i < 16384u; ++i)
            gate_far_set_byte(aperture, (uint16_t)(page_offset + i),
                              (color & 1u) ? 0xff : 0);
        return 0;
    }

    /* 2488:0169: first program GC write mode 2 with AL=color, then copy
     * its latched color to height*pitch VGA aperture bytes using write mode
     * 1. The four planes ultimately contain all-ones or all-zeros bytes. */
    if (mode != 3) {
        size = (uint16_t)(height * (uint32_t)pitch);
        if (aperture != 0xa000 || (uint32_t)page_offset + size > 65536u)
            abort();
        gate_intro_vga16_fill_bytes(page_offset, (uint16_t)size,
                                     (uint8_t)(color & 15u));
        return 0;
    }

    /* 2488:01d7: REP STOSW, count=(height*(bpp*pitch) mod 65536)/2.
     * AH is copied from AL; an odd byte at the end is never written. */
    size = (uint16_t)((uint16_t)(height * (uint32_t)gate_far_byte(
        ds, (uint16_t)(descriptor + 0x1e))) * (uint32_t)pitch);
    size &= ~1u;
    if (aperture != 0xa000 || (uint32_t)page_offset + size > 65536u)
        abort();
    for (i = 0; i < size; ++i)
        gate_far_set_byte(aperture, (uint16_t)(page_offset + i),
                          (uint8_t)color);
    return 0;
}

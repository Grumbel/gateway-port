/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_LAUNCHER_INTRO_HOST_MEMORY_H
#define GATE_LAUNCHER_INTRO_HOST_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include "gate_exe.h"

/* Native memory implementation for the reconstructed 8086 C entry points.
 * An original DOS-RAM capture may initialize bytes[]; this structure never
 * embeds any proprietary game data in the source repository. Synthetic
 * stack segments alias C locals during a call, so FAR callbacks can write
 * directly to a caller's uint16_t or image work array. */
enum {
    GATE_INTRO_RAM_BYTES = 0x100000,
    GATE_INTRO_MAX_STACK = 4096,
    GATE_INTRO_MAX_ALLOCATIONS = 256
};

typedef struct GateIntroAllocation {
    uint32_t linear;
    uint32_t length;
    uint8_t active;
} GateIntroAllocation;

typedef struct GateIntroStackView {
    void *native_pointer;
    uint16_t synthetic_segment;
} GateIntroStackView;

typedef struct GateIntroMemory {
    uint8_t bytes[GATE_INTRO_RAM_BYTES];
    /* VGA mode 12h has four independent 64KiB bitplanes at A000; the
     * conventional 20-bit address space cannot represent all four. */
    uint8_t vga_planes[4][0x10000];
    uint16_t ds_segment;
    uint16_t stack_view_count;
    GateIntroStackView stack_views[GATE_INTRO_MAX_STACK];
    GateIntroAllocation allocations[GATE_INTRO_MAX_ALLOCATIONS];
    /* Optional host-only tap after a complete native VFX image draw.
     * The original source-C renderer does not know about frame export. */
    void (*frame_ready)(void *userdata);
    void *frame_userdata;
} GateIntroMemory;

/* Caller owns storage and controls its lifetime. The bound context must
 * remain attached until all reconstructed intro functions return. */
void gate_intro_memory_attach(GateIntroMemory *memory);
void gate_intro_memory_detach(void);
void gate_intro_memory_notify_frame(void);

/* Native 16-byte-aligned conventional-RAM arena for original VFX pixel
 * allocations. Only the standalone C intro uses the reserved 60000..9efff
 * physical span; it must not be enabled during guest CPU emulation. Reuses
 * freed intervals. A buffer may exceed 64KiB and cross a FAR segment
 * boundary; the caller must normalize subsequent 16-bit offset wraps.
 * Failure returns segment=offset=0. */
GateFarPtr gate_intro_memory_alloc(uint32_t bytes);
uint16_t gate_intro_memory_free(GateFarPtr pointer);

/* Native VGA mode 12h (640x480x16) plane storage used by original 24a7:02fe.
 * Unrelated to A000:offset RAM used by linear mode 13h. These accessors
 * operate on palette indices; palette programming is a separate service. */
uint8_t gate_intro_vga16_pixel(uint16_t x, uint16_t y);
void gate_intro_vga16_set_pixel(uint16_t x, uint16_t y, uint8_t index);
/* Fill a physical byte interval in all four VGA planes, including mode-1
 * page offsets and 800x600 modes outside the 640x480 pixel-view bounds. */
void gate_intro_vga16_fill_bytes(uint16_t offset, uint16_t count, uint8_t index);

#endif

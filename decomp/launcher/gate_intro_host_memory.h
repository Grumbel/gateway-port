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
    uint16_t ds_segment;
    uint16_t stack_view_count;
    GateIntroStackView stack_views[GATE_INTRO_MAX_STACK];
    GateIntroAllocation allocations[GATE_INTRO_MAX_ALLOCATIONS];
} GateIntroMemory;

/* Caller owns storage and controls its lifetime. The bound context must
 * remain attached until all reconstructed intro functions return. */
void gate_intro_memory_attach(GateIntroMemory *memory);
void gate_intro_memory_detach(void);

/* Native 16-byte-aligned conventional-RAM arena for original VFX pixel
 * allocations. Only the standalone C intro uses the reserved 60000..9efff
 * physical span; it must not be enabled during guest CPU emulation. Reuses
 * freed intervals. Failure returns segment=offset=0. */
GateFarPtr gate_intro_memory_alloc(uint32_t bytes);
uint16_t gate_intro_memory_free(GateFarPtr pointer);

#endif

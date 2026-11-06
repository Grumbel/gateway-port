/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_LAUNCHER_INTRO_HOST_MEMORY_H
#define GATE_LAUNCHER_INTRO_HOST_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* Native memory implementation for the reconstructed 8086 C entry points.
 * An original DOS-RAM capture may initialize bytes[]; this structure never
 * embeds any proprietary game data in the source repository. Synthetic
 * stack segments alias C locals during a call, so FAR callbacks can write
 * directly to a caller's uint16_t or image work array. */
enum { GATE_INTRO_RAM_BYTES = 0x100000, GATE_INTRO_MAX_STACK = 4096 };

typedef struct GateIntroStackView {
    void *native_pointer;
    uint16_t synthetic_segment;
} GateIntroStackView;

typedef struct GateIntroMemory {
    uint8_t bytes[GATE_INTRO_RAM_BYTES];
    uint16_t ds_segment;
    uint16_t stack_view_count;
    GateIntroStackView stack_views[GATE_INTRO_MAX_STACK];
} GateIntroMemory;

/* Caller owns storage and controls its lifetime. The bound context must
 * remain attached until all reconstructed intro functions return. */
void gate_intro_memory_attach(GateIntroMemory *memory);
void gate_intro_memory_detach(void);

#endif

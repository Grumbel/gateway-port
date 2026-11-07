/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl.h"
#include "gate_intro_host_memory.h"

/* The live original calls 1e2c:0008/0138 for font allocations. The native
 * adapter uses its own reserved conventional-memory arena, never touches
 * the DOS heap inside the captured guest snapshot. */
GateFarPtr gate_far_1e2c_0008(uint16_t bytes)
{
    return gate_intro_memory_alloc(bytes);
}

void gate_far_1e2c_0138(GateFarPtr pointer)
{
    (void)gate_intro_memory_free(pointer);
}

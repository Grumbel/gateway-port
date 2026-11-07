/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

/* Source-level extraction of the original straight-line prologue at
 * GATE.EXE:1309..134b. This helper is NOT an additional original function.
 * The following FAR call at 134c belongs to the original enclosing intro.
 * Keeping this block callable makes guest-entry memory-differential tests
 * possible while the remaining startup services are reconstructed. */
void gate_exe_prepare_intro_globals(void)
{
    gate_far_set_word(gate_ds_word(0x4e46), 0x0660, 0x0401);
    gate_far_set_word(gate_ds_word(0x4e48), 0x065c, 0x007f);
    gate_far_set_word(gate_ds_word(0x4e4a), 0x065e, 1);
    gate_far_set_word(gate_ds_word(0x4e4c), 0x064e, 0);
    gate_far_set_word(gate_ds_word(0x4e4e), 0x0658,
                      (uint16_t)(gate_linked_allocate_intro_memory(0x1000) - 0x12b2));
}

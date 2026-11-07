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
    gate_far_set_word(gate_ds_word(GATE_DS_STARTUP_CONFIG_FIRST_SEGMENT),
                      GATE_DS_MUSIC_STARTUP_CONFIG, GATE_STARTUP_MUSIC_CONFIG_INITIAL_WORD);
    gate_far_set_word(gate_ds_word(GATE_DS_STARTUP_CONFIG_SECOND_SEGMENT),
                      GATE_DS_STARTUP_PARAMETER_TWO, GATE_STARTUP_SECOND_PARAMETER_INITIAL_WORD);
    gate_far_set_word(gate_ds_word(GATE_DS_STARTUP_CONFIG_THIRD_SEGMENT),
                      GATE_DS_STARTUP_PARAMETER_THREE, GATE_STARTUP_THIRD_PARAMETER_INITIAL_WORD);
    gate_far_set_word(gate_ds_word(GATE_DS_STARTUP_CONFIG_FOURTH_SEGMENT),
                      GATE_DS_STARTUP_PARAMETER_FOUR, GATE_STARTUP_FOURTH_PARAMETER_INITIAL_WORD);
    gate_far_set_word(gate_ds_word(GATE_DS_STARTUP_CONFIG_FIFTH_SEGMENT), GATE_DS_STARTUP_ALLOCATED_SEGMENT,
                      (uint16_t)(gate_linked_allocate_intro_memory(GATE_STARTUP_MEMORY_PARAGRAPHS) -
                                 GATE_STARTUP_MEMORY_SEGMENT_BIAS));
}

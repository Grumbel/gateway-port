/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"
#include "gate_c_runtime_constants.h"

/* Linked Microsoft C 5.1 CRT rand.c:_srand (OMF PUBDEF verified against
 * GATE.RTL:091d8) at 12a5:3afa (physical 1654a in the 22-second original
 * intro RAM). The argument is a single 16-bit
 * WORD: high half of the retained 32-bit state is cleared. */
void gate_linked_seed_runtime_random(uint16_t seed)
{
    gate_ds_set_word(GATE_DS_RANDOM_STATE_LOW, seed);
    gate_ds_set_word(GATE_DS_RANDOM_STATE_HIGH, 0);
}

/* Linked Microsoft C 5.1 CRT rand.c:_rand (OMF PUBDEF verified against
 * GATE.RTL:091ea) at 12a5:3b0c, physical 1655c in the same capture.
 * The 8086 multiplies a little-endian 32-bit state by 000343fdh and adds
 * 00269ec3h, retaining only the low 32 bits, then masks bit 15 of DX.
 * RTL:13115 and RTL:12f6b consume this exact 0..32767 return value for
 * variable intro animation frame delays. */
uint16_t gate_linked_runtime_random(void)
{
    uint32_t state = (uint32_t)gate_ds_word(GATE_DS_RANDOM_STATE_LOW) |
                     ((uint32_t)gate_ds_word(GATE_DS_RANDOM_STATE_HIGH) << GATE_CRT_WORD_BITS);
    state = state * GATE_CRT_RANDOM_MULTIPLIER + GATE_CRT_RANDOM_INCREMENT;
    gate_ds_set_word(GATE_DS_RANDOM_STATE_LOW, (uint16_t)state);
    gate_ds_set_word(GATE_DS_RANDOM_STATE_HIGH, (uint16_t)(state >> GATE_CRT_WORD_BITS));
    return (uint16_t)((state >> GATE_CRT_WORD_BITS) & GATE_CRT_RANDOM_RESULT_MASK);
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_audio_startup.h"
#include "gate_exe.h"

/* Original GATE.RTL:c3c5..c3d8 (linked 0a46:01f4..0207).
 * The actual music loader occupies the remainder of c3bd..c6fe and is
 * deliberately NOT reconstructed by this small predicate. */
uint16_t gate_rtl_c3bd_music_initialization_required(void)
{
    return (uint16_t)(((gate_far_byte(gate_ds_segment(), GATE_DS_MUSIC_STARTUP_FLAGS) & 6u) != 0u) &&
                      (gate_ds_word(GATE_DS_MUSIC_STARTUP_CONFIG) != 0u));
}

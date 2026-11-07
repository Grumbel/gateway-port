/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_RTL_AUDIO_STARTUP_H
#define GATE_RTL_AUDIO_STARTUP_H

#include <stdint.h>

/* True when GATE.RTL:c3bd takes the original music-load path. */
uint16_t gate_rtl_c3bd_music_initialization_required(void);

#endif

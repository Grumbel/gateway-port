/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATEWAY_INTRO_SOURCE_C_BOOTSTRAP_H
#define GATEWAY_INTRO_SOURCE_C_BOOTSTRAP_H

#include "gate_intro_host_memory.h"

/* Recreate only the byte-verified original GATE.EXE data copy at the
 * 22-second source-C boundary. This is not a DOS/RTLink cold initializer.
 * Return 0 for a missing/incompatible/unreadable original file. */
int gate_intro_restore_static_exe_data(GateIntroMemory *memory,
                                       const char *dos_directory);

/* Restore unchanged, nonzero GATE.RTL bytes in a linked resident bank while
 * leaving zero placeholders (and their runtime relocation words) untouched.
 * This still needs those relocated words from a separate startup source. */
int gate_intro_restore_static_rtl_data(GateIntroMemory *memory,
                                       const char *dos_directory);

/* The 22-second default intro has the 256-color palette from original
 * GATE_301.PIC entry #5 in its DS buffer. Read it through the PIC directory;
 * no palette bytes or captured guest state are embedded in the host. */
int gate_intro_restore_initial_palette(GateIntroMemory *memory,
                                       const char *dos_directory);

#endif

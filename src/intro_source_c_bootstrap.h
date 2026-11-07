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

#endif

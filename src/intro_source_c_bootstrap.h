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
 * The intro's corrected linked palette operands do not read that old code
 * bank, so the original RTL relocation words are not cold-start inputs. */
int gate_intro_restore_static_rtl_data(GateIntroMemory *memory,
                                       const char *dos_directory);

/* The 22-second default intro has the 256-color palette from original
 * GATE_301.PIC entry #5 in its DS buffer. Read it through the PIC directory;
 * no palette bytes or captured guest state are embedded in the host. */
int gate_intro_restore_initial_palette(GateIntroMemory *memory,
                                       const char *dos_directory);

/* Rebuild the pinned launcher's 105 linked FAR segment-selector words from
 * its DS load segment. The two resident target banks differ by 10 paragraphs.
 * This reconstructs a verified table, not the full original RTLink loader. */
int gate_intro_restore_linked_selectors(GateIntroMemory *memory);

/* Initialize the launcher defaults already established at the 22-second
 * original DOS boundary: resource-extension FAR pointers, active VGA mode,
 * video ports and the executable-derived filename prefix. This is only the
 * pinned default configuration, not a general DOS startup replacement. */
int gate_intro_restore_default_runtime_state(GateIntroMemory *memory);

/* Recreate the installed DOS interrupt-vector entries seen at the original
 * pinned introduction boundary from the relocated EXE load paragraph. Other
 * vectors remain unchanged; this is not a full DOS interrupt dispatcher. */
int gate_intro_restore_default_vectors(GateIntroMemory *memory);

#endif

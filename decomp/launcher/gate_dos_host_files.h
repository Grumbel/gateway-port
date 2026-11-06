/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_LAUNCHER_DOS_HOST_FILES_H
#define GATE_LAUNCHER_DOS_HOST_FILES_H

#include <stdio.h>

/* Native host adapter for the five reconstructed DOS file wrappers.
 * The DOS directory belongs to the caller, and every opened filename is
 * resolved beneath it. Initialize the entire struct to zero before use. */
typedef struct GateDosHostFiles {
    const char *directory;
    FILE *handles[256];
} GateDosHostFiles;

void gate_dos_host_files_attach(GateDosHostFiles *files);
void gate_dos_host_files_close_all(GateDosHostFiles *files);

#endif

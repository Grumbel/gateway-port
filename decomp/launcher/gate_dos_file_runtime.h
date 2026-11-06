/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_DOS_FILE_RUNTIME_H
#define GATE_LAUNCHER_DOS_FILE_RUNTIME_H

#include "gate_exe.h"

/* Result of the original DOS INT 21h service. Carry is an independent CPU
 * flag: AX=0 is a valid success for open/seek, not a reliable error bit. */
typedef struct GateDosFileResult {
    uint16_t ax;
    uint16_t dx;
    uint8_t carry;
} GateDosFileResult;

/* Narrow host boundary for the original 21h/3c,3d,3e,3f,40,42 services.
 * The source-decompiled wrappers retain their exact caller-visible ABI. */
GateDosFileResult gate_dos_file_open(GateFarPtr path, uint16_t access,
                                     uint16_t create);
GateDosFileResult gate_dos_file_close(uint16_t handle);
GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr destination,
                                     uint16_t count);
GateDosFileResult gate_dos_file_write(uint16_t handle, GateFarPtr source,
                                      uint16_t count);
GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin,
                                     uint32_t offset);

#endif

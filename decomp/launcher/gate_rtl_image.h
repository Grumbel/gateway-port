/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_IMAGE_DECOMP_H
#define GATE_LAUNCHER_RTL_IMAGE_DECOMP_H

#include "gate_exe.h"

/* First original image-display entry, called at GATE.EXE 0f10:1014.
 * Return value is original AX=0 or 1, ignored by the launcher caller. */
uint16_t gate_rtl_12b19(uint16_t resource, uint16_t mode);

/* Original image dependencies awaiting their bounded decompilations. */
uint16_t gate_rtl_12c33(GateFarPtr work, uint16_t resource, uint16_t mode);
void gate_rtl_1266f(void);
void gate_rtl_12ed7(GateFarPtr work);

#endif

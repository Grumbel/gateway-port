/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_MODE_H
#define GATE_LAUNCHER_RTL_MODE_H

#include "gate_exe.h"

/* The original mode-switch entry in GATE.RTL, linked as 0cdc:0004.
 * Its AX result is 1 for success and 0 for a rejected/failed mode. */
uint16_t gate_rtl_f14d(int16_t requested_mode);

/* Near helper called at RTL:f25a to program the VGA attribute palette. */
void gate_rtl_f49b(void);

/* Original linked resident hardware calls, with their identities preserved.
 * The query writes at least two bytes (adapter type, monitor subtype).
 * The original clip stack order, closest argument first, is bottom,
 * right, top, left. Hardware implementations live outside decomp/. */
void gate_rtl_mode_query_adapter(uint8_t adapter[8]); /* 242a:000a */
uint16_t gate_rtl_mode_svga_variant(void);             /* 241d:0004 */
uint16_t gate_rtl_mode_set_bios(uint16_t mode);        /* 2409:0008 */
uint16_t gate_rtl_mode_start(uint16_t enabled);       /* 2409:003e */
void gate_rtl_mode_clip(uint16_t bottom, uint16_t right,
                        uint16_t top, uint16_t left); /* 25b7:003a */
void gate_rtl_mode_pen(uint16_t mode);                /* 25b7:0008 */

#endif

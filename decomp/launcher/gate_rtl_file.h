/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_FILE_DECOMP_H
#define GATE_LAUNCHER_RTL_FILE_DECOMP_H

#include "gate_exe.h"

/* Source identities are ORIGINAL GATE.RTL file offsets.  These functions
 * were linked into the 0c33 bank in the captured intro, but no runtime
 * segment number is a stable function identity. */
void gate_rtl_e4ff(GateFarPtr base);
uint16_t gate_rtl_e511(GateFarPtr path);
uint16_t gate_rtl_e531(uint16_t type, uint16_t resource);
void gate_rtl_e582(uint16_t handle, uint32_t offset);
uint16_t gate_rtl_e59b(uint16_t handle);
uint16_t gate_rtl_e5c5(uint16_t handle);
uint16_t gate_rtl_e5e3(uint16_t handle, GateFarPtr destination, uint16_t count);
uint16_t gate_rtl_e5fc(uint16_t handle, GateFarPtr destination,
                       uint16_t element_size, uint16_t count);
uint16_t gate_rtl_e61e(uint16_t handle, uint16_t word);
uint16_t gate_rtl_e637(uint16_t handle, uint16_t word);
uint16_t gate_rtl_e650(uint16_t handle, GateFarPtr source, uint16_t count);
uint16_t gate_rtl_e669(uint16_t handle, GateFarPtr source,
                       uint16_t element_size, uint16_t count);
void gate_rtl_e68b(uint16_t handle);

/* The original low-level runtime/DOS calls retain their FAR identities.
 * Their complete implementations and ABI have not been recovered yet. */
uint16_t gate_far_12a5_3b52(GateFarPtr path, uint16_t code);
void gate_far_12a5_3366(GateFarPtr output, GateFarPtr format,
                        GateFarPtr base, uint16_t resource, GateFarPtr table);
uint16_t gate_far_1c29_0000(GateFarPtr filename, uint16_t flags);
uint16_t gate_far_1c29_0074(uint16_t handle, uint16_t origin, uint32_t offset);
uint16_t gate_far_1c29_0036(uint16_t handle, GateFarPtr destination,
                            uint16_t count);
uint16_t gate_far_1c29_0055(uint16_t handle, GateFarPtr source,
                            uint16_t count);
uint16_t gate_far_1c29_0028(uint16_t handle);

/* Original routines e59b/e5c5 read an uninitialized stack WORD.  In the
 * guest it holds whatever bytes occupied the local slot on function entry.
 * The host abstraction must supply this entry-state value, not fabricate a
 * success/failure fallback. */
uint16_t gate_rtl_file_stack_word(void);

/* The original 8086 unsigned DIV faults on a zero element size.  This
 * callback must transfer control to the guest's divide-exception handler. */
_Noreturn void gate_rtl_file_divide_fault(void);

#endif

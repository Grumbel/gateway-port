/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"

/* Original 0c33 bank's linked names, isolated so native file/resource
 * integration can link without pulling the unrelated graphics/runtime
 * service bank. These are linker aliases, not new decompiled routines. */
void gate_far_0c33_019a(uint16_t handle) { gate_rtl_e68b(handle); }

uint16_t gate_far_0c33_0040(uint16_t type, uint16_t resource)
{
    return gate_rtl_e531(type, resource);
}

void gate_far_0c33_0091(uint16_t handle, uint32_t offset)
{
    gate_rtl_e582(handle, offset);
}

uint16_t gate_far_0c33_00f2(uint16_t handle, GateFarPtr destination,
                             uint16_t count)
{
    return gate_rtl_e5e3(handle, destination, count);
}

uint16_t gate_far_0c33_00d4(uint16_t handle)
{
    return gate_rtl_e5c5(handle);
}

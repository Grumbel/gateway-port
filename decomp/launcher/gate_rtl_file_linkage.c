/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_file.h"

/* Original 0c33 bank's linked names, isolated so native file/resource
 * integration can link without pulling the unrelated graphics/runtime
 * service bank. These are linker aliases, not new decompiled routines. */
void gate_linked_close_resource_file(uint16_t handle) { gate_rtl_close_resource_file(handle); }

uint16_t gate_linked_open_resource_file(uint16_t type, uint16_t resource)
{
    return gate_rtl_open_resource_file(type, resource);
}

void gate_linked_seek_resource_file(uint16_t handle, uint32_t offset)
{
    gate_rtl_seek_resource_file(handle, offset);
}

uint16_t gate_linked_read_resource_bytes(uint16_t handle, GateFarPtr destination,
                             uint16_t count)
{
    return gate_rtl_read_resource_bytes(handle, destination, count);
}

uint16_t gate_linked_read_resource_word(uint16_t handle)
{
    return gate_rtl_read_resource_word(handle);
}

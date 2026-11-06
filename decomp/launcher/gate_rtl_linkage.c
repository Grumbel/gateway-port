/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"
#include "gate_rtl.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_palette.h"
#include "gate_rtl_image.h"
#include "gate_rtl_file.h"

/* Source-level linker aliases, not additional recovered original routines.
 * The original EXE calls fixed linked segment:offset addresses; the
 * GATE.RTL file offsets below were verified against captured DOS memory.
 * Keeping this bridge separate preserves the original caller source and
 * allows individual C test targets to use their original-call mock oracles. */

uint16_t gate_far_0c4d_0511(void) { return gate_rtl_ebf7(); }
uint16_t gate_far_0c4d_0524(uint16_t ch) { return gate_rtl_ec0a(ch); }
uint16_t gate_far_0c4d_05be(GateFarPtr string) { return gate_rtl_eca4(string); }
void gate_far_0c4d_0653(int16_t x, int16_t y) { gate_rtl_ed39(x, y); }
void gate_far_0c4d_0698(GateFarPtr string) { gate_rtl_ed7e(string); }

/* The original file I/O wrappers are in the separately relocated 0c33
 * bank.  Keep a one-to-one alias for each recovered native entry. */
void gate_far_0c33_019a(uint16_t handle) { gate_rtl_e68b(handle); }
uint16_t gate_far_0c33_0040(uint16_t type, uint16_t resource)
{
    return gate_rtl_e531(type, resource);
}
void gate_far_0c33_0091(uint16_t handle, uint32_t offset)
{
    gate_rtl_e582(handle, offset);
}
uint16_t gate_far_0c33_00f2(uint16_t handle, GateFarPtr destination, uint16_t count)
{
    return gate_rtl_e5e3(handle, destination, count);
}
uint16_t gate_far_0c33_00d4(uint16_t handle) { return gate_rtl_e5c5(handle); }

void gate_far_0f10_0c6e(void) { gate_rtl_12773(); }
void gate_far_0f10_0d1e(void) { gate_rtl_12823(); }
void gate_far_0f10_0d8e(void) { gate_rtl_12893(); }
void gate_far_0f10_1680(GateFarPtr work) { gate_rtl_13185(work); }
void gate_far_0f10_16c6(GateFarPtr work) { gate_rtl_131cb(work); }

void gate_far_0cdc_04d2(GateFarPtr palette) { gate_rtl_f61b(palette); }

/* The original graphics code passes SS:offset of its local palette array,
 * while host C represents that stack storage as a native pointer. Keep
 * this source-level FAR-pointer translation at the link boundary. */
void gate_rtl_graphics_palette_upload(const uint8_t *palette)
{
    GateFarPtr original_pointer = palette != 0
        ? gate_stack_pointer((void *)palette)
        : (GateFarPtr){0, 0};
    gate_rtl_f61b(original_pointer);
}

void gate_far_0f10_1014(uint16_t resource, uint16_t mode)
{
    (void)gate_rtl_12b19(resource, mode);
}

void gate_far_0f10_112e(GateFarPtr work, uint16_t resource, uint16_t mode)
{
    (void)gate_rtl_12c33(work, resource, mode);
}
void gate_far_0f10_13d2(GateFarPtr work) { gate_rtl_12ed7(work); }
void gate_far_0f10_1466(uint16_t first, uint16_t next, uint16_t count)
{
    gate_rtl_12f6b(first, next, (int16_t)count);
}

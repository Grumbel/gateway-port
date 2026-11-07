/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"
#include "gate_rtl.h"
#include "gate_rtl_graphics.h"
#include "gate_rtl_palette.h"
#include "gate_rtl_mode.h"
#include "gate_rtl_draw.h"
#include "gate_rtl_image.h"

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
void gate_far_0c4d_061b(uint16_t ink, uint16_t background)
{
    gate_rtl_ed01(ink, (int16_t)background);
}

void gate_far_0f10_0c6e(void) { gate_rtl_12773(); }
void gate_far_0f10_0d1e(void) { gate_rtl_12823(); }
void gate_far_0f10_0d8e(void) { gate_rtl_12893(); }
void gate_far_0f10_1680(GateFarPtr work) { gate_rtl_13185(work); }
void gate_far_0f10_16c6(GateFarPtr work) { gate_rtl_131cb(work); }

void gate_far_0cdc_04d2(GateFarPtr palette) { gate_rtl_f61b(palette); }
void gate_far_0cdc_0004(uint16_t mode) { (void)gate_rtl_f14d((int16_t)mode); }
void gate_far_0cdc_0aac(uint16_t color) { (void)gate_rtl_fbf5(color); }
void gate_far_0cdc_0aee(uint16_t style, uint16_t left, uint16_t top,
                         uint16_t right, uint16_t bottom)
{
    gate_rtl_fc37(style, left, top, right, bottom);
}
void gate_far_0cdc_0e50(uint16_t left, uint16_t top, uint16_t right,
                         uint16_t bottom, uint16_t style)
{
    gate_rtl_ff99(left, top, right, bottom, style);
}

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
void gate_far_0f10_15a6(uint16_t first, uint16_t count)
{
    gate_rtl_130ab(first, (int16_t)count);
}

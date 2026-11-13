/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_GRAPHICS_DECOMP_H
#define GATE_LAUNCHER_RTL_GRAPHICS_DECOMP_H

#include "gate_exe.h"

/* Original RTL:12773..12995 palette-fade immediates. The division is a
 * SIGNED 16-bit product divided by 64, rounded toward zero. Mode 1 is a
 * separate destructive 16-byte three-pass transform with 100ms waits. */
enum {
    GATE_RTL_PALETTE_SCALE_DENOMINATOR = 64,
    GATE_RTL_PALETTE_FADE_STEP = 4,
    GATE_RTL_PALETTE_MODE1_PASSES = 3,
    GATE_RTL_PALETTE_MODE1_BYTES = 16,
    GATE_RTL_PALETTE_MODE1_BIT_SHIFT = 3,
    GATE_RTL_PALETTE_MODE1_LOW_BITS_MASK = 0x07,
    GATE_RTL_PALETTE_MODE1_PASS_WAIT_MS = 100,
};

/* Original GATE.RTL file offsets, not DOS runtime addresses. */
void gate_rtl_fade_palette_in(void); /* fade palette in, linked as 0f10:0c6e */
void gate_rtl_clear_palette_buffer(void); /* clear palette and video buffer, 0f10:0d1e */
void gate_rtl_fade_palette_out(void); /* fade palette out, linked as 0f10:0d8e */
void gate_rtl_initialize_picture_record(GateFarPtr work); /* init work area, 0f10:1680 */
void gate_rtl_release_picture_record(GateFarPtr work); /* cleanup work area, 0f10:16c6 */

/* Linked data pointers in the RTL data segment. The original operands
 * DS:000a, DS:0010, DS:0016 and DS:0018 are relocated far globals. The
 * host accessors are temporary representations of those C globals. */
uint16_t gate_rtl_graphics_mode(void);                /* [DS:000a]->WORD */
void gate_rtl_graphics_set_index(uint16_t value);     /* [DS:0010]->WORD */
int16_t gate_rtl_graphics_byte_count(uint16_t index); /* [DS:0016][mode] */
uint8_t gate_rtl_graphics_byte(uint16_t index);       /* [DS:0018][index] */
void gate_rtl_graphics_set_byte(uint16_t index, uint8_t value);

/* Real, independently observed linked call sites. The first is
 * GATE.RTL:0xf61b (linked 0cdc:04d2), still awaiting its own decompile.
 * A null pointer represents the relocated FAR pointer to the [DS:0018]
 * palette buffer (fixups target 2 + 0f9a; the file holds 0000:0000
 * placeholders); non-null points to the original stack-local palette
 * bytes. Its byte count is
 * determined by the original hardware mode, not passed on the stack. */
void gate_rtl_graphics_palette_upload(const uint8_t *palette);
void gate_rtl_graphics_delay(uint32_t delay); /* linked 123f:00b6 */
void gate_rtl_graphics_release(GateFarPtr data); /* linked 145d:016c */

#endif

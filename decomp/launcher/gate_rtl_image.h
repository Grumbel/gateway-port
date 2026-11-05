/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_RTL_IMAGE_DECOMP_H
#define GATE_LAUNCHER_RTL_IMAGE_DECOMP_H

#include "gate_exe.h"

/* First original image-display entry, called at GATE.EXE 0f10:1014.
 * Return value is original AX=0 or 1, ignored by the launcher caller. */
uint16_t gate_rtl_12b19(uint16_t resource, uint16_t mode);

/* GATE.RTL:0x12c33..0x12d66, original resource-loading subroutine. */
uint16_t gate_rtl_12c33(GateFarPtr work, uint16_t resource, uint16_t mode);

/* GATE.RTL:0x11ecb..0x120f1, original resource selector and file reader. */
uint16_t gate_rtl_11ecb(uint16_t resource, uint16_t mode);
/* Original dependencies of 0x12c33, not yet decompiled. All far service
 * identities are verified against a private, linked guest-RAM snapshot. */
void gate_rtl_12205(GateFarPtr work);

/* RTL DS:000a, DS:000c, DS:0014 hold the segments of relocated globals,
 * rather than flat host pointers. The accessor must preserve those WORDs. */
uint16_t gate_rtl_image_mode_segment(void);
uint16_t gate_rtl_image_palette_segment(void);
uint16_t gate_rtl_image_header_segment(void);

/* Linked GATE.RTL:0x12c8c = 145d:00f7. The last two parameters of the
 * original call are a FAR output pointer and a zero-valued flag. */
uint16_t gate_far_145d_00f7(uint16_t header_word8, uint16_t header_word6,
                            uint16_t video_mode, GateFarPtr destination,
                            uint16_t zero_flag);
/* Linked 1479:0002 at RTL:0x12ca3, then 0eec:018a at RTL:0x12d33. */
void gate_far_1479_0002(uint16_t zero_flag, GateFarPtr destination);
uint16_t gate_far_0eec_018a(uint16_t work_word20, uint16_t work_word22);

/* RTL's near DS:0000..0010 words, plus the relocated segment words at
 * DS:0012 (resource file handle) and DS:0016 (format-length table). */
uint16_t gate_rtl_resource_ds_word(uint16_t offset);
void gate_rtl_resource_ds_set_word(uint16_t offset, uint16_t value);
uint16_t gate_rtl_resource_handle_segment(void);
uint16_t gate_rtl_resource_size_table_segment(void);

/* Original far services reached by RTL:0x11ecb, linked into 0c33. The
 * meaning of the 32-bit argument of 0c33:00f2 is still unverified; retain
 * the original WORD/DWORD call contract rather than assigning a file API. */
void gate_far_0c33_019a(uint16_t handle);
uint16_t gate_far_0c33_0040(uint16_t zero_word, uint16_t resource_id);
void gate_far_0c33_0091(uint16_t handle, uint32_t offset);
uint16_t gate_far_0c33_00f2(uint16_t handle, uint32_t dword_arg,
                            uint16_t word_arg);
uint16_t gate_far_0c33_00d4(uint16_t handle);

/* More image dependencies awaiting bounded decompilations. */
void gate_rtl_1266f(void);
void gate_rtl_12ed7(GateFarPtr work);

#endif

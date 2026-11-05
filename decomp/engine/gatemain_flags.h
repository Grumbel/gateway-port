/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATEWAY_GATEMAIN_FLAGS_DECOMP_H
#define GATEWAY_GATEMAIN_FLAGS_DECOMP_H

#include <stdint.h>

/* Manual C decompilation of GATEMAIN.EXE, NOT the original lost source.
 * Function identifiers are ORIGINAL FILE OFFSETS, not runtime IPs.
 * Read/write intrinsics stand in for the original 16-bit DS/ES addressing. */
typedef struct GatemainFarPtr {
    uint16_t segment;
    uint16_t offset;
} GatemainFarPtr;

uint16_t gatemain_ds_word(uint16_t offset);
uint16_t gatemain_far_word(uint16_t segment, uint16_t offset);
uint8_t gatemain_far_byte(uint16_t segment, uint16_t offset);
void gatemain_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);

GatemainFarPtr gatemain_exe_27db(uint16_t object); /* state at +4 */
GatemainFarPtr gatemain_exe_284b(uint16_t object, uint16_t property,
                                  uint16_t *bit); /* property byte and bit */
uint16_t gatemain_exe_28e4(uint16_t object, uint16_t property); /* test */
void gatemain_exe_291e(uint16_t object, uint16_t property);     /* set */
void gatemain_exe_2951(uint16_t object, uint16_t property);     /* clear */

#endif

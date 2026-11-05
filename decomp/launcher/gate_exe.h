/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_EXE_DECOMP_H
#define GATE_LAUNCHER_EXE_DECOMP_H

#include <stdint.h>

/*
 * Manual C decompilation of GATE.EXE, not the original lost C source.
 * Function names identify ORIGINAL FILE OFFSETS, not runtime addresses.
 * The external calls retain their original far-address identities because
 * their source names and full prototypes have not been recovered.
 *
 * These accessors are a test/host substitute for the original compiler's
 * DS/ES segmented pointers, not a proposed native engine interface.
 */
typedef struct GateFarPtr {
    uint16_t segment;
    uint16_t offset;
} GateFarPtr;

uint16_t gate_ds_word(uint16_t offset);
void gate_ds_set_word(uint16_t offset, uint16_t value);
uint16_t gate_ds_segment(void);
uint16_t gate_far_word(uint16_t segment, uint16_t offset);
uint8_t gate_far_byte(uint16_t segment, uint16_t offset);
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value);

/* Linked 16-bit far calls; conventions inferred from caller instructions. */
uint16_t gate_far_0df2_000a(void); /* keyboard pending */
uint16_t gate_far_0df2_0030(void); /* consume key */
void gate_far_0df2_00f8(void);     /* keyboard cleanup */
uint16_t gate_far_0e48_07fe(void); /* mouse flags in AL */
uint32_t gate_far_02a5_38c4(void); /* clock returned in DX:AX */
uint16_t gate_far_02a5_2bba(GateFarPtr text); /* string length, AX */
void gate_far_123f_0004(void);
void gate_far_0c4d_018d(uint16_t value);
void gate_far_0c4d_061b(uint16_t handle, uint16_t mode);
uint16_t gate_far_0c4d_05be(GateFarPtr string);
uint16_t gate_far_0c4d_0524(uint16_t character);
uint16_t gate_far_0c4d_0511(void);
void gate_far_0c4d_0653(int16_t x, int16_t y);
void gate_far_0c4d_0698(GateFarPtr string);
void gate_far_0cdc_04d2(GateFarPtr palette);
uint16_t gate_far_0e2c_0094(uint16_t value);
void gate_far_0a46_01ec(void);
void gate_far_0f10_0d1e(void);
void gate_far_0f10_0d8e(void);
void gate_far_0f10_0c6e(void);
void gate_far_0f10_1014(uint16_t image, uint16_t mode);
void gate_far_0f10_15a6(uint16_t image, uint16_t value);
void gate_far_0f10_1466(uint16_t first, uint16_t next, uint16_t count);
void gate_far_0f10_1680(GateFarPtr buffer);
void gate_far_0f10_112e(GateFarPtr buffer, uint16_t image, uint16_t mode);
void gate_far_0f10_13d2(GateFarPtr buffer);
void gate_far_0f10_16c6(GateFarPtr buffer);
void gate_far_0cdc_07d8(void);
void gate_far_0cdc_0004(uint16_t value);
void gate_far_0cdc_0aac(uint16_t value);
void gate_far_0cdc_0aee(uint16_t arg0, uint16_t arg1, uint16_t arg2,
                         uint16_t arg3, uint16_t arg4);
void gate_far_0e48_011b(void);
void gate_far_0cdc_0e50(uint16_t left, uint16_t top, uint16_t right,
                         uint16_t bottom, uint16_t style);
void gate_far_0df2_0128(void);

/* A stack-local buffer's SS:offset; a host substitute for 16-bit &local. */
GateFarPtr gate_stack_pointer(void *buffer);

/* Linked near calls not yet independently decompiled. */
void gate_exe_103a(uint16_t *left, uint16_t *top, uint16_t *right,
                   uint16_t *bottom, GateFarPtr text);

/* The original routines use far return (`RETF`), with C-style stack args. */
uint16_t gate_exe_0e37(void);
void gate_exe_0f18(int16_t delay);
void gate_exe_1c11(int16_t bar);
void gate_exe_1c6c(void);
void gate_exe_1302(void);
void gate_exe_1a14(void);
void gate_exe_0f61(uint16_t image);
void gate_exe_117e(uint16_t left, uint16_t top, uint16_t right, GateFarPtr text);

#endif

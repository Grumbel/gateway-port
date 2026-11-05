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
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value);

/* Linked 16-bit far calls; conventions inferred from caller instructions. */
uint16_t gate_far_0df2_000a(void); /* keyboard pending */
uint16_t gate_far_0df2_0030(void); /* consume key */
void gate_far_0df2_00f8(void);     /* keyboard cleanup */
uint16_t gate_far_0e48_07fe(void); /* mouse flags in AL */
uint32_t gate_far_02a5_38c4(void); /* clock returned in DX:AX */
void gate_far_123f_0004(void);
void gate_far_0c4d_018d(uint16_t value);
void gate_far_0c4d_061b(uint16_t handle, uint16_t mode);
uint16_t gate_far_0c4d_05be(GateFarPtr string);
uint16_t gate_far_0c4d_0511(void);
void gate_far_0c4d_0653(int16_t x, int16_t y);
void gate_far_0c4d_0698(GateFarPtr string);
void gate_far_0cdc_04d2(GateFarPtr palette);

/* The original routines use far return (`RETF`), with C-style stack args. */
uint16_t gate_exe_0e37(void);
void gate_exe_0f18(int16_t delay);
void gate_exe_1c11(int16_t bar);
void gate_exe_1c6c(void);

#endif

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_DOS_BOOTSTRAP_H
#define GATE_DOS_BOOTSTRAP_H

#include "GATEDOS.H"

/* Source-C startup owner. The supplied FAR callbacks and external-code
 * segments MUST refer to real code in the finished DOS executable; neither
 * this structure nor a successful preparation proves that code is runnable. */
typedef struct GateDosBootConfig {
    GateFarPtr exe_path;
    GateFarPtr rtl_path;
    GateFarPtr exe_callback;
    GateFarPtr return_callback;
    uint16_t external_a_segment;
    uint16_t external_b_segment;
} GateDosBootConfig;

/* Two independent DOS allocations. This is NOT the original RTLink MCB
 * layout: the original loader placed both banks in the PSP-owned block. */
typedef struct GateDosBootImage {
    uint16_t lower_segment;
    uint16_t resident_segment;
} GateDosBootImage;

/* The caller initializes image to {0,0} before the first prepare. A second
 * prepare on a live image is rejected without touching its ownership.
 * Return 0 only when both source images pass their full relocation-neutral
 * fingerprints. All failures after allocation release it and unbind DS.
 * Data preparation errors -1..-11 map to owner statuses -71..-81, so
 * original-file DOS probes can report the precise stage that failed.
 * The input files remain read-only and are closed before returning. */
int gate_dos_boot_prepare(const GateDosBootConfig *config,
                          GateDosBootImage *image);
void gate_dos_boot_release(GateDosBootImage *image);

#endif

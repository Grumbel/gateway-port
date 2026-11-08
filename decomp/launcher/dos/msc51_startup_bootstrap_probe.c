/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Real DOS original-file integration probe for the future launcher entry.
 * These FAR callbacks are TEST-ONLY nonexecutable pointers. No original
 * code is entered; a successful CRC must not be called a playable intro. */
#include "BOOT.H"
#include <stdio.h>

static char original_exe_name[] = "E:\\GATE.EXE";
static char original_rtl_name[] = "E:\\GATE.RTL";
static char unused_callback[32];

int main(void)
{
    GateDosBootConfig config;
    GateDosBootImage image;
    int status;

    image.lower_segment = 0u;
    image.resident_segment = 0u;
    config.exe_path = gate_stack_pointer(original_exe_name);
    config.rtl_path = gate_stack_pointer(original_rtl_name);
    config.exe_callback = gate_stack_pointer(unused_callback);
    config.return_callback = gate_stack_pointer(unused_callback + 1);
    /* These are intentionally not executable external-code segments. The
     * checksums normalize them; the real DOS main needs actual bindings. */
    config.external_a_segment = 0x3333u;
    config.external_b_segment = 0x4444u;
    status = gate_dos_boot_prepare(&config, &image);
    if (status == 0)
        gate_dos_boot_release(&image);
    if (status != 0 || image.lower_segment != 0u ||
        image.resident_segment != 0u) {
        printf("DOS intro image owner probe: FAIL %d\n", status);
        return 1;
    }
    puts("DOS intro image owner probe: PASS (two allocated, CRC-checked images; no code executed)");
    return 0;
}

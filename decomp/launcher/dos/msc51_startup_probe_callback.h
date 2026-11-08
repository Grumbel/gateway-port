/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_DOS_BOOT_PROBE_CALLBACK_H
#define GATE_DOS_BOOT_PROBE_CALLBACK_H

#include "BOOT.H"

/* Probe-only callback preimage: original RTL:4bc8 is the IRET immediately
 * before the RETF at +4bc9, followed by its checked instruction context.
 * The data initializer validates these bytes before installing DS:049d.
 * Read them at runtime from the user's original file, never compile game
 * bytes into the probe. The buffer is NEVER called as executable code.
 * Both BOOTCHK and BOOTPREP must supply this same verified context rather
 * than a zero-filled placeholder (which always fails data preflight). */
static char gate_probe_return_context[40];

static int gate_probe_load_return_context(GateFarPtr rtl_path,
                                          GateFarPtr *return_callback)
{
    GateDosFileResult result;
    uint16_t handle;
    int error = 0;

    if (return_callback == 0 || rtl_path.segment == 0u)
        return -1;
    result = gate_dos_file_open(rtl_path, 0u, 0u);
    if (result.carry != 0u)
        return -2;
    handle = result.ax;
    result = gate_dos_file_seek(handle, 0u, 0x4bc8ul);
    if (result.carry != 0u || result.ax != 0x4bc8u || result.dx != 0u)
        error = -3;
    if (error == 0) {
        result = gate_dos_file_read(handle,
                      gate_stack_pointer(gate_probe_return_context),
                      (uint16_t)sizeof gate_probe_return_context);
        if (result.carry != 0u ||
            result.ax != sizeof gate_probe_return_context)
            error = -4;
    }
    result = gate_dos_file_close(handle);
    if (result.carry != 0u && error == 0)
        error = -5;
    if (error == 0)
        *return_callback = gate_stack_pointer(gate_probe_return_context + 1u);
    return error;
}

#endif

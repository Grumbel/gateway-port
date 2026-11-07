/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_exe.h"

/* Original linked 0df2:00f8..0127 in the relocated 22-second launcher.
 * The input buffer is DS:1340. Non-space/non-return keys are put back for
 * the caller; the value read is returned in AX even when it is buffered.
 * No key is consumed if the original pending-key service returns zero. */
uint16_t gate_linked_read_buffered_key(void)
{
    uint16_t key = 0;

    if (gate_linked_keyboard_pending() != 0) {
        key = gate_linked_read_keyboard_key();
        if (key != 0x20 && key != 0x0d)
            gate_ds_set_word(0x1340, key);
    }
    return key;
}

/* Original linked 0df2:0128..0136: drain pending keys, including any
 * replayed buffered key, until the pending-key service reports zero. */
void gate_linked_flush_keyboard(void)
{
    while (gate_linked_keyboard_pending() != 0)
        (void)gate_linked_read_keyboard_key();
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Real BIOS keyboard and INT 33h mouse boundary for linked 0df2/0e48.
 * Buffered key and Ctrl-Break state are those of the original linked DS.
 * The original DOS Ctrl-Break / INT 21h 0c00 path still needs independent
 * disassembly verification; do not infer its extra behavior from this host. */
#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>

/* INT 16h/AH=01h sets ZF for an empty BIOS keyboard buffer. The original
 * linked function also checks DS:1340 and DS:133e, in that order. */
uint16_t gate_linked_keyboard_pending(void)
{
    union REGS input, output;
    input.x.ax = 0x0100u;
    int86(0x16, &input, &output);
    if ((output.x.flags & 0x0040u) == 0u)
        return 1u;
    if (gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY) != 0xffffu)
        return 1u;
    return gate_ds_word(GATE_DS_INPUT_PENDING_COUNT) != 0u ? 1u : 0u;
}

/* The original read prefers Ctrl-Break and the one-word unget slot over
 * INT 16h blocking input. BIOS returns scan code in AH and ASCII in AL. */
uint16_t gate_linked_read_keyboard_key(void)
{
    union REGS input, output;
    uint16_t pending = gate_ds_word(GATE_DS_INPUT_PENDING_COUNT);
    if (pending != 0u) {
        gate_ds_set_word(GATE_DS_INPUT_PENDING_COUNT, (uint16_t)(pending - 1u));
    } else if (gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY) != 0xffffu) {
        uint16_t key = gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY);
        gate_ds_set_word(GATE_DS_INPUT_BUFFERED_KEY, 0xffffu);
        return key;
    }
    input.x.ax = 0u;
    int86(0x16, &input, &output);
    return output.x.ax;
}

/* INT 33h AX=0003h returns the current mouse button mask in BX. */
uint16_t gate_linked_intro_mouse_flags(void)
{
    union REGS input, output;
    input.x.ax = 3u;
    input.x.bx = 0u;
    int86(0x33, &input, &output);
    return output.x.bx;
}

/* Original linked 0e48:011b initializes the installed mouse driver. */
void gate_linked_reset_intro_mouse(void)
{
    union REGS input, output;
    input.x.ax = 0u;
    int86(0x33, &input, &output);
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_exe.h"
#include "gate_intro_host_input.h"

#include <stdlib.h>

static GateIntroHostInput *active_input;

void gate_intro_input_attach(GateIntroHostInput *input)
{
    active_input = input;
}

void gate_intro_input_detach(void)
{
    active_input = 0;
}

uint32_t gate_linked_exe_elapsed_milliseconds(void)
{
    if (active_input == 0 || active_input->clock_ms == 0)
        abort();
    return active_input->clock_ms(active_input->userdata);
}

/* This is the same C runtime clock invoked from the RTL intro animator,
 * but through another linked call site (12a5 versus EXE's 02a5). */
uint32_t gate_linked_animation_elapsed_milliseconds(void)
{
    return gate_linked_exe_elapsed_milliseconds();
}

uint16_t gate_linked_keyboard_pending(void)
{
    if (active_input == 0 || active_input->key_pending == 0)
        abort();
    /* Original linked 0df2:000a checks the host/BIOS queue first, followed
     * by DS:1340 (one ungotten key) and DS:133e (Ctrl-Break count). */
    if (active_input->key_pending(active_input->userdata) != 0)
        return 1;
    return gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY) != 0xffff || gate_ds_word(GATE_DS_INPUT_PENDING_COUNT) != 0;
}

uint16_t gate_linked_read_keyboard_key(void)
{
    if (active_input == 0 || active_input->read_key == 0)
        abort();
    /* The original 0df2:0030 consumes a pending DOS Ctrl-Break before
     * buffered/BIOS keys. The special INT 21h 0x0c00 branch is not yet
     * independently modeled by the host; forward to the same key source. */
    if (gate_ds_word(GATE_DS_INPUT_PENDING_COUNT) != 0) {
        gate_ds_set_word(GATE_DS_INPUT_PENDING_COUNT, (uint16_t)(gate_ds_word(GATE_DS_INPUT_PENDING_COUNT) - 1u));
        return active_input->read_key(active_input->userdata);
    }
    if (gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY) != 0xffff) {
        uint16_t key = gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY);
        gate_ds_set_word(GATE_DS_INPUT_BUFFERED_KEY, 0xffff);
        return key;
    }
    return active_input->read_key(active_input->userdata);
}

uint16_t gate_linked_intro_mouse_flags(void)
{
    if (active_input == 0 || active_input->mouse_flags == 0)
        abort();
    return active_input->mouse_flags(active_input->userdata);
}

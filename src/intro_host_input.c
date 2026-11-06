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

uint32_t gate_far_02a5_38c4(void)
{
    if (active_input == 0 || active_input->clock_ms == 0)
        abort();
    return active_input->clock_ms(active_input->userdata);
}

/* This is the same C runtime clock invoked from the RTL intro animator,
 * but through another linked call site (12a5 versus EXE's 02a5). */
uint32_t gate_far_12a5_38c4(void)
{
    return gate_far_02a5_38c4();
}

uint16_t gate_far_0df2_000a(void)
{
    if (active_input == 0 || active_input->key_pending == 0)
        abort();
    return active_input->key_pending(active_input->userdata);
}

uint16_t gate_far_0df2_0030(void)
{
    if (active_input == 0 || active_input->read_key == 0)
        abort();
    return active_input->read_key(active_input->userdata);
}

uint16_t gate_far_0e48_07fe(void)
{
    if (active_input == 0 || active_input->mouse_flags == 0)
        abort();
    return active_input->mouse_flags(active_input->userdata);
}

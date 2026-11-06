/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_LAUNCHER_INTRO_HOST_INPUT_H
#define GATE_LAUNCHER_INTRO_HOST_INPUT_H

#include <stdint.h>

/* Explicit host callbacks for the original intro's linked timing/keyboard
 * and mouse services. Callbacks may use SDL or a deterministic test clock;
 * this interface does not create its own time source or event thread.
 * The original source-C entry remains in charge of deadlines and Escape. */
typedef struct GateIntroHostInput {
    void *userdata;
    uint32_t (*clock_ms)(void *userdata);
    uint16_t (*key_pending)(void *userdata);
    uint16_t (*read_key)(void *userdata);
    uint16_t (*mouse_flags)(void *userdata);
} GateIntroHostInput;

void gate_intro_input_attach(GateIntroHostInput *input);
void gate_intro_input_detach(void);

#endif

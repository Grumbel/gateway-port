/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "gate_rtl_audio_startup.h"
#include "gate_exe.h"

#include <assert.h>
#include <stdint.h>

static uint16_t mode_word;
static uint8_t audio_flags;

uint16_t gate_ds_segment(void) { return 0x2a7a; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    assert(segment == 0x2a7a);
    assert(offset == 0x0662);
    return audio_flags;
}
uint16_t gate_ds_word(uint16_t offset)
{
    assert(offset == 0x0660);
    return mode_word;
}

int main(void)
{
    unsigned i;
    for (i = 0; i < 256; ++i) {
        audio_flags = (uint8_t)i;
        mode_word = 0;
        assert(gate_rtl_c3bd_music_initialization_required() == 0);
        mode_word = 0x0401;
        assert(gate_rtl_c3bd_music_initialization_required() ==
               (((i & 6u) != 0u) ? 1u : 0u));
    }
    return 0;
}

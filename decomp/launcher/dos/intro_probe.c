/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Standalone DOS toolchain probe. This is NOT the game's entry point. */
#include "../gate_exe.h"

#include <stdio.h>

extern void gate_far_12a5_3afa(uint16_t seed);
extern uint16_t gate_far_12a5_3b0c(void);

static uint16_t data_words[2];

uint16_t gate_ds_word(uint16_t offset)
{
    return (offset == 0x358e) ? data_words[0] :
           (offset == 0x3590) ? data_words[1] : 0;
}

void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    if (offset == 0x358e)
        data_words[0] = value;
    else if (offset == 0x3590)
        data_words[1] = value;
}

int main(void)
{
    uint32_t expected = 1;
    unsigned int i;

    gate_far_12a5_3afa(1);
    for (i = 0; i < 32; ++i) {
        expected = expected * 0x343fdu + 0x269ec3u;
        if (gate_far_12a5_3b0c() != (uint16_t)((expected >> 16) & 0x7fffu)) {
            puts("Intro C runtime: FAIL");
            return 1;
        }
    }
    puts("Intro C runtime: PASS (32 DOS rand steps)");
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_captions.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    static const uint32_t expected_ms[] = {
        19500, 11850, 10700, 14050, 16000, 10150,
        15100, 13900, 10750, 16900, 14350, 14550,
        10600, 11100, 19950, 13600, 17650, 20650
    };
    GateSourceCCaptions *player;
    uint32_t total_pixels = 0;
    unsigned page;
    if (argc != 3) {
        fputs("usage: launcher-intro-source-c-captions-tests guest-22s.bin DOS-DIR\n",
              stderr);
        return 2;
    }
    player = gate_source_c_captions_open(argv[1], argv[2]);
    assert(player);
    assert(gate_source_c_captions_count() == 18);
    assert(!gate_source_c_captions_render(player, 18));
    for (page = 0; page < gate_source_c_captions_count(); ++page) {
        const uint8_t *pixels;
        unsigned i;
        unsigned ink = 0;
        assert(gate_source_c_captions_render(player, page));
        pixels = gate_source_c_captions_pixels(player);
        assert(pixels);
        for (i = 0; i < 320u * 200u; ++i)
            ink += pixels[i] != 0;
        assert(ink > 1000);
        assert(gate_source_c_captions_duration(player) == expected_ms[page]);
        total_pixels += ink;
    }
    assert(total_pixels == 323066);
    gate_source_c_captions_close(player);
    printf("source-C caption runtime: 18 original panels, %u occupied pixels\n",
           total_pixels);
    return 0;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "intro_source_c_captions.h"

#include <stdint.h>
#include <stdio.h>

/* Headless source-C presentation diagnostic, independent of SDL3. The
 * directory must already exist; each page emits a diagnostic grayscale PGM,
 * not a claim of matching the original VGA DAC colors. */
int main(int argc, char **argv)
{
    GateSourceCCaptions *player;
    unsigned page;
    if (argc != 4) {
        fputs("usage: gateway-intro-source-c-dump guest-22s.bin DOS-DIR OUTPUT-DIR\n",
              stderr);
        return 2;
    }
    player = gate_source_c_captions_open(argv[1], argv[2]);
    if (!player) {
        fputs("cannot open source-C caption state\n", stderr);
        return 1;
    }
    for (page = 0; page < gate_source_c_captions_count(); ++page) {
        char path[4096];
        const uint8_t *pixels;
        FILE *output;
        unsigned i;
        if (!gate_source_c_captions_render(player, page) ||
            snprintf(path, sizeof path, "%s/source-c-%02u.pgm", argv[3], page) >=
            (int)sizeof path) {
            gate_source_c_captions_close(player);
            return 1;
        }
        output = fopen(path, "wb");
        if (!output) {
            perror(path);
            gate_source_c_captions_close(player);
            return 1;
        }
        if (fprintf(output, "P5\n320 200\n255\n") < 0) {
            fclose(output);
            gate_source_c_captions_close(player);
            return 1;
        }
        pixels = gate_source_c_captions_pixels(player);
        for (i = 0; i < 320u * 200u; ++i) {
            uint8_t index = pixels[i];
            uint8_t gray = index == 0 ? 0 : index == 1 ? 188 :
                           index == 2 ? 110 : index == 255 ? 245 : index;
            if (fwrite(&gray, 1, 1, output) != 1) {
                fclose(output);
                gate_source_c_captions_close(player);
                return 1;
            }
        }
        if (fclose(output) != 0) {
            gate_source_c_captions_close(player);
            return 1;
        }
        printf("%s (%u ms reconstructed caption wait)\n", path,
               (unsigned)gate_source_c_captions_duration(player));
    }
    gate_source_c_captions_close(player);
    return 0;
}

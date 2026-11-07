/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATEWAY_INTRO_SOURCE_C_CAPTIONS_H
#define GATEWAY_INTRO_SOURCE_C_CAPTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GateSourceCCaptions GateSourceCCaptions;

/* Host-side bridge to the recovered original EXE:117e/103a caption C.
 * Requires a guest RAM capture for original EXE text and relocated tables.
 * This is an isolated caption player, NOT the full EXE:1302 intro. */
GateSourceCCaptions *gate_source_c_captions_open(const char *memory_image,
                                                  const char *dos_directory);
void gate_source_c_captions_close(GateSourceCCaptions *player);
unsigned gate_source_c_captions_count(void);
int gate_source_c_captions_render(GateSourceCCaptions *player, unsigned page);
const uint8_t *gate_source_c_captions_pixels(const GateSourceCCaptions *player);
uint32_t gate_source_c_captions_duration(const GateSourceCCaptions *player);

#ifdef __cplusplus
}
#endif

#endif

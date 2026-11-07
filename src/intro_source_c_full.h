/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#ifndef GATE_INTRO_SOURCE_C_FULL_H
#define GATE_INTRO_SOURCE_C_FULL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional realtime services for the recovered original GATE.EXE:1302.
 * All callbacks occur on the source-C execution thread. The frame sink
 * copies the supplied framebuffer and VGA DAC before returning; neither
 * pointer is valid after the function has exited. A NULL hooks argument
 * selects the deterministic, no-input headless clock. */
typedef struct GateSourceCFullHooks {
    void *userdata;
    uint32_t (*clock_ms)(void *userdata);
    void (*delay_ms)(void *userdata, uint32_t milliseconds);
    uint16_t (*key_pending)(void *userdata);
    uint16_t (*read_key)(void *userdata);
    void (*frame_ready)(void *userdata, const uint8_t *pixels,
                        const uint8_t dac[256][3]);
    /* Called only if the recovered RTL:c3bd predicates enable music at
     * the original EXE:134c common-init call. Audio synthesis is native. */
    void (*music_start)(void *userdata);
    /* Optional original opening: mode 12h's 640x480/16-color logo before
     * GATE.EXE:1302 switches to mode 13h. Legacy 22s replay uses mode 13h. */
    uint8_t original_video_start;
    void (*frame_ready_vga16)(void *userdata,
                              const uint8_t planes[4][0x10000],
                              const uint8_t dac[256][3],
                              const uint8_t attributes[32]);
} GateSourceCFullHooks;

/* Pass a 1 MiB original DOS guest RAM snapshot, or "--from-original" to
 * build the verified default intro startup entirely from the DOS assets.
 * 0 = original entry returned normally, nonzero = missing/invalid input.
 * Only one intro can be active at a time: the original RTL host globals are
 * process-global, exactly like the existing headless source-C host. */
int gate_source_c_full_run(const char *snapshot, const char *dos_directory,
                           const GateSourceCFullHooks *hooks);

#ifdef __cplusplus
}
#endif

#endif

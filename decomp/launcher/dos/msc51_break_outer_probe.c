/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* End-to-end real-mode process test for the source-C outer Ctrl-Break
 * sequence. The first five operations are linked to their recovered DOS
 * implementations; the final one enters the *MS C* exit() runtime.
 * Expect DOS process exit code 1: the test batch checks it explicitly. */
#include "GATEMEM.H"
#include "GATEDOS.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gate_linked_handle_control_key(void);

/* M13.OBJ contains the full font selector as well as its original cache
 * destructor. These two metrics are reachable only through selection,
 * which this test never enters; reject them explicitly (the full DOS
 * launcher instead links their real source from M08.OBJ). */
uint16_t gate_rtl_font_line_advance(void)
{
    puts("Ctrl-Break outer probe: unexpected font line metrics");
    exit(3);
    return 0u;
}

uint16_t gate_rtl_font_character_advance(uint16_t character)
{
    (void)character;
    puts("Ctrl-Break outer probe: unexpected font glyph metrics");
    exit(3);
    return 0u;
}

static void set_saved_vector(uint16_t number, uint16_t offset)
{
    void (interrupt far *handler)(void) = _dos_getvect(number);
    gate_ds_set_word(offset, (uint16_t)FP_OFF(handler));
    gate_ds_set_word((uint16_t)(offset + 2u), (uint16_t)FP_SEG(handler));
}

int main(void)
{
    unsigned segment = 0u;
    GateDosFileResult resource;
    FILE *existing;
    char filename[13];
    unsigned i;

    strcpy(filename, "BRKFILE.TMP");
    existing = fopen(filename, "rb");
    if (existing != NULL) {
        fclose(existing);
        puts("Ctrl-Break outer probe: BRKFILE.TMP already exists");
        return 3;
    }
    if (_dos_allocmem(0x0620u, &segment) != 0u) {
        puts("Ctrl-Break outer probe: DOS allocation failed");
        return 3;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_ds_set_word(GATE_DS_CONTROL_STREAM_OPEN, 0xffffu);
    gate_ds_set_word(GATE_DS_MUSIC_STARTUP_FLAGS, 0u);
    gate_ds_set_word(GATE_DS_VFX_STATE_FLAGS, 0u);
    for (i = 0; i < GATE_FONT_SLOT_COUNT; ++i) {
        gate_far_set_word((uint16_t)segment, (uint16_t)(
            GATE_DS_FONT_CACHE_BASE + GATE_FONT_SLOT_BYTES * i +
            GATE_FONT_SLOT_REFCOUNT), 0u);
    }
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, (uint16_t)segment);
    gate_ds_set_word(GATE_DS_RESOURCE_HANDLE_SEGMENT, (uint16_t)segment);
    gate_far_set_word((uint16_t)segment, GATE_DS_GRAPHICS_MODE, 0u);
    set_saved_vector(0x1cu, GATE_DS_BREAK_INT1C_OFFSET);
    set_saved_vector(0x1bu, GATE_DS_BREAK_INT1B_OFFSET);
    set_saved_vector(0x23u, GATE_DS_BREAK_INT23_OFFSET);
    resource = gate_dos_file_open(gate_stack_pointer(filename), 2u, 1u);
    if (resource.carry != 0u || resource.ax < 3u) {
        puts("Ctrl-Break outer probe: resource file creation failed");
        return 3;
    }
    gate_far_set_word((uint16_t)segment, 0x005au, resource.ax);

    /* A successful control handler MUST terminate this DOS process with
     * code 1. The MS C 5.1 exit() path itself replaces RTL:621e; neither
     * its near-call atexit cleanup nor original callbacks are recreated. */
    puts("Ctrl-Break outer six-call DOS probe: entered");
    gate_linked_handle_control_key();
    puts("Ctrl-Break outer probe: unexpected return");
    return 3;
}

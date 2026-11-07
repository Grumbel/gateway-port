/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

#include "GATEMEM.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>

uint16_t gate_linked_keyboard_pending(void);
uint16_t gate_linked_read_keyboard_key(void);
uint16_t gate_linked_intro_mouse_flags(void);
void gate_linked_reset_intro_mouse(void);

static int check(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        printf("Input probe: %s got %04x expected %04x\n", label, actual, expected);
        return 0;
    }
    return 1;
}

int main(void)
{
    unsigned int segment = 0;
    int valid = 1;
    uint16_t mouse;

    if (_dos_allocmem(0x1000u, &segment) != 0) {
        puts("Input probe: DOS segment allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_ds_set_word(GATE_DS_INPUT_BUFFERED_KEY, 0x005au);
    gate_ds_set_word(GATE_DS_INPUT_PENDING_COUNT, 0u);
    valid &= check(gate_linked_keyboard_pending(), 1u, "unget key pending");
    valid &= check(gate_linked_read_keyboard_key(), 0x005au, "unget key consumed");
    valid &= check(gate_ds_word(GATE_DS_INPUT_BUFFERED_KEY), 0xffffu,
                   "unget slot reset");
    /* No input is injected into the global BIOS queue: only check its
     * status and mouse poll without consuming another program's keys. */
    valid &= check(gate_linked_keyboard_pending() <= 1u, 1u, "BIOS status binary");
    mouse = gate_linked_intro_mouse_flags();
    printf("Input probe: real mouse button flags %04x\n", mouse);
    if (_dos_freemem(segment) != 0) {
        puts("Input probe: DOS segment free failed");
        return 1;
    }
    puts(valid ? "Input probe: PASS" : "Input probe: FAIL");
    return valid ? 0 : 1;
}

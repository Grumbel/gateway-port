/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Genuine DOS INT 21h IVT, file-close and BIOS-mode exercise of the
 * independently recovered fifth Ctrl-Break callee RTL:11e5f..11ec5.
 * Never install a fabricated interrupt target or close an inherited DOS
 * handle: saved entries are read from the live IVT and every temporary
 * handle is created by this probe. */
#include "GATEMEM.H"
#include "GATEDOS.H"
#include "RTLLAY.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>

void gate_rtl_control_teardown(uint16_t reset_display);

static GateFarPtr vector_at(uint16_t index)
{
    GateFarPtr result;
    void (interrupt far *handler)(void) = _dos_getvect(index);
    result.segment = (uint16_t)FP_SEG(handler);
    result.offset = (uint16_t)FP_OFF(handler);
    return result;
}

static int check_vector(uint16_t number, GateFarPtr expected)
{
    GateFarPtr actual;
    actual = vector_at(number);
    if (actual.segment != expected.segment || actual.offset != expected.offset) {
        printf("Ctrl-Break: INT %02x restored %04x:%04x, wanted %04x:%04x\n",
               number, actual.segment, actual.offset,
               expected.segment, expected.offset);
        return 0;
    }
    return 1;
}

static void save_vector(uint16_t offset, GateFarPtr value)
{
    gate_ds_set_word(offset, value.offset);
    gate_ds_set_word((uint16_t)(offset + 2u), value.segment);
}

static int check_bios_mode(uint16_t wanted)
{
    union REGS in, out;
    memset(&in, 0, sizeof in);
    in.h.ah = 0x0fu;
    int86(0x10, &in, &out);
    if (out.h.al != wanted) {
        printf("Ctrl-Break: BIOS video mode %02x, wanted %02x\n",
               out.h.al, wanted);
        return 0;
    }
    return 1;
}

static int exercise_cleanup(uint16_t mode, uint16_t reset)
{
    char filename[13];
    FILE *existing;
    GateFarPtr onec, oneb, twentythree;
    GateDosFileResult opened, reopened;
    union REGS in, out;
    int valid = 1;

    strcpy(filename, "CTRLBR.TMP");
    existing = fopen(filename, "rb");
    if (existing != NULL) {
        fclose(existing);
        puts("Ctrl-Break: CTRLBR.TMP already exists; refusing overwrite");
        return 0;
    }
    onec = vector_at(0x1cu);
    oneb = vector_at(0x1bu);
    twentythree = vector_at(0x23u);
    if ((oneb.segment == 0u && oneb.offset == 0u) ||
        (twentythree.segment == 0u && twentythree.offset == 0u) ||
        (mode != 0u && onec.segment == 0u && onec.offset == 0u)) {
        puts("Ctrl-Break: required current DOS vector is null");
        return 0;
    }
    opened = gate_dos_file_open(gate_stack_pointer(filename), 2u, 1u);
    if (opened.carry != 0u || opened.ax < 3u) {
        puts("Ctrl-Break: disposable resource file create failed");
        if (opened.carry == 0u)
            (void)gate_dos_file_close(opened.ax);
        return 0;
    }
    gate_far_set_word(gate_ds_segment(), GATE_DS_GRAPHICS_MODE, mode);
    gate_far_set_word(gate_ds_segment(), 0x005au, opened.ax);
    save_vector(GATE_DS_BREAK_INT1C_OFFSET, onec);
    save_vector(GATE_DS_BREAK_INT1B_OFFSET, oneb);
    save_vector(GATE_DS_BREAK_INT23_OFFSET, twentythree);
    if (reset != 0u) {
        memset(&in, 0, sizeof in);
        in.h.ah = 0u;
        in.h.al = 0x13u;
        int86(0x10, &in, &out);
        valid &= check_bios_mode(0x13u);
    }
    gate_rtl_control_teardown(reset);
    valid &= check_vector(0x1cu, onec);
    valid &= check_vector(0x1bu, oneb);
    valid &= check_vector(0x23u, twentythree);
    if (reset != 0u)
        valid &= check_bios_mode(3u);

    /* The callee has already closed the file; a second close must fail. */
    reopened = gate_dos_file_close(opened.ax);
    if (reopened.carry == 0u) {
        puts("Ctrl-Break: resource file remained open after cleanup");
        valid = 0;
    }
    if (remove(filename) != 0) {
        puts("Ctrl-Break: disposable resource file removal failed");
        valid = 0;
    }
    return valid;
}

int main(void)
{
    unsigned segment = 0u;
    int valid;
    if (_dos_allocmem(0x0620u, &segment) != 0u) {
        puts("Ctrl-Break: private DOS data allocation failed");
        return 1;
    }
    gate_dos_bind_ds((uint16_t)segment);
    gate_ds_set_word(GATE_DS_IMAGE_MODE_SEGMENT, (uint16_t)segment);
    gate_ds_set_word(GATE_DS_RESOURCE_HANDLE_SEGMENT, (uint16_t)segment);
    valid = exercise_cleanup(0u, 0u);
    if (valid)
        valid &= exercise_cleanup(2u, 1u);
    (void)_dos_freemem(segment);
    if (valid)
        puts("Original Ctrl-Break fifth cleanup DOS probe: PASS");
    else
        puts("Original Ctrl-Break fifth cleanup DOS probe: FAIL");
    return valid ? 0 : 1;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Optional real-file test of the exact startup data copy. The original
 * GATE.EXE must be mounted read-only as E:\GATE.EXE by the caller. No
 * game bytes are shipped with this source or the generated DOS workspace.
 * This is a DOSBox probe, not the reconstructed launcher main(). */
#include "GATEDOS.H"
#include <dos.h>
#include <stdio.h>

#define GATE_REAL_EXE_BYTES 22968ul
#define GATE_REAL_COPY_SOURCE 0x41e6ul
#define GATE_REAL_COPY_COUNT 0x1758u
#define GATE_REAL_DEST 0x3592u
#define GATE_REAL_PARAGRAPHS 0x100au

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs);

int main(void)
{
    const char filename[] = "E:\\GATE.EXE";
    uint8_t buffer[128];
    unsigned int allocated = 0u;
    uint16_t handle = 0u;
    uint16_t data_segment = 0u;
    uint16_t i, j, count;
    int good = 1;
    int have_allocation = 0;
    int have_handle = 0;
    GateDosFileResult result;

    /* The DOS allocation must cover both overlapping 64KiB windows,
     * including the extra ten paragraphs past the lower segment. */
    if (_dos_allocmem(GATE_REAL_PARAGRAPHS, &allocated) != 0u)
        good = 0;
    else {
        have_allocation = 1;
        data_segment = (uint16_t)(allocated + 10u);
        if (gate_dos_bind_intro_aliases((uint16_t)allocated,
                                        GATE_REAL_PARAGRAPHS) != 0)
            good = 0;
    }

    if (good) {
        result = gate_dos_file_open(gate_stack_pointer((void *)filename), 0u, 0u);
        if (result.carry)
            good = 0;
        else {
            have_handle = 1;
            handle = result.ax;
        }
    }

    /* Check the DOS-visible file length independently of the data-copy
     * function. The host also verifies the full pinned SHA-256 before
     * launching this guest; this is not a substitute for that check. */
    if (good) {
        result = gate_dos_file_seek(handle, 2u, 0ul);
        if (result.carry ||
            ((uint32_t)result.dx << 16 | result.ax) != GATE_REAL_EXE_BYTES)
            good = 0;
    }

    if (good) {
        gate_ds_set_word((uint16_t)(GATE_REAL_DEST - 2u), 0x7e35u);
        gate_ds_set_word((uint16_t)(GATE_REAL_DEST + GATE_REAL_COPY_COUNT), 0x4912u);
        if (gate_dos_load_intro_static_data(handle, (uint16_t)allocated,
                                            GATE_REAL_PARAGRAPHS) != 0)
            good = 0;
    }

    /* Compare the copied 5976-byte image against an independent second
     * read of the *real* original file, in bounded 128-byte DOS reads. */
    if (good) {
        result = gate_dos_file_seek(handle, 0u, GATE_REAL_COPY_SOURCE);
        if (result.carry || result.dx != 0u || result.ax != 0x41e6u)
            good = 0;
    }
    for (i = 0u; good && i < GATE_REAL_COPY_COUNT; i = (uint16_t)(i + count)) {
        count = (uint16_t)(GATE_REAL_COPY_COUNT - i);
        if (count > sizeof buffer)
            count = sizeof buffer;
        result = gate_dos_file_read(handle, gate_stack_pointer(buffer), count);
        if (result.carry || result.ax != count) {
            good = 0;
            break;
        }
        for (j = 0u; j < count; ++j) {
            if (buffer[j] != gate_far_byte(data_segment,
                                             (uint16_t)(GATE_REAL_DEST + i + j))) {
                good = 0;
                break;
            }
        }
    }

    if (good && (gate_ds_word((uint16_t)(GATE_REAL_DEST - 2u)) != 0x7e35u ||
                 gate_ds_word((uint16_t)(GATE_REAL_DEST + GATE_REAL_COPY_COUNT)) != 0x4912u ||
                 gate_ds_word(0x4d82u) != (uint16_t)allocated ||
                 gate_ds_word(0x4d84u) != data_segment))
        good = 0;

    if (have_handle) {
        result = gate_dos_file_close(handle);
        if (result.carry)
            good = 0;
    }
    if (have_allocation) {
        gate_dos_bind_ds(0u);
        if (_dos_freemem(allocated) != 0u)
            good = 0;
    }
    puts(good ? "Original EXE data probe: PASS (5976 bytes, matching disk image)"
              : "Original EXE data probe: FAIL");
    return good ? 0 : 1;
}

/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Optional original-file DOS probe: execute the *reconstructed C* prologue
 * of GATE.EXE:1309..134b, not any unknown relocated resident code.
 * The five selector/target addresses are independently pinned by original
 * intro-entry RAM snapshots. No intro graphics/IRQ/RTLink callbacks run.
 * The BOOT image callback addresses here are deliberately nonexecutable. */
#include "BOOT.H"
#include "BOOTCB.H"
#include <dos.h>
#include <stdio.h>

#define GATE_ENTRY_MEMORY_BIAS 0x12b2u

typedef struct GateStartupWord {
    uint16_t selector;
    uint16_t target;
    uint16_t expected;
} GateStartupWord;

static const GateStartupWord startup_words[] = {
    {0x4e46u, 0x0660u, 0x0401u},
    {0x4e48u, 0x065cu, 0x007fu},
    {0x4e4au, 0x065eu, 0x0001u},
    {0x4e4cu, 0x064eu, 0x0000u}
};

static char original_exe_name[] = "E:\\GATE.EXE";
static char original_rtl_name[] = "E:\\GATE.RTL";
static char unused_exe_callback[32];

/* The real reconstructed source object (Mxx.OBJ), not a test substitute,
 * supplies this function. Its original call requests 1000h DOS paragraphs. */
void gate_exe_prepare_intro_globals(void);

int main(void)
{
    GateDosBootConfig config;
    GateDosBootImage image;
    uint16_t data_segment;
    uint16_t fifth_segment = 0u;
    uint16_t workspace = 0u;
    uint16_t i;
    int status;
    int good = 1;

    image.lower_segment = image.resident_segment = 0u;
    config.exe_path = gate_stack_pointer(original_exe_name);
    config.rtl_path = gate_stack_pointer(original_rtl_name);
    config.exe_callback = gate_stack_pointer(unused_exe_callback);
    status = gate_probe_load_return_context(config.rtl_path,
                                            &config.return_callback);
    if (status != 0) {
        printf("DOS intro C entry prologue: FAIL return context=%d\n", status);
        return 1;
    }
    config.external_a_segment = 0x3333u;
    config.external_b_segment = 0x4444u;
    status = gate_dos_boot_prepare(&config, &image);
    if (status != 0) {
        printf("DOS intro C entry prologue: FAIL image status=%d\n", status);
        return 1;
    }

    data_segment = (uint16_t)(image.lower_segment + 0x000au);
    for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i) {
        if (gate_ds_word(startup_words[i].selector) != data_segment)
            good = 0;
    }
    if (gate_ds_word(0x4e4eu) != data_segment)
        good = 0;

    if (good) {
        /* A copied original value could accidentally satisfy the test.
         * Poison every destination only after both full-image CRCs pass. */
        for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i)
            gate_far_set_word(data_segment, startup_words[i].target, 0xdeadu);
        gate_far_set_word(data_segment, 0x0658u, 0xdeadu);
        gate_exe_prepare_intro_globals();
        for (i = 0u; i < sizeof startup_words / sizeof startup_words[0]; ++i) {
            if (gate_far_word(data_segment, startup_words[i].target) !=
                startup_words[i].expected)
                good = 0;
        }
        fifth_segment = gate_ds_word(0x4e4eu);
        workspace = (uint16_t)(gate_far_word(fifth_segment, 0x0658u) +
                               GATE_ENTRY_MEMORY_BIAS);
        if (workspace == 0u || workspace == image.lower_segment ||
            workspace == image.resident_segment)
            good = 0;
        /* Do not free a guessed segment if the prologue failed. The
         * original retains the allocation; DOS owns and reclaims it when
         * this one-shot probe exits. */
    }
    gate_dos_boot_release(&image);
    if (image.lower_segment != 0u || image.resident_segment != 0u)
        good = 0;
    if (!good) {
        puts("DOS intro C entry prologue: FAIL selectors, values or allocation");
        return 1;
    }
    puts("DOS intro C entry prologue: PASS (five recovered writes, original data/CRC, workspace observed; no intro code entered)");
    return 0;
}

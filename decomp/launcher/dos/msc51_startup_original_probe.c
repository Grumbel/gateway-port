/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Optional real-file test of both startup data image sources. Original
 * GATE.EXE and GATE.RTL must be mounted read-only on E: by the caller. No
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
#define GATE_REAL_RTL_BYTES 109193ul
#define GATE_RTL_SOURCE 0x0029ul
#define GATE_RTL_COUNT 0x3592u
#define GATE_RTL_RESIDENT_SOURCE 0x56deul
#define GATE_RTL_RESIDENT_COUNT 0x3d00u

int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs);
int gate_dos_load_intro_rtl_data(uint16_t handle, uint16_t lower_segment,
                                 uint16_t paragraphs);
int gate_dos_fix_intro_early_bytes(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_load_intro_resident_bank(uint16_t rtl_handle,
                                      uint16_t resident_segment,
                                      uint16_t paragraphs);
int gate_dos_prepare_intro_resident_bank(uint16_t rtl_handle,
                                         uint16_t resident_segment,
                                         uint16_t paragraphs,
                                         uint16_t data_segment,
                                         GateFarPtr exe_callback);
int gate_dos_relocate_intro_resident_self_calls(uint16_t resident_segment,
                                                uint16_t paragraphs);
int gate_dos_relocate_intro_resident_ds_loads(uint16_t resident_segment,
                                               uint16_t paragraphs,
                                               uint16_t data_segment);
int gate_dos_relocate_intro_exe_call(uint16_t resident_segment,
                                     uint16_t paragraphs, GateFarPtr target);
int gate_dos_relocate_intro_resident_direct_segments(uint16_t resident_segment,
                                                      uint16_t paragraphs,
                                                      uint16_t data_segment);
int gate_dos_clear_intro_rtl_prefix(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_startup_words(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_segs(uint16_t lower_segment, uint16_t paragraphs,
                                uint16_t resident_segment);
int gate_dos_fix_intro_rtl_offs(uint16_t lower_segment, uint16_t paragraphs);
int gate_dos_fix_intro_rtl_external(uint16_t lower_segment, uint16_t paragraphs,
                                    uint16_t first_segment, uint16_t second_segment);

typedef struct ExpectedOffset {
    uint16_t site, source, expected;
} ExpectedOffset;
static const ExpectedOffset expected_offsets[] = {
    {0x0fb6u, 0x0000u, 0x0f92u}, {0x0fbau, 0x0004u, 0x0f96u},
    {0x0fbeu, 0x0008u, 0x0f9au}, {0x0fc2u, 0x000cu, 0x0f9eu},
    {0x0fc6u, 0x0010u, 0x0fa2u}, {0x0fcau, 0x0014u, 0x0fa6u},
    {0x1538u, 0x001au, 0x14fau}, {0x153cu, 0x0020u, 0x1500u},
    {0x1540u, 0x002au, 0x150au}, {0x1544u, 0x002fu, 0x150fu},
    {0x1548u, 0x0039u, 0x1519u}, {0x154cu, 0x003fu, 0x151fu},
    {0x1550u, 0x0049u, 0x1529u}, {0x1554u, 0x004eu, 0x152eu},
    {0x33dcu, 0x0000u, 0x0c02u}, {0x33e0u, 0x0000u, 0x0c02u},
    {0x33e4u, 0x0000u, 0x0c02u}, {0x33e8u, 0x0000u, 0x0c02u},
    {0x33ecu, 0x0000u, 0x0c02u},
    {0x3244u, 0x0000u, 0x2c44u}, {0x324au, 0x0000u, 0x2c44u},
    {0x33acu, 0x06e4u, 0x3328u}, {0x353eu, 0x0000u, 0x3530u},
    {0x3542u, 0x0004u, 0x3534u}
};
static const ExpectedOffset external_a[] = {
    {0x2814u, 0u, 0x000au}, {0x2852u, 0u, 0x0032u},
    {0x2890u, 0u, 0x0032u}, {0x28ceu, 0u, 0x0053u},
    {0x290cu, 0u, 0x0065u}, {0x294au, 0u, 0x0065u},
    {0x2988u, 0u, 0x0065u}, {0x29c6u, 0u, 0x0065u}
};
static const ExpectedOffset external_b[] = {
    {0x2a93u, 0u, 0x006bu}, {0x2aa7u, 0u, 0x00d6u},
    {0x2abbu, 0u, 0x00d6u}, {0x2acfu, 0u, 0x0133u},
    {0x2ae3u, 0u, 0x0189u}, {0x2af7u, 0u, 0x0189u},
    {0x2b0bu, 0u, 0x0189u}, {0x2b1fu, 0u, 0x0189u},
    {0x2b33u, 0u, 0x01e3u}, {0x2b47u, 0u, 0x0272u},
    {0x2b5bu, 0u, 0x0440u}
};

static const uint16_t observed_ds_sites[] = {
    0x0fb8u, 0x0fbcu, 0x0fc0u, 0x0fc4u, 0x0fc8u, 0x0fccu,
    0x153au, 0x153eu, 0x1542u, 0x1546u, 0x154au, 0x154eu,
    0x1552u, 0x1556u, 0x26d8u, 0x2b78u, 0x2c10u, 0x3246u,
    0x324cu, 0x33aeu, 0x3540u, 0x3544u
};
static const uint16_t observed_cs_sites[] = {
    0x33deu, 0x33e2u, 0x33e6u, 0x33eau, 0x33eeu
};

/* Avoid C2073 on Microsoft C 5.1: initialized arrays need static storage. */
static char original_exe_name[] = "E:\\GATE.EXE";
static char original_rtl_name[] = "E:\\GATE.RTL";
static const uint16_t observed_early_words[8] = {
    0x00e0u, 0x00efu, 0x020bu, 0x020bu,
    0x0203u, 0x010bu, 0x00feu, 0x011du
};
static const uint16_t observed_paragraph_sites[13] = {
    0x113du, 0x1170u, 0x1273u, 0x12cbu, 0x131du,
    0x398du, 0x3998u, 0x39c3u, 0x39cfu,
    0x3a06u, 0x3a11u, 0x3a64u, 0x3a6fu
};

int main(void)
{
    uint8_t buffer[128];
    unsigned int allocated = 0u;
    unsigned int resident_allocated = 0u;
    uint16_t handle = 0u;
    uint16_t data_segment = 0u;
    uint16_t i, j, count;
    uint8_t exe_first_byte = 0u;
    GateFarPtr exe_target;
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
        result = gate_dos_file_open(gate_stack_pointer(original_exe_name), 0u, 0u);
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

    if (good)
        exe_first_byte = gate_far_byte(data_segment, GATE_REAL_DEST);

    /* Close the EXE before opening the RTL to check the two independent
     * source files and to exercise ownership of the caller's DOS handles. */
    if (have_handle) {
        result = gate_dos_file_close(handle);
        have_handle = 0;
        if (result.carry)
            good = 0;
    }

    if (good) {
        result = gate_dos_file_open(gate_stack_pointer(original_rtl_name), 0u, 0u);
        if (result.carry)
            good = 0;
        else {
            have_handle = 1;
            handle = result.ax;
        }
    }

    if (good) {
        result = gate_dos_file_seek(handle, 2u, 0ul);
        if (result.carry ||
            ((uint32_t)result.dx << 16 | result.ax) != GATE_REAL_RTL_BYTES)
            good = 0;
    }

    if (good && gate_dos_load_intro_rtl_data(handle, (uint16_t)allocated,
                                             GATE_REAL_PARAGRAPHS) != 0)
        good = 0;

    /* The 13714-byte source image ends exactly where the separately
     * verified EXE static data begins. No relocation fixups are applied. */
    if (good) {
        result = gate_dos_file_seek(handle, 0u, GATE_RTL_SOURCE);
        if (result.carry || result.dx != 0u || result.ax != GATE_RTL_SOURCE)
            good = 0;
    }
    for (i = 0u; good && i < GATE_RTL_COUNT; i = (uint16_t)(i + count)) {
        count = (uint16_t)(GATE_RTL_COUNT - i);
        if (count > sizeof buffer)
            count = sizeof buffer;
        result = gate_dos_file_read(handle, gate_stack_pointer(buffer), count);
        if (result.carry || result.ax != count) {
            good = 0;
            break;
        }
        for (j = 0u; j < count; ++j) {
            if (buffer[j] != gate_far_byte(data_segment, (uint16_t)(i + j))) {
                good = 0;
                break;
            }
        }
    }
    if (good && (gate_far_byte(data_segment, GATE_REAL_DEST) != exe_first_byte ||
                 gate_ds_word(0x4d82u) != (uint16_t)allocated ||
                 gate_ds_word(0x4d84u) != data_segment))
        good = 0;
    /* Source-image comparison only: this raw code has NOT been relocated
     * and must not be called. Verify all bytes via a second DOS file read. */
    if (good && _dos_allocmem(0x03d0u, &resident_allocated) != 0u)
        good = 0;
    if (good && gate_dos_load_intro_resident_bank(handle,
                      (uint16_t)resident_allocated, 0x03d0u) != 0)
        good = 0;
    if (good) {
        result = gate_dos_file_seek(handle, 0u, GATE_RTL_RESIDENT_SOURCE);
        if (result.carry || result.dx != 0u || result.ax != 0x56deu)
            good = 0;
    }
    for (i = 0u; good && i < GATE_RTL_RESIDENT_COUNT; i = (uint16_t)(i + count)) {
        count = (uint16_t)(GATE_RTL_RESIDENT_COUNT - i);
        if (count > sizeof buffer)
            count = sizeof buffer;
        result = gate_dos_file_read(handle, gate_stack_pointer(buffer), count);
        if (result.carry || result.ax != count) {
            good = 0;
            break;
        }
        for (j = 0u; j < count; ++j)
            if (buffer[j] != gate_far_byte((uint16_t)resident_allocated,
                                          (uint16_t)(i + j))) {
                good = 0;
                break;
            }
    }
    /* Exercise the composed bank bootstrap against the original file.
     * The preliminary load above was compared bytewise before relocation. */
    exe_target.segment = 0x1234u;
    exe_target.offset = 0x0030u;
    if (good && gate_dos_prepare_intro_resident_bank(handle,
                    (uint16_t)resident_allocated, 0x03d0u,
                    data_segment, exe_target) != 0)
        good = 0;
    /* Include an out-of-copy-range self-call target to verify that the
     * original offset is retained even before later banks are loaded. */
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x09d7u) != 0x0bdeu ||
                 gate_far_word((uint16_t)resident_allocated, 0x09d9u) !=
                     (uint16_t)resident_allocated ||
                 gate_far_word((uint16_t)resident_allocated, 0x1307u) != 0x3d60u ||
                 gate_far_word((uint16_t)resident_allocated, 0x1309u) !=
                     (uint16_t)resident_allocated ||
                 gate_far_word((uint16_t)resident_allocated, 0x3b1du) != 0x3c5cu))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x020eu) != data_segment ||
                 gate_far_word((uint16_t)resident_allocated, 0x0a59u) != data_segment))
        good = 0;
    /* The synthetic EXE callback is not an executable target. The real
     * recompiled EXE's equivalent routine is not yet identified. */
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x0a4eu) != 0x0030u ||
                 gate_far_word((uint16_t)resident_allocated, 0x0a50u) != 0x1234u))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x14c8u) != data_segment ||
                 gate_far_word((uint16_t)resident_allocated, 0x1502u) != data_segment ||
                 gate_far_word((uint16_t)resident_allocated, 0x09b9u) != data_segment ||
                 gate_far_word((uint16_t)resident_allocated, 0x019cu) !=
                     (uint16_t)resident_allocated))
        good = 0;
    for (i = 0u; good && i < 13u; ++i)
        if (gate_far_word((uint16_t)resident_allocated,
                          observed_paragraph_sites[i]) != 0x1000u)
            good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x0b01u) != 0x2bf0u ||
                 gate_far_word((uint16_t)resident_allocated, 0x25f7u) != 0x2bf0u ||
                 gate_far_word((uint16_t)resident_allocated, 0x3344u) != 0x2bf0u))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x0f93u) != 0x3244u ||
                 gate_far_word((uint16_t)resident_allocated, 0x3328u) != 0x3244u ||
                 gate_far_word((uint16_t)resident_allocated, 0x111eu) != 0x3334u ||
                 gate_far_word((uint16_t)resident_allocated, 0x31bbu) != 0x3334u))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x1874u) != 0x630eu ||
                 gate_far_word((uint16_t)resident_allocated, 0x191au) != 0x62dau ||
                 gate_far_word((uint16_t)resident_allocated, 0x223au) != 0x646eu))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x1a2eu) != 0x1b66u ||
                 gate_far_word((uint16_t)resident_allocated, 0x1b66u) != 0x1b36u ||
                 gate_far_word((uint16_t)resident_allocated, 0x1b90u) != 0x1b26u))
        good = 0;
    if (good && (gate_far_word((uint16_t)resident_allocated, 0x002du) != 0x05abu ||
                 gate_far_word((uint16_t)resident_allocated, 0x0116u) != 0x05aau ||
                 gate_far_word((uint16_t)resident_allocated, 0x09a2u) != 0x05d3u))
        good = 0;
    if (good && gate_dos_clear_intro_rtl_prefix((uint16_t)allocated,
                                               GATE_REAL_PARAGRAPHS) != 0)
        good = 0;
    for (i = 2u; good && i < 66u; ++i)
        if (gate_far_byte(data_segment, i) != 0u)
            good = 0;
    if (good && gate_dos_fix_intro_rtl_startup_words((uint16_t)allocated,
                                                     GATE_REAL_PARAGRAPHS) != 0)
        good = 0;
    if (good && (gate_ds_word(0x0525u) != 0x0527u ||
                 gate_ds_word(0x26d6u) != 0x16d4u ||
                 gate_ds_word(0x2b74u) != 0x0b57u ||
                 gate_ds_word(0x2bc6u) != 0x2b76u ||
                 gate_ds_word(0x2c0eu) != 0x2c12u ||
                 gate_ds_word(0x2c24u) != 0x65f0u))
        good = 0;

    /* The pinned original RTL contains zero words at all 27 observed
     * sites. Exercise the preflight and segment-parameter relocation only
     * *after* independently comparing all original source-image bytes.
     * 1234h is a test code segment; no code is jumped to from the probe. */
    if (good && gate_dos_fix_intro_rtl_segs((uint16_t)allocated,
                                           GATE_REAL_PARAGRAPHS, 0x1234u) != 0)
        good = 0;
    for (i = 0u; good && i < sizeof observed_ds_sites /
                              sizeof observed_ds_sites[0]; ++i)
        if (gate_ds_word(observed_ds_sites[i]) != data_segment)
            good = 0;
    for (i = 0u; good && i < sizeof observed_cs_sites /
                              sizeof observed_cs_sites[0]; ++i)
        if (gate_ds_word(observed_cs_sites[i]) != 0x1234u)
            good = 0;
    if (good && gate_dos_fix_intro_rtl_offs((uint16_t)allocated,
                                           GATE_REAL_PARAGRAPHS) != 0)
        good = 0;
    for (i = 0u; good && i < sizeof expected_offsets /
                              sizeof expected_offsets[0]; ++i)
        if (gate_ds_word(expected_offsets[i].site) != expected_offsets[i].expected)
            good = 0;
    /* Do not use original-run segment addresses: 3333h and 4444h here
     * are test-only caller-supplied handles. Each source FAR pointer must
     * be all-zero in the pinned original RTL image. */
    if (good && gate_dos_fix_intro_rtl_external((uint16_t)allocated,
                                               GATE_REAL_PARAGRAPHS,
                                               0x3333u, 0x4444u) != 0)
        good = 0;
    for (i = 0u; good && i < sizeof external_a /
                              sizeof external_a[0]; ++i)
        if (gate_ds_word(external_a[i].site) != external_a[i].expected ||
            gate_ds_word((uint16_t)(external_a[i].site + 2u)) != 0x3333u)
            good = 0;
    for (i = 0u; good && i < sizeof external_b /
                              sizeof external_b[0]; ++i)
        if (gate_ds_word(external_b[i].site) != external_b[i].expected ||
            gate_ds_word((uint16_t)(external_b[i].site + 2u)) != 0x4444u)
            good = 0;
    if (good && gate_dos_fix_intro_early_bytes((uint16_t)allocated,
                                               GATE_REAL_PARAGRAPHS) != 0)
        good = 0;
    if (good && gate_far_byte(data_segment, 0x0518u) != 0x15u)
        good = 0;
    for (i = 0u; good && i < 8u; ++i)
        if (gate_ds_word((uint16_t)(0x0547u + 2u * i)) != observed_early_words[i])
            good = 0;
    if (good && (gate_far_byte(data_segment, GATE_REAL_DEST) != exe_first_byte ||
                 gate_ds_word(0x4d82u) != (uint16_t)allocated ||
                 gate_ds_word(0x4d84u) != data_segment))
        good = 0;

    if (have_handle) {
        result = gate_dos_file_close(handle);
        if (result.carry)
            good = 0;
    }
    if (resident_allocated != 0u && _dos_freemem(resident_allocated) != 0u)
        good = 0;
    if (have_allocation) {
        gate_dos_bind_ds(0u);
        if (_dos_freemem(allocated) != 0u)
            good = 0;
    }
    puts(good ? "Original EXE/RTL data probe: PASS (5976 + 13714 bytes, 27 seg + 24 off + 19 external pointers)"
              : "Original EXE/RTL data probe: FAIL");
    return good ? 0 : 1;
}

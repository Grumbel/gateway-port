/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Include the staged DOS implementation in this test translation unit so
 * the original-derived static relocation manifest can be exercised without
 * duplicating 98 callsite/target pairs in a second source file. */
#include "DATA.C"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t memory[0x100000];
static uint16_t data_segment;

uint16_t gate_ds_segment(void) { return data_segment; }
void gate_dos_bind_ds(uint16_t segment) { data_segment = segment; }
uint8_t gate_far_byte(uint16_t segment, uint16_t offset)
{
    return memory[(unsigned long)segment * 16ul + offset];
}
void gate_far_set_byte(uint16_t segment, uint16_t offset, uint8_t value)
{
    memory[(unsigned long)segment * 16ul + offset] = value;
}
uint16_t gate_far_word(uint16_t segment, uint16_t offset)
{
    return (uint16_t)(gate_far_byte(segment, offset) |
        ((uint16_t)gate_far_byte(segment, (uint16_t)(offset + 1u)) << 8));
}
void gate_far_set_word(uint16_t segment, uint16_t offset, uint16_t value)
{
    gate_far_set_byte(segment, offset, (uint8_t)value);
    gate_far_set_byte(segment, (uint16_t)(offset + 1u), (uint8_t)(value >> 8));
}
uint16_t gate_ds_word(uint16_t offset) { return gate_far_word(data_segment, offset); }
void gate_ds_set_word(uint16_t offset, uint16_t value)
{
    gate_far_set_word(data_segment, offset, value);
}
int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs)
{
    (void)lower_segment;
    (void)paragraphs;
    return -1;
}
GateDosFileResult gate_dos_file_seek(uint16_t handle, uint16_t origin, uint32_t offset)
{
    GateDosFileResult result;
    (void)handle; (void)origin; (void)offset;
    memset(&result, 0, sizeof result);
    result.carry = 1u;
    return result;
}
GateDosFileResult gate_dos_file_read(uint16_t handle, GateFarPtr target, uint16_t length)
{
    GateDosFileResult result;
    (void)handle; (void)target; (void)length;
    memset(&result, 0, sizeof result);
    result.carry = 1u;
    return result;
}

int main(void)
{
    unsigned int i;
    uint16_t segment = 0x3000u;
    GateFarPtr external;
    uint16_t first_site = gate_resident_self_calls[0].site;
    uint16_t last_site = gate_resident_self_calls[
        sizeof gate_resident_self_calls / sizeof gate_resident_self_calls[0] - 1u].site;

    if (sizeof gate_resident_self_calls / sizeof gate_resident_self_calls[0] != 98u)
        abort();
    memset(memory, 0x90, sizeof memory);
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        if (site + 5u > 0x3d00u || (i && site <= gate_resident_self_calls[i - 1u].site + 4u))
            abort();
        gate_far_set_byte(segment, site, 0x9au);
        gate_far_set_word(segment, (uint16_t)(site + 1u), 0u);
        gate_far_set_word(segment, (uint16_t)(site + 3u), 0u);
    }
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_byte(segment, last_site, 0x90u);
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, (uint16_t)(first_site + 1u)) != 0u)
        abort();
    gate_far_set_byte(segment, last_site, 0x9au);
    if (gate_dos_relocate_intro_resident_self_calls(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        if (gate_far_byte(segment, site) != 0x9au ||
            gate_far_word(segment, (uint16_t)(site + 1u)) !=
                gate_resident_self_calls[i].target ||
            gate_far_word(segment, (uint16_t)(site + 3u)) != segment)
            abort();
    }
    if (gate_far_byte(segment, (uint16_t)(first_site - 1u)) != 0x90u ||
        gate_far_byte(segment, (uint16_t)(last_site + 5u)) != 0x90u)
        abort();
    for (i = 0u; i < sizeof gate_resident_ds_loads /
                      sizeof gate_resident_ds_loads[0]; ++i) {
        uint16_t site = gate_resident_ds_loads[i];
        gate_far_set_byte(segment, (uint16_t)(site - 1u), 0xb8u);
        gate_far_set_word(segment, site, 0u);
        gate_far_set_byte(segment, (uint16_t)(site + 2u), 0x8eu);
        gate_far_set_byte(segment, (uint16_t)(site + 3u), 0xd8u);
    }
    if (sizeof gate_resident_ds_loads / sizeof gate_resident_ds_loads[0] != 7u ||
        gate_dos_relocate_intro_resident_ds_loads(segment, 0x03cfu, 0x200au) != -1)
        abort();
    gate_far_set_byte(segment, (uint16_t)(gate_resident_ds_loads[6] + 3u), 0x90u);
    if (gate_dos_relocate_intro_resident_ds_loads(segment, 0x03d0u, 0x200au) != -2 ||
        gate_far_word(segment, gate_resident_ds_loads[0]) != 0u)
        abort();
    gate_far_set_byte(segment, (uint16_t)(gate_resident_ds_loads[6] + 3u), 0xd8u);
    if (gate_dos_relocate_intro_resident_ds_loads(segment, 0x03d0u, 0x200au) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_ds_loads /
                      sizeof gate_resident_ds_loads[0]; ++i)
        if (gate_far_word(segment, gate_resident_ds_loads[i]) != 0x200au)
            abort();
    external.segment = 0x4242u;
    external.offset = 0x0030u;
    gate_far_set_byte(segment, 0x0a4du, 0x9au);
    gate_far_set_word(segment, 0x0a4eu, 0u);
    gate_far_set_word(segment, 0x0a50u, 0u);
    if (gate_dos_relocate_intro_exe_call(segment, 0x03cfu, external) != -1)
        abort();
    gate_far_set_byte(segment, 0x0a4du, 0x90u);
    if (gate_dos_relocate_intro_exe_call(segment, 0x03d0u, external) != -2 ||
        gate_far_word(segment, 0x0a4eu) != 0u)
        abort();
    gate_far_set_byte(segment, 0x0a4du, 0x9au);
    if (gate_dos_relocate_intro_exe_call(segment, 0x03d0u, external) != 0 ||
        gate_far_word(segment, 0x0a4eu) != external.offset ||
        gate_far_word(segment, 0x0a50u) != external.segment)
        abort();
    for (i = 0u; i < sizeof gate_resident_cmp_ds /
                      sizeof gate_resident_cmp_ds[0]; ++i) {
        uint16_t site = gate_resident_cmp_ds[i];
        gate_far_set_byte(segment, (uint16_t)(site - 3u), 0x81u);
        gate_far_set_byte(segment, (uint16_t)(site - 2u), 0x7eu);
        gate_far_set_byte(segment, (uint16_t)(site - 1u), 0x0au);
        gate_far_set_word(segment, site, 0u);
    }
    for (i = 0u; i < sizeof gate_resident_mov_dx_ds /
                      sizeof gate_resident_mov_dx_ds[0]; ++i) {
        uint16_t site = gate_resident_mov_dx_ds[i];
        gate_far_set_byte(segment, (uint16_t)(site - 4u), 0xb8u);
        gate_far_set_byte(segment, (uint16_t)(site - 1u), 0xbau);
        gate_far_set_word(segment, site, 0u);
    }
    gate_far_set_byte(segment, 0x09b8u, 0xbfu);
    gate_far_set_word(segment, 0x09b9u, 0u);
    gate_far_set_byte(segment, 0x09bbu, 0x8bu);
    gate_far_set_byte(segment, 0x09bcu, 0x36u);
    gate_far_set_byte(segment, 0x0197u, 0x26u);
    gate_far_set_byte(segment, 0x0198u, 0xc7u);
    gate_far_set_byte(segment, 0x0199u, 0x06u);
    gate_far_set_word(segment, 0x019au, 0x0072u);
    gate_far_set_word(segment, 0x019cu, 0u);
    if (gate_dos_relocate_intro_resident_direct_segments(segment, 0x03cfu,
                                                          0x200au) != -1)
        abort();
    gate_far_set_word(segment, 0x019cu, 1u);
    if (gate_dos_relocate_intro_resident_direct_segments(segment, 0x03d0u,
                                                          0x200au) != -2 ||
        gate_far_word(segment, gate_resident_cmp_ds[0]) != 0u)
        abort();
    gate_far_set_word(segment, 0x019cu, 0u);
    if (gate_dos_relocate_intro_resident_direct_segments(segment, 0x03d0u,
                                                          0x200au) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_cmp_ds /
                      sizeof gate_resident_cmp_ds[0]; ++i)
        if (gate_far_word(segment, gate_resident_cmp_ds[i]) != 0x200au)
            abort();
    for (i = 0u; i < sizeof gate_resident_mov_dx_ds /
                      sizeof gate_resident_mov_dx_ds[0]; ++i)
        if (gate_far_word(segment, gate_resident_mov_dx_ds[i]) != 0x200au)
            abort();
    if (gate_far_word(segment, 0x09b9u) != 0x200au ||
        gate_far_word(segment, 0x019cu) != segment)
        abort();
    if (gate_dos_prepare_intro_resident_bank(1u, segment, 0x03cfu,
                                             0x200au, external) != -1)
        abort();
    /* The fixture deliberately rejects file I/O, so the composed loader
     * must stop at that boundary without disturbing relocated guest RAM. */
    if (gate_dos_prepare_intro_resident_bank(1u, segment, 0x03d0u,
                                             0x200au, external) != -2 ||
        gate_far_word(segment, 0x09d7u) != 0x0bdeu)
        abort();
    /* Reconstruct only opcode contexts and zero words in fresh paragraph
     * relocation operands; the actual file/snapshot oracle is in Python. */
    for (i = 0u; i < sizeof gate_resident_paragraph_fixups /
                      sizeof gate_resident_paragraph_fixups[0]; ++i) {
        uint16_t site = gate_resident_paragraph_fixups[i].site;
        uint8_t opcode = gate_resident_paragraph_fixups[i].opcode;
        gate_far_set_word(segment, site, 0u);
        gate_far_set_byte(segment, (uint16_t)(site - (opcode == 0x81u ? 3u : 1u)),
                          opcode);
        if (opcode == 0x81u) {
            gate_far_set_byte(segment, (uint16_t)(site - 2u),
                              site == 0x1170u ? 0x46u : 0x47u);
            gate_far_set_byte(segment, (uint16_t)(site - 1u),
                              site == 0x1170u ? 0x08u : 0x02u);
            if (site != 0x1170u)
                gate_far_set_byte(segment, (uint16_t)(site - 4u), 0x26u);
        }
    }
    if (gate_dos_relocate_intro_resident_paragraphs(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_byte(segment, 0x3a6eu, 0x90u);
    if (gate_dos_relocate_intro_resident_paragraphs(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x113du) != 0u)
        abort();
    gate_far_set_byte(segment, 0x3a6eu, 0x05u);
    if (gate_dos_relocate_intro_resident_paragraphs(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_paragraph_fixups /
                      sizeof gate_resident_paragraph_fixups[0]; ++i)
        if (gate_far_word(segment, gate_resident_paragraph_fixups[i].site) != 0x1000u)
            abort();
    for (i = 0u; i < sizeof gate_resident_flag_offsets /
                      sizeof gate_resident_flag_offsets[0]; ++i) {
        uint16_t site = gate_resident_flag_offsets[i].site;
        gate_far_set_word(segment, (uint16_t)(site - 2u),
                          gate_resident_flag_offsets[i].prefix);
        gate_far_set_word(segment, site, 0u);
    }
    if (gate_dos_relocate_intro_resident_flag_offsets(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_word(segment, 0x3344u, 1u);
    if (gate_dos_relocate_intro_resident_flag_offsets(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x0b01u) != 0u)
        abort();
    gate_far_set_word(segment, 0x3344u, 0u);
    if (gate_dos_relocate_intro_resident_flag_offsets(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_flag_offsets /
                      sizeof gate_resident_flag_offsets[0]; ++i)
        if (gate_far_word(segment, gate_resident_flag_offsets[i].site) != 0x2bf0u)
            abort();
    for (i = 0u; i < sizeof gate_resident_state_3244 /
                      sizeof gate_resident_state_3244[0]; ++i) {
        uint16_t site = gate_resident_state_3244[i].site;
        gate_far_set_word(segment, (uint16_t)(site - 2u),
                          gate_resident_state_3244[i].prefix);
        gate_far_set_word(segment, site, 0u);
    }
    for (i = 0u; i < sizeof gate_resident_state_3334 /
                      sizeof gate_resident_state_3334[0]; ++i) {
        uint16_t site = gate_resident_state_3334[i].site;
        gate_far_set_word(segment, (uint16_t)(site - 2u),
                          gate_resident_state_3334[i].prefix);
        gate_far_set_word(segment, site, 0u);
    }
    if (gate_dos_relocate_intro_resident_state_offsets(segment, 0x03cfu) != -1)
        abort();
    /* Corrupt a site in the SECOND group. Both groups must remain intact. */
    gate_far_set_word(segment, 0x31bbu, 0x0010u);
    if (gate_dos_relocate_intro_resident_state_offsets(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x0f93u) != 0u ||
        gate_far_word(segment, 0x111eu) != 0u)
        abort();
    gate_far_set_word(segment, 0x31bbu, 0u);
    if (gate_dos_relocate_intro_resident_state_offsets(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_state_3244 /
                      sizeof gate_resident_state_3244[0]; ++i)
        if (gate_far_word(segment, gate_resident_state_3244[i].site) != 0x3244u)
            abort();
    for (i = 0u; i < sizeof gate_resident_state_3334 /
                      sizeof gate_resident_state_3334[0]; ++i)
        if (gate_far_word(segment, gate_resident_state_3334[i].site) != 0x3334u)
            abort();
    /* Some opcode prefixes overlap the previous operand's high byte;
     * first lay down original words, then the checked instruction prefixes. */
    for (i = 0u; i < sizeof gate_resident_small_offsets /
                      sizeof gate_resident_small_offsets[0]; ++i)
        gate_far_set_word(segment, gate_resident_small_offsets[i].site,
                          gate_resident_small_offsets[i].original);
    for (i = 0u; i < sizeof gate_resident_small_offsets /
                      sizeof gate_resident_small_offsets[0]; ++i)
        gate_far_set_word(segment,
                          (uint16_t)(gate_resident_small_offsets[i].site - 2u),
                          gate_resident_small_offsets[i].prefix);
    if (gate_dos_relocate_intro_resident_small_offsets(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_word(segment, 0x223au, 0xffffu);
    if (gate_dos_relocate_intro_resident_small_offsets(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x1874u) != 0x0034u)
        abort();
    gate_far_set_word(segment, 0x223au, 0x0194u);
    if (gate_dos_relocate_intro_resident_small_offsets(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_small_offsets /
                      sizeof gate_resident_small_offsets[0]; ++i)
        if (gate_far_word(segment, gate_resident_small_offsets[i].site) !=
            (uint16_t)(gate_resident_small_offsets[i].original + 0x62dau))
            abort();
    gate_far_set_byte(segment, 0x1a2bu, 0x2eu);
    gate_far_set_byte(segment, 0x1a2cu, 0xffu);
    gate_far_set_byte(segment, 0x1a2du, 0xa7u);
    gate_far_set_word(segment, 0x1a2eu, 0x02feu);
    for (i = 0u; i < sizeof gate_resident_dispatch_source_words /
                      sizeof gate_resident_dispatch_source_words[0]; ++i)
        gate_far_set_word(segment, (uint16_t)(0x1b66u + 2u * i),
                          gate_resident_dispatch_source_words[i]);
    if (gate_dos_relocate_intro_resident_dispatch_table(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_word(segment, 0x1b90u, 0xffffu);
    if (gate_dos_relocate_intro_resident_dispatch_table(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x1a2eu) != 0x02feu ||
        gate_far_word(segment, 0x1b66u) != 0x02ceu)
        abort();
    gate_far_set_word(segment, 0x1b90u, 0x02beu);
    if (gate_dos_relocate_intro_resident_dispatch_table(segment, 0x03d0u) != 0 ||
        gate_far_word(segment, 0x1a2eu) != 0x1b66u)
        abort();
    for (i = 0u; i < sizeof gate_resident_dispatch_source_words /
                      sizeof gate_resident_dispatch_source_words[0]; ++i)
        if (gate_far_word(segment, (uint16_t)(0x1b66u + 2u * i)) !=
            (uint16_t)(gate_resident_dispatch_source_words[i] + 0x1868u))
            abort();
    for (i = 0u; i < sizeof gate_resident_early_offsets /
                      sizeof gate_resident_early_offsets[0]; ++i)
        gate_far_set_word(segment, gate_resident_early_offsets[i].site, 0u);
    for (i = 0u; i < sizeof gate_resident_early_offsets /
                      sizeof gate_resident_early_offsets[0]; ++i)
        gate_far_set_word(segment,
                          (uint16_t)(gate_resident_early_offsets[i].site - 2u),
                          gate_resident_early_offsets[i].prefix);
    if (gate_dos_relocate_intro_resident_early_offsets(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_word(segment, 0x09a2u, 1u);
    if (gate_dos_relocate_intro_resident_early_offsets(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x002du) != 0u)
        abort();
    gate_far_set_word(segment, 0x09a2u, 0u);
    if (gate_dos_relocate_intro_resident_early_offsets(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_early_offsets /
                      sizeof gate_resident_early_offsets[0]; ++i)
        if (gate_far_word(segment, gate_resident_early_offsets[i].site) !=
            gate_resident_early_offsets[i].target)
            abort();
    for (i = 0u; i < sizeof gate_resident_cs_state_offsets /
                      sizeof gate_resident_cs_state_offsets[0]; ++i) {
        gate_far_set_word(segment, gate_resident_cs_state_offsets[i].site, 0u);
        gate_far_set_word(segment,
                          (uint16_t)(gate_resident_cs_state_offsets[i].site - 2u),
                          gate_resident_cs_state_offsets[i].prefix);
    }
    if (gate_dos_relocate_intro_resident_cs_state(segment, 0x03cfu) != -1)
        abort();
    gate_far_set_word(segment, 0x0373u, 1u);
    if (gate_dos_relocate_intro_resident_cs_state(segment, 0x03d0u) != -2 ||
        gate_far_word(segment, 0x0261u) != 0u)
        abort();
    gate_far_set_word(segment, 0x0373u, 0u);
    if (gate_dos_relocate_intro_resident_cs_state(segment, 0x03d0u) != 0)
        abort();
    for (i = 0u; i < sizeof gate_resident_cs_state_offsets /
                      sizeof gate_resident_cs_state_offsets[0]; ++i)
        if (gate_far_word(segment, gate_resident_cs_state_offsets[i].site) !=
            gate_resident_cs_state_offsets[i].target)
            abort();
    puts("98 self-calls, 7 DS loads, EXE call and 7 direct segments: PASS");
    return 0;
}

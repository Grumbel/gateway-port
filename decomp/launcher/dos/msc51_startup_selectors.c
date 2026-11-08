/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Original loader startup observations, not a reconstructed loader.
 * All 45 checked RTL/EXE FAR segment selectors in DS:4d82..4e4e have
 * settled by the original 100ms capture. Twelve point at data DS, 33
 * at DS-000ah; both 64KiB segment windows overlap by 160 bytes.
 *
 * This helper installs ONLY these observed segment words in an otherwise
 * caller-initialized DOS allocation. It does not load the original data,
 * relocate code, create a valid mode descriptor, or execute the intro.
 */
#include "GATEMEM.H"

#define GATE_INTRO_ALIAS_SHIFT 0x000au
#define GATE_INTRO_ALIAS_PARAGRAPHS 0x100au

static const uint16_t gate_intro_lower_selectors[] = {
    0x4d82u, 0x4d86u, 0x4d88u, 0x4d8au, 0x4d8cu, 0x4d8eu,
    0x4d90u, 0x4d92u, 0x4d94u, 0x4d96u, 0x4d98u, 0x4d9au,
    0x4d9cu, 0x4d9eu, 0x4da0u, 0x4da2u, 0x4da4u, 0x4da6u,
    0x4da8u, 0x4daau, 0x4dd0u, 0x4dd2u, 0x4dd4u, 0x4dd6u,
    0x4dd8u, 0x4de2u, 0x4de6u, 0x4de8u, 0x4deau, 0x4e2eu,
    0x4e30u, 0x4e40u, 0x4e44u
};

static const uint16_t gate_intro_data_selectors[] = {
    0x4d84u, 0x4dceu, 0x4ddau, 0x4ddcu, 0x4e24u, 0x4e2au,
    0x4e42u, 0x4e46u, 0x4e48u, 0x4e4au, 0x4e4cu, 0x4e4eu
};

/* The caller owns at least 100ah contiguous paragraphs starting at
 * lower_segment. Reserving only 1000h paragraphs would leave the last
 * 160 bytes of the data-DS window outside its allocation. Return -1
 * before binding on an invalid segment or an undersized allocation. */
int gate_dos_bind_intro_aliases(uint16_t lower_segment, uint16_t paragraphs)
{
    uint16_t data_segment;
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_INTRO_ALIAS_PARAGRAPHS)
        return -1;

    data_segment = (uint16_t)(lower_segment + GATE_INTRO_ALIAS_SHIFT);
    gate_dos_bind_ds(data_segment);
    for (i = 0u; i < sizeof gate_intro_lower_selectors /
                      sizeof gate_intro_lower_selectors[0]; ++i)
        gate_ds_set_word(gate_intro_lower_selectors[i], lower_segment);
    for (i = 0u; i < sizeof gate_intro_data_selectors /
                      sizeof gate_intro_data_selectors[0]; ++i)
        gate_ds_set_word(gate_intro_data_selectors[i], data_segment);
    return 0;
}

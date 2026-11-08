/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Original startup observations:
 * - all 5976 bytes at EXE:041e6 are copied to runtime DS:3592;
 * - at 100ms, 13454/13714 bytes of RTL:00029..035ba match runtime
 *   DS:0000..3591, with several long exact spans and modified words.
 *
 * The second routine stages only the *unmodified RTL preimage*: the
 * mismatched 260 bytes at 100ms are partly reconstructed: 27 source-zero
 * segment words, 24 independently observed offset words, and two externally
 * segmented FAR-pointer tables below. Original DOS MCB captures establish
 * that both targets lie INSIDE the existing PSP-owned program block, not in
 * separately allocated DOS blocks. The caller must supply their actual
 * segment placement in the rebuilt program layout. No game data is embedded.
 * The file-copy routines require
 * caller-owned segment windows and file handles; none of these routines
 * loads resident banks, creates BSS or implements DOS launcher main().
 */
#include "GATEDOS.H"

#define GATE_STATIC_EXE_SOURCE 0x000041e6ul
#define GATE_STATIC_DATA_OFFSET 0x3592u
#define GATE_STATIC_DATA_BYTES 0x1758u
#define GATE_RTL_DATA_SOURCE 0x00000029ul
#define GATE_RTL_DATA_BYTES 0x3592u
#define GATE_DATA_MIN_PARAS 0x100au

/* At 100, 125, 175 and 250ms the first 64 bytes after the two-byte
 * RTL data header are zero, whereas the raw RTL preimage contains resident
 * instructions there. This is an observed early initialization boundary,
 * not proof of the original loader's clearing mechanism. It must run only
 * on the newly loaded RTL data image, before any original code uses the
 * fields (56 of the 63 changed bytes are subsequently rewritten). */
int gate_dos_clear_intro_rtl_prefix(uint16_t lower_segment, uint16_t paragraphs)
{
    uint16_t i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;
    for (i = 2u; i < 66u; ++i)
        gate_far_set_byte(gate_ds_segment(), i, 0u);
    return 0;
}

/* -1 invalid allocation/binding, -2 failed/inexact seek, -3 read error
 * or short read. On a short read, the destination may be partly written;
 * callers must discard that allocation rather than entering the intro.
 * Does not close the caller-owned handle. */
int gate_dos_load_intro_static_data(uint16_t handle, uint16_t lower_segment,
                                    uint16_t paragraphs)
{
    GateDosFileResult result;
    GateFarPtr target;

    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    result = gate_dos_file_seek(handle, 0u, GATE_STATIC_EXE_SOURCE);
    if (result.carry || result.ax != 0x41e6u || result.dx != 0u)
        return -2;

    target.segment = gate_ds_segment();
    target.offset = GATE_STATIC_DATA_OFFSET;
    result = gate_dos_file_read(handle, target, GATE_STATIC_DATA_BYTES);
    if (result.carry || result.ax != GATE_STATIC_DATA_BYTES)
        return -3;
    return 0;
}

/* The *source file image* for the still-unrelocated leading data segment.
 * -1 invalid allocation/binding, -2 inexact seek, -3 read/short read.
 * Do not enter game code with this preimage alone: its original runtime
 * counterpart differs in hundreds of relocated or initialized bytes.
 */
int gate_dos_load_intro_rtl_data(uint16_t handle, uint16_t lower_segment,
                                 uint16_t paragraphs)
{
    GateDosFileResult result;
    GateFarPtr target;

    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    result = gate_dos_file_seek(handle, 0u, GATE_RTL_DATA_SOURCE);
    if (result.carry || result.ax != 0x0029u || result.dx != 0u)
        return -2;

    target.segment = gate_ds_segment();
    target.offset = 0u;
    result = gate_dos_file_read(handle, target, GATE_RTL_DATA_BYTES);
    if (result.carry || result.ax != GATE_RTL_DATA_BYTES)
        return -3;
    return 0;
}

/* Observed RTL file +0029h -> eventual DS word locations. In the original
 * unrelocated file all 27 sites are 0000h. At 100,125,175 and 250ms in
 * the user's original DOS RAM captures, 22 hold DS=2a7ah and five hold
 * resident CS=12a5h. Several occur in regular four-byte FAR-pointer
 * tables. The values passed by our caller are not the original fixed
 * segments; both must be derived from the new load layout.
 *
 * Observed replacement words alone do NOT establish the mechanism used
 * by the original loader, nor identify the other 206 changed bytes.
 */
static const uint16_t gate_rtl_ds_seg_fixups[] = {
    0x0fb8u, 0x0fbcu, 0x0fc0u, 0x0fc4u, 0x0fc8u, 0x0fccu,
    0x153au, 0x153eu, 0x1542u, 0x1546u, 0x154au, 0x154eu,
    0x1552u, 0x1556u, 0x26d8u, 0x2b78u, 0x2c10u, 0x3246u,
    0x324cu, 0x33aeu, 0x3540u, 0x3544u
};

static const uint16_t gate_rtl_cs_seg_fixups[] = {
    0x33deu, 0x33e2u, 0x33e6u, 0x33eau, 0x33eeu
};

/* Apply only the proven early segment-word values to a fresh raw RTL
 * preimage. Require ALL 27 fields to be zero *before* modifying any;
 * on -2, the source is not the expected raw image and remains untouched.
 * -1 invalid allocation/segment binding, -2 unexpected source value.
 * The caller must have independently loaded/validated the resident bank
 * and pass its actual code segment; this helper does not load it.
 */
int gate_dos_fix_intro_rtl_segs(uint16_t lower_segment, uint16_t paragraphs,
                                uint16_t resident_segment)
{
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS || resident_segment == 0u ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    for (i = 0u; i < sizeof gate_rtl_ds_seg_fixups /
                      sizeof gate_rtl_ds_seg_fixups[0]; ++i)
        if (gate_ds_word(gate_rtl_ds_seg_fixups[i]) != 0u)
            return -2;
    for (i = 0u; i < sizeof gate_rtl_cs_seg_fixups /
                      sizeof gate_rtl_cs_seg_fixups[0]; ++i)
        if (gate_ds_word(gate_rtl_cs_seg_fixups[i]) != 0u)
            return -2;

    for (i = 0u; i < sizeof gate_rtl_ds_seg_fixups /
                      sizeof gate_rtl_ds_seg_fixups[0]; ++i)
        gate_ds_set_word(gate_rtl_ds_seg_fixups[i], gate_ds_segment());
    for (i = 0u; i < sizeof gate_rtl_cs_seg_fixups /
                      sizeof gate_rtl_cs_seg_fixups[0]; ++i)
        gate_ds_set_word(gate_rtl_cs_seg_fixups[i], resident_segment);
    return 0;
}

typedef struct GateIntroOffsetFixup {
    uint16_t site;
    uint16_t source;
    uint16_t addend;
} GateIntroOffsetFixup;

/* The user's actual 100..250ms RAM captures show these 24 offset words
 * after the raw RTL +0029h preimage is initialized. Their source values
 * are independently checked in the original file. The first six are
 * rebased by 0f92h, the next eight by 14e0h, and five FAR code-pointer
 * offsets change from zero to 0c02h (their CS words are among the 27
 * segment replacements above). Five more DS FAR-pointer offsets, whose
 * segment words were already staged above, use addends 2c44h (three
 * fields) or 3530h (two fields). These are offsets, not DOS segments.
 */
static const GateIntroOffsetFixup gate_rtl_offset_fixups[] = {
    {0x0fb6u, 0x0000u, 0x0f92u}, {0x0fbau, 0x0004u, 0x0f92u},
    {0x0fbeu, 0x0008u, 0x0f92u}, {0x0fc2u, 0x000cu, 0x0f92u},
    {0x0fc6u, 0x0010u, 0x0f92u}, {0x0fcau, 0x0014u, 0x0f92u},
    {0x1538u, 0x001au, 0x14e0u}, {0x153cu, 0x0020u, 0x14e0u},
    {0x1540u, 0x002au, 0x14e0u}, {0x1544u, 0x002fu, 0x14e0u},
    {0x1548u, 0x0039u, 0x14e0u}, {0x154cu, 0x003fu, 0x14e0u},
    {0x1550u, 0x0049u, 0x14e0u}, {0x1554u, 0x004eu, 0x14e0u},
    {0x33dcu, 0x0000u, 0x0c02u}, {0x33e0u, 0x0000u, 0x0c02u},
    {0x33e4u, 0x0000u, 0x0c02u}, {0x33e8u, 0x0000u, 0x0c02u},
    {0x33ecu, 0x0000u, 0x0c02u},
    {0x3244u, 0x0000u, 0x2c44u}, {0x324au, 0x0000u, 0x2c44u},
    {0x33acu, 0x06e4u, 0x2c44u}, {0x353eu, 0x0000u, 0x3530u},
    {0x3542u, 0x0004u, 0x3530u}
};

/* Apply the 24 observed near-offset fixes with the same no-partial-write
 * contract as the segment fixer. Run after copying the raw RTL preimage,
 * before or after segment-word replacements. This does not discover the
 * original loader's relocation table, or initialize unrelated data.
 * -1 invalid allocation/DS binding; -2 source value mismatch.
 */
int gate_dos_fix_intro_rtl_offs(uint16_t lower_segment, uint16_t paragraphs)
{
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    for (i = 0u; i < sizeof gate_rtl_offset_fixups /
                      sizeof gate_rtl_offset_fixups[0]; ++i)
        if (gate_ds_word(gate_rtl_offset_fixups[i].site) !=
            gate_rtl_offset_fixups[i].source)
            return -2;

    for (i = 0u; i < sizeof gate_rtl_offset_fixups /
                      sizeof gate_rtl_offset_fixups[0]; ++i)
        gate_ds_set_word(gate_rtl_offset_fixups[i].site,
                         (uint16_t)(gate_rtl_offset_fixups[i].source +
                                    gate_rtl_offset_fixups[i].addend));
    return 0;
}

/* Six further zero-source words become constant offsets in every supplied
 * 100..250ms original snapshot. These are recorded as an observed initial
 * state, not assigned to an unproven relocation rule. In particular the
 * field at 2c0e is later modified; this is its *startup* value only.
 * The source must still contain all six zero words before any are written. */
static const GateIntroOffsetFixup gate_rtl_startup_words[] = {
    {0x0525u, 0u, 0x0527u}, {0x26d6u, 0u, 0x16d4u},
    {0x2b74u, 0u, 0x0b57u}, {0x2bc6u, 0u, 0x2b76u},
    {0x2c0eu, 0u, 0x2c12u}, {0x2c24u, 0u, 0x65f0u}
};

int gate_dos_fix_intro_rtl_startup_words(uint16_t lower_segment,
                                         uint16_t paragraphs)
{
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;
    for (i = 0u; i < sizeof gate_rtl_startup_words /
                      sizeof gate_rtl_startup_words[0]; ++i)
        if (gate_ds_word(gate_rtl_startup_words[i].site) != 0u)
            return -2;
    for (i = 0u; i < sizeof gate_rtl_startup_words /
                      sizeof gate_rtl_startup_words[0]; ++i)
        gate_ds_set_word(gate_rtl_startup_words[i].site,
                         gate_rtl_startup_words[i].addend);
    return 0;
}

/* The authentic 125ms DS:049d FAR pointer targets the single RETF opcode
 * at original RTL:4bc9, immediately after an IRET and before a MOV. The
 * original file-to-memory identity is confirmed by the adjacent relocated
 * code at RTL:4bca..4bf0. The standalone DOS loader must pass the *new*
 * code address; original runtime 17ec:013f is not a portable constant.
 * Refuse a missing/incorrect bank or a previously initialized pointer.
 * -1 invalid layout/target, -2 wrong code or nonzero source pointer. */
int gate_dos_fix_intro_return_pointer(uint16_t lower_segment,
                                      uint16_t paragraphs, GateFarPtr target)
{
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au) ||
        target.segment == 0u || target.offset == 0u || target.offset > 0xffdeu)
        return -1;
    if (gate_ds_word(0x049du) != 0u || gate_ds_word(0x049fu) != 0u ||
        gate_far_byte(target.segment, (uint16_t)(target.offset - 1u)) != 0xcfu ||
        gate_far_byte(target.segment, target.offset) != 0xcbu ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 1u)) != 0x8bu ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 0x1du)) != 0xfau ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 0x1eu)) != 0x8bu ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 0x1fu)) != 0x46u ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 0x20u)) != 0x06u ||
        gate_far_byte(target.segment, (uint16_t)(target.offset + 0x21u)) != 0xa3u)
        return -2;
    gate_ds_set_word(0x049du, target.offset);
    gate_ds_set_word(0x049fu, target.segment);
    return 0;
}

typedef struct GateIntroExternalPtr {
    uint16_t site;
    uint16_t offset;
} GateIntroExternalPtr;

/* The original 100..250ms captures show two regular FAR-pointer tables:
 * eight entries at DS:2814 (stride 003eh) and eleven at DS:2a93 (stride
 * 0014h). All 76 source bytes are zero in the pinned RTL preimage. The
 * first table's observed segment was 252ch; the second's was 25d7h.
 * Those are original-run addresses, NOT portable segments. Both reside in
 * the original PSP's allocation (offsets 153ch and 15e7h paragraphs past
 * PSP=0ff0); the PSP block shrinks to 2a8ah paragraphs at 350ms. We stage
 * only observed pointer offsets; the caller must provide the corresponding
 * actual program-image segments. No allocation inference is made here.
 */
static const GateIntroExternalPtr gate_rtl_external_a[] = {
    {0x2814u, 0x000au}, {0x2852u, 0x0032u},
    {0x2890u, 0x0032u}, {0x28ceu, 0x0053u},
    {0x290cu, 0x0065u}, {0x294au, 0x0065u},
    {0x2988u, 0x0065u}, {0x29c6u, 0x0065u}
};

static const GateIntroExternalPtr gate_rtl_external_b[] = {
    {0x2a93u, 0x006bu}, {0x2aa7u, 0x00d6u},
    {0x2abbu, 0x00d6u}, {0x2acfu, 0x0133u},
    {0x2ae3u, 0x0189u}, {0x2af7u, 0x0189u},
    {0x2b0bu, 0x0189u}, {0x2b1fu, 0x0189u},
    {0x2b33u, 0x01e3u}, {0x2b47u, 0x0272u},
    {0x2b5bu, 0x0440u}
};

/* -1 invalid DS binding/allocation/target segment; -2 unexpected source
 * (ALL sites checked before writing). Must run on an untouched, copied
 * RTL preimage. The caller must identify both target image segments;
 * this routine neither allocates memory nor verifies their contents.
 */
int gate_dos_fix_intro_rtl_external(uint16_t lower_segment, uint16_t paragraphs,
                                    uint16_t first_segment, uint16_t second_segment)
{
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        first_segment == 0u || second_segment == 0u ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;

    for (i = 0u; i < sizeof gate_rtl_external_a /
                      sizeof gate_rtl_external_a[0]; ++i)
        if (gate_ds_word(gate_rtl_external_a[i].site) != 0u ||
            gate_ds_word((uint16_t)(gate_rtl_external_a[i].site + 2u)) != 0u)
            return -2;
    for (i = 0u; i < sizeof gate_rtl_external_b /
                      sizeof gate_rtl_external_b[0]; ++i)
        if (gate_ds_word(gate_rtl_external_b[i].site) != 0u ||
            gate_ds_word((uint16_t)(gate_rtl_external_b[i].site + 2u)) != 0u)
            return -2;

    for (i = 0u; i < sizeof gate_rtl_external_a /
                      sizeof gate_rtl_external_a[0]; ++i) {
        gate_ds_set_word(gate_rtl_external_a[i].site, gate_rtl_external_a[i].offset);
        gate_ds_set_word((uint16_t)(gate_rtl_external_a[i].site + 2u), first_segment);
    }
    for (i = 0u; i < sizeof gate_rtl_external_b /
                      sizeof gate_rtl_external_b[0]; ++i) {
        gate_ds_set_word(gate_rtl_external_b[i].site, gate_rtl_external_b[i].offset);
        gate_ds_set_word((uint16_t)(gate_rtl_external_b[i].site + 2u), second_segment);
    }
    return 0;
}

/* A checked *data-only* bootstrap for the reconstructed DOS entry. All
 * segment parameters come from the caller's actual allocation and relocated
 * resident-code placement, never from original captured absolute addresses.
 * File handles and allocated memory remain caller-owned. On any error the
 * data allocation is partially initialized and MUST be discarded; no game
 * entry may run. The 14 still-unrecovered early data bytes and resident
 * driver/interrupt setup are intentionally not fabricated here.
 * Return 0 success, -1 bad layout, -2..-10 failure at the indicated step. */
int gate_dos_prepare_intro_data(uint16_t exe_handle, uint16_t rtl_handle,
                                uint16_t lower_segment, uint16_t paragraphs,
                                uint16_t resident_segment,
                                uint16_t external_a_segment,
                                uint16_t external_b_segment,
                                GateFarPtr return_callback)
{
    extern int gate_dos_bind_intro_aliases(uint16_t, uint16_t);
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS || resident_segment == 0u ||
        external_a_segment == 0u || external_b_segment == 0u ||
        return_callback.segment == 0u)
        return -1;
    if (gate_dos_bind_intro_aliases(lower_segment, paragraphs) != 0)
        return -2;
    if (gate_dos_load_intro_rtl_data(rtl_handle, lower_segment, paragraphs) != 0)
        return -3;
    if (gate_dos_load_intro_static_data(exe_handle, lower_segment, paragraphs) != 0)
        return -4;
    if (gate_dos_clear_intro_rtl_prefix(lower_segment, paragraphs) != 0)
        return -5;
    if (gate_dos_fix_intro_rtl_segs(lower_segment, paragraphs,
                                    resident_segment) != 0)
        return -6;
    if (gate_dos_fix_intro_rtl_offs(lower_segment, paragraphs) != 0)
        return -7;
    if (gate_dos_fix_intro_rtl_startup_words(lower_segment, paragraphs) != 0)
        return -8;
    if (gate_dos_fix_intro_rtl_external(lower_segment, paragraphs,
                                        external_a_segment,
                                        external_b_segment) != 0)
        return -9;
    if (gate_dos_fix_intro_return_pointer(lower_segment, paragraphs,
                                          return_callback) != 0)
        return -10;
    return 0;
}

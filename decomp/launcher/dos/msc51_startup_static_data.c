/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Original startup observations:
 * - all 5976 bytes at EXE:041e6 are copied to runtime DS:3592;
 * - at 100ms, 13454/13714 bytes of RTL:00029..035ba match runtime
 *   DS:0000..3591, with several long exact spans and modified words.
 *
 * The second routine stages only the *unmodified RTL preimage*: the
 * mismatched 260 bytes at 100ms are only partly reconstructed: the 27
 * source-zero segment words listed below are observed in original RAM.
 * The remaining 206 mismatch bytes have no established initialization
 * rule. No original data is embedded. The file-copy routines require
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

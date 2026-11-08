/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

/* Original startup observations:
 * - all 5976 bytes at EXE:041e6 are copied to runtime DS:3592;
 * - at 100ms, 13454/13714 bytes of RTL:00029..035ba match runtime
 *   DS:0000..3591, with several long exact spans and modified words.
 *
 * The second routine stages only the *unmodified RTL preimage*: the
 * mismatched 260 bytes at 100ms are now reproduced as observed: 27 source-zero
 * segment words, 24 independently observed offset words, two externally
 * segmented FAR-pointer tables and the remaining early initializers below.
 * This does not identify the original loader's implementation. Original DOS MCB captures establish
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
#define GATE_RESIDENT_SOURCE 0x000056deul
#define GATE_RESIDENT_BYTES 0x3d00u
#define GATE_RESIDENT_PARAS 0x03d0u

/* The original RTL:56de..93dd bank is present at linked relative segment
 * 02a5 by 125ms. Copy only the *raw* bank; the original snapshot differs
 * at relocated words, so executing this image before relocation is unsafe.
 * The caller supplies its own allocated segment and retains the file handle.
 * -1 bad destination, -2 bad seek, -3 failed/short read. On -3 discard the
 * allocation: it may contain a partial code image. */
int gate_dos_load_intro_resident_bank(uint16_t rtl_handle,
                                      uint16_t resident_segment,
                                      uint16_t paragraphs)
{
    GateDosFileResult result;
    GateFarPtr target;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    result = gate_dos_file_seek(rtl_handle, 0u, GATE_RESIDENT_SOURCE);
    if (result.carry || result.ax != 0x56deu || result.dx != 0u)
        return -2;
    target.segment = resident_segment;
    target.offset = 0u;
    result = gate_dos_file_read(rtl_handle, target, GATE_RESIDENT_BYTES);
    if (result.carry || result.ax != GATE_RESIDENT_BYTES)
        return -3;
    return 0;
}

/* These 98 CALL FAR instructions have original source operands 0000:0000
 * and independently match the same resident segment in 125, 175, 250 and
 * 500ms snapshots. Only the target *offsets* are fixed; the caller supplies
 * the new linked resident segment. A few targets extend beyond this raw
 * 3d00h bank, so this does not make the bank safe to execute by itself. */
typedef struct GateResidentSelfCall {
    uint16_t site;
    uint16_t target;
} GateResidentSelfCall;
static const GateResidentSelfCall gate_resident_self_calls[] = {
    {0x09d6u, 0x0bdeu}, {0x09deu, 0x0ea1u}, {0x0a26u, 0x0a7cu},
    {0x0a2du, 0x0dfau}, {0x0a32u, 0x0c52u}, {0x0a53u, 0x0b40u},
    {0x0a68u, 0x0bdeu}, {0x0a6du, 0x0ea1u}, {0x0b6cu, 0x0c2cu},
    {0x0be5u, 0x0ea1u}, {0x0bf9u, 0x0ea1u}, {0x0c3fu, 0x0bdeu},
    {0x0c48u, 0x0ea1u}, {0x0ea8u, 0x0e76u}, {0x0f85u, 0x17dau},
    {0x0fb0u, 0x15f4u}, {0x0fc1u, 0x2352u}, {0x0fddu, 0x2b84u},
    {0x1002u, 0x2b3eu}, {0x101cu, 0x2c92u}, {0x1029u, 0x3b74u},
    {0x1068u, 0x17dau}, {0x1092u, 0x22c2u}, {0x10b1u, 0x16c2u},
    {0x10d5u, 0x3c5cu}, {0x1109u, 0x2e12u}, {0x1152u, 0x133cu},
    {0x1190u, 0x2e12u}, {0x1208u, 0x3a22u}, {0x1254u, 0x133cu},
    {0x12deu, 0x133cu}, {0x1306u, 0x3d60u}, {0x13ebu, 0x259cu},
    {0x14ddu, 0x2d66u}, {0x1590u, 0x2686u}, {0x15b1u, 0x2372u},
    {0x15d6u, 0x2686u}, {0x1610u, 0x283cu}, {0x165fu, 0x2851u},
    {0x1764u, 0x23ecu}, {0x1831u, 0x2686u}, {0x186eu, 0x0c08u},
    {0x1cf1u, 0x2caeu}, {0x1d06u, 0x2bbau}, {0x1f9eu, 0x1436u},
    {0x1fdcu, 0x1436u}, {0x2048u, 0x1436u}, {0x20dfu, 0x2bbau},
    {0x2345u, 0x1436u}, {0x26dfu, 0x27d0u}, {0x2882u, 0x27f6u},
    {0x28aeu, 0x2a7au}, {0x2a66u, 0x2a7au}, {0x2cf6u, 0x2bbau},
    {0x2d1bu, 0x2bbau}, {0x2d44u, 0x2bfcu}, {0x2dfau, 0x0f2eu},
    {0x2ea4u, 0x3158u}, {0x2ebdu, 0x17dau}, {0x2ee0u, 0x2372u},
    {0x2f49u, 0x2372u}, {0x2fbbu, 0x2372u}, {0x2fd5u, 0x2372u},
    {0x303du, 0x3c90u}, {0x307cu, 0x3bc0u}, {0x3083u, 0x3c5cu},
    {0x30dau, 0x2372u}, {0x3103u, 0x259cu}, {0x3183u, 0x2372u},
    {0x32aau, 0x2372u}, {0x331cu, 0x2372u}, {0x33afu, 0x1868u},
    {0x33dbu, 0x1436u}, {0x342eu, 0x1868u}, {0x3459u, 0x1436u},
    {0x34a5u, 0x0f2eu}, {0x34dfu, 0x35dau}, {0x34f2u, 0x3bc0u},
    {0x350cu, 0x346au}, {0x3563u, 0x346au}, {0x359au, 0x37a0u},
    {0x35bbu, 0x36beu}, {0x35f6u, 0x2cd0u}, {0x3625u, 0x2bd2u},
    {0x363du, 0x2c38u}, {0x3647u, 0x3c5cu}, {0x3697u, 0x2bd2u},
    {0x37c7u, 0x3c5cu}, {0x37fau, 0x3c5cu}, {0x3816u, 0x3c5cu},
    {0x383eu, 0x3c5cu}, {0x3871u, 0x35dau}, {0x38a2u, 0x36beu},
    {0x38dau, 0x34d8u}, {0x38f8u, 0x3c5cu}, {0x3921u, 0x34d8u},
    {0x3972u, 0x2e12u}, {0x3b1cu, 0x3c5cu}
};

/* All 98 original zero-operand instructions must match before any write.
 * -1 invalid destination, -2 mismatched/unrelocated source instruction. */
int gate_dos_relocate_intro_resident_self_calls(uint16_t resident_segment,
                                                uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        if (gate_far_byte(resident_segment, site) != 0x9au ||
            gate_far_word(resident_segment, (uint16_t)(site + 1u)) != 0u ||
            gate_far_word(resident_segment, (uint16_t)(site + 3u)) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_self_calls /
                      sizeof gate_resident_self_calls[0]; ++i) {
        uint16_t site = gate_resident_self_calls[i].site;
        gate_far_set_word(resident_segment, (uint16_t)(site + 1u),
                         gate_resident_self_calls[i].target);
        gate_far_set_word(resident_segment, (uint16_t)(site + 3u),
                         resident_segment);
    }
    return 0;
}

/* Seven original MOV AX,0000 / MOV DS,AX sequences are linked with the
 * original data segment (2a7ah in the captured run). The replacement DOS
 * executable needs the freshly allocated data segment, not that address.
 * Sites name the immediate WORD, one byte after the B8 opcode. */
static const uint16_t gate_resident_ds_loads[] = {
    0x020eu, 0x0339u, 0x037cu, 0x059du, 0x05e0u, 0x07c8u, 0x0a59u
};

int gate_dos_relocate_intro_resident_ds_loads(uint16_t resident_segment,
                                               uint16_t paragraphs,
                                               uint16_t data_segment)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS || data_segment == 0u)
        return -1;
    for (i = 0u; i < sizeof gate_resident_ds_loads /
                      sizeof gate_resident_ds_loads[0]; ++i) {
        uint16_t site = gate_resident_ds_loads[i];
        if (gate_far_byte(resident_segment, (uint16_t)(site - 1u)) != 0xb8u ||
            gate_far_word(resident_segment, site) != 0u ||
            gate_far_byte(resident_segment, (uint16_t)(site + 2u)) != 0x8eu ||
            gate_far_byte(resident_segment, (uint16_t)(site + 3u)) != 0xd8u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_ds_loads /
                      sizeof gate_resident_ds_loads[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_ds_loads[i], data_segment);
    return 0;
}

/* The one remaining zero-operand CALL FAR in this bank is at +0a4d and
 * targets original load-segment:0030 (1000:0030 in the observed run).
 * It is an EXE-side boundary, not a resident self-call. The replacement
 * executable must supply its OWN equivalent FAR routine address; original
 * 1000h/0030h is not valid for the rebuilt Microsoft C image. */
int gate_dos_relocate_intro_exe_call(uint16_t resident_segment,
                                     uint16_t paragraphs, GateFarPtr target)
{
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS || target.segment == 0u)
        return -1;
    if (gate_far_byte(resident_segment, 0x0a4du) != 0x9au ||
        gate_far_word(resident_segment, 0x0a4eu) != 0u ||
        gate_far_word(resident_segment, 0x0a50u) != 0u)
        return -2;
    gate_far_set_word(resident_segment, 0x0a4eu, target.offset);
    gate_far_set_word(resident_segment, 0x0a50u, target.segment);
    return 0;
}

/* Another seven stable startup operands are direct references to the linked
 * data or resident segment, but not the MOV AX / MOV DS sequences above:
 * three CMP WORD PTR [BP+0a],imm16, two MOV DX,imm16, one MOV DI,imm16,
 * and a MOV ES:[0072],imm16 storing resident CS. Every surrounding opcode
 * is verified in the original 125..500ms captures. This cluster does not
 * classify unrelated words which happen to equal the same segment value. */
static const uint16_t gate_resident_cmp_ds[] = {
    0x14c8u, 0x14d6u, 0x14f7u
};
static const uint16_t gate_resident_mov_dx_ds[] = {
    0x1502u, 0x150fu
};

int gate_dos_relocate_intro_resident_direct_segments(uint16_t resident_segment,
                                                      uint16_t paragraphs,
                                                      uint16_t data_segment)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS || data_segment == 0u)
        return -1;
    for (i = 0u; i < sizeof gate_resident_cmp_ds /
                      sizeof gate_resident_cmp_ds[0]; ++i) {
        uint16_t site = gate_resident_cmp_ds[i];
        if (gate_far_byte(resident_segment, (uint16_t)(site - 3u)) != 0x81u ||
            gate_far_byte(resident_segment, (uint16_t)(site - 2u)) != 0x7eu ||
            gate_far_byte(resident_segment, (uint16_t)(site - 1u)) != 0x0au ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_mov_dx_ds /
                      sizeof gate_resident_mov_dx_ds[0]; ++i) {
        uint16_t site = gate_resident_mov_dx_ds[i];
        if (gate_far_byte(resident_segment, (uint16_t)(site - 1u)) != 0xbau ||
            gate_far_byte(resident_segment, (uint16_t)(site - 4u)) != 0xb8u ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    if (gate_far_byte(resident_segment, 0x09b8u) != 0xbfu ||
        gate_far_word(resident_segment, 0x09b9u) != 0u ||
        gate_far_byte(resident_segment, 0x09bbu) != 0x8bu ||
        gate_far_byte(resident_segment, 0x09bcu) != 0x36u ||
        gate_far_byte(resident_segment, 0x0197u) != 0x26u ||
        gate_far_byte(resident_segment, 0x0198u) != 0xc7u ||
        gate_far_byte(resident_segment, 0x0199u) != 0x06u ||
        gate_far_word(resident_segment, 0x019au) != 0x0072u ||
        gate_far_word(resident_segment, 0x019cu) != 0u)
        return -2;
    for (i = 0u; i < sizeof gate_resident_cmp_ds /
                      sizeof gate_resident_cmp_ds[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_cmp_ds[i], data_segment);
    for (i = 0u; i < sizeof gate_resident_mov_dx_ds /
                      sizeof gate_resident_mov_dx_ds[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_mov_dx_ds[i], data_segment);
    gate_far_set_word(resident_segment, 0x09b9u, data_segment);
    gate_far_set_word(resident_segment, 0x019cu, resident_segment);
    return 0;
}

/* The original Microsoft C large-model far-memory helpers adjust segment
 * registers by 1000h paragraphs (64KiB) when the 16-bit offset wraps.
 * These are arithmetic constants, NOT the original EXE load segment: all
 * four 125..500ms RAM captures contain 1000h at the same thirteen source-
 * zero instruction operands. Check the opcode context at each site before
 * writing, without changing another instruction on preflight failure. */
typedef struct GateResidentParagraphFixup {
    uint16_t site;
    uint8_t opcode;
} GateResidentParagraphFixup;
static const GateResidentParagraphFixup gate_resident_paragraph_fixups[] = {
    {0x113du, 0x81u}, {0x1170u, 0x81u},
    {0x1273u, 0x25u}, {0x12cbu, 0x81u}, {0x131du, 0x25u},
    {0x398du, 0x05u}, {0x3998u, 0x05u},
    {0x39c3u, 0x2du}, {0x39cfu, 0x2du},
    {0x3a06u, 0x05u}, {0x3a11u, 0x05u},
    {0x3a64u, 0x05u}, {0x3a6fu, 0x05u}
};

int gate_dos_relocate_intro_resident_paragraphs(uint16_t resident_segment,
                                                  uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_paragraph_fixups /
                      sizeof gate_resident_paragraph_fixups[0]; ++i) {
        uint16_t site = gate_resident_paragraph_fixups[i].site;
        uint8_t opcode = gate_resident_paragraph_fixups[i].opcode;
        uint16_t opcode_site = (uint16_t)(site - (opcode == 0x81u ? 3u : 1u));
        if (gate_far_byte(resident_segment, opcode_site) != opcode ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
        if (opcode == 0x81u) {
            if (site == 0x1170u) {
                if (gate_far_byte(resident_segment, (uint16_t)(site - 2u)) != 0x46u ||
                    gate_far_byte(resident_segment, (uint16_t)(site - 1u)) != 0x08u)
                    return -2;
            } else if (gate_far_byte(resident_segment, (uint16_t)(site - 4u)) != 0x26u ||
                       gate_far_byte(resident_segment, (uint16_t)(site - 2u)) != 0x47u ||
                       gate_far_byte(resident_segment, (uint16_t)(site - 1u)) != 0x02u) {
                return -2;
            }
        }
    }
    for (i = 0u; i < sizeof gate_resident_paragraph_fixups /
                      sizeof gate_resident_paragraph_fixups[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_paragraph_fixups[i].site,
                         0x1000u);
    return 0;
}

/* Twenty-four absolute DS-offset operands refer to the same byte-level
 * resident runtime field at DS:2bf0h. All are 0000h in RTL:56de..93dd and
 * become 2bf0h at 125,175,250 and 500ms. They are offset operands, so
 * unlike the separately reconstructed DS-segment words they do NOT change
 * when the rebuilt DOS program is loaded at another segment. Each saved
 * prefix is the exact original two bytes before the operand. */
typedef struct GateResidentDataOffsetFixup {
    uint16_t site;
    uint16_t prefix;
} GateResidentDataOffsetFixup;
static const GateResidentDataOffsetFixup gate_resident_flag_offsets[] = {
    {0x0b01u, 0xbff7u}, {0x0b16u, 0xa780u}, {0x0b27u, 0x8f80u},
    {0x0b88u, 0x87f6u}, {0x15a3u, 0x87f6u}, {0x236cu, 0x87c6u},
    {0x23e6u, 0xa780u}, {0x257fu, 0x8f88u}, {0x25bau, 0x87f6u},
    {0x25d3u, 0x87f6u}, {0x25dau, 0xa780u}, {0x25f7u, 0x8f80u},
    {0x2606u, 0x8f80u}, {0x262bu, 0x87f6u}, {0x269eu, 0x87f6u},
    {0x26b0u, 0x87f6u}, {0x276cu, 0x87f6u}, {0x27b9u, 0x87f6u},
    {0x2d79u, 0x87f6u}, {0x30a5u, 0x87f6u}, {0x30afu, 0x87f6u},
    {0x31f3u, 0x87f6u}, {0x3299u, 0x87f6u}, {0x3344u, 0x87f6u}
};

int gate_dos_relocate_intro_resident_flag_offsets(uint16_t resident_segment,
                                                   uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_flag_offsets /
                      sizeof gate_resident_flag_offsets[0]; ++i) {
        uint16_t site = gate_resident_flag_offsets[i].site;
        if (gate_far_word(resident_segment, (uint16_t)(site - 2u)) !=
                gate_resident_flag_offsets[i].prefix ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_flag_offsets /
                      sizeof gate_resident_flag_offsets[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_flag_offsets[i].site,
                         0x2bf0u);
    return 0;
}

/* Two additional source-zero DS-offset groups remain fixed under the
 * original loader's segment placement: 17 at 3244h (MOV/ SUB immediates)
 * and 13 at 3334h (byte TEST displacements / ADD immediates). Four settled
 * RAM captures agree at every site. The C source does not assign a semantic
 * type to either original data field. All sites in BOTH groups are checked
 * before any operand is changed. */
static const GateResidentDataOffsetFixup gate_resident_state_3244[] = {
    {0x0f93u, 0x2d06u}, {0x1050u, 0xb856u}, {0x1103u, 0xb87du},
    {0x118au, 0xb824u}, {0x1347u, 0x2d06u}, {0x139eu, 0x2dc3u},
    {0x144cu, 0x2dc3u}, {0x14a7u, 0x2dc3u}, {0x1547u, 0x2dc3u},
    {0x1642u, 0x2d04u}, {0x1787u, 0x2dc3u}, {0x17fau, 0x2dc3u},
    {0x22c9u, 0xb804u}, {0x2e7au, 0x2dc3u}, {0x2f0bu, 0x2dc3u},
    {0x31a9u, 0x2dc3u}, {0x3328u, 0x2d06u}
};
static const GateResidentDataOffsetFixup gate_resident_state_3334[] = {
    {0x111eu, 0x87f6u}, {0x11a5u, 0x87f6u}, {0x1358u, 0x05e0u},
    {0x13b0u, 0x87f6u}, {0x145du, 0x05e0u}, {0x14b9u, 0x87f6u},
    {0x1559u, 0x87f6u}, {0x1653u, 0x05e0u}, {0x1798u, 0x05e0u},
    {0x180cu, 0x87f6u}, {0x2e8bu, 0x05e0u}, {0x2f1du, 0x87f6u},
    {0x31bbu, 0x87f6u}
};

static int gate_dos_check_resident_state_offsets(uint16_t segment,
                                                 const GateResidentDataOffsetFixup *sites,
                                                 unsigned int count)
{
    unsigned int i;
    for (i = 0u; i < count; ++i)
        if (gate_far_word(segment, (uint16_t)(sites[i].site - 2u)) != sites[i].prefix ||
            gate_far_word(segment, sites[i].site) != 0u)
            return -2;
    return 0;
}

int gate_dos_relocate_intro_resident_state_offsets(uint16_t resident_segment,
                                                    uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    if (gate_dos_check_resident_state_offsets(resident_segment,
                                              gate_resident_state_3244,
                                              sizeof gate_resident_state_3244 /
                                              sizeof gate_resident_state_3244[0]) != 0 ||
        gate_dos_check_resident_state_offsets(resident_segment,
                                              gate_resident_state_3334,
                                              sizeof gate_resident_state_3334 /
                                              sizeof gate_resident_state_3334[0]) != 0)
        return -2;
    for (i = 0u; i < sizeof gate_resident_state_3244 /
                      sizeof gate_resident_state_3244[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_state_3244[i].site, 0x3244u);
    for (i = 0u; i < sizeof gate_resident_state_3334 /
                      sizeof gate_resident_state_3334[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_state_3334[i].site, 0x3334u);
    return 0;
}

/* The original 02a5 bank contains a 180-site cluster of linked absolute
 * data offsets sharing the same 62dah addend. The original source operands
 * range from 0000h through 0194h. Every site matches the same addend and
 * exact original two-byte instruction prefix in all four settled captures.
 * The addend is DS-relative, not a load-segment relocation. Entries name
 * only offsets, original source words and opcode guards, not machine code. */
typedef struct GateResidentSmallOffsetFixup {
    uint16_t site;
    uint16_t original;
    uint16_t prefix;
} GateResidentSmallOffsetFixup;
static const GateResidentSmallOffsetFixup gate_resident_small_offsets[] = {
    {0x1874u, 0x0034u, 0xb800u}, {0x1877u, 0x002eu, 0xa300u}, {0x187bu, 0x0030u, 0x1e8cu},
    {0x1884u, 0x001cu, 0xa310u}, {0x1888u, 0x001eu, 0x1689u}, {0x1891u, 0x0002u, 0xa308u},
    {0x1895u, 0x0004u, 0x1689u}, {0x1899u, 0x0028u, 0x06c7u}, {0x189fu, 0x0026u, 0x06c7u},
    {0x18b1u, 0x002au, 0x06c7u}, {0x18b8u, 0x000cu, 0xa3c0u}, {0x18bbu, 0x0008u, 0xa300u},
    {0x18beu, 0x0024u, 0xa300u}, {0x18c1u, 0x000au, 0xa300u}, {0x18c4u, 0x0022u, 0xa300u},
    {0x18c7u, 0x0020u, 0xa300u}, {0x18cau, 0x0006u, 0xa300u}, {0x18cdu, 0x0000u, 0xa300u},
    {0x18d0u, 0x001au, 0xa300u}, {0x18d4u, 0x0194u, 0x06c7u}, {0x18e7u, 0x0194u, 0x06c7u},
    {0x18f9u, 0x000cu, 0x06ffu}, {0x18fdu, 0x0020u, 0x06c7u}, {0x190cu, 0x000cu, 0x3e83u},
    {0x1913u, 0x0020u, 0x06ffu}, {0x191au, 0x0000u, 0x06ffu}, {0x1929u, 0x001au, 0x06ffu},
    {0x1948u, 0x0032u, 0xb80au}, {0x195bu, 0x0032u, 0x3e83u}, {0x1962u, 0x001au, 0x06ffu},
    {0x1965u, 0x0032u, 0xa100u}, {0x196au, 0x0032u, 0xa3d8u}, {0x1977u, 0x0022u, 0x06ffu},
    {0x1981u, 0x002au, 0xb80au}, {0x1994u, 0x002au, 0x3e83u}, {0x199bu, 0x002au, 0x06c7u},
    {0x19a1u, 0x0022u, 0x0effu}, {0x19c0u, 0x000au, 0x06c7u}, {0x19c6u, 0x000au, 0x3e83u},
    {0x19e6u, 0x000au, 0x06c7u}, {0x19eeu, 0x000au, 0x06c7u}, {0x19f6u, 0x000au, 0x06c7u},
    {0x1a14u, 0x0008u, 0x06ffu}, {0x1a32u, 0x001cu, 0x1ec4u}, {0x1a38u, 0x0026u, 0xa11fu},
    {0x1a3fu, 0x001cu, 0x0683u}, {0x1a48u, 0x0024u, 0x06ffu}, {0x1a4cu, 0x0000u, 0x06c7u},
    {0x1a66u, 0x0006u, 0x06ffu}, {0x1a6au, 0x0008u, 0x06ffu}, {0x1a6eu, 0x0022u, 0x3e83u},
    {0x1a75u, 0x002cu, 0x06c7u}, {0x1a7eu, 0x002cu, 0x06c7u}, {0x1a84u, 0x0022u, 0x06ffu},
    {0x1a88u, 0x002au, 0x06c7u}, {0x1a8eu, 0x000au, 0x3e83u}, {0x1a99u, 0x000au, 0xa3c0u},
    {0x1aa0u, 0x0032u, 0x0639u}, {0x1aa5u, 0x0032u, 0xa127u}, {0x1aacu, 0x001au, 0x3e83u},
    {0x1ab3u, 0x0032u, 0x06c7u}, {0x1abcu, 0x0032u, 0x2e83u}, {0x1ac0u, 0x0032u, 0xa105u},
    {0x1ac9u, 0x0032u, 0xa3c0u}, {0x1acdu, 0x001cu, 0x0683u}, {0x1aeeu, 0x001au, 0x3e83u},
    {0x1afau, 0x0032u, 0xa300u}, {0x1b03u, 0x0032u, 0xa3c0u}, {0x1b0au, 0x0032u, 0x06c7u},
    {0x1b10u, 0x001cu, 0x2e83u}, {0x1b20u, 0x001cu, 0x0683u}, {0x1b48u, 0x000au, 0x3e83u},
    {0x1b94u, 0x0028u, 0x3e83u}, {0x1b9bu, 0x0026u, 0x3e83u}, {0x1ba2u, 0x0002u, 0x1ec4u},
    {0x1bf9u, 0x0026u, 0x3e83u}, {0x1c00u, 0x0002u, 0x1ec4u}, {0x1c0fu, 0x0026u, 0xa103u},
    {0x1c26u, 0x0024u, 0x06ffu}, {0x1c2au, 0x000au, 0x3e83u}, {0x1c31u, 0x000au, 0x3e83u},
    {0x1c38u, 0x001cu, 0x1ec4u}, {0x1c49u, 0x001cu, 0x0683u}, {0x1c50u, 0x0024u, 0x3e83u},
    {0x1c57u, 0x001cu, 0x1ec4u}, {0x1c68u, 0x001cu, 0x1ec4u}, {0x1c76u, 0x001cu, 0x0683u},
    {0x1c7bu, 0x0000u, 0x3e83u}, {0x1c91u, 0x0192u, 0xa3c0u}, {0x1c94u, 0x002eu, 0xa101u},
    {0x1c98u, 0x0030u, 0x168bu}, {0x1ca2u, 0x0024u, 0x3e83u}, {0x1cdeu, 0x000eu, 0xb800u},
    {0x1cfbu, 0x0022u, 0x3e83u}, {0x1d10u, 0x002au, 0x0e8bu}, {0x1d33u, 0x0008u, 0x0e8bu},
    {0x1d63u, 0x0024u, 0x3e83u}, {0x1d69u, 0x000cu, 0xa114u}, {0x1d6du, 0x0020u, 0x060bu},
    {0x1d9eu, 0x001cu, 0xa100u}, {0x1da2u, 0x001eu, 0x168bu}, {0x1dacu, 0x001cu, 0x0683u},
    {0x1db4u, 0x000au, 0x3e83u}, {0x1dbbu, 0x001cu, 0x1ec4u}, {0x1dccu, 0x001cu, 0x0683u},
    {0x1dd4u, 0x001cu, 0x1ec4u}, {0x1de4u, 0x001cu, 0x0683u}, {0x1de9u, 0x000au, 0x3e83u},
    {0x1e1bu, 0x0022u, 0x3639u}, {0x1e21u, 0x002au, 0x0e8bu}, {0x1e49u, 0x0032u, 0x3e8bu},
    {0x1e4fu, 0x001au, 0x3e83u}, {0x1e6cu, 0x001au, 0x3e83u}, {0x1e87u, 0x001cu, 0xa106u},
    {0x1e8bu, 0x001eu, 0x168bu}, {0x1eabu, 0x0022u, 0x3e83u}, {0x1eb2u, 0x002au, 0x06c7u},
    {0x1ebeu, 0x002au, 0x3e83u}, {0x1ec5u, 0x002au, 0x06c7u}, {0x1ecbu, 0x0008u, 0x36ffu},
    {0x1ecfu, 0x002au, 0x36ffu}, {0x1ed6u, 0x0030u, 0x36ffu}, {0x1edau, 0x002eu, 0x36ffu},
    {0x1ef6u, 0x0000u, 0x3e83u}, {0x1efdu, 0x0030u, 0x36ffu}, {0x1f01u, 0x002eu, 0x36ffu},
    {0x1f11u, 0x0000u, 0x3e83u}, {0x1f18u, 0x002au, 0x3e83u}, {0x1f1fu, 0x0030u, 0x36ffu},
    {0x1f23u, 0x002eu, 0x36ffu}, {0x1f33u, 0x001cu, 0x0683u}, {0x1f38u, 0x0192u, 0x06c7u},
    {0x1f3du, 0x000cu, 0xa100u}, {0x1f41u, 0x0020u, 0x060bu}, {0x1f72u, 0x0028u, 0x3e83u},
    {0x1f79u, 0x0002u, 0x1ec4u}, {0x1f98u, 0x0004u, 0x36ffu}, {0x1fabu, 0x0028u, 0x06ffu},
    {0x1fb2u, 0x0026u, 0x06ffu}, {0x1fc2u, 0x0028u, 0x3e83u}, {0x1fd2u, 0x0004u, 0x36ffu},
    {0x1fd6u, 0x0002u, 0x36ffu}, {0x1fdau, 0x0194u, 0x36ffu}, {0x1fe9u, 0x0028u, 0x06ffu},
    {0x1ff4u, 0x0002u, 0x1ec4u}, {0x1ffdu, 0x0194u, 0xa0d4u}, {0x2012u, 0x0028u, 0x3e83u},
    {0x201cu, 0x0026u, 0x0601u}, {0x2031u, 0x0028u, 0x3e83u}, {0x203au, 0x0004u, 0x36ffu},
    {0x203eu, 0x0002u, 0x36ffu}, {0x2055u, 0x0028u, 0x06ffu}, {0x2063u, 0x0002u, 0x1ec4u},
    {0x2073u, 0x0002u, 0x1ec4u}, {0x2088u, 0x0028u, 0x3e83u}, {0x2092u, 0x0026u, 0x0601u},
    {0x20a2u, 0x002eu, 0xa156u}, {0x20a6u, 0x0030u, 0x168bu}, {0x20b8u, 0x0194u, 0x3e83u},
    {0x20bfu, 0x0022u, 0x0639u}, {0x20c5u, 0x0006u, 0x0639u}, {0x20cbu, 0x002cu, 0x0639u},
    {0x20d1u, 0x0194u, 0x06c7u}, {0x20d7u, 0x0032u, 0x368bu}, {0x20f1u, 0x001au, 0x3e83u},
    {0x2101u, 0x0194u, 0x3e83u}, {0x211au, 0x0194u, 0x3e83u}, {0x2125u, 0x001au, 0x3e83u},
    {0x2139u, 0x0192u, 0x3e83u}, {0x2147u, 0x001au, 0x3e83u}, {0x2166u, 0x0192u, 0x3e83u},
    {0x2187u, 0x001au, 0x3e83u}, {0x218eu, 0x0194u, 0x06c7u}, {0x21a2u, 0x000cu, 0x3e83u},
    {0x21c5u, 0x0192u, 0x3e83u}, {0x21ccu, 0x0008u, 0x3e83u}, {0x21fau, 0x001cu, 0x1ec4u},
    {0x2201u, 0x001cu, 0x0683u}, {0x2230u, 0x0022u, 0x3639u}, {0x223au, 0x0194u, 0x06c7u}
};

int gate_dos_relocate_intro_resident_small_offsets(uint16_t resident_segment,
                                                    uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_small_offsets /
                      sizeof gate_resident_small_offsets[0]; ++i) {
        uint16_t site = gate_resident_small_offsets[i].site;
        if (gate_far_word(resident_segment, (uint16_t)(site - 2u)) !=
                gate_resident_small_offsets[i].prefix ||
            gate_far_word(resident_segment, site) !=
                gate_resident_small_offsets[i].original)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_small_offsets /
                      sizeof gate_resident_small_offsets[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_small_offsets[i].site,
                         (uint16_t)(gate_resident_small_offsets[i].original + 0x62dau));
    return 0;
}

/* CS:1a2b's indirect JMP [BX+disp16] indexes this 22-WORD local near-
 * target table. In the original RTL source it starts at bank +1b66 with
 * word offsets in the 01c4..02de range, while all four settled DOS RAM
 * captures contain the same entries plus 1868h. The JMP's displacement
 * similarly changes from 02feh to the table address 1b66h. This is a
 * separate near-control-flow fixup, not a DS-global pointer relocation. */
static const uint16_t gate_resident_dispatch_source_words[] = {
    0x02ceu, 0x01e2u, 0x02d4u, 0x02d4u, 0x02d4u, 0x02deu,
    0x01e2u, 0x02deu, 0x02deu, 0x02deu, 0x02deu, 0x01c8u,
    0x01f6u, 0x01fcu, 0x02deu, 0x02deu, 0x02c4u, 0x02deu,
    0x01deu, 0x02deu, 0x02deu, 0x02beu
};

int gate_dos_relocate_intro_resident_dispatch_table(uint16_t resident_segment,
                                                     uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    if (gate_far_byte(resident_segment, 0x1a2bu) != 0x2eu ||
        gate_far_byte(resident_segment, 0x1a2cu) != 0xffu ||
        gate_far_byte(resident_segment, 0x1a2du) != 0xa7u ||
        gate_far_word(resident_segment, 0x1a2eu) != 0x02feu)
        return -2;
    for (i = 0u; i < sizeof gate_resident_dispatch_source_words /
                      sizeof gate_resident_dispatch_source_words[0]; ++i)
        if (gate_far_word(resident_segment, (uint16_t)(0x1b66u + 2u * i)) !=
            gate_resident_dispatch_source_words[i])
            return -2;
    gate_far_set_word(resident_segment, 0x1a2eu, 0x1b66u);
    for (i = 0u; i < sizeof gate_resident_dispatch_source_words /
                      sizeof gate_resident_dispatch_source_words[0]; ++i)
        gate_far_set_word(resident_segment, (uint16_t)(0x1b66u + 2u * i),
                         (uint16_t)(gate_resident_dispatch_source_words[i] + 0x1868u));
    return 0;
}


/* The early part of the first resident bank directly accesses relocated
 * DS state at 138 original 0000h absolute-memory operands. In the four
 * settled captures each becomes a stable offset between 0500h and 05ffh.
 * These are data-segment OFFSETS, not hard-coded segment addresses. The
 * saved two-byte prefixes verify their original instruction contexts. */
typedef struct GateResidentEarlyOffsetFixup {
    uint16_t site;
    uint16_t target;
    uint16_t prefix;
} GateResidentEarlyOffsetFixup;
static const GateResidentEarlyOffsetFixup gate_resident_early_offsets[] = {
    {0x002du, 0x05abu, 0x1e88u}, {0x003eu, 0x05b0u, 0x06c7u}, {0x0044u, 0x05b2u, 0x06c7u},
    {0x004au, 0x05b4u, 0x06c6u}, {0x0057u, 0x05bau, 0x3e81u}, {0x0062u, 0x05b0u, 0x06c7u},
    {0x0068u, 0x05b2u, 0x06c7u}, {0x0081u, 0x05bau, 0x3e81u}, {0x0089u, 0x05b0u, 0x06c7u},
    {0x008fu, 0x05b2u, 0x06c7u}, {0x00a5u, 0x05b0u, 0x06c7u}, {0x00abu, 0x05b2u, 0x06c7u},
    {0x00e2u, 0x05b0u, 0x16ffu}, {0x00f4u, 0x05b4u, 0x3e80u}, {0x0116u, 0x05aau, 0x3e80u},
    {0x0121u, 0x05b2u, 0x16ffu}, {0x0136u, 0x05c8u, 0x06c6u}, {0x0145u, 0x05c8u, 0x3e80u},
    {0x015au, 0x05bau, 0x3e89u}, {0x016eu, 0x05bcu, 0xa300u}, {0x0175u, 0x05beu, 0xa300u},
    {0x017du, 0x05c0u, 0x0e89u}, {0x0181u, 0x05c2u, 0x1689u}, {0x0189u, 0x05c4u, 0x0e89u},
    {0x018du, 0x05c6u, 0x1689u}, {0x01c3u, 0x05bcu, 0xa1fau}, {0x01cau, 0x05beu, 0xa100u},
    {0x01d9u, 0x05acu, 0xa100u}, {0x01eau, 0x05c0u, 0x0e8bu}, {0x01eeu, 0x05c2u, 0x168bu},
    {0x01f6u, 0x05c4u, 0x0e8bu}, {0x01fau, 0x05c6u, 0x168bu}, {0x0214u, 0x05c8u, 0x0efeu},
    {0x0224u, 0x05aau, 0x06c6u}, {0x022fu, 0x05b5u, 0x06c6u}, {0x0234u, 0x05b4u, 0x06c6u},
    {0x023au, 0x05b4u, 0x3e80u}, {0x0244u, 0x05bau, 0x3e81u}, {0x024fu, 0x05aau, 0x3e80u},
    {0x0256u, 0x05b5u, 0x0e3au}, {0x025cu, 0x05b5u, 0x0e88u}, {0x027eu, 0x05aeu, 0xa1e9u},
    {0x0291u, 0x05aau, 0x3e80u}, {0x02a7u, 0x05b6u, 0xa300u}, {0x02b1u, 0x05b8u, 0xa300u},
    {0x02cfu, 0x05aau, 0x06c6u}, {0x0329u, 0x05b6u, 0x1effu}, {0x0389u, 0x05aau, 0x06c6u},
    {0x039fu, 0x05aau, 0x0686u}, {0x03c8u, 0x05b6u, 0xa1c0u}, {0x03cfu, 0x05b8u, 0xa100u},
    {0x03deu, 0x05acu, 0xa100u}, {0x048eu, 0x05aau, 0x06c6u}, {0x0495u, 0x05b4u, 0x3e83u},
    {0x049fu, 0x05bau, 0x3e81u}, {0x04aau, 0x05aau, 0x3e80u}, {0x04b1u, 0x05b5u, 0x0e3au},
    {0x04b7u, 0x05b5u, 0x0e88u}, {0x04bcu, 0x0558u, 0x068cu}, {0x04c1u, 0x0555u, 0x3689u},
    {0x04c9u, 0x053cu, 0xa32eu}, {0x04ceu, 0x0564u, 0x2688u}, {0x04d9u, 0x05aeu, 0xa1e9u},
    {0x04dfu, 0x057fu, 0xa32eu}, {0x04e8u, 0x0573u, 0xa32eu}, {0x04ecu, 0x05aau, 0x3e80u},
    {0x0500u, 0x0535u, 0xb8c0u}, {0x0508u, 0x05b6u, 0xa300u}, {0x0512u, 0x05b8u, 0xa300u},
    {0x0530u, 0x05aau, 0x06c6u}, {0x054au, 0x053cu, 0xa32eu}, {0x0560u, 0x0555u, 0x3689u},
    {0x0567u, 0x053cu, 0xa32eu}, {0x057du, 0x0573u, 0x0681u}, {0x058du, 0x05b6u, 0x1effu},
    {0x05c4u, 0x0558u, 0xa32eu}, {0x05ccu, 0x0555u, 0x3689u}, {0x05d0u, 0x057fu, 0xa12eu},
    {0x05d7u, 0x0573u, 0xa32eu}, {0x05edu, 0x05aau, 0x06c6u}, {0x0664u, 0x05d3u, 0x168bu},
    {0x066du, 0x05d3u, 0x168bu}, {0x0694u, 0x05d3u, 0x168bu}, {0x06b7u, 0x05d3u, 0x168bu},
    {0x06f0u, 0x05b4u, 0x3e80u}, {0x06fau, 0x05aau, 0x3e80u}, {0x0701u, 0x05b5u, 0x0e3au},
    {0x070du, 0x05b5u, 0x0e88u}, {0x0717u, 0x05ceu, 0x8d8au}, {0x071du, 0x05d3u, 0x168bu},
    {0x073eu, 0x05d5u, 0xa0fau}, {0x0754u, 0x05e2u, 0xa305u}, {0x0760u, 0x05e4u, 0xa302u},
    {0x0793u, 0x05d6u, 0x1688u}, {0x0796u, 0x05d7u, 0xa300u}, {0x079au, 0x05dcu, 0x1e89u},
    {0x079eu, 0x05deu, 0x06c7u}, {0x07aeu, 0x05e0u, 0xa300u}, {0x07b2u, 0x05d6u, 0x162au},
    {0x07b6u, 0x05dbu, 0x1688u}, {0x07bdu, 0x05aau, 0x06c6u}, {0x07ceu, 0x05d3u, 0x168bu},
    {0x07d5u, 0x05dcu, 0xa1ecu}, {0x0827u, 0x05d6u, 0x1688u}, {0x082au, 0x05d7u, 0xa300u},
    {0x082eu, 0x05dcu, 0x1e89u}, {0x0832u, 0x05deu, 0x06c7u}, {0x0842u, 0x05e0u, 0xa300u},
    {0x0846u, 0x05d6u, 0x162au}, {0x084au, 0x05dbu, 0x1688u}, {0x0868u, 0x05dbu, 0x3e80u},
    {0x086fu, 0x05dbu, 0x06feu}, {0x0873u, 0x05e0u, 0x0e8bu}, {0x0877u, 0x05d7u, 0x0e2bu},
    {0x087bu, 0x05d9u, 0x0e89u}, {0x0882u, 0x05dcu, 0x0e29u}, {0x0886u, 0x05deu, 0x1e83u},
    {0x088eu, 0x05deu, 0x0effu}, {0x0892u, 0x05d7u, 0x1e8bu}, {0x0896u, 0x05d9u, 0x0e8bu},
    {0x08adu, 0x05d6u, 0xa002u}, {0x08bfu, 0x05dbu, 0x0efeu}, {0x08c3u, 0x05d6u, 0x06feu},
    {0x08c7u, 0x05d7u, 0x06c7u}, {0x08cdu, 0x05d9u, 0x0e8bu}, {0x08d1u, 0x05d3u, 0x168bu},
    {0x08eau, 0x05d5u, 0xa00au}, {0x08fdu, 0x05e2u, 0xa1c0u}, {0x0903u, 0x05e4u, 0xa107u},
    {0x091eu, 0x05aau, 0x06c6u}, {0x0929u, 0x05d3u, 0x168bu}, {0x0945u, 0x05d3u, 0x168bu},
    {0x095eu, 0x05aau, 0xbbd0u}, {0x0964u, 0x05d3u, 0x168bu}, {0x0987u, 0x05aau, 0x3e80u},
    {0x098eu, 0x05d3u, 0x168bu}, {0x099bu, 0x05aau, 0x3e80u}, {0x09a2u, 0x05d3u, 0x168bu}
};

int gate_dos_relocate_intro_resident_early_offsets(uint16_t resident_segment,
                                                    uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_early_offsets /
                      sizeof gate_resident_early_offsets[0]; ++i) {
        uint16_t site = gate_resident_early_offsets[i].site;
        if (gate_far_word(resident_segment, (uint16_t)(site - 2u)) !=
                gate_resident_early_offsets[i].prefix ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_early_offsets /
                      sizeof gate_resident_early_offsets[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_early_offsets[i].site,
                         gate_resident_early_offsets[i].target);
    return 0;
}

/* CS-prefixed timer/interrupt bookkeeping in the early resident routines.
 * These thirteen zero-source absolute operands resolve to offsets within
 * the resident code segment itself, not the DS:05xx state above. Every
 * value is unchanged across the four settled original RAM captures. */
static const GateResidentEarlyOffsetFixup gate_resident_cs_state_offsets[] = {
    {0x0261u, 0x02f3u, 0x068cu}, {0x0266u, 0x02f0u, 0x3689u},
    {0x026eu, 0x02dbu, 0xa32eu}, {0x0284u, 0x031bu, 0xa32eu},
    {0x028du, 0x030fu, 0xa32eu}, {0x02e5u, 0x02dbu, 0xa32eu},
    {0x02fcu, 0x02f0u, 0x3689u}, {0x0303u, 0x02dbu, 0xa32eu},
    {0x0319u, 0x030fu, 0x0681u}, {0x0360u, 0x02f3u, 0xa32eu},
    {0x0368u, 0x02f0u, 0x3689u}, {0x036cu, 0x031bu, 0xa12eu},
    {0x0373u, 0x030fu, 0xa32eu}
};

int gate_dos_relocate_intro_resident_cs_state(uint16_t resident_segment,
                                              uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_cs_state_offsets /
                      sizeof gate_resident_cs_state_offsets[0]; ++i) {
        uint16_t site = gate_resident_cs_state_offsets[i].site;
        if (gate_far_word(resident_segment, (uint16_t)(site - 2u)) !=
                gate_resident_cs_state_offsets[i].prefix ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_cs_state_offsets /
                      sizeof gate_resident_cs_state_offsets[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_cs_state_offsets[i].site,
                         gate_resident_cs_state_offsets[i].target);
    return 0;
}

/* First resident runtime-service absolute operands, observed after linking
 * in the same four settled DOS snapshots. The original file contains zero
 * WORDs; the relocated DS offsets lie in the 2bxx/2cxx service-data range.
 * The separate DS:2bf0 flag group is intentionally excluded. */
static const GateResidentEarlyOffsetFixup gate_resident_service_offsets[] = {
    {0x09eeu, 0x2b76u, 0x2689u}, {0x09f3u, 0x2b72u, 0x2689u},
    {0x09feu, 0x2b70u, 0xa336u}, {0x0a13u, 0x2be7u, 0x1e8cu},
    {0x0a3bu, 0x2c0cu, 0x36ffu}, {0x0a3fu, 0x2c0au, 0x36ffu},
    {0x0a43u, 0x2c08u, 0x36ffu}, {0x0a47u, 0x2c06u, 0x36ffu},
    {0x0a4bu, 0x2c04u, 0x36ffu}, {0x0a63u, 0x2b74u, 0x06c7u},
    {0x0a79u, 0x2b74u, 0x16ffu}, {0x0a8au, 0x2bd5u, 0x1e89u},
    {0x0a8eu, 0x2bd7u, 0x068cu}, {0x0aa4u, 0x2be7u, 0x068eu},
    {0x0ad2u, 0x2be7u, 0x068eu}, {0x0baeu, 0x2bd5u, 0x16c5u},
    {0x0bb8u, 0x2c16u, 0x3e80u}, {0x0bbfu, 0x2c17u, 0xa01eu},
    {0x0bc3u, 0x2c18u, 0x16c5u}, {0x0becu, 0x2c1eu, 0x3e83u},
    {0x0bf3u, 0x2c1cu, 0x1effu}, {0x0c2au, 0x2c20u, 0x2effu},
    {0x0c54u, 0x2c26u, 0x068fu}, {0x0c58u, 0x2c28u, 0x068fu},
    {0x0c65u, 0x2be7u, 0x068eu}, {0x0c6eu, 0x2c10u, 0x068cu},
    {0x0c81u, 0x2c0eu, 0x3e89u}, {0x0c94u, 0x2be7u, 0x1e8eu},
    {0x0d16u, 0x2c04u, 0x3e89u}, {0x0d2du, 0x2c08u, 0x1e8cu},
    {0x0d41u, 0x2c0eu, 0x36c5u}, {0x0d4fu, 0x2be7u, 0x1e8eu},
    {0x0df7u, 0x2c26u, 0x2effu}, {0x0e00u, 0x2be7u, 0x1e8eu},
    {0x0e42u, 0x2c0au, 0x2e89u}, {0x0e46u, 0x2c0cu, 0x1e8cu},
    {0x0ee8u, 0x2be7u, 0x1e8bu}, {0x0f02u, 0x2b76u, 0x2e8bu},
    {0x0f3du, 0x2be9u, 0x3e80u}
};

int gate_dos_relocate_intro_resident_service_offsets(uint16_t resident_segment,
                                                      uint16_t paragraphs)
{
    unsigned int i;
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS)
        return -1;
    for (i = 0u; i < sizeof gate_resident_service_offsets /
                      sizeof gate_resident_service_offsets[0]; ++i) {
        uint16_t site = gate_resident_service_offsets[i].site;
        if (gate_far_word(resident_segment, (uint16_t)(site - 2u)) !=
                gate_resident_service_offsets[i].prefix ||
            gate_far_word(resident_segment, site) != 0u)
            return -2;
    }
    for (i = 0u; i < sizeof gate_resident_service_offsets /
                      sizeof gate_resident_service_offsets[0]; ++i)
        gate_far_set_word(resident_segment, gate_resident_service_offsets[i].site,
                         gate_resident_service_offsets[i].target);
    return 0;
}

/* Stage the first reconstructed resident bank in the eventual DOS entry.
 * Unlike the data-only bootstrap below, this operation does not establish
 * the remaining resident banks, interrupt vectors, or an executable entry.
 * Failure leaves a partially loaded/relocated allocation: discard it.
 * -1 bad arguments, -2 file load, -3 self calls, -4 DS loads,
 * -5 EXE callback, -6 direct segment operands, -7 paragraph carries,
 * -8 linked DS flag offsets, -9 additional state offsets,
 * -10 linked small data offsets, -11 CS-local dispatch table,
 * -12 early DS-state offsets, -13 early resident CS-state offsets,
 * -14 runtime-service DS offsets. */
int gate_dos_prepare_intro_resident_bank(uint16_t rtl_handle,
                                         uint16_t resident_segment,
                                         uint16_t paragraphs,
                                         uint16_t data_segment,
                                         GateFarPtr exe_callback)
{
    if (resident_segment == 0u || resident_segment > 0xfc30u ||
        paragraphs < GATE_RESIDENT_PARAS || data_segment == 0u ||
        exe_callback.segment == 0u)
        return -1;
    if (gate_dos_load_intro_resident_bank(rtl_handle, resident_segment,
                                          paragraphs) != 0)
        return -2;
    if (gate_dos_relocate_intro_resident_self_calls(resident_segment,
                                                    paragraphs) != 0)
        return -3;
    if (gate_dos_relocate_intro_resident_ds_loads(resident_segment,
                                                  paragraphs, data_segment) != 0)
        return -4;
    if (gate_dos_relocate_intro_exe_call(resident_segment, paragraphs,
                                         exe_callback) != 0)
        return -5;
    if (gate_dos_relocate_intro_resident_direct_segments(resident_segment,
                                                         paragraphs,
                                                         data_segment) != 0)
        return -6;
    if (gate_dos_relocate_intro_resident_paragraphs(resident_segment,
                                                    paragraphs) != 0)
        return -7;
    if (gate_dos_relocate_intro_resident_flag_offsets(resident_segment,
                                                      paragraphs) != 0)
        return -8;
    if (gate_dos_relocate_intro_resident_state_offsets(resident_segment,
                                                       paragraphs) != 0)
        return -9;
    if (gate_dos_relocate_intro_resident_small_offsets(resident_segment,
                                                       paragraphs) != 0)
        return -10;
    if (gate_dos_relocate_intro_resident_dispatch_table(resident_segment,
                                                        paragraphs) != 0)
        return -11;
    if (gate_dos_relocate_intro_resident_early_offsets(resident_segment,
                                                       paragraphs) != 0)
        return -12;
    if (gate_dos_relocate_intro_resident_cs_state(resident_segment,
                                                 paragraphs) != 0)
        return -13;
    if (gate_dos_relocate_intro_resident_service_offsets(resident_segment,
                                                         paragraphs) != 0)
        return -14;
    return 0;
}


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

/* All five original DOS captures at 100,125,175,250 and 500ms agree on
 * this initialized state, while RTL:+0541 and +0570..057f are zero in the
 * verified source image. The eight words have NOT been identified as mode
 * values, callbacks or pointers; reproducing them establishes only the
 * observed early bytes, not their original initialization mechanism. */
static const uint16_t gate_rtl_early_words[] = {
    0x00e0u, 0x00efu, 0x020bu, 0x020bu,
    0x0203u, 0x010bu, 0x00feu, 0x011du
};

int gate_dos_fix_intro_early_bytes(uint16_t lower_segment, uint16_t paragraphs)
{
    unsigned int i;
    if (lower_segment == 0u || lower_segment > 0xfff5u ||
        paragraphs < GATE_DATA_MIN_PARAS ||
        gate_ds_segment() != (uint16_t)(lower_segment + 0x000au))
        return -1;
    if (gate_far_byte(gate_ds_segment(), 0x0518u) != 0u)
        return -2;
    for (i = 0u; i < sizeof gate_rtl_early_words /
                      sizeof gate_rtl_early_words[0]; ++i)
        if (gate_ds_word((uint16_t)(0x0547u + i * 2u)) != 0u)
            return -2;
    gate_far_set_byte(gate_ds_segment(), 0x0518u, 0x15u);
    for (i = 0u; i < sizeof gate_rtl_early_words /
                      sizeof gate_rtl_early_words[0]; ++i)
        gate_ds_set_word((uint16_t)(0x0547u + i * 2u), gate_rtl_early_words[i]);
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
 * entry may run. The early data bytes are restored as observed, but their
 * actual initialization mechanism and resident driver/interrupt setup
 * remain unknown.
 * Return 0 success, -1 bad layout, -2..-11 failure at the indicated step. */
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
    if (gate_dos_fix_intro_early_bytes(lower_segment, paragraphs) != 0)
        return -11;
    return 0;
}

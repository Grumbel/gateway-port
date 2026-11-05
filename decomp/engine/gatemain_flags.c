/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gatemain_flags.h"

static GatemainFarPtr null_far_pointer(void)
{
    GatemainFarPtr ptr = {0, 0};
    return ptr;
}

static uint16_t object_count(void)
{
    /* Original [word DS:8d54]:38dc, not an inline object limit. */
    return gatemain_far_word(gatemain_ds_word(0x8d54), 0x38dc);
}

static uint16_t object_index(uint16_t object)
{
    /* Original signed IMUL 6, with DX discarded. */
    return (uint16_t)(object * 6u);
}

/* GATEMAIN.EXE:0x27db..0x284a. The compiler emits an eight-way CS-relative
 * jump table at file 0x2833, but ALL EIGHT entries branch to 0x2828.
 * Each accepted type 1..8 therefore returns the same FAR state pointer
 * offset by four bytes. The addition never carries into the segment. */
GatemainFarPtr gatemain_exe_27db(uint16_t object)
{
    uint16_t index, directory, type;
    GatemainFarPtr ptr;
    if ((int16_t)object <= 0 || object > object_count())
        return null_far_pointer();

    index = object_index(object);
    directory = gatemain_ds_word(0x8d50);
    ptr.offset = gatemain_far_word(directory, (uint16_t)(index + 0xbcu));
    ptr.segment = gatemain_far_word(directory, (uint16_t)(index + 0xbeu));
    type = (uint16_t)((uint16_t)gatemain_far_byte(directory,
                                                  (uint16_t)(index + 0xbau)) - 1u);
    if (type > 7)
        return null_far_pointer();
    ptr.offset = (uint16_t)(ptr.offset + 4u);
    return ptr;
}

/* GATEMAIN.EXE:0x284b..0x28e3. Compiler-local [BP-6] stores the bit
 * position through the caller's FAR pointer [BP+10] on SUCCESS ONLY.
 * A failed lookup returns 0000:0000 without touching *bit. A property ID
 * is merely required to be nonzero, so the four-byte table index wraps
 * modulo 65536 for an out-of-range 16-bit property. */
GatemainFarPtr gatemain_exe_284b(uint16_t object, uint16_t property,
                                  uint16_t *bit)
{
    GatemainFarPtr ptr;
    uint16_t index, directory, properties, mask, type;
    uint16_t position;

    if ((int16_t)object <= 0 || object > object_count())
        return null_far_pointer();

    ptr = gatemain_exe_27db(object);
    if (property == 0 || (uint16_t)(ptr.offset | ptr.segment) == 0)
        return null_far_pointer();

    index = (uint16_t)(property << 2);
    directory = gatemain_ds_word(0x8d50);
    properties = gatemain_ds_word(0x8d56);
    mask = gatemain_far_word(properties, (uint16_t)(index + 0x16u));
    type = gatemain_far_byte(directory,
                             (uint16_t)(object_index(object) + 0xbau));
    if ((mask & (uint16_t)(1u << (type - 1u))) == 0)
        return null_far_pointer();

    position = gatemain_far_byte(properties, (uint16_t)(index + 0x14u));
    *bit = position & 7u;
    ptr.offset = (uint16_t)(ptr.offset + (position >> 3));
    return ptr;
}

/* GATEMAIN.EXE:0x28e4..0x291d. The shift is 16-bit SHR AX,CL (with CL in
 * 0..7 after a successful resolver), followed by AND AX,1. */
uint16_t gatemain_exe_28e4(uint16_t object, uint16_t property)
{
    uint16_t bit = 0;
    GatemainFarPtr ptr = gatemain_exe_284b(object, property, &bit);
    if ((uint16_t)(ptr.offset | ptr.segment) == 0)
        return 0;
    return (uint16_t)(((unsigned)gatemain_far_byte(ptr.segment, ptr.offset) >> bit) & 1u);
}

/* GATEMAIN.EXE:0x291e..0x2950. Only the selected byte is modified, with
 * OR ES:[BX],AL. AX on return is not a documented C result. */
void gatemain_exe_291e(uint16_t object, uint16_t property)
{
    uint16_t bit = 0;
    GatemainFarPtr ptr = gatemain_exe_284b(object, property, &bit);
    if ((uint16_t)(ptr.offset | ptr.segment) != 0) {
        uint8_t value = gatemain_far_byte(ptr.segment, ptr.offset);
        gatemain_far_set_byte(ptr.segment, ptr.offset,
                               (uint8_t)(value | (uint8_t)(1u << bit)));
    }
}

/* GATEMAIN.EXE:0x2951..0x2985. NOT AL and AND ES:[BX],AL are byte sized;
 * adjacent object flags are unaffected. */
void gatemain_exe_2951(uint16_t object, uint16_t property)
{
    uint16_t bit = 0;
    GatemainFarPtr ptr = gatemain_exe_284b(object, property, &bit);
    if ((uint16_t)(ptr.offset | ptr.segment) != 0) {
        uint8_t value = gatemain_far_byte(ptr.segment, ptr.offset);
        gatemain_far_set_byte(ptr.segment, ptr.offset,
                               (uint8_t)(value & (uint8_t)~(uint8_t)(1u << bit)));
    }
}

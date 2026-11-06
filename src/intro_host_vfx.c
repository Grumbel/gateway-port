/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

#include <stdlib.h>

/* Bounded native implementation of the original linked 24a7:000c VFX
 * bitblitter's 320x200, eight-bit mode-3 handler (24a7:04c4).
 *
 * The original handler operates on an already allocated/decompressed 0xca00
 * image object, NOT a GATE_*.PIC archive entry. It writes directly to VGA
 * segment a000, preserving 16-bit offset wrap and the four original raster
 * operations. This host implementation deliberately rejects other video
 * modes and image plane formats: their original handlers are different.
 *
 * Source evidence: 22-second guest RAM, physical 0x24a7c..0x24b8f
 * (24a7:000c), 0x24f34..0x24fc2 (24a7:04c4), and mode-3 coordinate
 * resolver 0x25313..0x25324 (252c:0053). */
void gate_far_24a7_000c(uint16_t unused, uint16_t y, uint16_t x,
                        uint16_t signed_mode, GateFarPtr image)
{
    uint16_t count;
    uint16_t rows;
    uint16_t stride;
    uint16_t source_offset;
    uint16_t source_segment;
    uint16_t destination;
    int16_t available;
    uint16_t i;
    uint16_t row;

    (void)unused; /* original caller supplies the image origin (0,0) */

    if (gate_ds_word(0x15cc) != 3)
        abort(); /* 1bpp/4bpp original handlers not yet implemented */

    /* Original 244c:0025 validates the CA00 signature before drawing. */
    if (gate_far_word(image.segment, image.offset) != 0xca00)
        return;

    /* In mode 3 the original descriptor is 320x200, eight bits per pixel,
     * with one image plane. Reject a malformed/cross-mode object rather than
     * interpreting its packed data as an eight-bit bitmap. */
    if (gate_far_byte(image.segment, (uint16_t)(image.offset + 0x12u)) != 1 ||
        gate_far_byte(image.segment, (uint16_t)(image.offset + 0x13u)) != 8)
        abort();

    /* Original signed JG/JGE horizontal/vertical clipping at 24a7:009c
     * and 24a7:00dc. There is intentionally no left/top clipping: the
     * original coordinate resolver performs unsigned 16-bit arithmetic. */
    available = (int16_t)(uint16_t)(320u - x);
    if (available <= 0)
        return;
    count = gate_far_word(image.segment, (uint16_t)(image.offset + 0x30u));
    if (available < (int16_t)gate_far_word(image.segment,
                                          (uint16_t)(image.offset + 0x2cu)))
        count = (uint16_t)available; /* 8-bit pixels = one byte each */

    available = (int16_t)(uint16_t)(200u - y);
    if (available <= 0)
        return;
    rows = gate_far_word(image.segment, (uint16_t)(image.offset + 0x2eu));
    if (available < (int16_t)rows)
        rows = (uint16_t)available;
    if (count == 0 || rows == 0)
        return;

    stride = gate_far_word(image.segment, (uint16_t)(image.offset + 0x14u));
    source_offset = gate_far_word(image.segment, (uint16_t)(image.offset + 0x18u));
    source_segment = gate_far_word(image.segment, (uint16_t)(image.offset + 0x1au));

    /* 252c:0053 maps VGA mode 13h (x,y) to A000:(320*y+x). */
    destination = (uint16_t)(320u * y + x);
    for (row = 0; row < rows; ++row) {
        for (i = 0; i < count; ++i) {
            const uint16_t src = (uint16_t)(source_offset + i);
            const uint16_t dst = (uint16_t)(destination + i);
            const uint8_t pixel = gate_far_byte(source_segment, src);
            const uint8_t old = gate_far_byte(0xa000, dst);
            uint8_t result;
            switch (signed_mode) {
            case 0: result = pixel; break;
            case 3: result = (uint8_t)(old ^ pixel); break;
            case 1: result = (uint8_t)(old & pixel); break;
            default: result = (uint8_t)(old | pixel); break;
            }
            gate_far_set_byte(0xa000, dst, result);
        }
        /* The 8086 loop adds (stride - copied) after LODS/REP MOVS;
         * destination likewise adds (320 - copied). Both net +stride
         * and +320, with the original offset-only wraparound. */
        source_offset = (uint16_t)(source_offset + stride);
        destination = (uint16_t)(destination + 320u);
    }
}

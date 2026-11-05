/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* GATE.RTL:0x120f3..0x1212f. The low format nibble indexes the original
 * signed-byte format tags at relocated RTL DS:1480. Compare the tag
 * against the mode-indexed byte at RTL DS:1486+word(mode-segment:0fce). */
uint16_t gate_rtl_120f3(void)
{
    uint16_t header = gate_rtl_image_header_segment();
    uint16_t index = gate_far_byte(header, GATE_RTL_HEADER_OFFSET + 5) & 0x0fu;
    uint16_t mode = gate_far_word(gate_rtl_image_mode_segment(), GATE_RTL_VIDEO_MODE_OFFSET);
    uint8_t tag = gate_rtl_image_data_byte((uint16_t)(GATE_RTL_FORMAT_TAGS_OFFSET + index));
    if (gate_rtl_image_data_byte((uint16_t)(GATE_RTL_FORMAT_MODES_OFFSET + mode)) == tag)
        return (uint16_t)(int16_t)(int8_t)tag;
    return 0;
}

/* GATE.RTL:0x12131..0x121b4. If header bit 0x10 is set, copy 16 or 48
 * bytes from an original mode-specific source into the relocated FAR
 * global (RTL DS:4ddc segment, offset 0fdc). Other format values return. */
void gate_rtl_12131(uint16_t format)
{
    uint16_t header = gate_rtl_image_header_segment();
    GateFarPtr source;
    uint16_t count;
    uint16_t i;
    if ((gate_far_byte(header, GATE_RTL_HEADER_OFFSET + 5) & 0x10u) == 0)
        return;
    if (format == 0xffffu) {
        source = gate_far_0cdc_0430(gate_rtl_image_palette_source());
        count = 16;
    } else if (format == 1) {
        source = gate_far_0cdc_0396(gate_rtl_image_palette_source());
        count = 48;
    } else {
        return;
    }

    for (i = 0; i < count; ++i)
        gate_far_set_byte(gate_rtl_image_transform_segment(),
            (uint16_t)(GATE_RTL_TRANSFORM_OFFSET + i),
            gate_far_byte(source.segment, (uint16_t)(source.offset + i)));
}

/* Two's-complement signed truncation idiom from the original compiler.
 * The intermediate negation and SAR are WORD operations; -32768 remains
 * negative when negated, so this is not always equivalent to C division. */
static uint16_t rtl_signed_quarter(uint16_t value)
{
    uint16_t sign = (value & 0x8000u) ? 0xffffu : 0;
    uint16_t magnitude = (uint16_t)((value ^ sign) - sign);
    uint16_t quotient = (uint16_t)(magnitude >> 2);
    if ((magnitude & 0x8000u) != 0)
        quotient |= 0xc000u; /* original SAR by 2 */
    return (uint16_t)((quotient ^ sign) - sign);
}

/* GATE.RTL:0x121b5..0x12204. Both dispatch variants are deliberately
 * computed from the LOW 16 bits, with the original signed IDIV remainder. */
uint16_t gate_rtl_121b5(uint16_t format, uint16_t value)
{
    if (format == 0xffffu) {
        int16_t remainder = (int16_t)value % 4;
        uint16_t quotient = rtl_signed_quarter(value);
        return (uint16_t)(quotient * 3u + (uint16_t)remainder);
    }
    if (format == 1) {
        int16_t product = (int16_t)(uint16_t)(value << 2);
        return (uint16_t)(product / 3);
    }
    return value;
}

/* GATE.RTL:0x12383..0x123d3. In-place row compaction for mode -1. Copy
 * consecutive rows with signed index remainder != 3; skip every fourth
 * row. Both FAR offsets are advanced as 16-bit WORDs; REP MOVS copies
 * forward and does not advance the segment on offset wrap. */
void gate_rtl_12383(GateFarPtr source, GateFarPtr destination,
                    int16_t first, int16_t last, uint16_t bytes_per_row)
{
    uint16_t row = (uint16_t)first;
    while ((int16_t)row <= last) {
        uint16_t i;
        if ((int16_t)row % 4 != 3) {
            for (i = 0; i < bytes_per_row; ++i) {
                uint8_t b = gate_far_byte(source.segment,
                    (uint16_t)(source.offset + i));
                gate_far_set_byte(destination.segment,
                    (uint16_t)(destination.offset + i), b);
            }
            destination.offset = (uint16_t)(destination.offset + bytes_per_row);
        }
        row = (uint16_t)(row + 1u);
        source.offset = (uint16_t)(source.offset + bytes_per_row);
    }
}

static uint16_t work_word(GateFarPtr work, uint16_t index)
{
    return gate_far_word(work.segment, (uint16_t)(work.offset + index));
}

static void work_set_word(GateFarPtr work, uint16_t index, uint16_t value)
{
    gate_far_set_word(work.segment, (uint16_t)(work.offset + index), value);
}

/* GATE.RTL:0x12205..0x1230e. Original image post-load adjustment:
 * format selection, optional palette-table setup, source offset/extent
 * transform, special signed remainder adjustment, and mode-dependent
 * output path. The format-positive case calls the original print service
 * with relocated RTL DS:148b; the other compacts the buffer in place. */
void gate_rtl_12205(GateFarPtr work)
{
    uint16_t format = gate_rtl_120f3();
    uint16_t starting;
    uint16_t ending;
    uint16_t span;
    uint16_t adjusted_span;
    GateFarPtr data;

    if (format == 0)
        return;
    gate_rtl_12131(format);

    span = (uint16_t)(work_word(work, 0x12) -
                      work_word(work, 0x0e) + 1u);
    starting = work_word(work, 4);
    ending = (uint16_t)(starting + work_word(work, 0x14) -
                        work_word(work, 0x10));
    work_set_word(work, 4, gate_rtl_121b5(format, starting));
    adjusted_span = (uint16_t)(gate_rtl_121b5(format, ending) -
                               work_word(work, 4) + 1u);

    if ((int16_t)format < 0 && (int16_t)ending % 4 == 3) {
        adjusted_span = (uint16_t)(adjusted_span - 1u);
        ending = (uint16_t)(ending - 1u);
    } else if ((int16_t)format > 0 && (int16_t)ending % 3 == 2) {
        adjusted_span = (uint16_t)(adjusted_span + 1u);
    }

    if ((int16_t)format > 0) {
        gate_far_1258_172e((GateFarPtr){gate_rtl_image_data_segment(), GATE_RTL_MESSAGE_OFFSET});
        return;
    }

    data = (GateFarPtr){work.segment, (uint16_t)(work.offset + 8u)};
    {
        GateFarPtr pixels = {work_word(data, 0x1a), work_word(data, 0x18)};
        uint16_t row_bytes = (uint16_t)(work_word(data, 0x14) << 2);
        gate_rtl_12383(pixels, pixels, (int16_t)starting,
                       (int16_t)ending, row_bytes);
    }
    work_set_word(data, 0x0c, (uint16_t)(adjusted_span - 1u));
    work_set_word(data, 0x2e, adjusted_span);
    work_set_word(data, 0x34, (uint16_t)(adjusted_span << 2));
    (void)span; /* original BP-2 local is assigned but never subsequently read */
}

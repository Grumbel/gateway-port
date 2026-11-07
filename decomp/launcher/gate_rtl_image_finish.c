/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#include "gate_rtl_image.h"

/* GATE.RTL:0x120f3..0x1212f. The low format nibble indexes the original
 * signed-byte format tags at relocated RTL DS:1480. Compare the tag
 * against the mode-indexed byte at RTL DS:1486+word(mode-segment:0fce). */
uint16_t gate_rtl_detect_picture_format(void)
{
    uint16_t header = gate_rtl_image_header_segment();
    uint16_t index = gate_far_byte(header, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FLAGS) & GATE_PICTURE_HEADER_FORMAT_MASK;
    uint16_t mode = gate_far_word(gate_rtl_image_mode_segment(), GATE_RTL_VIDEO_MODE_OFFSET);
    uint8_t tag = gate_rtl_image_data_byte((uint16_t)(GATE_RTL_FORMAT_TAGS_OFFSET + index));
    if (gate_rtl_image_data_byte((uint16_t)(GATE_RTL_FORMAT_MODES_OFFSET + mode)) == tag)
        return (uint16_t)(int16_t)(int8_t)tag;
    return 0;
}

/* GATE.RTL:0x12131..0x121b4. If the header palette bit is set, copy 16 or 48
 * bytes from an original mode-specific source into the relocated FAR
 * global (RTL DS:4ddc segment, offset 0fdc). Other format values return. */
void gate_rtl_prepare_picture_palette_translation(uint16_t format)
{
    uint16_t header = gate_rtl_image_header_segment();
    GateFarPtr source;
    uint16_t count;
    uint16_t i;
    if ((gate_far_byte(header, GATE_RTL_HEADER_OFFSET + GATE_PICTURE_HEADER_FLAGS) &
         GATE_PICTURE_HEADER_HAS_PALETTE) == 0)
        return;
    if (format == GATE_PICTURE_FORMAT_TAG_COMPACT_ROWS) {
        source = gate_linked_transform_picture_palette_indexed(gate_rtl_image_palette_source());
        count = GATE_PICTURE_COMPACT_PALETTE_BYTES;
    } else if (format == GATE_PICTURE_FORMAT_TAG_EXPAND_ROWS) {
        source = gate_linked_transform_picture_palette_rgb(gate_rtl_image_palette_source());
        count = GATE_PICTURE_EXPAND_PALETTE_BYTES;
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
    uint16_t sign = (value & GATE_PICTURE_WORD_SIGN_BIT) ? GATE_PICTURE_NEGATION_SIGN_MASK : 0;
    uint16_t magnitude = (uint16_t)((value ^ sign) - sign);
    uint16_t quotient = (uint16_t)(magnitude >> GATE_PICTURE_QUARTER_SHIFT);
    if ((magnitude & GATE_PICTURE_WORD_SIGN_BIT) != 0)
        quotient |= GATE_PICTURE_QUARTER_SAR_SIGN_FILL;
    return (uint16_t)((quotient ^ sign) - sign);
}

/* GATE.RTL:0x121b5..0x12204. Both dispatch variants are deliberately
 * computed from the LOW 16 bits, with the original signed IDIV remainder. */
uint16_t gate_rtl_transform_picture_coordinate(uint16_t format, uint16_t value)
{
    if (format == GATE_PICTURE_FORMAT_TAG_COMPACT_ROWS) {
        int16_t remainder = (int16_t)value % GATE_PICTURE_QUARTER_DIVISOR;
        uint16_t quotient = rtl_signed_quarter(value);
        return (uint16_t)(quotient * GATE_PICTURE_THREE_QUARTERS_MULTIPLIER + (uint16_t)remainder);
    }
    if (format == GATE_PICTURE_FORMAT_TAG_EXPAND_ROWS) {
        int16_t product = (int16_t)(uint16_t)(value << GATE_PICTURE_FOUR_THIRDS_SHIFT);
        return (uint16_t)(product / GATE_PICTURE_FOUR_THIRDS_DIVISOR);
    }
    return value;
}

/* GATE.RTL:0x12383..0x123d3. In-place row compaction for mode -1. Copy
 * consecutive rows with signed index remainder != 3; skip every fourth
 * row. Both FAR offsets are advanced as 16-bit WORDs; REP MOVS copies
 * forward and does not advance the segment on offset wrap. */
void gate_rtl_compact_picture_rows(GateFarPtr source, GateFarPtr destination,
                    int16_t first, int16_t last, uint16_t bytes_per_row)
{
    uint16_t row = (uint16_t)first;
    while ((int16_t)row <= last) {
        uint16_t i;
        if ((int16_t)row % GATE_PICTURE_QUARTER_DIVISOR != GATE_PICTURE_DISCARDED_ROW_REMAINDER) {
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
void gate_rtl_postprocess_picture(GateFarPtr work)
{
    uint16_t format = gate_rtl_detect_picture_format();
    uint16_t starting;
    uint16_t ending;
    uint16_t span;
    uint16_t adjusted_span;
    GateFarPtr data;

    if (format == 0)
        return;
    gate_rtl_prepare_picture_palette_translation(format);

    span = (uint16_t)(work_word(work, GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_RIGHT) -
                      work_word(work, GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_LEFT) + 1u);
    starting = work_word(work, GATE_PICTURE_RECORD_Y);
    ending = (uint16_t)(starting + work_word(work, GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_BOTTOM) -
                        work_word(work, GATE_PICTURE_RECORD_VFX_IMAGE + GATE_VFX_IMAGE_TOP));
    work_set_word(work, GATE_PICTURE_RECORD_Y,
                  gate_rtl_transform_picture_coordinate(format, starting));
    adjusted_span = (uint16_t)(gate_rtl_transform_picture_coordinate(format, ending) -
                               work_word(work, GATE_PICTURE_RECORD_Y) + 1u);

    if ((int16_t)format < 0 &&
        (int16_t)ending % GATE_PICTURE_QUARTER_DIVISOR == GATE_PICTURE_DISCARDED_ROW_REMAINDER) {
        adjusted_span = (uint16_t)(adjusted_span - 1u);
        ending = (uint16_t)(ending - 1u);
    } else if ((int16_t)format > 0 &&
               (int16_t)ending % GATE_PICTURE_FOUR_THIRDS_DIVISOR == GATE_PICTURE_EXPANDED_LAST_ROW_REMAINDER) {
        adjusted_span = (uint16_t)(adjusted_span + 1u);
    }

    if ((int16_t)format > 0) {
        gate_linked_print_intro_diagnostic((GateFarPtr){gate_rtl_image_data_segment(), GATE_RTL_MESSAGE_OFFSET});
        return;
    }

    data = (GateFarPtr){work.segment, (uint16_t)(work.offset + GATE_PICTURE_RECORD_VFX_IMAGE)};
    {
        GateFarPtr pixels = {work_word(data, GATE_VFX_IMAGE_PIXELS_SEGMENT),
                             work_word(data, GATE_VFX_IMAGE_PIXELS_OFFSET)};
        uint16_t row_bytes = (uint16_t)(work_word(data, GATE_VFX_IMAGE_ROW_STRIDE) * GATE_PICTURE_VFX_ROW_STRIDE_FACTOR);
        gate_rtl_compact_picture_rows(pixels, pixels, (int16_t)starting,
                       (int16_t)ending, row_bytes);
    }
    work_set_word(data, GATE_VFX_IMAGE_BOTTOM, (uint16_t)(adjusted_span - 1u));
    work_set_word(data, GATE_VFX_IMAGE_HEIGHT, adjusted_span);
    work_set_word(data, GATE_VFX_IMAGE_PLANE_ROWS, (uint16_t)(adjusted_span * GATE_PICTURE_VFX_PLANE_ROWS_FACTOR));
    (void)span; /* original BP-2 local is assigned but never subsequently read */
}

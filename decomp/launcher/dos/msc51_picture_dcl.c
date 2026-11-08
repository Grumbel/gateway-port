/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Bounded, streaming PKWARE DCL adapter for the 0eec:018a service.
 *
 * This is a portable translation of src/pic.cpp's verified DCL codec,
 * NOT a translation of the unrecovered original DOS decompressor. The
 * original RTL selector leaves an open DOS handle at the compressed body;
 * the original allocator has already supplied the destination buffer.
 * Never fetch extra bytes from a subsequent resource without rewinding.
 * Static scratch is intentional: an /AL DOS stack cannot hold a 4KiB ring.
 */
#include "GATEDOS.H"
#include "RTLIMAC.H"
#include <string.h>

uint16_t gate_rtl_image_mode_segment(void);
uint16_t gate_rtl_image_header_segment(void);
uint16_t gate_rtl_resource_handle_segment(void);

#define DCL_RING 4096u
#define DCL_CHUNK 256u
#define DCL_LIMIT 0x3f000ul

typedef struct DclTable {
    uint16_t count[14];
    uint8_t symbols[256];
} DclTable;

typedef struct DclInput {
    uint16_t handle;
    uint16_t offset;
    uint16_t length;
    uint16_t bit_buffer;
    uint16_t bits_left;
    uint32_t bytes_read;
    uint8_t bad;
} DclInput;

typedef struct DclOutput {
    uint16_t segment;
    uint16_t offset;
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t x;
    uint16_t y;
    uint32_t expected;
    uint32_t produced;
    uint8_t planes[4];
} DclOutput;

static uint8_t dcl_buffer[DCL_CHUNK];
static uint8_t dcl_ring[DCL_RING];
static DclTable dcl_tables[3];

/* Each packed item encodes (run length - 1) in its top nibble and
 * canonical Huffman code width in its bottom nibble. */
static const uint8_t dcl_literal[98] = {
    11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,9,
    7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,
    7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,
    8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,
    44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,44,173
};
static const uint8_t dcl_length[6] = {2,35,36,53,38,23};
static const uint8_t dcl_distance[7] = {2,20,53,230,247,151,248};
static const uint16_t dcl_base[16] = {
    3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264
};
static const uint8_t dcl_extra[16] = {
    0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8
};

static int dcl_build(DclTable *table, const uint8_t *encoded, uint16_t size)
{
    uint16_t next[14];
    uint8_t lengths[256];
    uint16_t items = 0;
    uint16_t i, j, length;
    memset(table, 0, sizeof *table);
    for (i = 0; i < size; ++i) {
        length = (uint16_t)(encoded[i] & 15u);
        if (length >= 14u || items + (encoded[i] >> 4) + 1u > 256u)
            return 0;
        for (j = 0; j <= (encoded[i] >> 4); ++j) {
            lengths[items++] = (uint8_t)length;
            ++table->count[length];
        }
    }
    next[0] = 0;
    next[1] = 0;
    for (length = 1; length < 13u; ++length)
        next[length + 1] = (uint16_t)(next[length] + table->count[length]);
    for (i = 0; i < items; ++i)
        if (lengths[i] != 0u)
            table->symbols[next[lengths[i]]++] = (uint8_t)i;
    return 1;
}

static int dcl_byte(DclInput *input, uint16_t *result)
{
    GateDosFileResult read;
    GateFarPtr pointer;
    if (input->bad)
        return 0;
    if (input->offset == input->length) {
        /* Bounded I/O and a read-ahead rollback preserve the file cursor. */
        if (input->bytes_read >= 0x1000000ul)
            return 0;
        pointer = gate_stack_pointer(dcl_buffer);
        read = gate_dos_file_read(input->handle, pointer, DCL_CHUNK);
        if (read.carry || read.ax == 0 || read.ax > DCL_CHUNK) {
            input->bad = 1u;
            return 0;
        }
        input->bytes_read += read.ax;
        input->length = read.ax;
        input->offset = 0;
    }
    *result = dcl_buffer[input->offset++];
    return 1;
}

static int dcl_bits(DclInput *input, uint16_t size, uint16_t *value)
{
    uint16_t i, byte;
    *value = 0;
    for (i = 0; i < size; ++i) {
        if (input->bits_left == 0u) {
            if (!dcl_byte(input, &byte))
                return 0;
            input->bit_buffer = byte;
            input->bits_left = 8u;
        }
        *value |= (uint16_t)((input->bit_buffer & 1u) << i);
        input->bit_buffer >>= 1;
        --input->bits_left;
    }
    return 1;
}

static int dcl_symbol(DclInput *input, const DclTable *table,
                      uint16_t *symbol)
{
    uint16_t code = 0, first = 0, index = 0;
    uint16_t length, bit, count;
    for (length = 1; length < 14u; ++length) {
        if (!dcl_bits(input, 1u, &bit))
            return 0;
        code = (uint16_t)(code | (bit ^ 1u));
        count = table->count[length];
        if (code >= first && code - first < count) {
            *symbol = table->symbols[index + code - first];
            return 1;
        }
        index = (uint16_t)(index + count);
        first = (uint16_t)((first + count) << 1);
        code <<= 1;
    }
    return 0;
}

static void dcl_put(DclOutput *output, uint32_t linear, uint8_t byte)
{
    uint32_t address = (uint32_t)output->offset + linear;
    gate_far_set_byte((uint16_t)(output->segment +
                      (uint16_t)((address >> 16) << 12)),
                      (uint16_t)address, byte);
}

static void dcl_pixel(DclOutput *output, uint16_t x, uint16_t y,
                      uint8_t color)
{
    uint16_t plane;
    uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
    uint32_t base = ((uint32_t)y * 4ul) * output->stride + (x >> 3);
    for (plane = 0; plane < 4u; ++plane)
        if (color & (1u << plane))
            output->planes[plane] |= mask;
    if ((x & 7u) == 7u || x + 1u == output->width) {
        for (plane = 0; plane < 4u; ++plane) {
            dcl_put(output, base + (uint32_t)plane * output->stride,
                    output->planes[plane]);
            output->planes[plane] = 0;
        }
    }
}

static int dcl_emit(DclOutput *output, uint8_t byte)
{
    if (output->produced >= output->expected)
        return 0;
    dcl_ring[(uint16_t)output->produced & (DCL_RING - 1u)] = byte;
    if (output->mode == 3u) {
        dcl_put(output, output->produced, byte);
    } else {
        dcl_pixel(output, output->x, output->y, (uint8_t)(byte & 15u));
        if (output->x + 1u < output->width)
            dcl_pixel(output, (uint16_t)(output->x + 1u), output->y,
                      (uint8_t)(byte >> 4));
        output->x = (uint16_t)(output->x + 2u);
        if (output->x >= output->width) {
            output->x = 0;
            ++output->y;
        }
    }
    ++output->produced;
    return 1;
}

static int dcl_decode(DclInput *input, DclOutput *output)
{
    uint16_t literal, dict, flag, sym, extra, length, shift, high, low;
    uint32_t distance;
    uint16_t i;
    if (!dcl_build(&dcl_tables[0], dcl_literal, sizeof dcl_literal) ||
        !dcl_build(&dcl_tables[1], dcl_length, sizeof dcl_length) ||
        !dcl_build(&dcl_tables[2], dcl_distance, sizeof dcl_distance) ||
        !dcl_bits(input, 8u, &literal) || !dcl_bits(input, 8u, &dict) ||
        literal > 1u || dict < 4u || dict > 6u)
        return 0;

    for (;;) {
        if (!dcl_bits(input, 1u, &flag))
            return 0;
        if (flag == 0u) {
            if (!(literal ? dcl_symbol(input, &dcl_tables[0], &sym)
                          : dcl_bits(input, 8u, &sym)) ||
                !dcl_emit(output, (uint8_t)sym))
                return 0;
        } else {
            if (!dcl_symbol(input, &dcl_tables[1], &sym) || sym >= 16u ||
                !dcl_bits(input, dcl_extra[sym], &extra))
                return 0;
            length = (uint16_t)(dcl_base[sym] + extra);
            if (length == 519u)
                return output->produced == output->expected;
            shift = length == 2u ? 2u : dict;
            if (!dcl_symbol(input, &dcl_tables[2], &high) ||
                !dcl_bits(input, shift, &low))
                return 0;
            distance = ((uint32_t)high << shift) + low + 1u;
            if (distance > output->produced || distance > DCL_RING ||
                (uint32_t)length > output->expected - output->produced)
                return 0;
            for (i = 0; i < length; ++i) {
                uint8_t byte = dcl_ring[(uint16_t)(output->produced - distance) &
                                        (DCL_RING - 1u)];
                if (!dcl_emit(output, byte))
                    return 0;
            }
        }
    }
}

uint16_t gate_linked_decompress_picture_pixels(uint16_t pixel_offset,
                                                uint16_t pixel_segment)
{
    DclInput input;
    DclOutput output;
    GateDosFileResult seek;
    uint16_t mode = gate_far_word(gate_rtl_image_mode_segment(),
                                  GATE_RTL_VIDEO_MODE_OFFSET);
    uint16_t header = gate_rtl_image_header_segment();
    uint16_t width = gate_far_word(header, GATE_RTL_HEADER_OFFSET + 6u);
    uint16_t height = gate_far_word(header, GATE_RTL_HEADER_OFFSET + 8u);
    uint32_t size, planar;
    int ok;

    if (mode != 2u && mode != 3u)
        return 0xfffau;
    size = (uint32_t)(mode == 2u ? (width + 1ul) / 2ul : width) * height;
    planar = (uint32_t)((width + 7ul) / 8ul) * 4ul * height;
    if (!pixel_segment || !size || size > DCL_LIMIT ||
        (mode == 2u && planar > DCL_LIMIT))
        return 0xffe6u;
    memset(&input, 0, sizeof input);
    memset(&output, 0, sizeof output);
    input.handle = gate_far_word(gate_rtl_resource_handle_segment(),
                                  GATE_RTL_FILE_HANDLE_OFFSET);
    output.segment = pixel_segment;
    output.offset = pixel_offset;
    output.mode = mode;
    output.width = width;
    output.height = height;
    output.stride = (uint16_t)((width + 7ul) / 8ul);
    output.expected = size;
    ok = dcl_decode(&input, &output);
    if (input.length > input.offset) {
        /* DOS AH=42h relative seek accepts a signed 32-bit displacement.
         * The unread bytes belong to the next resource/record. */
        seek = gate_dos_file_seek(input.handle, 1u,
              (uint32_t)(0ul - (uint32_t)(input.length - input.offset)));
        if (seek.carry)
            ok = 0;
    }
    return ok ? 0u : 0xfffau;
}

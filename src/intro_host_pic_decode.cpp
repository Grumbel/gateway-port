// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

extern "C" {
#include "gate_dos_host_files.h"
#include "gate_rtl_image.h"
}
#include "pic.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

// Native implementation of the original 0eec:018a compressed-picture
// service, for Gateway's mode-12h (four-plane) and mode-13h DCL streams.
// The original resource
// selector has *already* opened the file, consumed the image's position
// table and optional palette, and left the DOS handle at the compressed
// payload. The original 145d allocator has *already* allocated the image's
// pixel buffer. We reuse the repository's independently tested PKWARE DCL
// decoder. In mode 12h the stream stores packed low-nibble-first 4-bit
// indices; the native codec packs these into the original VFX image's
// interleaved four 1-bit plane-rows, even-padded per plane-row. This is
// explicitly a host-level codec replacement, NOT a C translation of the
// DOS 1af5 decompression driver.

static void set_extended_byte(std::uint16_t segment, std::uint16_t offset,
                              std::uint32_t index, std::uint8_t byte)
{
    const std::uint32_t linear = static_cast<std::uint32_t>(offset) + index;
    gate_far_set_byte(static_cast<std::uint16_t>(segment + ((linear >> 16) << 12)),
                      static_cast<std::uint16_t>(linear), byte);
}

extern "C" std::uint16_t gate_far_0eec_018a(std::uint16_t pixel_offset,
                                               std::uint16_t pixel_segment)
{
    try {
        const std::uint16_t mode = gate_far_word(gate_rtl_image_mode_segment(),
                                                 GATE_RTL_VIDEO_MODE_OFFSET);
        if (mode != 2 && mode != 3)
            return 0xfffau; // other VFX formats not yet supported

        const std::uint16_t hdr = gate_rtl_image_header_segment();
        const std::uint16_t width = gate_far_word(hdr, GATE_RTL_HEADER_OFFSET + 6);
        const std::uint16_t height = gate_far_word(hdr, GATE_RTL_HEADER_OFFSET + 8);
        const std::uint32_t row_bytes = mode == 2 ? (width + 1u) / 2u : width;
        const std::uint32_t size = row_bytes * height;
        if (!size || size > 0x3f000u || pixel_segment == 0)
            return 0xffe6u;

        const std::uint16_t handle = gate_far_word(gate_rtl_resource_handle_segment(),
                                                   GATE_RTL_FILE_HANDLE_OFFSET);
        FILE *file = gate_dos_host_file_stream(handle);
        if (!file)
            return 0xfffau;
        const long start = std::ftell(file);
        if (start < 0 || std::fseek(file, 0, SEEK_END) != 0)
            return 0xfffau;
        const long end = std::ftell(file);
        if (end < start || end - start > 16 * 1024 * 1024 ||
            std::fseek(file, start, SEEK_SET) != 0)
            return 0xfffau;

        std::vector<std::uint8_t> compressed(static_cast<std::size_t>(end - start));
        if (std::fread(compressed.data(), 1, compressed.size(), file) != compressed.size())
            return 0xfffau;
        const auto pixels = gateway::explode_dcl(compressed, size);
        if (pixels.size() != size)
            return 0xfffau;

        if (mode == 3) {
            // Original 245d:0006 pads each 8bpp row to an even byte count.
            const std::uint32_t stride = (width + 1u) & ~1u;
            for (std::uint32_t row = 0; row < height; ++row)
                for (std::uint32_t x = 0; x < width; ++x)
                    set_extended_byte(pixel_segment, pixel_offset,
                        row * stride + x, pixels[row * width + x]);
        } else {
            // VGA mode 12h: scanline 0 consists of plane 0/1/2/3,
            // scanline 1 repeats them, etc. Each bit is MSB-first,
            // although the DCL color indices are low-nibble-first.
            const std::uint32_t stride = ((width + 7u) / 8u + 1u) & ~1u;
            std::vector<std::uint8_t> planar(stride * height * 4u);
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint8_t packed = pixels[y * row_bytes + x / 2u];
                    const std::uint8_t color =
                        static_cast<std::uint8_t>((packed >> (4u * (x & 1u))) & 15u);
                    for (std::uint32_t plane = 0; plane < 4; ++plane)
                        if (color & (1u << plane))
                            planar[(y * 4u + plane) * stride + x / 8u] |=
                                static_cast<std::uint8_t>(0x80u >> (x & 7u));
                }
            }
            if (planar.size() > 0x3f000u)
                return 0xffe6u;
            for (std::uint32_t i = 0; i < planar.size(); ++i)
                set_extended_byte(pixel_segment, pixel_offset, i, planar[i]);
        }
        return 0;
    } catch (...) {
        // Do not let C++ decode errors cross the original C ABI. The
        // source-C loader takes nonzero as failure and frees the record.
        return 0xfffau;
    }
}

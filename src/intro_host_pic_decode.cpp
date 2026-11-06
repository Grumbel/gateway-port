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
// service, for Gateway's mode-13h DCL streams. The original resource
// selector has *already* opened the file, consumed the image's position
// table and optional palette, and left the DOS handle at the compressed
// payload. The original 145d allocator has *already* allocated the image's
// pixel buffer. We reuse the repository's independently tested PKWARE DCL
// decoder; this is explicitly a host-level codec replacement, NOT a C
// translation of the DOS 1af5 decompression driver.
extern "C" std::uint16_t gate_far_0eec_018a(std::uint16_t pixel_offset,
                                               std::uint16_t pixel_segment)
{
    try {
        const std::uint16_t mode = gate_far_word(gate_rtl_image_mode_segment(),
                                                 GATE_RTL_VIDEO_MODE_OFFSET);
        if (mode != 3)
            return 0xfffau; // packed/planar VFX modes not yet supported

        const std::uint16_t hdr = gate_rtl_image_header_segment();
        const std::uint16_t width = gate_far_word(hdr, GATE_RTL_HEADER_OFFSET + 6);
        const std::uint16_t height = gate_far_word(hdr, GATE_RTL_HEADER_OFFSET + 8);
        const std::uint32_t size = static_cast<std::uint32_t>(width) * height;
        if (!size || size > 65535u || pixel_segment == 0)
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

        // Original 245d:0006 pads each 8bpp row to an even number of bytes.
        // DCL decodes tightly packed width*height pixels; do not overwrite
        // the padding that 1479:0002 has just initialized.
        const std::uint16_t stride = static_cast<std::uint16_t>((width + 1u) & ~1u);
        for (std::uint32_t row = 0; row < height; ++row)
            for (std::uint32_t x = 0; x < width; ++x)
                gate_far_set_byte(pixel_segment,
                    static_cast<std::uint16_t>(pixel_offset + row * stride + x),
                    pixels[row * width + x]);
        return 0;
    } catch (...) {
        // Do not let C++ decode errors cross the original C ABI. The
        // source-C loader takes nonzero as failure and frees the record.
        return 0xfffau;
    }
}

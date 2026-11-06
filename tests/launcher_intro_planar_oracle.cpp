// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

extern "C" {
#include "gate_intro_host_memory.h"
}

#include "pic.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

// Independent test oracle: the existing image-format decoder returns one
// palette index per pixel from the original low-nibble-first DCL stream.
// Compare every index with the four VGA bitplanes produced by the native
// original-loader path. This helper is linked into tests only, not the
// host codec or the reconstructed original C archive.
extern "C" int gate_intro_planar_pic_matches_reference(const char *directory,
                                                         std::uint16_t index,
                                                         std::uint16_t x,
                                                         std::uint16_t y)
{
    try {
        const auto filename = std::filesystem::path(directory) / "GATE_201.PIC";
        std::ifstream stream(filename, std::ios::binary);
        if (!stream)
            return 0;
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        const auto entries = gateway::parse_pic_directory(bytes);
        if (index >= entries.size())
            return 0;
        const auto expected = gateway::decode_pic(bytes, entries[index],
                                                   gateway::PicDialect::gateway);
        if (expected.bits != 4 || expected.width <= 0 || expected.height <= 0 ||
            x + expected.width > 640 || y + expected.height > 480 ||
            expected.pixels.size() !=
                static_cast<std::size_t>(expected.width * expected.height))
            return 0;
        for (int row = 0; row < expected.height; ++row)
            for (int col = 0; col < expected.width; ++col)
                if (gate_intro_vga16_pixel(static_cast<std::uint16_t>(x + col),
                                             static_cast<std::uint16_t>(y + row)) !=
                    expected.pixels[static_cast<std::size_t>(row * expected.width + col)])
                    return 0;
        return 1;
    } catch (...) {
        return 0;
    }
}

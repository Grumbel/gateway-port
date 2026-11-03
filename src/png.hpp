// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {
struct Rgb8 {
    std::uint8_t r = 0, g = 0, b = 0;
};

// Encodes an indexed image without compression. `pixels` holds one palette index per pixel, and
// `bits` (1, 4, or 8) selects the packed PNG sample depth.
std::vector<std::uint8_t> encode_indexed_png(int width, int height, int bits,
                                             std::span<const std::uint8_t> pixels,
                                             std::span<const Rgb8> palette);
} // namespace gateway

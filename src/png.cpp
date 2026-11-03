// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "png.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace {
void put_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xffffffff;
    for (auto byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
    }
    return ~crc;
}
void put_chunk(std::vector<std::uint8_t> &out, std::string_view type,
               std::span<const std::uint8_t> data) {
    put_u32(out, static_cast<std::uint32_t>(data.size()));
    const auto start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    put_u32(out, crc32(std::span(out).subspan(start)));
}
std::vector<std::uint8_t> zlib_stored(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (auto byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    std::size_t at = 0;
    do {
        const auto size = std::min<std::size_t>(data.size() - at, 65535);
        out.push_back(at + size == data.size());
        out.push_back(size & 255);
        out.push_back(size >> 8);
        out.push_back(~size & 255);
        out.push_back((~size >> 8) & 255);
        out.insert(out.end(), data.begin() + at, data.begin() + at + size);
        at += size;
    } while (at < data.size());
    put_u32(out, (b << 16) | a);
    return out;
}
} // namespace

namespace gateway {
std::vector<std::uint8_t> encode_indexed_png(int width, int height, int bits,
                                             std::span<const std::uint8_t> pixels,
                                             std::span<const Rgb8> palette) {
    if (width < 1 || height < 1 || (bits != 1 && bits != 4 && bits != 8))
        throw std::runtime_error("invalid PNG dimensions or depth");
    if (pixels.size() != static_cast<std::size_t>(width) * height)
        throw std::runtime_error("PNG pixel count mismatch");
    if (palette.empty() || palette.size() > 256)
        throw std::runtime_error("invalid PNG palette size");
    if (std::ranges::any_of(pixels, [&](auto v) { return v >= palette.size() || v >> bits; }))
        throw std::runtime_error("PNG pixel outside palette");

    const std::size_t row = (static_cast<std::size_t>(width) * bits + 7) / 8;
    std::vector<std::uint8_t> raw((row + 1) * height);
    for (int y = 0; y < height; ++y) {
        auto *line = raw.data() + y * (row + 1) + 1;
        for (int x = 0; x < width; ++x) {
            const std::size_t bit = static_cast<std::size_t>(x) * bits;
            line[bit / 8] |= pixels[static_cast<std::size_t>(y) * width + x]
                             << (8 - bits - static_cast<int>(bit % 8));
        }
    }

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    put_u32(header, width);
    put_u32(header, height);
    header.insert(header.end(), {static_cast<std::uint8_t>(bits), 3, 0, 0, 0});
    put_chunk(out, "IHDR", header);
    std::vector<std::uint8_t> colors;
    for (const auto &color : palette)
        colors.insert(colors.end(), {color.r, color.g, color.b});
    put_chunk(out, "PLTE", colors);
    put_chunk(out, "IDAT", zlib_stored(raw));
    put_chunk(out, "IEND", {});
    return out;
}
} // namespace gateway

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "mz.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
namespace {
struct MzFile {
    std::span<const std::uint8_t> file;
    std::size_t header = 0, size = 0;
    explicit MzFile(std::span<const std::uint8_t> bytes) : file(bytes) {
        if (file.size() < 28 || word(0) != 0x5a4d)
            throw std::runtime_error("expected a DOS MZ executable");
        const auto pages = word(4), last = word(2), relocations = word(6);
        if (!pages || last > 511 || word(26) != 0)
            throw std::runtime_error("unsupported or invalid MZ header");
        const std::size_t total = (std::size_t(pages) - 1) * 512 + (last ? last : 512);
        header = std::size_t(word(8)) * 16;
        const std::size_t table = word(24);
        if (header < 28 || total < header || total > file.size() ||
            (relocations && (table < 28 || table + std::size_t(relocations) * 4 > header)))
            throw std::runtime_error("invalid MZ image bounds");
        size = total - header;
    }
    std::uint16_t word(std::size_t at) const {
        if (at > file.size() || file.size() - at < 2)
            throw std::runtime_error("truncated MZ header or relocation table");
        return file[at] | (std::uint16_t(file[at + 1]) << 8);
    }
    std::vector<std::uint8_t> relocated(std::uint16_t factor) const {
        std::vector<std::uint8_t> image(file.begin() + header, file.begin() + header + size);
        const auto table = word(24);
        for (std::size_t i = 0; i < word(6); ++i) {
            const auto offset = std::size_t(word(table + i * 4 + 2)) * 16 + word(table + i * 4);
            if (offset >= size || size - offset < 2)
                throw std::runtime_error("relocation outside MZ image");
            const std::uint16_t value = (image[offset] | (image[offset + 1] << 8)) + factor;
            image[offset] = value & 255;
            image[offset + 1] = value >> 8;
        }
        return image;
    }
};
} // namespace
MzImage load_mz(std::span<const std::uint8_t> file, std::uint16_t load_segment) {
    const MzFile mz(file);
    const std::size_t base = std::size_t(load_segment) * 16;
    const std::size_t allocation = ((mz.size + 15) / 16 + mz.word(10)) * 16;
    if (load_segment < 16 || base + allocation > 0xa0000 ||
        std::size_t(mz.word(22)) * 16 + mz.word(20) >= mz.size ||
        std::size_t(mz.word(14)) * 16 + mz.word(16) > allocation)
        throw std::runtime_error("MZ entry, stack, or allocation outside conventional memory");
    MzImage result{std::vector<std::uint8_t>(1024 * 1024),
                   static_cast<std::uint16_t>(load_segment + mz.word(22)),
                   mz.word(20),
                   static_cast<std::uint16_t>(load_segment + mz.word(14)),
                   mz.word(16),
                   static_cast<std::uint16_t>(load_segment - 16),
                   static_cast<std::uint16_t>(load_segment - 16),
                   mz.size};
    const auto image = mz.relocated(load_segment);
    std::copy(image.begin(), image.end(), result.memory.begin() + base);
    const auto psp = base - 256;
    result.memory[psp] = 0xcd;
    result.memory[psp + 1] = 0x20;
    result.memory[psp + 0x81] = 0x0d;
    return result;
}
std::vector<std::uint8_t> load_mz_overlay(std::span<const std::uint8_t> file,
                                          std::uint16_t load_segment, std::uint16_t relocation) {
    const MzFile mz(file);
    if (std::size_t(load_segment) * 16 + mz.size > 0xa0000)
        throw std::out_of_range("MZ overlay outside conventional memory");
    return mz.relocated(relocation);
}
} // namespace gateway

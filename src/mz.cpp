#include "mz.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
MzImage load_mz(std::span<const std::uint8_t> file, std::uint16_t load_segment) {
    auto word = [&](std::size_t at) -> std::uint16_t {
        if (at > file.size() || file.size() - at < 2)
            throw std::runtime_error("truncated MZ header or relocation table");
        return file[at] | (std::uint16_t(file[at + 1]) << 8);
    };
    if (file.size() < 28 || word(0) != 0x5a4d)
        throw std::runtime_error("expected a DOS MZ executable");
    const auto pages = word(4), last = word(2), relocations = word(6);
    if (!pages || last > 511 || word(26) != 0)
        throw std::runtime_error("unsupported or invalid MZ header");
    const std::size_t total = (std::size_t(pages) - 1) * 512 + (last ? last : 512);
    const std::size_t header = std::size_t(word(8)) * 16;
    const std::size_t table = word(24);
    if (header < 28 || total < header || total > file.size() ||
        (relocations && (table < 28 || table + std::size_t(relocations) * 4 > header)))
        throw std::runtime_error("invalid MZ image bounds");
    const std::size_t base = std::size_t(load_segment) * 16;
    const std::size_t image_size = total - header;
    const std::size_t allocation = ((image_size + 15) / 16 + word(10)) * 16;
    if (load_segment < 16 || base + allocation > 0xa0000 ||
        std::size_t(word(22)) * 16 + word(20) >= image_size ||
        std::size_t(word(14)) * 16 + word(16) > allocation)
        throw std::runtime_error("MZ entry, stack, or allocation outside conventional memory");
    MzImage result{std::vector<std::uint8_t>(1024 * 1024),
                   static_cast<std::uint16_t>(load_segment + word(22)),
                   word(20),
                   static_cast<std::uint16_t>(load_segment + word(14)),
                   word(16),
                   static_cast<std::uint16_t>(load_segment - 16),
                   static_cast<std::uint16_t>(load_segment - 16),
                   image_size};
    std::copy_n(file.begin() + header, image_size, result.memory.begin() + base);
    const auto psp = base - 256;
    result.memory[psp] = 0xcd;
    result.memory[psp + 1] = 0x20;
    result.memory[psp + 0x81] = 0x0d;
    for (std::size_t i = 0; i < relocations; ++i) {
        const auto offset = std::size_t(word(table + i * 4 + 2)) * 16 + word(table + i * 4);
        if (offset >= image_size || image_size - offset < 2)
            throw std::runtime_error("relocation outside MZ image");
        const auto at = base + offset;
        const std::uint16_t value =
            (result.memory[at] | (result.memory[at + 1] << 8)) + load_segment;
        result.memory[at] = value & 0xff;
        result.memory[at + 1] = value >> 8;
    }
    return result;
}
} // namespace gateway

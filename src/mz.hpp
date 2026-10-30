#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {
struct MzImage {
    std::vector<std::uint8_t> memory;
    std::uint16_t cs, ip, ss, sp, ds, es;
    std::size_t image_size;
};
// A DOS-style allocation: PSP immediately before load_segment, no environment yet.
MzImage load_mz(std::span<const std::uint8_t> file, std::uint16_t load_segment = 0x1000);
} // namespace gateway

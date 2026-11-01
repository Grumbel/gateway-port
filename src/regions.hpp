#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gateway {
// The RGN object index refers to the zero-based OBJECT.DAT name list, NOT the
// internal engine object ID used by object-state services.
struct HitRegion {
    std::uint16_t name_index = 0;
    unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0; // Scene-local, inclusive.
};
struct RegionFrame {
    std::uint16_t default_name_index = 0;
    std::vector<HitRegion> boxes;
};

// The header count comes from the matching PIC directory. It cannot be inferred
// by looking for the smallest nonzero RGN offset: rectangle data looks like headers.
std::vector<RegionFrame> parse_regions(std::span<const std::uint8_t> file,
                                       std::size_t pic_entry_count);
// OBJECT.DAT is a length-prefixed sequence of NUL-terminated CP437 names.
std::vector<std::string> parse_object_names(std::span<const std::uint8_t> file);
} // namespace gateway

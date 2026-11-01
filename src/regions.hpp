#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gateway {
enum class RegionFormat { rectangles, early_rectangles, polygons };

struct RegionPoint {
    unsigned x = 0, y = 0;
};
// RGN records refer to native engine object IDs, NOT indexes into the
// zero-based OBJECT.DAT string list. Resolve names through the live table.
struct HitRegion {
    std::uint16_t object_id = 0;
    unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0; // Scene-local, inclusive.
    // Gateway II stores explicit vertices rather than packed rectangles.
    // Two-point records are ordinary rectangles and leave this empty.
    std::vector<RegionPoint> polygon{};
};
struct RegionFrame {
    std::uint16_t default_object_id = 0;
    std::vector<HitRegion> boxes;
};

// Gateway uses the PIC entry count. Earlier Spellcasting RGNs may have a larger
// fixed header table or fewer slots than their PICs; infer the data boundary
// from validated pointers before parsing. Gateway II's lists contain polygons.
std::vector<RegionFrame> parse_regions(std::span<const std::uint8_t> file,
                                       std::size_t pic_entry_count,
                                       RegionFormat format = RegionFormat::rectangles);
// OBJECT.DAT is a length-prefixed sequence of NUL-terminated CP437 names.
std::vector<std::string> parse_object_names(std::span<const std::uint8_t> file);
} // namespace gateway

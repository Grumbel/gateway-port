#include "regions.hpp"
#include <algorithm>
#include <stdexcept>

namespace gateway {
namespace {
std::uint16_t word(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2)
        throw std::runtime_error("RGN field past end of file");
    return data[offset] | std::uint16_t(data[offset + 1]) << 8;
}
} // namespace

std::vector<RegionFrame> parse_regions(std::span<const std::uint8_t> file,
                                       std::size_t pic_entry_count, RegionFormat format) {
    if (!pic_entry_count || pic_entry_count > 4096 ||
        (format != RegionFormat::early_rectangles && pic_entry_count > file.size() / 6))
        throw std::runtime_error("RGN header count is invalid");
    auto header_count = std::min(pic_entry_count, file.size() / 6);
    if (format == RegionFormat::early_rectangles) {
        // S101 uses 30/50/60/70/80 header slots, S201 normally 128, even
        // when the matching PIC directory has a different number of entries.
        // Reject candidate pointers within/behind their own header record:
        // rectangle data can otherwise be mistaken for additional headers.
        for (std::size_t i = 0; i < header_count; ++i) {
            const auto offset = word(file, i * 6);
            const auto count = word(file, i * 6 + 4);
            if (count && offset % 6 == 0 && offset >= (i + 1) * 6 &&
                offset <= file.size() && count <= (file.size() - offset) / 6)
                header_count = std::min(header_count, std::size_t(offset / 6));
        }
    }
    const auto header_size = header_count * 6;
    std::vector<RegionFrame> frames(pic_entry_count);
    for (std::size_t i = 0; i < header_count; ++i) {
        const auto at = i * 6;
        const auto offset = word(file, at);
        frames[i].default_object_id = word(file, at + 2);
        const auto count = word(file, at + 4);
        if (!count)
            continue;
        if (offset < header_size || offset > file.size()) {
            if (format == RegionFormat::early_rectangles)
                continue;
            throw std::runtime_error("RGN rectangle list outside file");
        }
        if (format == RegionFormat::polygons) {
            auto cursor = std::size_t(offset);
            for (unsigned j = 0; j < count; ++j) {
                if (cursor > file.size() || file.size() - cursor < 4)
                    throw std::runtime_error("RGN polygon header outside file");
                auto id = word(file, cursor);
                const auto vertices = word(file, cursor + 2);
                cursor += 4;
                if (vertices < 2 || vertices > (file.size() - cursor) / 4)
                    throw std::runtime_error("RGN polygon vertices outside file");
                if (!id)
                    id = frames[i].default_object_id;
                HitRegion region;
                region.object_id = id;
                region.x0 = region.y0 = 65535;
                for (unsigned k = 0; k < vertices; ++k) {
                    const RegionPoint p{word(file, cursor), word(file, cursor + 2)};
                    cursor += 4;
                    region.x0 = std::min(region.x0, p.x);
                    region.y0 = std::min(region.y0, p.y);
                    region.x1 = std::max(region.x1, p.x);
                    region.y1 = std::max(region.y1, p.y);
                    region.polygon.push_back(p);
                }
                if (vertices == 2)
                    region.polygon.clear(); // The two points are rectangle corners.
                frames[i].boxes.push_back(std::move(region));
            }
            continue;
        }
        if (count > (file.size() - offset) / 6) {
            if (format == RegionFormat::early_rectangles)
                continue;
            throw std::runtime_error("RGN rectangle list outside file");
        }
        for (unsigned j = 0; j < count; ++j) {
            const auto rect = offset + j * 6;
            auto name = word(file, rect);
            if (!name)
                name = frames[i].default_object_id;
            const auto first = word(file, rect + 2), last = word(file, rect + 4);
            const unsigned x0 = (first & 255) * 2, y0 = first >> 8;
            const unsigned x1 = (last & 255) * 2, y1 = last >> 8;
            if (x0 > x1 || y0 > y1) {
                if (format == RegionFormat::early_rectangles)
                    continue; // Unusable/sentinel boxes in a few S201 frames.
                throw std::runtime_error("RGN rectangle has inverted corners");
            }
            frames[i].boxes.push_back({name, x0, y0, x1, y1, {}});
        }
    }
    return frames;
}

std::vector<std::string> parse_object_names(std::span<const std::uint8_t> file) {
    if (file.size() < 2 || word(file, 0) != file.size() - 2)
        throw std::runtime_error("invalid OBJECT.DAT size header");
    std::vector<std::string> result;
    for (std::size_t at = 2; at < file.size();) {
        std::size_t end = at;
        while (end < file.size() && file[end])
            ++end;
        if (end == file.size())
            throw std::runtime_error("unterminated OBJECT.DAT name");
        result.emplace_back(reinterpret_cast<const char *>(file.data() + at), end - at);
        at = end + 1;
    }
    return result;
}
} // namespace gateway

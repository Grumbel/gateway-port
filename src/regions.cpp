#include "regions.hpp"
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
                                       std::size_t pic_entry_count) {
    if (!pic_entry_count || pic_entry_count > file.size() / 6 || pic_entry_count > 4096)
        throw std::runtime_error("RGN header count is invalid");
    const auto header_size = pic_entry_count * 6;
    std::vector<RegionFrame> frames(pic_entry_count);
    for (std::size_t i = 0; i < pic_entry_count; ++i) {
        const auto at = i * 6;
        const auto offset = word(file, at);
        frames[i].default_object_id = word(file, at + 2);
        const auto count = word(file, at + 4);
        if (!count)
            continue;
        if (offset < header_size || offset > file.size() || count > (file.size() - offset) / 6)
            throw std::runtime_error("RGN rectangle list outside file");
        for (unsigned j = 0; j < count; ++j) {
            const auto rect = offset + j * 6;
            auto name = word(file, rect);
            if (!name)
                name = frames[i].default_object_id;
            const auto first = word(file, rect + 2), last = word(file, rect + 4);
            const unsigned x0 = (first & 255) * 2, y0 = first >> 8;
            const unsigned x1 = (last & 255) * 2, y1 = last >> 8;
            if (x0 > x1 || y0 > y1)
                throw std::runtime_error("RGN rectangle has inverted corners");
            frames[i].boxes.push_back({name, x0, y0, x1, y1});
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

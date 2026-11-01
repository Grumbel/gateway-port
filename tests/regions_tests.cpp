#include "regions.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <typename Callback> void rejects(Callback cb) {
    try {
        cb();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("malformed RGN accepted");
}
} // namespace

int main() try {
    // Two six-byte headers, one two-record list; x coordinates are half-pixels.
    const std::array<std::uint8_t, 24> bytes{
        12, 0, 5, 0, 2, 0, 0, 0, 0, 0, 0, 0,
        7, 0, 3, 4, 6, 8, 0, 0, 1, 2, 4, 5};
    const auto frames = gateway::parse_regions(bytes, 2);
    require(frames.size() == 2 && frames[0].boxes.size() == 2 && frames[1].boxes.empty(),
            "RGN header and rectangle counts");
    const auto &first = frames[0].boxes[0];
    require(first.name_index == 7 && first.x0 == 6 && first.y0 == 4 && first.x1 == 12 &&
                first.y1 == 8,
            "RGN rectangle coordinates");
    require(frames[0].boxes[1].name_index == 5, "RGN default object name");
    rejects([&] { gateway::parse_regions(bytes, 5); });
    auto invalid = bytes;
    invalid[0] = 2;
    rejects([&] { gateway::parse_regions(invalid, 2); });
    invalid = bytes;
    invalid[16] = 0;
    rejects([&] { gateway::parse_regions(invalid, 2); });

    const std::array<std::uint8_t, 9> names{7, 0, 'O', 'n', 'e', 0, 'T', 'w', 0};
    const auto decoded = gateway::parse_object_names(names);
    require(decoded.size() == 2 && decoded[0] == "One" && decoded[1] == "Tw",
            "OBJECT.DAT name list");
    rejects([&] { gateway::parse_object_names(std::span(names).first(8)); });
    std::cout << "Gateway RGN region checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

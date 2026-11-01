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
void word(std::vector<std::uint8_t> &bytes, std::size_t offset, unsigned value) {
    bytes.at(offset) = value & 255;
    bytes.at(offset + 1) = value >> 8;
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
    require(first.object_id == 7 && first.x0 == 6 && first.y0 == 4 && first.x1 == 12 &&
                first.y1 == 8,
            "RGN rectangle coordinates");
    require(frames[0].boxes[1].object_id == 5, "RGN default native object ID");
    rejects([&] { gateway::parse_regions(bytes, 5); });
    auto invalid = bytes;
    invalid[0] = 2;
    rejects([&] { gateway::parse_regions(invalid, 2); });
    invalid = bytes;
    invalid[16] = 0;
    rejects([&] { gateway::parse_regions(invalid, 2); });

    // S101 has fewer fixed-header slots than some matching PIC directories.
    // Keep the PIC index count; the extra slots must not read the rectangle data.
    std::vector<std::uint8_t> early(36);
    word(early, 0, 24);
    word(early, 4, 2);
    word(early, 24, 123);
    word(early, 26, 0x0204);
    word(early, 28, 0x0508);
    word(early, 30, 124);
    word(early, 32, 0x1008);
    word(early, 34, 0x080a); // Inverted: skip this one only in the older format.
    const auto old = gateway::parse_regions(early, 6, gateway::RegionFormat::early_rectangles);
    require(old.size() == 6 && old[0].boxes.size() == 1 && old[0].boxes[0].object_id == 123 &&
                old[5].boxes.empty(),
            "early RGN header bound and malformed rectangle tolerance");
    rejects([&] { gateway::parse_regions(early, 6); });

    // Gateway II: each list contains (object ID, vertex count, x/y pairs),
    // with a two-vertex record representing a rectangular box.
    std::vector<std::uint8_t> polygons(38);
    word(polygons, 0, 6);
    word(polygons, 2, 9);
    word(polygons, 4, 2);
    word(polygons, 6, 7);
    word(polygons, 8, 4);
    for (std::size_t i = 0; i < 8; ++i)
        word(polygons, 10 + 2 * i, (i % 2 ? 5 : 10) * (i / 2));
    word(polygons, 26, 0);
    word(polygons, 28, 2);
    word(polygons, 30, 3);
    word(polygons, 32, 4);
    word(polygons, 34, 8);
    word(polygons, 36, 9);
    const auto poly = gateway::parse_regions(polygons, 1, gateway::RegionFormat::polygons);
    require(poly.size() == 1 && poly[0].boxes.size() == 2 &&
                poly[0].boxes[0].polygon.size() == 4 &&
                poly[0].boxes[0].object_id == 7 &&
                poly[0].boxes[1].polygon.empty() && poly[0].boxes[1].object_id == 9 &&
                poly[0].boxes[1].x0 == 3 && poly[0].boxes[1].x1 == 8,
            "Gateway II polygon vertices, bounding box and default ID");
    word(polygons, 8, 99);
    rejects([&] { gateway::parse_regions(polygons, 1, gateway::RegionFormat::polygons); });

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

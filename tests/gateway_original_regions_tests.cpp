// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

// Original-file, read-only audit of the scene debugger's actual RGN parser.
// The executable and game resources remain private, outside the repository.
#include "debug_scene.hpp"
#include "pic.hpp"
#include "regions.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: gateway-original-regions-tests GATEWAY-DOS");
    const std::filesystem::path root = argv[1];
    if (!std::filesystem::exists(root / "GATEMAIN.EXE")) {
        std::cout << "Original Gateway RGN audit skipped: no private files\n";
        return 77;
    }
    gateway::DebugSceneTracker tracker;
    unsigned pictures = 0, matched = 0;
    std::size_t frames = 0, regions = 0, anonymous = 0, outside_picture = 0;
    for (const auto &item : std::filesystem::directory_iterator(root)) {
        const auto name = item.path().filename().string();
        unsigned number = 0;
        if (std::sscanf(name.c_str(), "GATE_%3u.PIC", &number) != 1 ||
            name.size() != 12 || name.substr(8) != ".PIC")
            continue;
        ++pictures;
        const auto bytes = read(item.path());
        const auto directory = gateway::parse_pic_directory(bytes);
        require(!directory.empty(), "original Gateway PIC directory empty");
        const unsigned candidate = number >= 100 && number < 200 ? number - 100 : number;
        char region_name[16];
        std::snprintf(region_name, sizeof region_name, "GATE_%03u.RGN", candidate);
        const auto region_path = root / region_name;
        if (!std::filesystem::exists(region_path))
            continue;
        ++matched;
        const auto region_bytes = read(region_path);
        const auto entries = gateway::parse_regions(region_bytes, directory.size());
        require(tracker.add_picture(name, bytes, region_bytes),
                "scene tracker failed to associate original PIC and RGN");
        require(entries.size() == directory.size(), "PIC/RGN entry count mismatch");
        frames += entries.size();
        for (std::size_t frame = 0; frame < entries.size(); ++frame) {
            const auto &entry = entries[frame];
            for (const auto &box : entry.boxes) {
                require(box.x0 <= box.x1 && box.y0 <= box.y1,
                        "inverted original scene rectangle");
                require(box.object_id <= 734, "unknown original Gateway object identifier");
                ++regions;
                anonymous += box.object_id == 0;
                if (box.x1 >= 380 || box.y1 >= 232) {
                    ++outside_picture;
                    std::cout << "  outside scene: " << name << " frame " << frame
                              << " object " << box.object_id << " local ("
                              << box.x0 << ',' << box.y0 << ")-(" << box.x1 << ','
                              << box.y1 << ")\n";
                }
            }
        }
    }
    require(pictures == 40 && matched == 22 && frames == 773 && regions == 2521 &&
                anonymous == 160 && outside_picture == 4,
            "original Gateway PIC/RGN coverage changed");
    std::cout << "Gateway original scene regions: " << pictures << " PIC files, "
              << matched << " matched PIC/RGN pairs, " << frames << " frames, "
              << regions << " rectangles, " << anonymous << " anonymous, "
              << outside_picture << " extending beyond the scene picture\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

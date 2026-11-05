// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

// Optional differential test: execute the original resident picker in
// Unicorn against private RAM snapshots and compare its three outputs to
// our read-only debug view. No original bytes are committed to the repo.
#include "debug_scene.hpp"
#include <unicorn/unicorn.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void check(uc_err err) {
    if (err != UC_ERR_OK)
        throw std::runtime_error(uc_strerror(err));
}
std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void put16(std::array<std::uint16_t, 10> &stack, std::size_t index,
           std::uint16_t value) {
    stack.at(index) = value;
}
void set(uc_engine *uc, int id, std::uint16_t value) {
    check(uc_reg_write(uc, id, &value));
}
std::uint16_t get(uc_engine *uc, int id) {
    std::uint16_t value = 0;
    check(uc_reg_read(uc, id, &value));
    return value;
}
struct Pick {
    std::uint16_t result, group, slot, kind;
};
Pick original_pick(uc_engine *uc, int x, int y, std::uint16_t ds) {
    constexpr std::uint64_t entry = 0x2649 * 16 + 0x0541;
    constexpr std::uint64_t return_to = 0xf7000;
    // Far-call ABI: ret IP, ret CS, X, Y, then three far output pointers.
    // The routine begins with BP=SP-2 and reads X/Y at BP+6/BP+8.
    std::array<std::uint16_t, 10> stack{};
    put16(stack, 0, 0);
    put16(stack, 1, 0xf700);
    put16(stack, 2, std::uint16_t(x));
    put16(stack, 3, std::uint16_t(y));
    for (unsigned i = 0; i < 3; ++i) {
        put16(stack, 4 + i * 2, std::uint16_t(2 * i));
        put16(stack, 5 + i * 2, 0xf800);
    }
    check(uc_mem_write(uc, 0xf9100, stack.data(), sizeof stack));
    set(uc, UC_X86_REG_CS, 0x2649);
    set(uc, UC_X86_REG_IP, 0x0541);
    set(uc, UC_X86_REG_DS, ds);
    set(uc, UC_X86_REG_SS, 0xf900);
    set(uc, UC_X86_REG_SP, 0x0100);
    check(uc_emu_start(uc, entry, return_to, 0, 100000));
    require(std::uint32_t(get(uc, UC_X86_REG_CS)) * 16 + get(uc, UC_X86_REG_IP) == return_to,
            "original picker failed to far-return to test harness");
    std::array<std::uint16_t, 3> result{};
    check(uc_mem_read(uc, 0xf8000, result.data(), sizeof result));
    return {get(uc, UC_X86_REG_AX), result[0], result[1], result[2]};
}

std::map<std::string, std::string> read_metadata(const std::filesystem::path &path) {
    std::ifstream file(path);
    require(bool(file), "cannot open " + path.string());
    std::map<std::string, std::string> result;
    std::string line;
    while (std::getline(file, line)) {
        const auto equal = line.find('=');
        require(equal != std::string::npos && equal > 0 && equal + 1 < line.size(),
                "invalid picker capture metadata line");
        require(result.emplace(line.substr(0, equal), line.substr(equal + 1)).second,
                "duplicate picker capture metadata field");
    }
    require(result.at("format") == "gateway-picker-entry-v1", "wrong capture format");
    return result;
}

std::size_t compare_captured_picker(const std::filesystem::path &original,
                                    const std::filesystem::path &capture_meta,
                                    const std::set<std::pair<int, int>> &extra_points) {
    const auto details = read_metadata(capture_meta);
    const auto memory_path = capture_meta.parent_path() /
        (capture_meta.stem().string() + ".bin");
    const auto memory = read(memory_path);
    require(memory.size() == 0x100000, "picker entry snapshot must be 1 MiB");
    const auto ds = std::uint16_t(std::stoul(details.at("DS"), nullptr, 0));
    const auto cs = std::uint16_t(std::stoul(details.at("CS"), nullptr, 0));
    const auto ip = std::uint16_t(std::stoul(details.at("IP"), nullptr, 0));
    require(std::uint32_t(cs) * 16 + ip == 0x269d1,
            "capture was not taken at the verified native picker entry");
    constexpr std::size_t entry = 0x269d1;
    require(memory[entry] == 0x55 && memory[entry + 1] == 0x8b &&
            memory[entry + 2] == 0xec && memory[entry + 3] == 0x83,
            "capture contains a different picker implementation");

    gateway::DebugSceneTracker tracker;
    const auto picture_name = details.at("picture");
    if (!picture_name.empty()) {
        const auto pic = read(original / picture_name);
        const auto region_name = gateway::DebugSceneTracker::matching_rgn(picture_name);
        if (!region_name.empty() && std::filesystem::exists(original / region_name)) {
            require(tracker.add_picture(picture_name, pic, read(original / region_name)),
                    "cannot load captured PIC/RGN pair");
            const auto entries = gateway::parse_pic_directory(pic);
            const auto frame = std::stoul(details.at("picture_entry"));
            require(frame < entries.size() && entries[frame].offset,
                    "invalid captured picture directory entry");
            tracker.note_read(picture_name, entries[frame].offset, 1);
        }
    }
    tracker.set_native_picker_state(memory, ds);
    const auto view = tracker.view();
    require(view.native_picker_decoded, "capture has no decodable native picker table");

    std::set<std::pair<int, int>> points;
    points.emplace(std::stoi(details.at("x")), std::stoi(details.at("y")));
    points.insert(extra_points.begin(), extra_points.end());
    for (int y = 0; y < 480; y += 19)
        for (int x = 0; x < 640; x += 17)
            points.emplace(x, y);
    for (const auto &row : view.native_picker_rows) {
        for (int x : {row.x0 - 1, row.x0, row.x1, row.x1 + 1})
            for (int y : {row.y0 - 1, row.y0, row.y1, row.y1 + 1})
                if (x >= 0 && x < 640 && y >= 0 && y < 480)
                    points.emplace(x, y);
    }
    uc_engine *uc = nullptr;
    check(uc_open(UC_ARCH_X86, UC_MODE_16, &uc));
    std::size_t compared = 0;
    try {
        check(uc_mem_map(uc, 0, 0x100000, UC_PROT_ALL));
        check(uc_mem_write(uc, 0, memory.data(), memory.size()));
        for (const auto &[x, y] : points) {
            const auto expected = view.picker_at(x, y);
            const auto actual = original_pick(uc, x, y, ds);
            const bool equal = expected
                ? actual.result == 1 && actual.group == expected->group &&
                  actual.slot == expected->slot && actual.kind == expected->kind
                : actual.result == 0 && actual.group == 0xffff &&
                  actual.slot == 0xffff && actual.kind == 0;
            require(equal, capture_meta.filename().string() + " native picker mismatch at " +
                               std::to_string(x) + "," + std::to_string(y));
            ++compared;
        }
    } catch (...) {
        uc_close(uc);
        throw;
    }
    uc_close(uc);
    std::cout << capture_meta.filename().string() << ": " << compared
              << " native comparisons, PIC/RGN aligned=" << view.native_picker_aligned << '\n';
    return compared;
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 1) {
        std::cout << "native picker oracle skipped: provide original-game and RAM directories\n";
        return 77;
    }
    if ((argc == 4 || (argc == 6 && std::string(argv[4]) == "--points")) &&
        std::string(argv[2]) == "--captured") {
        const std::filesystem::path original = argv[1], captures = argv[3];
        require(std::filesystem::is_directory(captures), "no picker capture directory");
        std::map<unsigned, std::set<std::pair<int, int>>> extra_points;
        if (argc == 6) {
            std::ifstream file(argv[5]);
            require(bool(file), "cannot open picker oracle points TSV");
            std::string line;
            require(bool(std::getline(file, line)) && line == "call\tx\ty",
                    "invalid picker oracle points header");
            unsigned total = 0;
            while (std::getline(file, line)) {
                std::istringstream row(line);
                unsigned call = 0;
                int x = 0, y = 0;
                std::string trailing;
                require(bool(row >> call >> x >> y) && !(row >> trailing) &&
                        call >= 1 && call <= 128 && x >= 0 && x < 640 &&
                        y >= 0 && y < 480 && ++total <= 10000,
                        "invalid picker oracle sample row");
                extra_points[call].emplace(x, y);
            }
        }
        std::vector<std::filesystem::path> entries;
        for (const auto &item : std::filesystem::directory_iterator(captures))
            if (item.is_regular_file() && item.path().extension() == ".meta")
                entries.push_back(item.path());
        std::sort(entries.begin(), entries.end());
        require(!entries.empty(), "no native picker entry captures found");
        std::size_t comparisons = 0;
        for (const auto &entry : entries) {
            const auto capture_call = std::stoul(entry.stem().string().substr(5));
            comparisons += compare_captured_picker(original, entry,
                                                  extra_points[unsigned(capture_call)]);
            extra_points.erase(unsigned(capture_call));
        }
        require(extra_points.empty(), "oracle points reference a missing capture");
        std::cout << "Gateway native picker oracle: " << entries.size() << " captures, "
                  << comparisons << " original-code comparisons passed\n";
        return 0;
    }
    require(argc == 3,
            "usage: native-picker-oracle-tests GATEWAY-DOS RAM-CAPTURES\n"
            "   or: native-picker-oracle-tests GATEWAY-DOS --captured DIR [--points TSV]");
    const std::filesystem::path original = argv[1], captures = argv[2];
    if (captures == "--captured")
        throw std::runtime_error("--captured requires a capture directory");
    gateway::DebugSceneTracker tracker;
    const auto picture = read(original / "GATE_102.PIC");
    require(tracker.add_picture("GATE_102.PIC", picture, read(original / "GATE_002.RGN")),
            "cannot load original first-room PIC/RGN");
    const auto entries = gateway::parse_pic_directory(picture);
    require(entries.size() > 15 && entries[15].offset, "no first-room picture entry 15");
    tracker.note_read("GATE_102.PIC", entries[15].offset, 1);
    std::size_t compared = 0;
    for (const auto *stage : {"before", "open", "taken", "closed"}) {
        const auto memory = read(captures / (std::string(stage) + ".bin"));
        require(memory.size() == 0x100000, "snapshot must contain 1 MiB of guest memory");
        const auto metadata = read(captures / (std::string(stage) + ".debug.txt"));
        const std::string info(metadata.begin(), metadata.end());
        const auto found = info.find("object_ds=");
        require(found != std::string::npos, "snapshot has no DS metadata");
        const auto ds = std::uint16_t(std::stoul(info.substr(found + 10), nullptr, 16));
        constexpr std::size_t picker_entry = 0x2649 * 16 + 0x0541;
        require(memory[picker_entry] == 0x55 && memory[picker_entry + 1] == 0x8b &&
                    memory[picker_entry + 2] == 0xec && memory[picker_entry + 3] == 0x83,
                "snapshot does not contain expected native picker entrypoint");
        tracker.set_native_picker_state(memory, ds);
        const auto view = tracker.view();
        require(view.native_picker_aligned && view.native_picker_rows.size() == 57,
                "native picker geometry did not match room RGN");
        uc_engine *uc = nullptr;
        check(uc_open(UC_ARCH_X86, UC_MODE_16, &uc));
        try {
            check(uc_mem_map(uc, 0, 0x100000, UC_PROT_ALL));
            check(uc_mem_write(uc, 0, memory.data(), memory.size()));
            std::set<std::pair<int, int>> points;
            for (int y = 0; y < 480; y += 19)
                for (int x = 0; x < 640; x += 17)
                    points.insert({x, y});
            // Exercise borders, signed fallback clipping, inclusive upper
            // edges, and overlaps; not only easy interior sample points.
            for (const auto &row : view.native_picker_rows) {
                for (int x : {row.x0 - 1, row.x0, row.x1, row.x1 + 1})
                    for (int y : {row.y0 - 1, row.y0, row.y1, row.y1 + 1})
                        if (x >= 0 && x < 640 && y >= 0 && y < 480)
                            points.insert({x, y});
            }
            for (const auto &[x, y] : points) {
                const auto expected = view.picker_at(x, y);
                const auto actual = original_pick(uc, x, y, ds);
                const bool equal = expected
                    ? actual.result == 1 && actual.group == expected->group &&
                      actual.slot == expected->slot && actual.kind == expected->kind
                    : actual.result == 0 && actual.group == 0xffff &&
                      actual.slot == 0xffff && actual.kind == 0;
                require(equal, std::string(stage) + " picker mismatch at " +
                              std::to_string(x) + "," + std::to_string(y) +
                              ": native=" + std::to_string(actual.group) + "/" +
                              std::to_string(actual.slot) + "/" +
                              std::to_string(actual.kind));
                ++compared;
            }
        } catch (...) {
            uc_close(uc);
            throw;
        }
        uc_close(uc);
    }
    std::cout << "Gateway native picker oracle: " << compared
              << " original-code click comparisons passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

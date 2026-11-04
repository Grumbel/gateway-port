// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "host_scene_preview.hpp"
#include "scene_animation.hpp"
#include "scene_compositor.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<std::uint8_t> read_bytes(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::uint32_t> argb_pixels(const gateway::PicImage &picture) {
    std::vector<std::uint32_t> result;
    for (const auto index : picture.pixels) {
        check(index < picture.palette.size(), "invalid palette entry in test image");
        const auto rgb = picture.palette[index];
        result.push_back(0xff000000u | (std::uint32_t(rgb.r) << 16) |
                         (std::uint32_t(rgb.g) << 8) | rgb.b);
    }
    return result;
}

void synthetic_scene_preview() {
    gateway::PicImage base;
    base.bits = 4;
    base.width = 4;
    base.height = 3;
    base.pixels.assign(12, 2);
    base.palette = gateway::default_ega_palette();
    gateway::PicImage first;
    first.bits = 4;
    first.overlay = true;
    first.x = 1;
    first.y = 1;
    first.width = 2;
    first.height = 1;
    first.pixels = {1, 3};
    auto second = first;
    second.x = 2;
    second.pixels = {2, 1};
    const gateway::HostScenePreview preview(base, {first, second}, 1, 1);

    for (std::uint32_t mask = 0; mask < 4; ++mask) {
        std::vector<gateway::PicImage> enabled;
        if (mask & 1)
            enabled.push_back(first);
        if (mask & 2)
            enabled.push_back(second);
        const auto scene = gateway::compose_pic_layers(base, enabled);
        const auto colors = argb_pixels(scene);
        std::vector<std::uint32_t> screen(6 * 5, 0xffabcdef);
        for (int y = 0; y < 3; ++y)
            std::copy_n(colors.begin() + y * 4, 4, screen.begin() + (y + 1) * 6 + 1);
        const auto original = screen;
        const auto matched = preview.replace_if_exact(screen, 6, 5);
        check(matched == mask, "host preview inferred wrong synthetic XOR mask");
        check(screen == original, "host preview changed a matching original framebuffer");
        auto native_only = original;
        std::fill(native_only.begin() + 6 + 1, native_only.begin() + 6 + 5, 0u);
        std::fill(native_only.begin() + 12 + 1, native_only.begin() + 12 + 5, 0u);
        std::fill(native_only.begin() + 18 + 1, native_only.begin() + 18 + 5, 0u);
        check(preview.paint_mask(native_only, 6, 5, mask),
              "native compositor rejected valid synthetic XOR mask");
        check(native_only == original, "explicit host-only compositor differs from reference");
        check(!preview.paint_mask(native_only, 6, 5, 4),
              "native compositor accepted an unknown layer combination");
        check(native_only == original, "unknown mask changed the framebuffer");
        screen[1 * 6 + 1] ^= 1;
        const auto unexpected = screen;
        check(!preview.replace_if_exact(screen, 6, 5),
              "host preview accepted a mismatched original framebuffer");
        check(screen == unexpected, "host preview damaged an unmatched framebuffer");
    }
    std::vector<std::uint32_t> small(6 * 2, 0);
    check(!preview.replace_if_exact(small, 6, 2),
          "host preview should skip frames with no complete room rectangle");
    check(!preview.paint_mask(small, 6, 2, 0),
          "native compositor should skip truncated framebuffer");
    try {
        preview.replace_if_exact(small, 6, 3);
        throw std::runtime_error("invalid framebuffer length was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("dimensions") != std::string::npos,
              "unexpected invalid framebuffer error");
    }
    auto bad = first;
    bad.overlay = false;
    try {
        gateway::HostScenePreview invalid(base, {bad}, 1, 1);
        throw std::runtime_error("non-XOR optional preview layer was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("not an XOR") != std::string::npos,
              "unexpected invalid optional layer error");
    }
}

std::vector<std::uint32_t> ppm_argb(const fs::path &path) {
    const auto input = read_bytes(path);
    const std::string header = "P6\n640 480\n255\n";
    check(input.size() == header.size() + 640 * 480 * 3 &&
              std::equal(header.begin(), header.end(), input.begin()),
          "invalid original RGB frame");
    std::vector<std::uint32_t> pixels(640 * 480);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const auto offset = header.size() + i * 3;
        pixels[i] = 0xff000000u | (std::uint32_t(input[offset]) << 16) |
                    (std::uint32_t(input[offset + 1]) << 8) | input[offset + 2];
    }
    return pixels;
}

void check_original(const gateway::HostScenePreview &preview,
                    const fs::path &ppm, std::uint32_t expected_mask) {
    auto argb = ppm_argb(ppm);
    const auto original = argb;
    check(preview.replace_if_exact(argb, 640, 480) == expected_mask,
          "host scene preview does not match original VGA scene");
    check(argb == original, "host scene preview changed original VGA image");
    // Clear the room before painting: this validates an *independent* native
    // output using only the optional-layer mask, not the original room pixels.
    for (int y = 0; y < 224; ++y)
        std::fill_n(argb.begin() + y * 640 + 261, 379, 0xff000000u);
    check(preview.paint_mask(argb, 640, 480, expected_mask),
          "native compositor rejected original room geometry");
    check(argb == original, "explicit native compositor differs from original VGA");
}

std::uint32_t captured_rtl_deadline(const fs::path &ram_path) {
    const auto ram = read_bytes(ram_path);
    // These private captures use the same validated RTL entry DS=5b46 and
    // relocated displacements as tools/inspect_rtl_animation.py. These are
    // launch-specific offsets, NOT addresses suitable for the SDL runtime.
    constexpr std::size_t ds = 0x5b46u * 16;
    if (ram.size() != 1024 * 1024 || ram[ds + 0x1428] != 1 ||
        ram[ds + 0x1429] != 0 || ram[ds + 0x9fb4] != 1 ||
        ram[ds + 0x9fe6] != 0 || ram[ds + 0x9ff4] != 0)
        throw std::runtime_error("unexpected private RTL animation snapshot layout");
    const std::size_t offset = ds + 0x9ffa;
    return std::uint32_t(ram[offset]) | (std::uint32_t(ram[offset + 1]) << 8) |
           (std::uint32_t(ram[offset + 2]) << 16) |
           (std::uint32_t(ram[offset + 3]) << 24);
}

void check_deadline_series(const gateway::HostScenePreview &preview, const fs::path &directory,
                           std::span<const int> times, std::uint32_t expected_reanchors) {
    if (!fs::exists(directory / ("t" + std::to_string(times.front()) + ".bin"))) {
        std::cout << "RTL RAM deadline captures unavailable; skipping phase comparison\n";
        return;
    }
    gateway::RtlXorDeadlinePhase tracker(500);
    std::size_t confident = 0, reanchors = 0;
    for (const auto ms : times) {
        const auto frame = ppm_argb(directory / ("t" + std::to_string(ms) + ".ppm"));
        auto scratch = frame;
        const auto mask = preview.replace_if_exact(scratch, 640, 480);
        check(mask.has_value(), "RTL deadline sample is not an exact first-room composition");
        const auto deadline = captured_rtl_deadline(directory / ("t" + std::to_string(ms) + ".bin"));
        const auto phase = tracker.observe(deadline);
        if (!phase) {
            tracker.synchronize(deadline, (*mask & 0b010u) != 0);
            ++reanchors;
            continue;
        }
        check(*phase == ((*mask & 0b010u) != 0),
              "RTL deadline-based phase prediction differs from original blink");
        // Repaint with the predicted indicator bit, rather than using the
        // original framebuffer's pixels or its inferred indicator phase.
        const auto independent_mask = (*mask & ~0b010u) |
                                      (*phase ? 0b010u : 0u);
        for (int y = 0; y < 224; ++y)
            std::fill_n(scratch.begin() + y * 640 + 261, 379, 0xff000000u);
        check(preview.paint_mask(scratch, 640, 480, independent_mask),
              "independent deadline scene painting failed");
        check(scratch == frame, "deadline-driven native scene differs from original frame");
        ++confident;
    }
    check(reanchors == expected_reanchors + 1,
          "unexpected number of RTL deadline discontinuities");
    check(confident + reanchors == times.size(), "missing RTL phase samples");
    std::cout << "Native RTL deadline: " << confident << " predicted frames, "
              << reanchors - 1 << " discontinuity re-anchors\n";
}

void original_room_preview(const fs::path &pic_path, const fs::path &scenes,
                           const fs::path &idle, const fs::path &actions) {
    const auto file = read_bytes(pic_path);
    const gateway::HostScenePreview preview(file, gateway::PicDialect::gateway, 15,
                                             std::array<std::size_t, 3>{16, 17, 18}, 261, 0);
    if (fs::is_directory(scenes)) {
        check_original(preview, scenes / "before.ppm", 0b110);
        check_original(preview, scenes / "open.ppm", 0b101);
        check_original(preview, scenes / "taken.ppm", 0b111);
        check_original(preview, scenes / "closed.ppm", 0b110);
        std::cout << "Native room preview: 4 still-frame matches\n";
    }
    if (fs::is_directory(idle)) {
        constexpr std::array<int, 8> idle_times{
            16000, 16250, 16500, 16750, 17000, 17250, 17500, 17750};
        for (int ms = 16000; ms <= 17750; ms += 250) {
            const auto mask = (ms / 500) % 2 == 1 ? 0b110u : 0b100u;
            check_original(preview, idle / ("t" + std::to_string(ms) + ".ppm"), mask);
        }
        check_deadline_series(preview, idle, idle_times, 0);
        std::cout << "Native room preview: 8 idle animation matches\n";
    }
    if (fs::is_directory(actions)) {
        constexpr std::array<int, 32> timestamps{
            19500, 19750, 20000, 20250, 20500, 20750, 21000, 21500,
            21750, 22000, 22250, 22500, 23000, 25000, 25500, 25750,
            26000, 26250, 26500, 26750, 27000, 30500, 31000, 31500,
            32000, 34500, 34750, 35000, 35250, 35500, 35750, 36000};
        const std::set<int> indicator_on{
            19750, 20000, 20750, 21000, 21500, 22250, 22500, 25500,
            26250, 26500, 30500, 31500, 34750, 35000, 35750, 36000};
        for (const int ms : timestamps) {
            const auto mask = 0b100u | (ms < 34500 ? 0b001u : 0) |
                              (indicator_on.contains(ms) ? 0b010u : 0);
            check_original(preview, actions / ("t" + std::to_string(ms) + ".ppm"), mask);
        }
        check_deadline_series(preview, actions, timestamps, 3);
        std::cout << "Native room preview: 32 action-interleaved matches\n";
    }
}

int main(int argc, char **argv) try {
    synthetic_scene_preview();
    if (argc == 5 && fs::exists(argv[1]))
        original_room_preview(argv[1], argv[2], argv[3], argv[4]);
    else
        std::cout << "Original private scene captures unavailable; synthetic preview passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << "host scene preview test: " << e.what() << '\n';
    return 1;
}

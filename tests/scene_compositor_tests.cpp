// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
#include "scene_animation.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::vector<std::uint8_t> read_bytes(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void synthetic_layers() {
    gateway::PicImage base;
    base.bits = 4;
    base.width = 4;
    base.height = 3;
    base.pixels = std::vector<std::uint8_t>(12, 2);
    base.palette = gateway::default_ega_palette();
    gateway::PicImage first;
    first.bits = 4;
    first.overlay = true;
    first.x = 1;
    first.y = 1;
    first.width = 2;
    first.height = 1;
    first.pixels = {1, 3};
    gateway::PicImage second = first;
    second.x = 2;
    second.pixels = {2, 1};

    const std::vector<gateway::PicImage> layers{first, second};
    const auto result = gateway::compose_pic_layers(base, layers);
    check(result.width == 4 && result.height == 3, "background bounds changed");
    check(result.pixels[5] == (0x2 ^ 0x1), "first layer failed");
    check(result.pixels[6] == (0x2 ^ 0x3 ^ 0x2), "overlapping layers failed");
    check(result.pixels[7] == (0x2 ^ 0x1), "second layer failed");
    check(result.pixels[0] == 2, "background pixel changed outside overlays");
    check(result.palette.size() == 16, "background palette was lost");
    check(gateway::compose_pic_layers(result, layers).pixels == base.pixels,
          "XOR layers are not reversible");

    std::vector<std::uint8_t> reference(6 * 5 * 3, 0);
    for (int y = 0; y < result.height; ++y)
        for (int x = 0; x < result.width; ++x) {
            const auto color = result.palette[result.pixels[static_cast<std::size_t>(y) *
                                                           result.width + x]];
            const auto at = (static_cast<std::size_t>(y + 1) * 6 + x + 1) * 3;
            reference[at] = color.r;
            reference[at + 1] = color.g;
            reference[at + 2] = color.b;
        }
    const auto match = gateway::compare_pic_scene_frame(result, reference, 6, 5, 1, 1);
    check(match.differing_pixels == 0 && match.compared_pixels == 12,
          "RGB reference match failed");
    reference[(2 * 6 + 2) * 3] ^= 0xff;
    check(gateway::compare_pic_scene_frame(result, reference, 6, 5, 1, 1)
              .differing_pixels == 1, "RGB reference mismatch count failed");
    try {
        gateway::compare_pic_scene_frame(result, reference, 6, 5, 4, 1);
        throw std::runtime_error("out-of-frame viewport was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("does not fit") != std::string::npos,
              "unexpected error for invalid RGB reference placement");
    }

    auto invalid = first;
    invalid.overlay = false;
    try {
        gateway::compose_pic_layers(base, std::span<const gateway::PicImage>(&invalid, 1));
        throw std::runtime_error("non-XOR layer was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("not an XOR") != std::string::npos,
              "unexpected error for non-XOR layer");
    }
    invalid.overlay = true;
    invalid.x = 3;
    try {
        gateway::compose_pic_layers(base, std::span<const gateway::PicImage>(&invalid, 1));
        throw std::runtime_error("out-of-bounds layer was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("does not fit") != std::string::npos,
              "unexpected error for out-of-bounds layer");
    }
}

void synthetic_animation() {
    gateway::PicImage background;
    background.bits = 4;
    background.width = 4;
    background.height = 2;
    background.pixels.assign(8, 2);
    background.palette = gateway::default_ega_palette();
    gateway::PicImage layer;
    layer.bits = 4;
    layer.overlay = true;
    layer.x = 1;
    layer.y = 0;
    layer.width = 2;
    layer.height = 1;
    layer.pixels = {1, 3};

    gateway::XorPicAnimation animation(background, layer, 500, 500);
    check(!animation.visible() && animation.next_toggle_ms() == 500,
          "animation did not start hidden");
    check(!animation.advance_to(499), "animation changed before first deadline");
    check(animation.advance_to(500) && animation.visible(),
          "animation missed first XOR deadline");
    check(animation.frame().pixels[1] == 3 && animation.frame().pixels[2] == 1,
          "animation XOR produced wrong pixels");
    check(!animation.advance_to(750), "animation changed between deadlines");
    check(animation.advance_to(1000) && !animation.visible(),
          "second XOR did not restore the original scene");
    check(animation.frame().pixels == background.pixels,
          "second XOR did not restore background pixels");
    check(animation.advance_to(1500) && animation.visible(),
          "third XOR did not activate the scene");
    check(!animation.advance_to(2600) && animation.visible(),
          "skipped even number of toggles must cancel");
    check(animation.next_toggle_ms() == 3000,
          "deadline did not advance across dropped frames");

    auto changed_background = background;
    changed_background.pixels.assign(8, 4);
    animation.replace_background(changed_background);
    check(animation.visible() && animation.next_toggle_ms() == 3000,
          "new background reset the animation phase");
    check(animation.frame().pixels[1] == (4 ^ 1) &&
              animation.frame().pixels[2] == (4 ^ 3),
          "animated layer was not reapplied to new background");
    check(animation.advance_to(3000) && !animation.visible() &&
              animation.frame().pixels == changed_background.pixels,
          "background was not restored after animated scene change");
    animation.synchronize(3250, true);
    check(animation.visible() && animation.frame().pixels[1] == (4 ^ 1),
          "animation phase synchronization did not apply XOR");
    check(animation.advance_to(3250) && !animation.visible(),
          "animation did not use externally supplied next deadline");

    try {
        animation.advance_to(2000);
        throw std::runtime_error("backward clock was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("backward") != std::string::npos,
              "unexpected backward-clock error");
    }
    try {
        gateway::XorPicAnimation invalid(background, layer, 100, 0);
        throw std::runtime_error("zero period was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("period") != std::string::npos,
              "unexpected zero-period error");
    }
    auto bad_layer = layer;
    bad_layer.overlay = false;
    try {
        gateway::XorPicAnimation invalid(background, bad_layer, 100, 500);
        throw std::runtime_error("non-overlay animation layer was accepted");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()).find("XOR") != std::string::npos,
              "unexpected invalid-animation-layer error");
    }
    try {
        gateway::XorPicAnimation overflow(background, layer, 0, 1);
        overflow.advance_to(UINT64_MAX);
        throw std::runtime_error("animation deadline overflow was accepted");
    } catch (const std::overflow_error &) {
        // This timing combination has no representable future deadline.
    }
}

void check_native_idle_animation(const std::vector<std::uint8_t> &pic,
                                 const fs::path &directory) {
    const auto base = gateway::decode_pic_scene(pic, gateway::PicDialect::gateway,
                                                 15, std::vector<std::size_t>{18});
    const auto entries = gateway::parse_pic_directory(pic);
    gateway::XorPicAnimation animation(
        base, gateway::decode_pic(pic, entries.at(17), gateway::PicDialect::gateway),
        16500, 500);
    const std::string header = "P6\n640 480\n255\n";
    for (int ms = 16000; ms <= 17750; ms += 250) {
        const auto native = read_bytes(directory / ("t" + std::to_string(ms) + ".ppm"));
        check(native.size() == header.size() + 640 * 480 * 3 &&
                  std::equal(header.begin(), header.end(), native.begin()),
              "invalid original animation frame");
        animation.advance_to(ms);
        const auto match = gateway::compare_pic_scene_frame(
            animation.frame(),
            std::span<const std::uint8_t>(native).subspan(header.size()),
            640, 480, 261, 0);
        check(match.compared_pixels == 84896 && match.differing_pixels == 0,
              "host animation differs from original first-room frame");
    }
}

void check_native_room(const std::vector<std::uint8_t> &pic, const fs::path &ppm_path,
                       std::uint32_t expected_mask) {
    const auto native = read_bytes(ppm_path);
    const std::string header = "P6\n640 480\n255\n";
    check(native.size() == header.size() + 640 * 480 * 3, "unexpected original PPM size");
    check(std::equal(header.begin(), header.end(), native.begin()), "unexpected original PPM header");
    const std::vector<std::size_t> candidates{16, 17, 18};
    const auto match = gateway::infer_pic_scene_layers(
        pic, gateway::PicDialect::gateway, 15, {}, candidates,
        std::span<const std::uint8_t>(native).subspan(header.size()), 640, 480, 261, 0);
    check(match.optional_layer_mask == expected_mask, "wrong inferred native overlays");
    check(match.equally_good_masks == 1, "native frame overlay set is ambiguous");
    check(match.compared_pixels == 379 * 224 && match.differing_pixels == 0,
          "native frame cannot be reconstructed exactly");
    std::vector<std::size_t> layers;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (expected_mask & (1u << i))
            layers.push_back(candidates[i]);
    const auto scene = gateway::decode_pic_scene(pic, gateway::PicDialect::gateway, 15, layers);
    check(scene.width == 379 && scene.height == 224 && scene.x == 0 && scene.y == 0,
          "unexpected original scene geometry");
    std::size_t different = 0;
    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            const auto index = scene.pixels[static_cast<std::size_t>(y) * scene.width + x];
            check(index < scene.palette.size(), "invalid palette index in scene");
            const auto rgb = scene.palette[index];
            // The actual native scene begins at column 261 (not 260).
            const auto at = header.size() + (static_cast<std::size_t>(y) * 640 + 261 + x) * 3;
            if (rgb.r != native[at] || rgb.g != native[at + 1] || rgb.b != native[at + 2])
                ++different;
        }
    }
    check(different == 0, "composed scene differs from native room pixels");
}

int main(int argc, char **argv) try {
    synthetic_layers();
    synthetic_animation();
    if ((argc == 6 || argc == 7) && fs::exists(argv[1]) && fs::exists(argv[2]) &&
        fs::exists(argv[3]) && fs::exists(argv[4]) && fs::exists(argv[5])) {
        const auto pic = read_bytes(argv[1]);
        check_native_room(pic, argv[2], 0b110); // indicator + card
        check_native_room(pic, argv[3], 0b101); // drawer + card
        check_native_room(pic, argv[4], 0b111); // drawer + indicator + card
        check_native_room(pic, argv[5], 0b110); // indicator + card
        std::cout << "Original native scenes: all four frames match all 84,896 pixels\n";
    } else {
        std::cout << "Original private PIC/PPM fixtures unavailable; synthetic tests passed\n";
    }
    if (argc == 7 && fs::exists(argv[1]) && fs::is_directory(argv[6])) {
        check_native_idle_animation(read_bytes(argv[1]), argv[6]);
        std::cout << "Host XOR animation: all eight idle captures match all 84,896 pixels\n";
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << "scene compositor test: " << e.what() << '\n';
    return 1;
}

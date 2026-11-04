// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
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
    if (argc == 6 && fs::exists(argv[1]) && fs::exists(argv[2]) &&
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
    return 0;
} catch (const std::exception &e) {
    std::cerr << "scene compositor test: " << e.what() << '\n';
    return 1;
}

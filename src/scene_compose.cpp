// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
#include "scene_animation.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::size_t entry_number(const std::string &arg) {
    std::size_t n = 0;
    auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if (ec != std::errc{} || ptr != arg.data() + arg.size())
        throw std::runtime_error("invalid PIC entry number: " + arg);
    return n;
}

int coordinate(const std::string &arg) {
    int n = 0;
    auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if (ec != std::errc{} || ptr != arg.data() + arg.size())
        throw std::runtime_error("invalid screen coordinate: " + arg);
    return n;
}

std::uint64_t millisecond_value(const std::string &arg) {
    std::uint64_t n = 0;
    auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if (ec != std::errc{} || ptr != arg.data() + arg.size())
        throw std::runtime_error("invalid guest milliseconds: " + arg);
    return n;
}

struct PpmFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

PpmFrame read_ppm(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    std::string magic;
    int max_color = 0;
    PpmFrame frame;
    in >> magic >> frame.width >> frame.height >> max_color;
    if (!in || magic != "P6" || frame.width < 1 || frame.height < 1 ||
        frame.width > 8192 || frame.height > 8192 || max_color != 255 ||
        in.get() != '\n')
        throw std::runtime_error("expected an uncompressed P6 PPM with 8-bit RGB pixels");
    frame.pixels.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height * 3)
        throw std::runtime_error("PPM pixel count does not match dimensions");
    return frame;
}

void usage() {
    std::cout << "usage: gateway-scene [-o IMAGE.png] --base ENTRY [--layer ENTRY ...] FILE.PIC\n"
                 "       [--reference-ppm FRAME.ppm --screen-x X --screen-y Y\n"
                 "        --optional-layer ENTRY [--optional-layer ENTRY ...]]\n"
                 "       [--animate-layer ENTRY --animate-next-toggle-ms MS\n"
                 "        --animate-period-ms MS --at-ms MS [--animation-visible]]\n"
                 "Compose a decoded PIC base with enabled, positioned XOR overlays.\n"
                 "With a reference P6 frame, enumerate optional layers and report the\n"
                 "closest pixel match. Alternatively, animate one XOR layer using\n"
                 "an explicit guest-clock deadline, phase and interval.\n"
                 "Animation is opt-in and does not replace the guest scheduler.\n";
}
} // namespace

int main(int argc, char **argv) try {
    fs::path output;
    fs::path input;
    fs::path reference;
    std::size_t base = 0;
    bool have_base = false;
    std::vector<std::size_t> layers;
    std::vector<std::size_t> optional_layers;
    std::size_t animated_layer = 0;
    std::uint64_t next_toggle_ms = 0;
    std::uint64_t at_ms = 0;
    std::uint64_t period_ms = 0;
    bool have_animated_layer = false;
    bool have_toggle_time = false;
    bool have_period = false;
    bool have_at = false;
    bool initially_visible = false;
    int screen_x = 0;
    int screen_y = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--base" && i + 1 < argc) {
            base = entry_number(argv[++i]);
            have_base = true;
        } else if (arg == "--layer" && i + 1 < argc) {
            layers.push_back(entry_number(argv[++i]));
        } else if (arg == "--optional-layer" && i + 1 < argc) {
            optional_layers.push_back(entry_number(argv[++i]));
        } else if (arg == "--animate-layer" && i + 1 < argc) {
            animated_layer = entry_number(argv[++i]);
            have_animated_layer = true;
        } else if (arg == "--animate-next-toggle-ms" && i + 1 < argc) {
            next_toggle_ms = millisecond_value(argv[++i]);
            have_toggle_time = true;
        } else if (arg == "--animate-period-ms" && i + 1 < argc) {
            period_ms = millisecond_value(argv[++i]);
            have_period = true;
        } else if (arg == "--at-ms" && i + 1 < argc) {
            at_ms = millisecond_value(argv[++i]);
            have_at = true;
        } else if (arg == "--animation-visible") {
            initially_visible = true;
        } else if (arg == "--reference-ppm" && i + 1 < argc) {
            reference = argv[++i];
        } else if (arg == "--screen-x" && i + 1 < argc) {
            screen_x = coordinate(argv[++i]);
        } else if (arg == "--screen-y" && i + 1 < argc) {
            screen_y = coordinate(argv[++i]);
        } else if (!arg.empty() && arg.front() != '-' && input.empty()) {
            input = arg;
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + arg);
        }
    }
    if (input.empty() || (output.empty() && reference.empty()) || !have_base ||
        (reference.empty() && !optional_layers.empty()) ||
        (have_animated_layer != have_toggle_time) ||
        (have_animated_layer != have_period) ||
        (have_animated_layer != have_at) ||
        (!have_animated_layer && initially_visible) ||
        (have_animated_layer && !optional_layers.empty()) ||
        (have_animated_layer && (period_ms == 0 || period_ms > UINT32_MAX))) {
        usage();
        return 2;
    }
    std::ifstream in(input, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + input.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                           std::istreambuf_iterator<char>()};
    if (have_animated_layer) {
        const auto entries = gateway::parse_pic_directory(bytes);
        if (animated_layer >= entries.size())
            throw std::runtime_error("animated scene layer outside PIC directory");
        auto base_image = gateway::decode_pic_scene(bytes, gateway::PicDialect::gateway,
                                                     base, layers);
        auto animation = gateway::XorPicAnimation(
            std::move(base_image),
            gateway::decode_pic(bytes, entries[animated_layer], gateway::PicDialect::gateway),
            next_toggle_ms, static_cast<std::uint32_t>(period_ms), initially_visible);
        animation.advance_to(at_ms);
        const auto &result = animation.frame();
        if (!reference.empty()) {
            const auto ppm = read_ppm(reference);
            const auto match = gateway::compare_pic_scene_frame(
                result, ppm.pixels, ppm.width, ppm.height, screen_x, screen_y);
            std::cout << reference << ": " << match.differing_pixels << '/'
                      << match.compared_pixels << " differing scene pixels; animated layer "
                      << animated_layer << (animation.visible() ? " on" : " off") << '\n';
        }
        if (!output.empty()) {
            if (result.palette.empty())
                throw std::runtime_error("scene background has no usable palette");
            const auto png = gateway::encode_indexed_png(
                result.width, result.height, result.bits, result.pixels, result.palette);
            std::ofstream out(output, std::ios::binary);
            if (!out)
                throw std::runtime_error("cannot write " + output.string());
            out.write(reinterpret_cast<const char *>(png.data()),
                      static_cast<std::streamsize>(png.size()));
            if (!out)
                throw std::runtime_error("error writing " + output.string());
            std::cout << output << ": animated scene at " << at_ms << " guest ms\n";
        }
        return 0;
    }
    if (!reference.empty()) {
        const auto ppm = read_ppm(reference);
        const auto match = gateway::infer_pic_scene_layers(
            bytes, gateway::PicDialect::gateway, base, layers, optional_layers,
            ppm.pixels, ppm.width, ppm.height, screen_x, screen_y);
        std::cout << reference << ": " << match.differing_pixels << '/'
                  << match.compared_pixels << " differing scene pixels; "
                  << match.equally_good_masks << " equally good layer sets; selected entries:";
        for (const auto entry : layers)
            std::cout << ' ' << entry;
        for (std::size_t i = 0; i < optional_layers.size(); ++i) {
            if (match.optional_layer_mask & (1u << i)) {
                layers.push_back(optional_layers[i]);
                std::cout << ' ' << optional_layers[i];
            }
        }
        std::cout << '\n';
    }
    if (output.empty())
        return 0;
    const auto result = gateway::decode_pic_scene(bytes, gateway::PicDialect::gateway,
                                                   base, layers);
    if (result.palette.empty())
        throw std::runtime_error("scene background has no usable palette");
    const auto png = gateway::encode_indexed_png(result.width, result.height,
                                                  result.bits, result.pixels, result.palette);
    std::ofstream out(output, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot write " + output.string());
    out.write(reinterpret_cast<const char *>(png.data()),
              static_cast<std::streamsize>(png.size()));
    if (!out)
        throw std::runtime_error("error writing " + output.string());
    std::cout << output << ": " << result.width << 'x' << result.height << ", "
              << layers.size() << " active XOR layers\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << "gateway-scene: " << e.what() << '\n';
    return 1;
}

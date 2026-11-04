// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
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

void usage() {
    std::cout << "usage: gateway-scene -o IMAGE.png --base ENTRY [--layer ENTRY ...] FILE.PIC\n"
                 "Compose a decoded PIC base with enabled, positioned XOR overlays.\n"
                 "The caller must supply the enabled layers; no story logic is inferred.\n";
}
} // namespace

int main(int argc, char **argv) try {
    fs::path output;
    fs::path input;
    std::size_t base = 0;
    bool have_base = false;
    std::vector<std::size_t> layers;
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
        } else if (!arg.empty() && arg.front() != '-' && input.empty()) {
            input = arg;
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + arg);
        }
    }
    if (input.empty() || output.empty() || !have_base) {
        usage();
        return 2;
    }
    std::ifstream in(input, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + input.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                           std::istreambuf_iterator<char>()};
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

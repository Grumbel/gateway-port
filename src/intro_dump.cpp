// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "story/intro_presentation.hpp"
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) try {
    if (argc != 5 || std::string_view(argv[2]) != "--ms") {
        std::cerr << "usage: gateway-intro-dump DOS-DIR --ms GUEST-MS OUTPUT.ppm\n";
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    std::uint32_t ms = 0;
    const std::string_view number(argv[3]);
    if (auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), ms);
        ec != std::errc{} || end != number.data() + number.size())
        throw std::runtime_error("invalid millisecond timestamp");
    const gateway::IntroPresentation presentation(argv[1]);
    const auto frame = presentation.frame_at(ms);
    std::ofstream out(argv[4], std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot create PPM output");
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (const auto p : frame.argb) {
        const char rgb[3]{static_cast<char>(p >> 16), static_cast<char>(p >> 8),
                          static_cast<char>(p)};
        out.write(rgb, 3);
    }
    if (!out)
        throw std::runtime_error("failed to write intro image");
    std::cout << "native intro " << ms << "ms, " << frame.width << 'x' << frame.height;
    if (ms >= 20128)
        std::cout << ", narrative page " << presentation.page_at(ms);
    std::cout << ", " << presentation.verified_pages() << " verified pages / "
              << presentation.text_count() << " original text records\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-intro-dump: " << error.what() << '\n';
    return 1;
}

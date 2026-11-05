// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "story/intro_presentation.hpp"
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) try {
    if ((argc != 5 && argc != 7) || std::string_view(argv[2]) != "--ms" ||
        (argc == 7 && std::string_view(argv[5]) != "--words" &&
         std::string_view(argv[5]) != "--ink-pixels")) {
        std::cerr << "usage: gateway-intro-dump DOS-DIR --ms GUEST-MS OUTPUT.ppm "
                     "[--words COUNT | --ink-pixels COUNT]\n";
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    std::uint32_t ms = 0;
    const std::string_view number(argv[3]);
    if (auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), ms);
        ec != std::errc{} || end != number.data() + number.size())
        throw std::runtime_error("invalid millisecond timestamp");
    std::optional<std::size_t> words;
    std::optional<std::size_t> ink_pixels;
    if (argc == 7) {
        auto &count_value = std::string_view(argv[5]) == "--words" ? words : ink_pixels;
        count_value = 0;
        const std::string_view count(argv[6]);
        if (auto [end, ec] = std::from_chars(count.data(), count.data() + count.size(),
                                             *count_value);
            ec != std::errc{} || end != count.data() + count.size())
            throw std::runtime_error("invalid intro caption progress count");
        if (ms < 20128)
            throw std::runtime_error("caption progress options require a prose intro timestamp");
    }
    const gateway::IntroPresentation presentation(argv[1]);
    gateway::IntroFrame frame;
    if (words)
        frame = presentation.page_frame_words(presentation.page_at(ms), *words);
    else if (ink_pixels)
        frame = presentation.page_frame_ink_pixels(presentation.page_at(ms), *ink_pixels);
    else
        frame = presentation.frame_at(ms);
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
    if (words)
        std::cout << ", " << *words << " words visible";
    if (ink_pixels)
        std::cout << ", " << *ink_pixels << " ink pixels visible";
    std::cout << ", " << presentation.verified_pages() << " verified pages / "
              << presentation.text_count() << " original text records\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-intro-dump: " << error.what() << '\n';
    return 1;
}

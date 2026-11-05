// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "story/intro_presentation.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const auto data = std::filesystem::path(argv[1]);
    if (!std::filesystem::exists(data / "GATE.EXE")) {
        std::cout << "original Gateway assets not installed: skipped\n";
        return 77;
    }
    const gateway::IntroPresentation intro(data);
    assert(intro.verified_pages() == 10);
    assert(intro.text_count() == 18);
    assert(intro.page_at(20127) == 0);
    assert(intro.page_at(39697) == 0);
    assert(intro.page_at(39698) == 1);
    assert(intro.page_at(175000) == 9);
    assert(intro.text(0).size() > 350);
    assert(intro.text(9).size() > 300);

    const auto splash = intro.frame_at(5000);
    assert(splash.width == 640 && splash.height == 480);
    std::size_t black = 0;
    for (auto pixel : splash.argb)
        black += pixel == 0xff000000u;
    // The original 5-second framebuffer has exactly 3,046 black pixels.
    assert(black == 3046);

    const auto title = intro.frame_at(9000);
    assert(title.width == 320 && title.height == 200);
    const auto bars = intro.frame_at(13000);
    std::size_t red = 0;
    for (const auto pixel : bars.argb)
        red += pixel == 0xffff0000u;
    assert(red == 2394); // seven 9x38 strokes, as in the native capture
    const auto spacecraft = intro.frame_at(41400);
    assert(spacecraft.argb[25 * 320 + 230] == 0xffffffffu);
    const auto before_caption = intro.frame_at(45000);
    const auto after_caption = intro.frame_at(45500);
    assert(before_caption.argb[100 * 320 + 20] != 0xffa2a2a2u);
    assert(after_caption.argb[100 * 320 + 20] == 0xffa2a2a2u);
    const auto first = intro.page_frame(0);
    assert(first.width == 320 && first.height == 200);
    assert(first.argb.front() == 0xffae5938u); // PIC_301 #5 at (0,0)
    assert(first.argb[97 * 320 + 13] == 0xffa2a2a2u); // caption background
    const auto next = intro.page_frame(3);
    assert(next.argb[13 * 320 + 13] == 0xffa2a2a2u);
    assert(next.argb.front() != first.argb.front());
    // Every verified narrative page must decode its source picture and fit its caption.
    for (std::size_t page = 0; page < intro.verified_pages(); ++page) {
        const auto frame = intro.page_frame(page);
        assert(frame.argb.size() == 320u * 200);
        assert(intro.page_info(page).start_ms >= 20128);
    }
    std::cout << "Gateway native intro: splash and first ten narrative pages decoded\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "intro test: " << error.what() << '\n';
    return 1;
}

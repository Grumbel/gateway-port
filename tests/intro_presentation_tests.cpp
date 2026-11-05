// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "story/intro_presentation.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

std::uint64_t rgb_fingerprint(const gateway::IntroFrame &frame) {
    // FNV-1a over PPM-order RGB bytes, independent of pixel endianness.
    std::uint64_t value = 14695981039346656037ull;
    for (const auto pixel : frame.argb)
        for (unsigned shift : {16u, 8u, 0u}) {
            value ^= (pixel >> shift) & 255u;
            value *= 1099511628211ull;
        }
    return value;
}

} // namespace

int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const auto data = std::filesystem::path(argv[1]);
    if (!std::filesystem::exists(data / "GATE.EXE")) {
        std::cout << "original Gateway assets not installed: skipped\n";
        return 77;
    }
    const gateway::IntroPresentation intro(data);
    assert(intro.verified_pages() == 18);
    assert(intro.text_count() == 18);
    assert(intro.page_at(20127) == 0);
    assert(intro.page_at(39697) == 0);
    assert(intro.page_at(39698) == 1);
    assert(intro.page_at(175000) == 9);
    assert(intro.page_at(180000) == 9);
    assert(intro.page_at(182000) == 10);
    assert(intro.page_at(198000) == 11);
    assert(intro.page_at(213000) == 12);
    assert(intro.page_at(223000) == 13);
    assert(intro.page_at(238000) == 14);
    assert(intro.page_at(274000) == 15);
    assert(intro.page_at(286000) == 16);
    assert(intro.page_at(303000) == 17);
    assert(intro.page_info(0).text_y == 96);  // bottom-clamped preferred y=120
    assert(intro.page_info(12).text_y == 122); // preferred y=120 fits here
    assert(intro.page_info(15).text_y == 63); // tall text moves preferred y=100 up
    assert(intro.page_info(4).text_x == 162);
    assert(intro.page_info(4).text_width == 147);
    assert(intro.text(0).size() > 350);
    assert(intro.text(9).size() > 300);
    assert(intro.text(17).size() > 400);
    assert(intro.page_display_duration_ms(0) == 19415);
    assert(intro.page_display_duration_ms(1) == 11765);
    assert(intro.page_display_duration_ms(17) == 20585);

    const auto splash = intro.frame_at(5000);
    assert(splash.width == 640 && splash.height == 480);
    std::size_t black = 0;
    for (auto pixel : splash.argb)
        black += pixel == 0xff000000u;
    // The original 5-second framebuffer has exactly 3,046 black pixels.
    assert(black == 3046);

    const auto title = intro.frame_at(9000);
    assert(title.width == 320 && title.height == 200);
    assert(intro.frame_at(15400).argb[64 * 320 + 11] == 0xff000000u);
    assert(intro.frame_at(15450).argb[64 * 320 + 11] == 0xff040404u);
    assert(intro.frame_at(15500).argb[64 * 320 + 11] == 0xffaeaeaeu);
    assert(intro.frame_at(15530).argb[64 * 320 + 11] == 0xffffffffu);
    assert(intro.frame_at(18000).argb[64 * 320 + 11] == 0xffffffffu);
    const auto bars = intro.frame_at(13000);
    std::size_t red = 0;
    for (const auto pixel : bars.argb)
        red += pixel == 0xffff0000u;
    assert(red == 2394); // seven 9x38 strokes, as in the native capture
    const auto spacecraft = intro.frame_at(41400);
    assert(spacecraft.argb[25 * 320 + 230] == 0xffffffffu);
    const auto before_caption = intro.frame_at(44999);
    const auto after_caption = intro.frame_at(45000);
    assert(before_caption.argb[100 * 320 + 20] != 0xffa2a2a2u);
    assert(after_caption.argb[100 * 320 + 20] == 0xffa2a2a2u);
    const auto first = intro.page_frame(0);
    assert(first.width == 320 && first.height == 200);
    assert(first.argb.front() == 0xffae5938u); // PIC_301 #5 at (0,0)
    assert(first.argb[97 * 320 + 13] == 0xffa2a2a2u); // caption background
    assert(first.argb[94 * 320 + 10] == 0xff000000u); // outer border
    assert(first.argb[95 * 320 + 11] == 0xffffffffu); // 2px highlight
    const auto next = intro.page_frame(3);
    assert(next.argb[13 * 320 + 13] == 0xffa2a2a2u);
    assert(next.argb.front() != first.argb.front());
    assert(intro.frame_at(258000).argb.size() == 320u * 200);
    assert(intro.frame_at(262000).argb.size() == 320u * 200);
    assert(intro.frame_at(266000).argb == intro.frame_at(267000).argb);
    const auto title_card = intro.frame_at(269000);
    assert(title_card.argb[70 * 320 + 185] != 0xff000000u);
    assert(intro.frame_at(268000).argb[70 * 320 + 185] == 0xff000000u);
    assert(intro.frame_at(284000).argb[70 * 320 + 185] == 0xff000000u);
    assert(intro.frame_at(285000).argb != intro.frame_at(284000).argb);
    // All eighteen rectangles were independently measured from the original
    // framebuffer. The implementation must instead derive them from the
    // original call arguments and the reconstructed proportional word layout.
    constexpr std::array<int, 18> original_x{{
        12, 12, 12, 12, 162, 182, 182, 132, 142, 142, 142, 12, 62, 142, 122, 12, 12, 12
    }};
    constexpr std::array<int, 18> original_y{{
        96, 96, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 122, 96, 12, 63, 12, 12
    }};
    constexpr std::array<int, 18> original_width{{
        297, 167, 147, 297, 147, 127, 127, 177, 167, 167, 167, 127, 247, 167, 187, 147, 297, 297
    }};
    constexpr std::array<int, 18> original_height{{
        93, 93, 93, 60, 148, 104, 159, 104, 82, 137, 115, 159, 60, 93, 148, 126, 82, 126
    }};
    for (std::size_t page = 0; page < intro.verified_pages(); ++page) {
        const auto frame = intro.page_frame(page);
        assert(frame.argb.size() == 320u * 200);
        const auto info = intro.page_info(page);
        assert(info.start_ms >= 20128);
        assert(info.text_x == original_x[page]);
        assert(info.text_y == original_y[page]);
        assert(info.text_width == original_width[page]);
        assert(info.text_height == original_height[page]);
    }
    // Fingerprints taken from independent, read-only original GATE.EXE
    // framebuffer captures. This detects changes even when the recovered
    // box geometry and line counts remain the same. Original pixels are not
    // embedded in source or Git.
    constexpr std::array<std::pair<std::uint32_t, std::uint64_t>, 22> pixel_exact{{
        // The original palette ramp visits bars 1,6,4,7,2,5,3, with
        // non-primary-color samples at 11.5 and 12 seconds.
        {8500, 0x9ebf9a6ec921bb25ull},
        {8610, 0xbff27daee428d2a5ull},
        {9000, 0x57ec896d90382a25ull},
        {9500, 0xf50f72a95651e435ull},
        {9750, 0x4c877b0a561178a5ull},
        {10000, 0xb4028243a992a825ull},
        {10500, 0x4d7912c733b1f469ull},
        {11000, 0x2905961d89689425ull},
        {11500, 0xc31237a932083cd1ull},
        {12000, 0xe8b6cb9b669248d1ull},
        {12030, 0x86a094f806c3b081ull},
        {12500, 0x2369d709925b6425ull},
        {19000, 0x9ebf9a6ec921bb25ull},
        {22000, 0xed01e80d9a31a63eull},
        {45000, 0x3080c454f329377full},
        {60000, 0x93414ea68191f519ull},
        {70000, 0xcf0ee22b73f42aa5ull},
        {100000, 0x446a224a1b3db04full},
        {145000, 0xf8744e400db78d32ull},
        {186000, 0x3ddb1e10ba0020edull},
        {274000, 0x94c1e710d35a7a92ull},
        {305000, 0x8dd2e333a431bef7ull},
    }};
    for (const auto &[ms, fingerprint] : pixel_exact)
        assert(rgb_fingerprint(intro.frame_at(ms)) == fingerprint);
    std::cout << "Gateway native intro: splash, all eighteen narrative pages, "
                 "and animations decoded\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "intro test: " << error.what() << '\n';
    return 1;
}

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: LicenseRef-Gateway-Derived

#pragma once

#include "pic.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gateway {

struct IntroFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> argb;
};

struct IntroPageInfo {
    std::uint32_t start_ms;
    unsigned pic_file;
    std::size_t pic_entry;
    int text_x, text_y, text_width, text_height;
};

// A bounded, independent reconstruction of the introductory presentation in
// GATE.EXE. It never executes guest code and reads all art/text from the user's
// original DOS distribution. All eighteen narrative pages, the title palette
// reveal, and several sprite-animation snapshots have reference captures;
// the original timing/input scheduler is still under investigation.
class IntroPresentation {
public:
    explicit IntroPresentation(const std::filesystem::path &data_directory);

    IntroFrame frame_at(std::uint32_t elapsed_ms) const;
    IntroFrame page_frame(std::size_t page) const;
    std::size_t page_at(std::uint32_t elapsed_ms) const;
    std::size_t verified_pages() const noexcept;
    std::size_t text_count() const noexcept { return paragraphs_.size(); }
    const std::string &text(std::size_t page) const;
    std::uint16_t page_display_duration_ms(std::size_t page) const;
    IntroPageInfo page_info(std::size_t page) const;

private:
    struct CaptionGlyph {
        unsigned char character;
        int x, y;
    };
    struct CaptionLayout {
        std::vector<CaptionGlyph> glyphs;
        int lines = 1;
    };
    struct Font {
        int row_bytes = 0;
        int height = 0;
        std::uint8_t first = 0;
        std::uint8_t last = 0;
        std::vector<std::uint8_t> widths;
        std::vector<std::uint8_t> spacing;
        std::vector<std::uint8_t> pixels;
        int advance(unsigned char character) const;
    };

    std::vector<std::uint8_t> pic201_, pic300_, pic301_, pic302_, pic303_, pic310_;
    std::vector<std::string> paragraphs_;
    Font caption_font_;
    std::vector<CaptionLayout> caption_layouts_;
    std::vector<IntroPageInfo> page_infos_;
    mutable std::map<std::pair<unsigned, std::size_t>, PicImage> picture_cache_;
    PicImage load_picture(unsigned file, std::size_t index) const;
    CaptionLayout layout_caption(std::size_t page, int line_width) const;
    void caption(IntroFrame &frame, std::size_t page) const;
};

} // namespace gateway

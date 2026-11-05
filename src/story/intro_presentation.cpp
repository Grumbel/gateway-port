// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: LicenseRef-Gateway-Derived

#include "intro_presentation.hpp"
#include "scene_compositor.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace gateway {
namespace {

// DOS-read transitions and caption rectangles measured from read-only GATE.EXE
// replays under a fixed guest clock. These are not yet verified as source-code
// timer constants. The PIC index is the original directory slot, not a sprite
// number inferred from its appearance.
constexpr std::array<IntroPageInfo, 18> observed_pages{{
    {20128, 301, 5, 12, 96, 297, 93},
    {39698, 301, 5, 12, 96, 167, 93},
    {55379, 301, 0, 12, 12, 147, 93},
    {67687, 301, 0, 12, 12, 297, 60},
    {82369, 301, 1, 162, 12, 147, 148},
    {101369, 301, 2, 182, 12, 127, 104},
    {114428, 301, 2, 182, 12, 127, 159},
    {132560, 302, 33, 132, 12, 177, 104},
    {152058, 302, 33, 142, 12, 167, 82},
    {163217, 302, 33, 142, 12, 167, 137},
    // The later panels were bracketed by independent original framebuffer
    // captures. The millisecond boundaries are still approximate.
    {181000, 302, 33, 142, 12, 167, 115},
    {197000, 301, 3, 12, 12, 127, 159},
    {212000, 301, 4, 62, 122, 247, 60},
    {222000, 301, 4, 142, 96, 167, 93},
    {235500, 302, 0, 122, 12, 187, 148},
    {271000, 310, 4, 12, 63, 147, 126},
    {285500, 303, 42, 12, 12, 297, 82},
    {302000, 303, 42, 12, 12, 297, 126},
}};

// GATE_300 #1..#26 preload at 12.568..13.148s, then display later.
// #27 is the final non-XOR title. The delta-frame scheduler remains
// approximate; the credit lettering uses palette index 23 in #27 itself.
constexpr std::array<std::uint32_t, 7> bar_reveal_ms{{
    8600, 9200, 9700, 10200, 10800, 11800, 12300,
}};
constexpr std::array<int, 7> bar_x{{24, 64, 105, 149, 194, 242, 284}};

struct SpriteKeyframe {
    std::uint32_t ms;
    unsigned entry;
};

// Timings sampled at 0.5-second intervals in the original DOS launcher.
// Adjacent sprite indices are interpolated between the measured points, so
// intermediate frames are plausible but are not yet instruction-level timing
// matches. Each listed sample was independently verified pixel-for-pixel.
constexpr std::array<SpriteKeyframe, 6> approach_ship{{
    {256000, 2}, {256500, 7}, {257000, 12},
    {257500, 17}, {258000, 22}, {258500, 27},
}};
constexpr std::array<SpriteKeyframe, 7> station_ship{{
    {260000, 37}, {260500, 40}, {261000, 43}, {261500, 47},
    {262000, 50}, {262500, 53}, {263000, 57},
}};
constexpr std::array<SpriteKeyframe, 6> departing_ship{{
    {263700, 1}, {264000, 7}, {264500, 14},
    {265000, 24}, {265500, 34}, {266000, 41},
}};

// The credit is not a second font. GATE_300 #27 already contains its 1,382
// glyph pixels as palette index 23, initially black. The native launcher
// brightens that one DAC slot (6-bit grayscale) during an ~85ms reveal.
constexpr std::array<SpriteKeyframe, 10> title_credit_ramp{{
    {15440, 0}, {15450, 1}, {15460, 9}, {15470, 18}, {15480, 26},
    {15490, 34}, {15500, 43}, {15510, 51}, {15520, 60}, {15530, 63},
}};

unsigned sprite_at(std::uint32_t ms, std::span<const SpriteKeyframe> timeline) {
    if (ms < timeline.front().ms)
        return timeline.front().entry;
    for (std::size_t i = 1; i < timeline.size(); ++i) {
        if (ms < timeline[i].ms) {
            const auto &a = timeline[i - 1], &b = timeline[i];
            return a.entry + static_cast<unsigned>(
                (std::uint64_t(ms - a.ms) * (b.entry - a.entry)) / (b.ms - a.ms));
        }
    }
    return timeline.back().entry;
}

std::vector<std::uint8_t> load_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open original intro resource: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::string> load_paragraphs(std::span<const std::uint8_t> exe) {
    // Original GATE.EXE is fingerprinted in docs/executable.md. Its eighteen
    // contiguous intro strings start at this file offset, not in GATEMAIN.
    // No copyrighted game text is embedded in our C++ sources.
    constexpr std::size_t expected_exe_size = 22968;
    constexpr std::size_t first_offset = 17039;
    constexpr std::size_t paragraph_count = 18;
    if (exe.size() != expected_exe_size)
        throw std::runtime_error("unsupported GATE.EXE: introductory prose unavailable");
    std::size_t at = first_offset;
    std::vector<std::string> paragraphs;
    // Scan consecutive NUL-terminated strings. The original .EXE, not this
    // project, supplies their copyrighted wording.
    while (at < exe.size() && paragraphs.size() < paragraph_count) {
        const auto end = std::find(exe.begin() + at, exe.end(), 0);
        if (end == exe.end() || end - (exe.begin() + at) > 4096)
            throw std::runtime_error("unterminated Gateway intro paragraph");
        std::string value(reinterpret_cast<const char *>(exe.data() + at),
                          static_cast<std::size_t>(end - (exe.begin() + at)));
        if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return ch == 10 || ch == 13 || ch >= 32;
            }))
            throw std::runtime_error("invalid Gateway intro paragraph");
        paragraphs.push_back(std::move(value));
        at = static_cast<std::size_t>(end - exe.begin()) + 1;
    }
    if (paragraphs.size() != paragraph_count || at >= exe.size() || exe[at] != 0)
        throw std::runtime_error("unsupported GATE.EXE: incomplete intro paragraph table");
    return paragraphs;
}

std::uint32_t pixel(Rgb8 rgb) {
    return 0xff000000u | (std::uint32_t(rgb.r) << 16) |
           (std::uint32_t(rgb.g) << 8) | rgb.b;
}

void fill(IntroFrame &frame, int x, int y, int w, int h, std::uint32_t color) {
    for (int row = std::max(0, y); row < std::min(frame.height, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(frame.width, x + w); ++col)
            frame.argb[static_cast<std::size_t>(row) * frame.width + col] = color;
}

void paint(IntroFrame &frame, const PicImage &picture) {
    if (picture.pixels.size() != std::size_t(picture.width) * picture.height ||
        picture.x < 0 || picture.y < 0 ||
        picture.x + picture.width > frame.width ||
        picture.y + picture.height > frame.height)
        throw std::runtime_error("intro image does not fit the screen");
    for (int y = 0; y < picture.height; ++y)
        for (int x = 0; x < picture.width; ++x) {
            const auto index = picture.pixels[std::size_t(y) * picture.width + x];
            if (index >= picture.palette.size())
                throw std::runtime_error("intro palette index out of range");
            frame.argb[std::size_t(picture.y + y) * frame.width + picture.x + x] =
                pixel(picture.palette[index]);
        }
}

IntroFrame blank(int width, int height, std::uint32_t color) {
    return {width, height, std::vector<std::uint32_t>(std::size_t(width) * height, color)};
}

void fade_vga_dac(IntroFrame &frame, unsigned eighths) {
    // The original VGA DAC has six bits per channel. The launcher animates
    // those values, then the probe expands them to eight bits. Performing the
    // fade on the already-expanded RGB would lose the observable quantization.
    for (auto &p : frame.argb) {
        const auto channel = [eighths](unsigned value) {
            const auto six = (value >> 2) * eighths / 8;
            return six << 2 | six >> 4;
        };
        p = 0xff000000u | (channel((p >> 16) & 255) << 16) |
            (channel((p >> 8) & 255) << 8) | channel(p & 255);
    }
}

} // namespace

int IntroPresentation::Font::advance(unsigned char character) const {
    if (character >= widths.size())
        return 4;
    const auto spacing_byte = spacing[character];
    return (spacing_byte & 15) + widths[character] + (spacing_byte >> 4);
}

IntroPresentation::IntroPresentation(const std::filesystem::path &directory)
    : pic201_(load_file(directory / "GATE_201.PIC")),
      pic300_(load_file(directory / "GATE_300.PIC")),
      pic301_(load_file(directory / "GATE_301.PIC")),
      pic302_(load_file(directory / "GATE_302.PIC")),
      pic303_(load_file(directory / "GATE_303.PIC")),
      pic310_(load_file(directory / "GATE_310.PIC")),
      paragraphs_(load_paragraphs(load_file(directory / "GATE.EXE"))) {
    const auto file = load_file(directory / "GATE_305.FNT");
    if (file.size() < 266 || file[0] != 1 || file[1] != 1 ||
        file[3] != 0x21 || file[4] != 0x7f || file[7] != 0xff ||
        file[8] != 0xff || file[9] != 0)
        throw std::runtime_error("unexpected intro proportional font layout");
    caption_font_.row_bytes = file[2];
    caption_font_.height = file[6];
    caption_font_.first = file[3];
    caption_font_.last = file[4];
    const auto expected = 266u + unsigned(caption_font_.last - caption_font_.first + 1) *
                                     unsigned(caption_font_.row_bytes) * caption_font_.height;
    if (!caption_font_.row_bytes || !caption_font_.height || file.size() != expected)
        throw std::runtime_error("unexpected intro font size");
    caption_font_.widths.assign(file.begin() + 10, file.begin() + 138);
    caption_font_.spacing.assign(file.begin() + 138, file.begin() + 266);
    caption_font_.pixels.assign(file.begin() + 266, file.end());
    if (paragraphs_.size() < observed_pages.size())
        throw std::runtime_error("intro has fewer text pages than verified frames");
}

std::size_t IntroPresentation::verified_pages() const noexcept {
    return observed_pages.size();
}

IntroPageInfo IntroPresentation::page_info(std::size_t page) const {
    return observed_pages.at(page);
}

const std::string &IntroPresentation::text(std::size_t page) const {
    return paragraphs_.at(page);
}

PicImage IntroPresentation::load_picture(unsigned file, std::size_t index) const {
    const auto key = std::pair{file, index};
    if (const auto it = picture_cache_.find(key); it != picture_cache_.end())
        return it->second;
    const std::vector<std::uint8_t> *data = nullptr;
    switch (file) {
    case 201: data = &pic201_; break;
    case 300: data = &pic300_; break;
    case 301: data = &pic301_; break;
    case 302: data = &pic302_; break;
    case 303: data = &pic303_; break;
    case 310: data = &pic310_; break;
    default: throw std::runtime_error("unknown native intro picture file");
    }
    const auto entries = parse_pic_directory(*data);
    if (index >= entries.size())
        throw std::out_of_range("intro picture entry out of range");
    auto picture = decode_pic(*data, entries[index], PicDialect::gateway);
    if (picture.palette.empty()) {
        if (picture.bits == 4)
            picture.palette = default_ega_palette();
        else if (picture.bits == 8) {
            // PIC #300's logo deltas reuse the palette of #0; the animated
            // spacecraft in #301 reuses the palette of the Venus backdrop #5.
            if (file == 300 && index > 0)
                picture.palette = load_picture(300, 0).palette;
            else if (file == 301 && index >= 6 && index <= 31)
                picture.palette = load_picture(301, 5).palette;
            else if (file == 302 && index >= 1 && index <= 32)
                picture.palette = load_picture(302, 0).palette;
            else if (file == 302 && index >= 34 && index <= 102)
                picture.palette = load_picture(302, 33).palette;
            else if (file == 303 && index >= 1 && index <= 41)
                picture.palette = load_picture(303, 0).palette;
            else if (file == 303 && index >= 43 && index <= 79)
                picture.palette = load_picture(303, 42).palette;
            else
                throw std::runtime_error("unresolved intro picture palette");
        }
    }
    picture_cache_.emplace(key, picture);
    return picture;
}

void IntroPresentation::caption(IntroFrame &frame, std::size_t page) const {
    const auto area = page_info(page);
    const int x = area.text_x, y = area.text_y;
    const int w = area.text_width, h = area.text_height;
    constexpr std::uint32_t black = 0xff000000u;
    constexpr std::uint32_t dark_gray = 0xff616161u;
    // The DOS dialog surrounds the measured interior with an *outer* black
    // edge, then two bevel pixels. The earlier prototype only drew the inner
    // edge; the missing left/top pixels were obvious against dark paintings.
    fill(frame, x - 2, y - 2, w + 4, h + 4, black);
    fill(frame, x - 1, y - 1, w + 1, h + 1, 0xffffffffu);
    fill(frame, x + 1, y + 1, w - 2, h - 2, 0xffa2a2a2u);
    fill(frame, x + w - 1, y + 1, 2, h - 2, dark_gray);
    fill(frame, x, y + h - 1, w + 1, 2, dark_gray);
    fill(frame, x - 1, y + h, 1, 1, dark_gray);
    fill(frame, x + w, y - 1, 1, 1, dark_gray);
    fill(frame, x + w - 1, y, 2, 1, dark_gray);

    // The original caption font is a proportional 10px bitmap with explicit
    // per-character margins. The exact text-break algorithm remains to be
    // recovered; words never exceed the fixed rectangle's width.
    const int left = x + 4, right = x + w - 4;
    int cursor_x = left, cursor_y = y + 3;
    const auto draw = [&](unsigned char ch, int px, int py) {
        if (ch < caption_font_.first || ch > caption_font_.last)
            return;
        const int width = caption_font_.widths[ch];
        const auto space = caption_font_.spacing[ch];
        const int offset_x = space & 15;
        const std::size_t start = std::size_t(ch - caption_font_.first) *
                                  caption_font_.row_bytes * caption_font_.height;
        for (int row = 0; row < caption_font_.height; ++row)
            for (int col = 0; col < width; ++col) {
                if (!(caption_font_.pixels[start + row * caption_font_.row_bytes + col / 8] &
                      (0x80u >> (col % 8))))
                    continue;
                const int draw_x = px + offset_x + col, draw_y = py + row;
                if (draw_x > x && draw_x < x + w - 1 &&
                    draw_y > y && draw_y < y + h - 1)
                    frame.argb[std::size_t(draw_y) * frame.width + draw_x] = black;
            }
    };
    const std::string &paragraph = paragraphs_[page];
    for (std::size_t at = 0; at < paragraph.size();) {
        if (paragraph[at] == '\n') {
            cursor_x = left;
            cursor_y += caption_font_.height + 1;
            ++at;
            continue;
        }
        if (paragraph[at] == ' ') {
            // The launcher discards a run of spaces following a hard or
            // wrapped line break (visible in the final indented paragraph).
            if (cursor_x != left)
                cursor_x += caption_font_.advance(' ');
            ++at;
            continue;
        }
        const std::size_t end = paragraph.find_first_of(" \n", at);
        const std::size_t length = end == std::string::npos ? paragraph.size() - at : end - at;
        int word_width = 0;
        for (std::size_t i = 0; i < length; ++i)
            word_width += caption_font_.advance(static_cast<unsigned char>(paragraph[at + i]));
        if (cursor_x != left && cursor_x + word_width > right) {
            cursor_x = left;
            cursor_y += caption_font_.height + 1;
        }
        for (std::size_t i = 0; i < length; ++i) {
            const auto ch = static_cast<unsigned char>(paragraph[at + i]);
            draw(ch, cursor_x, cursor_y);
            cursor_x += caption_font_.advance(ch);
        }
        at += length;
    }
}

IntroFrame IntroPresentation::page_frame(std::size_t page) const {
    const auto info = page_info(page);
    auto frame = blank(320, 200, 0xff000000u);
    paint(frame, load_picture(info.pic_file, info.pic_entry));
    caption(frame, page);
    return frame;
}

std::size_t IntroPresentation::page_at(std::uint32_t ms) const {
    std::size_t page = 0;
    for (std::size_t i = 1; i < observed_pages.size(); ++i)
        if (ms >= observed_pages[i].start_ms)
            page = i;
    return page;
}

IntroFrame IntroPresentation::frame_at(std::uint32_t ms) const {
    if (ms < 7539) {
        auto frame = blank(640, 480, 0xffffffffu);
        if (ms >= 3268) {
            const auto logo = load_picture(201, 0);
            for (int y = 0; y < logo.height; ++y)
                for (int x = 0; x < logo.width; ++x) {
                    const auto idx = logo.pixels[std::size_t(y) * logo.width + x];
                    const auto color = logo.palette.at(idx);
                    if (color.r == 0 && color.g == 170 && color.b == 0)
                        frame.argb[std::size_t(logo.y + y) * 640 + logo.x + x] = 0xff000000u;
                }
        }
        return frame;
    }
    if (ms < 18579) {
        auto frame = blank(320, 200, 0xff000000u);
        if (ms >= 14391) {
            const auto title = load_picture(300, 27);
            paint(frame, title);
            if (ms >= title_credit_ramp.front().ms) {
                const auto six = sprite_at(ms, title_credit_ramp);
                const auto eight = six << 2 | six >> 4;
                const auto color = 0xff000000u | (eight << 16) | (eight << 8) | eight;
                for (std::size_t i = 0; i < title.pixels.size(); ++i)
                    if (title.pixels[i] == 23)
                        frame.argb[i] = color;
            }
        } else if (ms >= 13200) {
            auto image = load_picture(300, 0);
            // Display begins after all 26 resource reads, not at read time.
            // The original display cadence still needs instruction-level
            // confirmation; approximately 45ms between frames fits captures.
            const auto count = std::min<std::size_t>(26, 1 + (ms - 13200) / 45);
            for (std::size_t i = 0; i < count; ++i) {
                auto layer = load_picture(300, i + 1);
                apply_pic_xor(layer, image);
                image.pixels = std::move(layer.pixels);
            }
            paint(frame, image);
        } else {
            // The seven 9x38 rectangles and their order are pixel-exact.
            // The timestamps are constrained by 500ms snapshots, not exact.
            for (std::size_t i = 0; i < bar_x.size(); ++i)
                if (ms >= bar_reveal_ms[i])
                    fill(frame, bar_x[i], 81, 9, 38, 0xffff0000u);
        }
        return frame;
    }
    if (ms < observed_pages.front().start_ms) {
        auto frame = blank(320, 200, 0xff000000u);
        paint(frame, load_picture(301, 5));
        return frame;
    }
    if (ms >= observed_pages[1].start_ms && ms < 45200) {
        // The first caption closes before the spaceship animation. The
        // picture resource sequence at 40.19..41.17s is a preload; the actual
        // 26-frame animation uses the BIOS-clock-like ~55ms display cadence.
        auto frame = blank(320, 200, 0xff000000u);
        paint(frame, load_picture(301, 5));
        if (ms >= 41168 && ms < 41168 + 26 * 55) {
            const auto entry = 6 + (ms - 41168) / 55;
            paint(frame, load_picture(301, entry));
        }
        return frame;
    }
    const auto backdrop = [&](unsigned file, unsigned entry) {
        auto frame = blank(320, 200, 0xff000000u);
        paint(frame, load_picture(file, entry));
        return frame;
    };
    const auto sprite = [&](unsigned file, unsigned background, unsigned entry) {
        auto frame = backdrop(file, background);
        paint(frame, load_picture(file, entry));
        return frame;
    };
    // The later section interleaves text panels with several picture-driven
    // animations. A scene change is not identical to a new prose record.
    if (ms >= 233000 && ms < observed_pages[14].start_ms)
        return backdrop(302, 0);
    if (ms >= 196000 && ms < observed_pages[11].start_ms)
        return backdrop(301, 3);
    if (ms >= 255300 && ms < 255900)
        return blank(320, 200, 0xff000000u);
    if (ms >= 255900 && ms < 259000) {
        const auto entry = ms < approach_ship.front().ms ? 1
                         : ms > approach_ship.back().ms
                             ? 27 + (ms - 258500) / 100
                             : sprite_at(ms, approach_ship);
        return sprite(302, 0, std::min<unsigned>(entry, 32));
    }
    if (ms >= 259000 && ms < 259300) {
        auto frame = backdrop(302, 0);
        fade_vga_dac(frame, 7);
        return frame;
    }
    if (ms >= 259300 && ms < 259700) {
        auto frame = backdrop(302, 33);
        // The source fades in a new scene by changing the six-bit VGA DAC.
        // An exact transition curve has not yet been recovered.
        const unsigned brightness = ms < 259500 ? 3 : ms < 259600 ? 4 : 6;
        fade_vga_dac(frame, brightness);
        return frame;
    }
    if (ms >= 259700 && ms < 263400)
        return sprite(302, 33, sprite_at(ms, station_ship));
    if (ms >= 263400 && ms < 263700)
        return blank(320, 200, 0xff000000u);
    if (ms >= 263700 && ms < 266000)
        return sprite(303, 0, sprite_at(ms, departing_ship));
    if (ms >= 266000 && ms < 267800)
        return backdrop(303, 0);
    if (ms >= 267800 && ms < 268500)
        return blank(320, 200, 0xff000000u);
    if (ms >= 268500 && ms < observed_pages[15].start_ms)
        return backdrop(310, 4);
    if (ms >= 283800 && ms < 284400)
        return blank(320, 200, 0xff000000u);
    if (ms >= 284400 && ms < observed_pages[16].start_ms)
        return backdrop(303, 42);
    return page_frame(page_at(ms));
}

} // namespace gateway

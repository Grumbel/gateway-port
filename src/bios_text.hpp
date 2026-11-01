#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace gateway {
// BIOS INT 10h AH=06h/07h on one 80x25 text page (4096 bytes, 4000 visible).
inline void scroll_text_window(std::span<std::uint8_t, 4096> page, bool down,
                               unsigned lines, std::uint8_t attribute, unsigned top,
                               unsigned left, unsigned bottom, unsigned right) {
    if (top > bottom || left > right || bottom >= 25 || right >= 80)
        throw std::runtime_error("invalid BIOS text scroll rectangle");
    const unsigned height = bottom - top + 1;
    if (!lines || lines >= height)
        lines = height;
    auto cell = [&](unsigned row, unsigned column) { return (row * 80 + column) * 2; };
    if (lines < height) {
        if (down) {
            for (unsigned row = bottom + 1 - lines; row-- > top;)
                for (unsigned column = left; column <= right; ++column) {
                    const auto src = cell(row, column), dst = cell(row + lines, column);
                    page[dst] = page[src];
                    page[dst + 1] = page[src + 1];
                }
        } else {
            for (unsigned row = top; row + lines <= bottom; ++row)
                for (unsigned column = left; column <= right; ++column) {
                    const auto src = cell(row + lines, column), dst = cell(row, column);
                    page[dst] = page[src];
                    page[dst + 1] = page[src + 1];
                }
        }
    }
    const unsigned blank_top = down ? top : bottom + 1 - lines;
    const unsigned blank_bottom = down ? top + lines : bottom + 1;
    for (unsigned row = blank_top; row < blank_bottom; ++row)
        for (unsigned column = left; column <= right; ++column) {
            const auto offset = cell(row, column);
            page[offset] = ' ';
            page[offset + 1] = attribute;
        }
}
} // namespace gateway

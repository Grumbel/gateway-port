#pragma once
#include "mouse.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace gateway {
// Render a high-contrast 16x16 arrow in framebuffer coordinates. The native
// SDL cursor is hidden so it cannot disagree with the DOS mouse position.
inline void draw_mouse_cursor(std::span<std::uint32_t> pixels, unsigned width, unsigned height,
                              const Mouse &mouse, bool text_mode) {
    if (mouse.visibility < 0 || !width || !height || pixels.size() < width * height)
        return;
    constexpr std::array<std::uint16_t, 16> shape{
        0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00,
        0xff80, 0xffc0, 0xffe0, 0xff00, 0xde00, 0x8e00, 0x0600, 0x0000};
    const int x = mouse.x * int(width) / 640;
    const int y = text_mode ? mouse.y * int(height) / 200 : mouse.y;
    for (unsigned row = 0; row < shape.size(); ++row)
        for (unsigned column = 0; column < 16; ++column) {
            if (!(shape[row] & (0x8000u >> column)))
                continue;
            const int px = x + int(column), py = y + int(row);
            if (px < 0 || py < 0 || px >= int(width) || py >= int(height))
                continue;
            const bool edge = row == 0 || column == 0 || !(shape[row - 1] & (0x8000u >> column)) ||
                              (column < 15 && !(shape[row] & (0x4000u >> column)));
            pixels[std::size_t(py) * width + px] = edge ? 0xff000000 : 0xffffffff;
        }
}
} // namespace gateway

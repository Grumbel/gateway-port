#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace gateway {
// DOS mouse coordinates use a 640-column grid even in 320-pixel modes.
struct Mouse {
    struct Button {
        std::uint16_t presses = 0, releases = 0;
        std::uint16_t press_x = 0, press_y = 0, release_x = 0, release_y = 0;
    };
    int x = 320, y = 240, min_x = 0, max_x = 639, min_y = 0, max_y = 479;
    int visibility = -1;
    bool warp_pending = false;
    std::uint16_t buttons = 0;
    int motion_x = 0, motion_y = 0;
    std::array<Button, 3> history{};
    void position(int next_x, int next_y) {
        x = std::clamp(next_x, min_x, max_x);
        y = std::clamp(next_y, min_y, max_y);
    }
    void move(int next_x, int next_y) {
        const auto old_x = x, old_y = y;
        position(next_x, next_y);
        motion_x = static_cast<std::int16_t>(motion_x + x - old_x);
        motion_y = static_cast<std::int16_t>(motion_y + y - old_y);
    }
    void button(unsigned index, bool down) {
        if (index >= history.size())
            return;
        const auto mask = 1u << index;
        if (bool(buttons & mask) == down)
            return;
        auto &entry = history[index];
        if (down) {
            buttons |= mask;
            ++entry.presses;
            entry.press_x = x;
            entry.press_y = y;
        } else {
            buttons &= ~mask;
            ++entry.releases;
            entry.release_x = x;
            entry.release_y = y;
        }
    }
};
} // namespace gateway

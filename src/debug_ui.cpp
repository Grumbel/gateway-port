#include "debug_ui.hpp"
#include "../third_party/font8x8/font8x8_basic.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace gateway {
namespace {
constexpr std::uint32_t panel = 0xff162034;
constexpr std::uint32_t border = 0xff75aaff;
constexpr std::uint32_t white = 0xffe6edfa;
constexpr std::uint32_t muted = 0xffa5b4c5;
constexpr std::uint32_t accent = 0xffffd65c;

void pixel(std::span<std::uint32_t> image, int width, int height, int x, int y,
           std::uint32_t color) {
    if (x >= 0 && y >= 0 && x < width && y < height)
        image[std::size_t(y) * width + x] = color;
}
void rect(std::span<std::uint32_t> image, int width, int height, int x0, int y0, int x1, int y1,
          std::uint32_t color, bool filled) {
    x0 = std::clamp(x0, 0, width);
    y0 = std::clamp(y0, 0, height);
    x1 = std::clamp(x1, 0, width);
    y1 = std::clamp(y1, 0, height);
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (filled || y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1)
                pixel(image, width, height, x, y, color);
}
void text(std::span<std::uint32_t> image, int width, int height, int x, int y,
          std::string_view message, std::uint32_t color, unsigned limit = 76) {
    for (unsigned i = 0; i < message.size() && i < limit; ++i) {
        const auto c = static_cast<unsigned char>(message[i]);
        if (c >= 128)
            continue;
        for (unsigned row = 0; row < 8; ++row)
            for (unsigned column = 0; column < 8; ++column)
                if (font8x8_basic[c][row] & (1u << column))
                    pixel(image, width, height, x + int(8 * i + column), y + int(row), color);
    }
}
int menu_left(int width) { return std::max(0, width - 354); }
} // namespace

int debug_menu_row_at(int x, int y, int width) {
    if (x < menu_left(width) + 8 || x >= width - 8 || y < 48 || y >= 48 + 20 * 6)
        return -1;
    return (y - 48) / 20;
}

void draw_debug_overlay(std::span<std::uint32_t> image, int width, int height,
                        const Mouse &mouse, const DebugOverlayData &data,
                        bool menu_open, bool show_hitboxes, unsigned selected_row) {
    if (width < 1 || height < 1 || image.size() < std::size_t(width) * height)
        return;
    int hovered = -1;
    if (show_hitboxes) {
        for (std::size_t i = 0; i < data.scene.boxes.size(); ++i) {
            const auto &box = data.scene.boxes[i];
            // In the original 640-column Gateway interface the picture occupies
            // x=260..639. RGN X coordinates are scene-relative and doubled.
            const int x0 = int((260 + box.x0) * width / 640);
            const int x1 = int((261 + box.x1) * width / 640);
            const int y0 = box.y0, y1 = box.y1 + 1;
            const bool over = mouse.x >= int(260 + box.x0) &&
                              mouse.x <= int(260 + box.x1) &&
                              mouse.y >= int(box.y0) && mouse.y <= int(box.y1);
            if (over)
                hovered = static_cast<int>(i);
            rect(image, width, height, x0, y0, x1, y1, over ? 0xffff62b8 : accent, false);
        }
        rect(image, width, height, 2, height - 23, std::min(width, 310), height - 2, panel,
             true);
        char info[128];
        std::snprintf(info, sizeof info, "RGN %s #%zu (%zu hitboxes)",
                      data.scene.region_file.c_str(), data.scene.entry, data.scene.boxes.size());
        text(image, width, height, 8, height - 18, info, white, 37);
        if (hovered >= 0) {
            const auto &box = data.scene.boxes[hovered];
            std::snprintf(info, sizeof info, "name[%u] %s", box.name_index, box.name.c_str());
            rect(image, width, height, 2, height - 44, std::min(width, 310), height - 24,
                 panel, true);
            text(image, width, height, 8, height - 39, info, accent, 37);
        }
    }
    if (!menu_open)
        return;
    const int left = menu_left(width), right = width - 6;
    rect(image, width, height, left, 8, right, 256, panel, true);
    rect(image, width, height, left, 8, right, 256, border, false);
    text(image, width, height, left + 13, 17, "GATEWAY DEBUG  [F10]", accent);
    const std::array<std::string, 6> entries{
        std::string("Hitboxes: ") + (show_hitboxes ? "ON" : "OFF"),
        std::string("Auto scene: ") + (data.scene.auto_follow ? "ON" : "OFF"),
        "Previous RGN frame",
        "Next RGN frame",
        "Dump guest + object state",
        "Close debug menu",
    };
    for (unsigned i = 0; i < entries.size(); ++i) {
        const int y = 48 + 20 * int(i);
        if (selected_row == i)
            rect(image, width, height, left + 7, y - 4, right - 7, y + 12, 0xff314a6a,
                 true);
        text(image, width, height, left + 13, y, entries[i],
             selected_row == i ? accent : white, 39);
    }
    char scene[160];
    std::snprintf(scene, sizeof scene, "Scene: %s #%zu %s", data.scene.picture.c_str(),
                  data.scene.entry, data.scene.auto_follow ? "(guess)" : "(manual)");
    text(image, width, height, left + 13, 179, scene, white, 39);
    text(image, width, height, left + 13, 193, data.registers, muted, 39);
    text(image, width, height, left + 13, 207, data.object_summary, muted, 39);
    text(image, width, height, left + 13, 221, data.dump_message, accent, 39);
    text(image, width, height, left + 13, 241, "Up/Dn Left/Right H A D Esc", muted, 39);
}
} // namespace gateway

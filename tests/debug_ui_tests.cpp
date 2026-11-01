#include "debug_ui.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main() try {
    const auto bounds = gateway::debug_menu_bounds(640, 480);
    require(bounds.x == 286 && bounds.y == 232, "menu defaults below Gateway scene");
    require(gateway::debug_menu_row_at(500, 272, 640, 480) == 0 &&
                gateway::debug_menu_row_at(500, 372, 640, 480) == 5 &&
                gateway::debug_menu_row_at(500, 48, 640, 480) < 0 &&
                gateway::debug_menu_title_at(500, 245, 640, 480),
            "bottom-right debug menu hit testing");
    const gateway::DebugMenuPosition moved{40, 50};
    require(gateway::debug_menu_row_at(100, 90, 640, 480, moved) == 0 &&
                gateway::debug_menu_title_at(100, 62, 640, 480, moved) &&
                !gateway::debug_menu_title_at(100, 120, 640, 480, moved),
            "relocated menu mouse interaction");
    require(gateway::debug_menu_bounds(640, 480, {999, 999}).x == 286 &&
                gateway::debug_menu_bounds(640, 480, {999, 999}).y == 232,
            "dragging cannot move window out of bounds");
    std::vector<std::uint32_t> pixels(640 * 480, 0xff123456);
    gateway::Mouse mouse;
    mouse.x = 265;
    mouse.y = 15;
    gateway::DebugOverlayData debug;
    debug.scene.region_file = "GATE_002.RGN";
    debug.scene.entry = 15;
    debug.scene.boxes.push_back({0, 10, 10, 20, 292, "drawer"});
    debug.scene.boxes.push_back({12, 10, 20, 20, 292, "drawer"});
    debug.scene.boxes.push_back({30, 10, 36, 20, 222, "wall"});
    debug.scene.boxes.push_back({40, 10, 50, 20, 0, ""});
    debug.scene.boxes.push_back({52, 10, 62, 20, 0, ""});
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, false, true, 0);
    require(pixels[10 * 640 + 260] == 0xffe6edfa,
            "hovered RGN object contour not drawn at scene x=260");
    require(pixels[15 * 640 + 271] != 0xffe6edfa &&
                pixels[15 * 640 + 271] != 0xff123456 &&
                pixels[15 * 640 + 271] == pixels[15 * 640 + 272],
            "adjacent rectangles of one object must have a translucent merged interior");
    require(pixels[15 * 640 + 290] == gateway::debug_hotspot_color(222) &&
                gateway::debug_hotspot_color(222) != gateway::debug_hotspot_color(292),
            "distinct engine objects should have distinct outline colors");
    require(pixels[15 * 640 + 311] == gateway::debug_hotspot_color(3) &&
                pixels[15 * 640 + 312] == gateway::debug_hotspot_color(4),
            "anonymous adjacent rectangles must not merge into one object");
    require(pixels[10 * 640 + 0] == 0xff123456,
            "debug overlay moved picture data on the left side");
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, true, false, 4);
    require(pixels[232 * 640 + 286] == 0xff75aaff &&
                pixels[100 * 640 + 500] == 0xff123456,
            "default debug menu must stay below the scene image");
    std::vector<std::uint32_t> relocated(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(relocated, 640, 480, mouse, debug, true, false, 0, moved);
    require(relocated[50 * 640 + 40] == 0xff75aaff &&
                relocated[232 * 640 + 500] == 0xff123456,
            "menu position must be movable without leaving another panel behind");
    gateway::DebugOverlayData polygon_debug;
    polygon_debug.scene.x_extent = 1;
    polygon_debug.scene.boxes.push_back({0, 0, 20, 20, 42, "polygon",
                                         {{0, 0}, {20, 0}, {20, 10},
                                          {10, 10}, {10, 20}, {0, 20}}});
    polygon_debug.scene.boxes.push_back({30, 0, 35, 5, 43, "two-point box"});
    std::vector<std::uint32_t> polygon_pixels(640 * 480, 0xff123456);
    gateway::draw_debug_overlay(polygon_pixels, 640, 480, mouse, polygon_debug, false, true, 0);
    require(polygon_pixels[15 * 640 + 265] != 0xff123456 &&
                polygon_pixels[15 * 640 + 275] == 0xff123456 &&
                polygon_pixels[5 * 640 + 275] != 0xff123456,
            "concave Gateway II polygon must not fill its missing corner");
    require(polygon_pixels[3 * 640 + 295] != 0xff123456 &&
                polygon_pixels[3 * 640 + 296] == 0xff123456,
            "Gateway II two-point region uses single-pixel X units");
    std::cout << "Gateway merged hotspot and draggable debug-menu raster checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

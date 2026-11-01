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
    require(gateway::debug_menu_row_at(500, 48, 640) == 0 &&
                gateway::debug_menu_row_at(500, 148, 640) == 5 &&
                gateway::debug_menu_row_at(200, 48, 640) < 0,
            "debug menu hit testing");
    std::vector<std::uint32_t> pixels(640 * 480, 0xff123456);
    gateway::Mouse mouse;
    mouse.x = 265;
    mouse.y = 15;
    gateway::DebugOverlayData debug;
    debug.scene.region_file = "GATE_002.RGN";
    debug.scene.entry = 15;
    debug.scene.boxes.push_back({0, 10, 20, 20, 359, "desk chair"});
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, false, true, 0);
    require(pixels[10 * 640 + 260] == 0xffff62b8,
            "RGN scene-local hitbox not drawn at screen x=260");
    require(pixels[15 * 640 + 265] == 0xff123456,
            "debug hitbox filled/modified the guest scene");
    require(pixels[10 * 640 + 0] == 0xff123456,
            "debug overlay moved picture data on the left side");
    gateway::draw_debug_overlay(pixels, 640, 480, mouse, debug, true, false, 4);
    require(pixels[8 * 640 + 286] == 0xff75aaff,
            "debug menu must appear even with hitboxes disabled");
    std::cout << "Gateway debug overlay raster checks passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

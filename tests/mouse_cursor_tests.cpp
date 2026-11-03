// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "mouse_cursor.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<std::uint32_t> pixels(640 * 350, 0xff123456);
        gateway::Mouse mouse;
        mouse.position(320, 175);
        gateway::draw_mouse_cursor(pixels, 640, 350, mouse, false);
        require(pixels[175 * 640 + 320] == 0xff123456, "hidden cursor modified image");
        mouse.visibility = 0;
        gateway::draw_mouse_cursor(pixels, 640, 350, mouse, false);
        require(pixels[175 * 640 + 320] == 0xff000000, "cursor outline missing");
        require(pixels[177 * 640 + 321] == 0xffffffff, "cursor interior missing");
        mouse.position(639, 349);
        gateway::draw_mouse_cursor(pixels, 640, 350, mouse, false);
        require(pixels.back() == 0xff000000, "cursor clipping at edge failed");
        std::cout << "Mouse cursor checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

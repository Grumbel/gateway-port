// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "ui_layout.hpp"
#include <cassert>

using gateway::compute_wide_ui_layout;

int main() {
    const auto wide = compute_wide_ui_layout(1280, 720, 640, 480);
    assert(wide.panels_visible);
    assert(wide.scene.x == 160 && wide.scene.y == 0);
    assert(wide.scene.width == 960 && wide.scene.height == 720);
    assert(wide.left_panel.width == 160 && wide.right_panel.width == 160);

    const auto classic = compute_wide_ui_layout(960, 720, 640, 480);
    assert(!classic.panels_visible);
    assert(classic.scene.x == 0 && classic.scene.y == 0);
    assert(classic.scene.width == 960 && classic.scene.height == 720);

    const auto portrait = compute_wide_ui_layout(720, 960, 640, 480);
    assert(!portrait.panels_visible);
    assert(portrait.scene.x == 0 && portrait.scene.y == 210);
    assert(portrait.scene.width == 720 && portrait.scene.height == 540);

    const auto invalid = compute_wide_ui_layout(0, 720, 640, 480);
    assert(invalid.scene.width == 0 && !invalid.panels_visible);

    const auto square_pixels = compute_wide_ui_layout(1280, 720, 640, 400);
    assert(!square_pixels.panels_visible);
    assert(square_pixels.scene.width == 1152 && square_pixels.scene.x == 64);

    const auto ultrawide = compute_wide_ui_layout(2560, 1080, 640, 480);
    assert(ultrawide.panels_visible);
    assert(ultrawide.scene.width == 1440 && ultrawide.scene.x == 560);
}

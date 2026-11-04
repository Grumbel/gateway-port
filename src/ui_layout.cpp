// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "ui_layout.hpp"
#include <algorithm>
#include <cstdint>

namespace gateway {

WideUiLayout compute_wide_ui_layout(int output_width, int output_height, int scene_width,
                                    int scene_height) {
    WideUiLayout result;
    if (output_width <= 0 || output_height <= 0 || scene_width <= 0 || scene_height <= 0)
        return result;

    const auto fitted_width = std::int64_t(output_height) * scene_width / scene_height;
    int width, height;
    if (fitted_width <= output_width) {
        width = int(fitted_width);
        height = output_height;
    } else {
        width = output_width;
        height = int(std::int64_t(output_width) * scene_height / scene_width);
    }
    width = std::clamp(width, 1, output_width);
    height = std::clamp(height, 1, output_height);
    const int left = (output_width - width) / 2;
    const int top = (output_height - height) / 2;
    result.scene = {left, top, width, height};
    result.left_panel = {0, 0, left, output_height};
    result.right_panel = {left + width, 0, output_width - (left + width), output_height};
    result.panels_visible = result.left_panel.width >= 120 && result.right_panel.width >= 120;
    return result;
}

} // namespace gateway

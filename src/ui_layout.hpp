// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

namespace gateway {

struct UiRect {
    int x = 0, y = 0, width = 0, height = 0;
};

struct WideUiLayout {
    UiRect scene;
    UiRect left_panel;
    UiRect right_panel;
    bool panels_visible = false;
};

// Fit the aspect-corrected original game to the window without cropping it.
// Only use the spare space for side panels when both are wide enough.
WideUiLayout compute_wide_ui_layout(int output_width, int output_height,
                                    int scene_width, int scene_height);

} // namespace gateway

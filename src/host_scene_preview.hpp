// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "pic.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gateway {

// A display-only bridge from the original VGA framebuffer to native PIC
// composition. The guest keeps drawing normally; the replacement is enabled
// only when exactly one precomputed native scene matches the original pixels.
// Unrecognized frames and other games always retain the original VGA image.
class HostScenePreview {
public:
    HostScenePreview(PicImage background, std::vector<PicImage> optional_layers,
                     int screen_x, int screen_y);
    HostScenePreview(std::span<const std::uint8_t> file, PicDialect dialect,
                     std::size_t background_entry, std::span<const std::size_t> optional_layers,
                     int screen_x, int screen_y);

    // Replace just the room rectangle, never the UI. Returns the layer mask
    // used by the host, or nullopt when no unique exact match is available.
    std::optional<std::uint32_t> replace_if_exact(std::span<std::uint32_t> frame,
                                                   int screen_width, int screen_height) const;

private:
    struct Variant {
        std::uint32_t mask = 0;
        std::vector<std::uint32_t> argb;
    };
    std::vector<Variant> variants_;
    int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
};

} // namespace gateway

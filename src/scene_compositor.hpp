// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "pic.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gateway {

// Host-side PIXEL composition only: caller chooses the background and enabled
// overlays. The original game decides layer eligibility in its native code.
// All input images use the original PIC coordinate system and palette indices.
PicImage compose_pic_layers(PicImage background, std::span<const PicImage> enabled_layers);

// Decode a background and selected XOR layers from the same original PIC file.
// Carries the most recent file palette forward, like gateway-extract.
PicImage decode_pic_scene(std::span<const std::uint8_t> file, PicDialect dialect,
                          std::size_t background_entry,
                          std::span<const std::size_t> layer_entries);

// Compare against the RGB bytes of a captured native frame. The origin
// locates the room picture within that complete screen, not within the PIC.
// Inference enumerates at most 10 optional overlays (2^10 combinations),
// leaving all story predicates and animation choices to the original engine.
struct SceneFrameMatch {
    std::uint32_t optional_layer_mask = 0;
    std::size_t differing_pixels = 0;
    std::size_t compared_pixels = 0;
    std::size_t equally_good_masks = 0;
};

SceneFrameMatch compare_pic_scene_frame(const PicImage &picture,
                                         std::span<const std::uint8_t> rgb,
                                         int frame_width, int frame_height,
                                         int origin_x, int origin_y);

SceneFrameMatch infer_pic_scene_layers(std::span<const std::uint8_t> file,
                                        PicDialect dialect, std::size_t background_entry,
                                        std::span<const std::size_t> fixed_layers,
                                        std::span<const std::size_t> optional_layers,
                                        std::span<const std::uint8_t> rgb,
                                        int frame_width, int frame_height,
                                        int origin_x, int origin_y);

} // namespace gateway

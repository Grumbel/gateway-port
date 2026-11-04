// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "pic.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

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

} // namespace gateway

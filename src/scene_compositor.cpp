// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
#include <stdexcept>
#include <utility>
#include <vector>

namespace gateway {

PicImage compose_pic_layers(PicImage background, std::span<const PicImage> enabled_layers) {
    if (background.overlay || background.pixels.empty() || background.width <= 0 ||
        background.height <= 0)
        throw std::runtime_error("scene background must be a decoded base picture");
    for (const auto &layer : enabled_layers) {
        if (!layer.overlay)
            throw std::runtime_error("scene layer is not an XOR overlay");
        PicImage next = layer;
        apply_pic_xor(next, background);
        background.pixels = std::move(next.pixels);
        // Keep the background's palette and original rectangle. Individual
        // overlay entries generally have no palette of their own.
    }
    return background;
}

PicImage decode_pic_scene(std::span<const std::uint8_t> file, PicDialect dialect,
                          std::size_t background_entry,
                          std::span<const std::size_t> layer_entries) {
    const auto entries = parse_pic_directory(file);
    if (background_entry >= entries.size())
        throw std::runtime_error("scene background index outside PIC directory");

    std::vector<Rgb8> palette4;
    std::vector<Rgb8> palette8;
    PicImage background;
    // Palette-bearing entries can precede the background. Propagate only
    // palette entries, not earlier picture or XOR content.
    for (std::size_t i = 0; i <= background_entry; ++i) {
        auto picture = decode_pic(file, entries[i], dialect);
        if (picture.palette.size() == 16)
            palette4 = picture.palette;
        else if (picture.palette.size() == 256)
            palette8 = picture.palette;
        if (i == background_entry)
            background = std::move(picture);
    }
    if (background.palette.empty()) {
        if (background.bits == 4)
            background.palette = palette4.empty() ? default_ega_palette() : palette4;
        else if (background.bits == 8)
            background.palette = palette8;
    }

    std::vector<PicImage> layers;
    layers.reserve(layer_entries.size());
    for (const auto entry : layer_entries) {
        if (entry >= entries.size())
            throw std::runtime_error("scene layer index outside PIC directory");
        layers.push_back(decode_pic(file, entries[entry], dialect));
    }
    return compose_pic_layers(std::move(background), layers);
}

} // namespace gateway

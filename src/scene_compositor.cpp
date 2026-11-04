// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_compositor.hpp"
#include <bit>
#include <limits>
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

SceneFrameMatch compare_pic_scene_frame(const PicImage &picture,
                                        std::span<const std::uint8_t> rgb,
                                        int frame_width, int frame_height,
                                        int origin_x, int origin_y) {
    if (frame_width <= 0 || frame_height <= 0 ||
        static_cast<std::uint64_t>(frame_width) * frame_height * 3 != rgb.size())
        throw std::runtime_error("invalid RGB reference frame dimensions or size");
    const auto left = static_cast<std::int64_t>(origin_x) + picture.x;
    const auto top = static_cast<std::int64_t>(origin_y) + picture.y;
    if (picture.overlay || picture.width <= 0 || picture.height <= 0 ||
        left < 0 || top < 0 || left + picture.width > frame_width ||
        top + picture.height > frame_height ||
        picture.pixels.size() != static_cast<std::size_t>(picture.width) * picture.height)
        throw std::runtime_error("scene rectangle does not fit reference frame");
    SceneFrameMatch result;
    result.compared_pixels = picture.pixels.size();
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            const auto index = picture.pixels[static_cast<std::size_t>(y) * picture.width + x];
            if (index >= picture.palette.size())
                throw std::runtime_error("scene pixel outside indexed color palette");
            const auto color = picture.palette[index];
            const auto at = (static_cast<std::size_t>(top + y) * frame_width + left + x) * 3;
            result.differing_pixels += (color.r != rgb[at] || color.g != rgb[at + 1] ||
                                        color.b != rgb[at + 2]);
        }
    }
    return result;
}

SceneFrameMatch infer_pic_scene_layers(std::span<const std::uint8_t> file,
                                       PicDialect dialect, std::size_t background_entry,
                                       std::span<const std::size_t> fixed_layers,
                                       std::span<const std::size_t> optional_layers,
                                       std::span<const std::uint8_t> rgb,
                                       int frame_width, int frame_height,
                                       int origin_x, int origin_y) {
    if (optional_layers.size() > 10)
        throw std::runtime_error("no more than 10 optional picture layers are supported");
    const auto base = decode_pic_scene(file, dialect, background_entry, fixed_layers);
    const auto directory = parse_pic_directory(file);
    std::vector<PicImage> options;
    options.reserve(optional_layers.size());
    for (const auto entry : optional_layers) {
        if (entry >= directory.size())
            throw std::runtime_error("optional scene layer outside PIC directory");
        auto layer = decode_pic(file, directory[entry], dialect);
        if (!layer.overlay)
            throw std::runtime_error("optional scene layer is not an XOR overlay");
        options.push_back(std::move(layer));
    }

    SceneFrameMatch best;
    best.differing_pixels = std::numeric_limits<std::size_t>::max();
    for (std::uint32_t mask = 0; mask < (1u << options.size()); ++mask) {
        std::vector<PicImage> layers;
        for (std::size_t i = 0; i < options.size(); ++i)
            if (mask & (1u << i))
                layers.push_back(options[i]);
        const auto picture = compose_pic_layers(base, layers);
        auto match = compare_pic_scene_frame(picture, rgb, frame_width, frame_height,
                                             origin_x, origin_y);
        if (match.differing_pixels < best.differing_pixels) {
            best = match;
            best.optional_layer_mask = mask;
            best.equally_good_masks = 1;
        } else if (match.differing_pixels == best.differing_pixels) {
            ++best.equally_good_masks;
            // Retain a deterministic, smallest matching layer set if the
            // screenshot cannot distinguish two combinations.
            if (std::popcount(mask) < std::popcount(best.optional_layer_mask))
                best.optional_layer_mask = mask;
        }
    }
    return best;
}

} // namespace gateway

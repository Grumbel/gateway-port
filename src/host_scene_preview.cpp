// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "host_scene_preview.hpp"
#include "scene_compositor.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gateway {
namespace {
std::vector<PicImage> decode_optional_layers(std::span<const std::uint8_t> file,
                                             PicDialect dialect,
                                             std::span<const std::size_t> layer_entries) {
    const auto entries = parse_pic_directory(file);
    std::vector<PicImage> layers;
    layers.reserve(layer_entries.size());
    for (const auto entry : layer_entries) {
        if (entry >= entries.size())
            throw std::runtime_error("host scene layer outside PIC directory");
        layers.push_back(decode_pic(file, entries[entry], dialect));
    }
    return layers;
}
} // namespace

HostScenePreview::HostScenePreview(std::span<const std::uint8_t> file, PicDialect dialect,
                                   std::size_t background_entry,
                                   std::span<const std::size_t> optional_layers,
                                   int screen_x, int screen_y)
    : HostScenePreview(decode_pic_scene(file, dialect, background_entry, {}),
                       decode_optional_layers(file, dialect, optional_layers),
                       screen_x, screen_y) {}

HostScenePreview::HostScenePreview(PicImage background,
                                   std::vector<PicImage> layers,
                                   int screen_x, int screen_y) {
    if (layers.size() > 10)
        throw std::runtime_error("host scene supports no more than 10 optional layers");
    if (background.overlay || background.width <= 0 || background.height <= 0 ||
        background.pixels.size() != std::size_t(background.width) * background.height)
        throw std::runtime_error("invalid host scene background dimensions");
    constexpr std::size_t max_cached_bytes = 64 * 1024 * 1024;
    if (background.pixels.size() >
        max_cached_bytes / (sizeof(std::uint32_t) * (std::size_t{1} << layers.size())))
        throw std::runtime_error("host scene cache would exceed 64 MiB");
    const auto left = std::int64_t(screen_x) + background.x;
    const auto top = std::int64_t(screen_y) + background.y;
    if (left < 0 || left > std::numeric_limits<int>::max() ||
        top < 0 || top > std::numeric_limits<int>::max())
        throw std::runtime_error("host scene origin must be on screen");
    x_ = static_cast<int>(left);
    y_ = static_cast<int>(top);
    width_ = background.width;
    height_ = background.height;

    for (const auto &layer : layers) {
        if (!layer.overlay)
            throw std::runtime_error("host scene layer is not an XOR overlay");
    }

    variants_.reserve(std::size_t{1} << layers.size());
    for (std::uint32_t mask = 0; mask < (1u << layers.size()); ++mask) {
        std::vector<PicImage> selected;
        for (std::size_t i = 0; i < layers.size(); ++i)
            if (mask & (1u << i))
                selected.push_back(layers[i]);
        const auto picture = compose_pic_layers(background, selected);
        Variant variant;
        variant.mask = mask;
        variant.argb.reserve(picture.pixels.size());
        for (const auto index : picture.pixels) {
            if (index >= picture.palette.size())
                throw std::runtime_error("host scene color outside palette");
            const auto color = picture.palette[index];
            variant.argb.push_back(0xff000000u | (std::uint32_t(color.r) << 16) |
                                   (std::uint32_t(color.g) << 8) | color.b);
        }
        variants_.push_back(std::move(variant));
    }
}

std::optional<std::uint32_t> HostScenePreview::replace_if_exact(
    std::span<std::uint32_t> frame, int screen_width, int screen_height) const {
    if (screen_width <= 0 || screen_height <= 0 ||
        std::uint64_t(screen_width) * screen_height != frame.size())
        throw std::runtime_error("invalid host scene framebuffer dimensions");
    if (std::int64_t(x_) + width_ > screen_width ||
        std::int64_t(y_) + height_ > screen_height)
        return std::nullopt;

    const Variant *matching = nullptr;
    for (const auto &variant : variants_) {
        bool equal = true;
        for (int y = 0; y < height_ && equal; ++y) {
            const auto start = std::size_t(y_ + y) * screen_width + x_;
            const auto row = std::size_t(y) * width_;
            equal = std::equal(variant.argb.begin() + row,
                               variant.argb.begin() + row + width_, frame.begin() + start);
        }
        if (!equal)
            continue;
        if (matching)
            return std::nullopt; // An ambiguous match is never safe to replace.
        matching = &variant;
    }
    if (!matching)
        return std::nullopt;

    for (int y = 0; y < height_; ++y) {
        const auto start = std::size_t(y_ + y) * screen_width + x_;
        const auto row = std::size_t(y) * width_;
        std::copy_n(matching->argb.begin() + row, width_, frame.begin() + start);
    }
    return matching->mask;
}

bool HostScenePreview::paint_mask(std::span<std::uint32_t> frame,
                                 int screen_width, int screen_height,
                                 std::uint32_t mask) const {
    if (screen_width <= 0 || screen_height <= 0 ||
        std::uint64_t(screen_width) * screen_height != frame.size())
        throw std::runtime_error("invalid host scene framebuffer dimensions");
    if (mask >= variants_.size() || std::int64_t(x_) + width_ > screen_width ||
        std::int64_t(y_) + height_ > screen_height)
        return false;

    const auto &argb = variants_[mask].argb;
    for (int y = 0; y < height_; ++y) {
        const auto start = std::size_t(y_ + y) * screen_width + x_;
        std::copy_n(argb.begin() + std::size_t(y) * width_, width_, frame.begin() + start);
    }
    return true;
}

} // namespace gateway

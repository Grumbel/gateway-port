// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "scene_animation.hpp"
#include "scene_compositor.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

namespace gateway {

XorPicAnimation::XorPicAnimation(PicImage background, PicImage layer,
                                 std::uint64_t first_toggle_ms, std::uint32_t period_ms,
                                 bool initially_visible)
    : background_(std::move(background)), layer_(std::move(layer)),
      next_toggle_ms_(first_toggle_ms), period_ms_(period_ms) {
    if (period_ms_ == 0)
        throw std::runtime_error("animation period must be positive");
    if (!layer_.overlay)
        throw std::runtime_error("animation layer must be an XOR overlay");
    // Validate the geometry even if the animation starts in its off phase.
    auto validated = compose_pic_layers(background_, std::span<const PicImage>(&layer_, 1));
    frame_ = initially_visible ? std::move(validated) : background_;
    visible_ = initially_visible;
}

void XorPicAnimation::toggle() {
    auto applied = layer_;
    apply_pic_xor(applied, frame_);
    frame_.pixels = std::move(applied.pixels);
    visible_ = !visible_;
}

bool XorPicAnimation::advance_to(std::uint64_t guest_ms) {
    if (guest_ms < last_observed_ms_)
        throw std::runtime_error("animation clock cannot move backward");
    if (guest_ms < next_toggle_ms_) {
        last_observed_ms_ = guest_ms;
        return false;
    }
    const auto elapsed_periods = (guest_ms - next_toggle_ms_) / period_ms_;
    if (elapsed_periods == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("animation deadline overflow");
    const auto count = elapsed_periods + 1;
    if (count > (std::numeric_limits<std::uint64_t>::max() - next_toggle_ms_) /
                    period_ms_)
        throw std::overflow_error("animation deadline overflow");
    next_toggle_ms_ += count * period_ms_;
    last_observed_ms_ = guest_ms;
    if ((count & 1) != 0) {
        toggle();
        return true;
    }
    return false;
}

void XorPicAnimation::replace_background(PicImage background) {
    // Verify before changing any observable state.
    auto composed = compose_pic_layers(background, std::span<const PicImage>(&layer_, 1));
    frame_ = visible_ ? std::move(composed) : background;
    background_ = std::move(background);
}

void XorPicAnimation::synchronize(std::uint64_t next_toggle_ms, bool visible) {
    if (next_toggle_ms < last_observed_ms_)
        throw std::runtime_error("animation deadline precedes observed clock");
    if (visible != visible_)
        toggle();
    next_toggle_ms_ = next_toggle_ms;
}

RtlXorDeadlinePhase::RtlXorDeadlinePhase(std::uint32_t period_ms)
    : period_ms_(period_ms) {
    if (period_ms_ == 0)
        throw std::runtime_error("RTL animation period must be positive");
}

std::optional<bool> RtlXorDeadlinePhase::observe(std::uint32_t next_deadline_ms) {
    if (last_deadline_ms_) {
        // A smaller deadline may be a restarted sequence, a clock change or
        // 32-bit wraparound. Without separate evidence, none is safe to infer.
        if (next_deadline_ms < *last_deadline_ms_ ||
            (next_deadline_ms - *last_deadline_ms_) % period_ms_ != 0) {
            visible_.reset();
        } else if (visible_) {
            const auto steps = (next_deadline_ms - *last_deadline_ms_) / period_ms_;
            if (steps & 1)
                visible_ = !*visible_;
        }
    }
    last_deadline_ms_ = next_deadline_ms;
    return visible_;
}

void RtlXorDeadlinePhase::synchronize(std::uint32_t next_deadline_ms, bool visible) {
    last_deadline_ms_ = next_deadline_ms;
    visible_ = visible;
}

} // namespace gateway

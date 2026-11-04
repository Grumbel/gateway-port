// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "pic.hpp"
#include <cstdint>

namespace gateway {

// A host-side animated XOR *picture*, independent of guest story conditions.
// The caller supplies the next transition time in guest milliseconds, along
// with its current phase. This is a rendering primitive, not an emulation of
// the game's resident animation scheduler or its 16-bit state layout.
class XorPicAnimation {
public:
    XorPicAnimation(PicImage background, PicImage layer, std::uint64_t first_toggle_ms,
                    std::uint32_t period_ms, bool initially_visible = false);

    // Advance monotonically, including across skipped frames. Returns true if
    // any visible pixels changed (an even number of XORs cancels out).
    bool advance_to(std::uint64_t guest_ms);

    // Room image updates must not restart an active blinking sequence.
    void replace_background(PicImage background);

    // Allows a guest state observation to correct the phase after a pause or
    // other original scheduler activity; does not change the current time.
    void synchronize(std::uint64_t next_toggle_ms, bool visible);

    const PicImage &frame() const noexcept { return frame_; }
    bool visible() const noexcept { return visible_; }
    std::uint64_t next_toggle_ms() const noexcept { return next_toggle_ms_; }

private:
    void toggle();

    PicImage background_;
    PicImage layer_;
    PicImage frame_;
    std::uint64_t next_toggle_ms_ = 0;
    std::uint64_t last_observed_ms_ = 0;
    std::uint32_t period_ms_ = 0;
    bool visible_ = false;
};

} // namespace gateway

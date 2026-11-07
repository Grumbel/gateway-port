// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace gateway {

// The source-C thread owns the VGA state. SDL reads only complete copies,
// preserving the correspondence between pixel indices and the current DAC.
// Never throttle publish(): a palette's final write can be followed by a
// long period without another source-C video callback. The SDL event loop
// already limits presentation to one snapshot per display frame.
struct IntroLiveFrame {
    std::array<std::uint8_t, 320u * 200u> indices{};
    std::array<std::uint8_t, 256u * 3u> palette{};
};

class IntroLiveFrameMailbox {
public:
    void publish(const std::uint8_t *indices, const std::uint8_t dac[256][3]) {
        std::lock_guard lock(mutex_);
        if (generation_ != 0 &&
            std::memcmp(indices, frame_.indices.data(), frame_.indices.size()) == 0 &&
            std::memcmp(dac, frame_.palette.data(), frame_.palette.size()) == 0)
            return;
        std::memcpy(frame_.indices.data(), indices, frame_.indices.size());
        std::memcpy(frame_.palette.data(), dac, frame_.palette.size());
        ++generation_;
    }

    bool read_after(std::uint64_t &seen_generation, IntroLiveFrame &frame) {
        std::lock_guard lock(mutex_);
        if (generation_ == seen_generation)
            return false;
        frame = frame_;
        seen_generation = generation_;
        return true;
    }

private:
    std::mutex mutex_;
    IntroLiveFrame frame_;
    std::uint64_t generation_ = 0;
};

} // namespace gateway

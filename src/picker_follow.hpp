// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstdint>

namespace gateway {

// Bounded, observational instruction window following an original far RETF.
// The first sample must be the exact return address from the guest stack.
// Incidental IRQ code between the RETF and caller may be skipped, but a
// missing continuation is reported rather than attributed to the caller.
class PickerFollowGate {
  public:
    enum class Kind { inactive, waiting, sample, finished, missed_return };
    struct Tick {
        Kind kind = Kind::inactive;
        unsigned call = 0;
        unsigned step = 0;
    };
    static constexpr unsigned max_wait = 4096;

    bool active() const { return active_; }
    unsigned call() const { return call_; }
    unsigned recorded() const { return recorded_; }
    unsigned skipped() const { return skipped_; }
    void clear() { active_ = false; }
    void arm(unsigned call, std::uint64_t return_linear,
             std::uint64_t retf_linear, unsigned limit) {
        active_ = limit != 0;
        call_ = call;
        return_linear_ = return_linear;
        retf_linear_ = retf_linear;
        limit_ = limit;
        recorded_ = 0;
        skipped_ = 0;
        retf_skipped_ = false;
    }
    Tick observe(std::uint64_t address) {
        if (!active_)
            return {};
        if (recorded_ == 0 && address != return_linear_) {
            // Hook ordering at RETF is implementation-dependent. Skip the
            // RETF itself, and tolerate IRQ instructions before the caller.
            if (address == retf_linear_ && !retf_skipped_) {
                retf_skipped_ = true;
                return {Kind::waiting, call_, 0};
            }
            if (++skipped_ <= max_wait)
                return {Kind::waiting, call_, 0};
            active_ = false;
            return {Kind::missed_return, call_, 0};
        }
        const auto step = recorded_++;
        if (recorded_ == limit_) {
            active_ = false;
            return {Kind::finished, call_, step};
        }
        return {Kind::sample, call_, step};
    }

  private:
    bool active_ = false;
    bool retf_skipped_ = false;
    unsigned call_ = 0, limit_ = 0, recorded_ = 0, skipped_ = 0;
    std::uint64_t return_linear_ = 0, retf_linear_ = 0;
};

} // namespace gateway

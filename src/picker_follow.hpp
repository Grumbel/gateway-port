// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstdint>
#include <optional>

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
    struct Instruction {
        unsigned call, step;
        std::uint64_t linear;
    };
    static constexpr unsigned max_wait = 4096;

    bool active() const { return active_; }
    unsigned call() const { return call_; }
    unsigned recorded() const { return recorded_; }
    unsigned skipped() const { return skipped_; }
    // Valid between this instruction's CODE hook and the next CODE hook,
    // including stores performed by the final instruction of the window.
    std::optional<Instruction> current_instruction() const { return current_; }
    void clear() {
        active_ = false;
        current_.reset();
    }
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
        current_.reset();
    }
    Tick observe(std::uint64_t address) {
        current_.reset();
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
        current_ = Instruction{call_, step, address};
        if (recorded_ == limit_) {
            active_ = false;
            return {Kind::finished, call_, step};
        }
        return {Kind::sample, call_, step};
    }

  private:
    bool active_ = false;
    bool retf_skipped_ = false;
    std::optional<Instruction> current_;
    unsigned call_ = 0, limit_ = 0, recorded_ = 0, skipped_ = 0;
    std::uint64_t return_linear_ = 0, retf_linear_ = 0;
};

// A candidate native dispatch can be associated with the most recent
// completed picker only while this short guest-time window is active. An
// association is temporal evidence, never proof of a causal click dispatch.
class PickerDispatchWindow {
  public:
    struct Match {
        unsigned call;
        unsigned age_ms;
    };
    void arm(unsigned call, unsigned now_ms, unsigned limit_ms) {
        call_ = call;
        start_ms_ = now_ms;
        limit_ms_ = limit_ms;
        active_ = limit_ms != 0;
    }
    void clear() { active_ = false; }
    bool active() const { return active_; }
    std::optional<Match> match(unsigned now_ms) {
        if (!active_)
            return {};
        if (now_ms < start_ms_ || now_ms - start_ms_ > limit_ms_) {
            active_ = false;
            return {};
        }
        return Match{call_, now_ms - start_ms_};
    }

  private:
    bool active_ = false;
    unsigned call_ = 0, start_ms_ = 0, limit_ms_ = 0;
};

} // namespace gateway

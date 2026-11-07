// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once

#include "opl.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gateway {

// The original GATE_004.MUS holds an MT-32 stream followed by an AdLib
// MIDIPlay/MDI stream for song zero. Return only the latter's original bytes.
std::vector<std::uint8_t> read_intro_mdi(const std::string &mus_path);

// Native OPL2 player for the AdLib MIDIPlay stream. This is a host audio
// bridge, not a reconstruction of the original DOS IRQ8 music driver.
class IntroMdiPlayer {
public:
    explicit IntroMdiPlayer(std::span<const std::uint8_t> mdi);
    std::vector<std::int16_t> render(unsigned frames);
    bool finished() const { return event_index_ == events_.size(); }
    std::uint64_t note_ons() const { return note_ons_; }
    std::uint64_t duration_samples() const { return duration_samples_; }

private:
    enum class Kind { patch, note_on, note_off, volume, mode, bend_range, pitch };
    struct Event {
        std::uint64_t sample = 0;
        Kind kind = Kind::note_off;
        unsigned voice = 0;
        int value = 0;
        std::array<std::uint8_t, 28> patch{};
    };
    struct Voice {
        std::array<std::uint8_t, 28> patch{};
        unsigned volume = 127;
        int note = 60;
        int pitch = 8192;
        bool active = false;
        unsigned block_frequency = 0;
    };

    void write(unsigned reg, unsigned value);
    void process(const Event &event);
    void program(unsigned voice);
    void level(unsigned voice);
    void tune(unsigned voice);
    void stop(unsigned voice);

    Opl opl_;
    std::vector<Event> events_;
    std::array<Voice, 11> voices_{};
    std::uint64_t at_sample_ = 0;
    std::uint64_t duration_samples_ = 0;
    std::uint64_t note_ons_ = 0;
    std::size_t event_index_ = 0;
    unsigned rhythm_keys_ = 0;
    unsigned bend_range_ = 2;
    bool percussion_ = false;
};

} // namespace gateway

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "intro_mdi_player.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

// All fixtures are freshly constructed MIDI headers and synthetic events;
// no original Gateway MIDI bytes are embedded in this test.
std::vector<std::uint8_t> midi(const std::vector<std::uint8_t> &events) {
    std::vector<std::uint8_t> out{
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 120,
        'M', 'T', 'r', 'k',
        std::uint8_t(events.size() >> 24), std::uint8_t(events.size() >> 16),
        std::uint8_t(events.size() >> 8), std::uint8_t(events.size())};
    out.insert(out.end(), events.begin(), events.end());
    out.insert(out.end(), {0, 0xff, 0x2f, 0}); // outside MTrk on AdLib
    return out;
}

int main(int argc, char **argv) {
    std::vector<std::uint8_t> events{
        0, 0xff, 0x7f, 34, 0, 0, 0x3f, 0, 1, 0, // FM patch on voice zero
        1, 1, 2, 15, 7, 1, 4, 7, 10, 0, 0, 0, 0,
        1, 1, 0, 15, 7, 1, 4, 7, 0, 0, 0, 0, 0, 0, 0,
        0, 0x90, 60, 100,      // C4
        0x83, 0x60, 0x80, 0, 0 // after 480 ticks (two seconds)
    };
    const auto file = midi(events);
    gateway::IntroMdiPlayer player(file);
    assert(player.note_ons() == 1);
    assert(player.duration_samples() == 96000);
    const auto first = player.render(4096);
    assert(first.size() == 8192);
    const bool audible = std::any_of(first.begin(), first.end(),
                                     [](std::int16_t v) { return v != 0; });
    assert(audible);
    for (int i = 0; i < 23; ++i) player.render(4096);
    assert(player.finished());

    auto broken = file;
    broken.back() = 1;
    try {
        gateway::IntroMdiPlayer invalid(broken);
        assert(false && "invalid end-of-track was accepted");
    } catch (const std::runtime_error &) {}

    // MIDIPlay's proprietary mode/pitch-range events plus MIDI running
    // status, and the one-to-one OPL rhythm voice assignment.
    gateway::IntroMdiPlayer rhythm(midi({
        0, 0xff, 0x7f, 6, 0, 0, 0x3f, 0, 2, 1,
        0, 0xff, 0x7f, 6, 0, 0, 0x3f, 0, 3, 12,
        0, 0x96, 35, 80, 0, 38, 75,
        0, 0xe6, 0, 64, 0, 0x86, 0, 0
    }));
    assert(rhythm.note_ons() == 2);
    assert(rhythm.render(1024).size() == 2048);

    if (argc == 2) {
        // Optional original-asset integration check, never part of the
        // synthetic fixture and never stored in the repository.
        const auto original = gateway::read_intro_mdi(argv[1]);
        gateway::IntroMdiPlayer intro(original);
        assert(intro.note_ons() == 3193);
        assert(intro.duration_samples() == 12233862);
        const auto samples = intro.render(2048);
        assert(samples.size() == 4096);
        bool heard = false;
        for (unsigned second = 0; second < 10; ++second) {
            const auto pcm = intro.render(gateway::Opl::sample_rate);
            heard |= std::any_of(pcm.begin(), pcm.end(),
                                 [](std::int16_t sample) { return sample != 0; });
        }
        assert(heard && "original AdLib intro emitted only silence");
        std::cout << "original intro: " << intro.note_ons() << " notes, "
                  << intro.duration_samples() << " samples\n";
    }
    return 0;
}

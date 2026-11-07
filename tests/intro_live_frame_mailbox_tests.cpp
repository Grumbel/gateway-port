// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "intro_live_frame_mailbox.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    gateway::IntroLiveFrameMailbox mailbox;
    gateway::IntroLiveFrame frame;
    std::array<std::uint8_t, 320u * 200u> indices{};
    std::uint8_t dac[256][3]{};
    std::uint64_t generation = 0;
    assert(!mailbox.read_after(generation, frame));

    // A color-only transition has no pixel writes. Every rapid update must
    // replace the pending snapshot, including the last one before idle.
    mailbox.publish(indices.data(), dac);
    assert(mailbox.read_after(generation, frame));
    assert(generation == 1);
    mailbox.publish(indices.data(), dac);
    assert(!mailbox.read_after(generation, frame));
    for (unsigned step = 1; step <= 63; ++step) {
        dac[23][0] = static_cast<std::uint8_t>(step);
        mailbox.publish(indices.data(), dac);
    }
    assert(mailbox.read_after(generation, frame));
    assert(generation == 64);
    assert(frame.palette[23 * 3] == 63);
    assert(!mailbox.read_after(generation, frame));

    // Index and palette edits in the same source-C interval must be exposed
    // together; no reader may receive pixels from an earlier DAC state.
    indices[123] = 23;
    dac[23][1] = 41;
    mailbox.publish(indices.data(), dac);
    assert(mailbox.read_after(generation, frame));
    assert(frame.indices[123] == 23);
    assert(frame.palette[23 * 3] == 63);
    assert(frame.palette[23 * 3 + 1] == 41);
    assert(!mailbox.read_after(generation, frame));

    std::cout << "source-C live VGA snapshots retain rapid final DAC updates\n";
}

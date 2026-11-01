#include "pit.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        gateway::Pit pit;
        pit.advance(54000);
        require(!pit.pending(), "default tick arrived early");
        pit.advance(1000);
        require(pit.pending(), "default tick missing");
        pit.acknowledge();
        require(!pit.pending(), "IRQ acknowledge failed");
        pit.out(0x43, 0x36);
        pit.out(0x40, 0xa9);
        pit.out(0x40, 4); // 1193 ticks, ~1ms.
        pit.advance(900);
        require(!pit.pending(), "programmed tick arrived early");
        pit.out(0x43, 0); // Latch must survive elapsed time between low/high reads.
        const unsigned low = pit.in(0x40);
        pit.advance(50);
        const unsigned count = low | (unsigned(pit.in(0x40)) << 8);
        require(count >= 118 && count <= 120, "latched count changed");
        pit.advance(100);
        require(pit.pending(), "programmed tick missing");
        pit.acknowledge();
        pit.out(0x43, 0x36);
        pit.out(0x40, 0);
        pit.out(0x40, 0);
        pit.advance(54000);
        require(!pit.pending(), "zero reload not 65536");
        // Spellcasting 101's title playback programs IRQ0 to ~16.6 kHz.
        // Each edge must become independently deliverable, rather than
        // collapsing sixteen IRQs into one 1-ms emulation step.
        gateway::Pit fast;
        fast.out(0x43, 0x34); // channel 0, mode 2, low/high
        fast.out(0x40, 0x48);
        fast.out(0x40, 0);
        unsigned edges = 0;
        std::uint64_t elapsed = 0;
        while (elapsed < 10000) {
            const auto delta = std::min<std::uint64_t>(10000 - elapsed,
                                                       fast.microseconds_until_irq());
            fast.advance(delta);
            elapsed += delta;
            if (fast.pending()) {
                fast.acknowledge();
                ++edges;
            }
        }
        require(edges >= 165 && edges <= 166, "high-frequency timer edges were lost");
        // Software-triggered one-shot counter must not retrigger every period.
        gateway::Pit one_shot;
        one_shot.out(0x43, 0x30); // channel 0, low/high, mode 0
        one_shot.out(0x40, 0xa9);
        one_shot.out(0x40, 4);
        one_shot.advance(1100);
        require(one_shot.pending(), "mode 0 terminal interrupt missing");
        one_shot.acknowledge();
        one_shot.advance(2000);
        require(!one_shot.pending(), "mode 0 retriggered after terminal count");
        // Also accept the standard hardware-triggered and strobe modes.
        for (unsigned mode : {1u, 4u, 5u}) {
            one_shot.out(0x43, 0x30 | (mode << 1));
            one_shot.out(0x40, 0xa9);
            one_shot.out(0x40, 4);
            one_shot.advance(1200);
            require(one_shot.pending(), "PIT one-shot/strobe mode not accepted");
            one_shot.acknowledge();
        }
        gateway::Pit speaker;
        speaker.out(0x43, 0xb6);
        speaker.out(0x42, 0xa9);
        speaker.out(0x42, 4); // Approximately 1 kHz.
        speaker.out(0x61, 3);
        speaker.advance(1000);
        speaker.out(0x61, 0);
        require((speaker.in(0x61) & 3) == 0, "speaker control readback");
        std::array<std::int16_t, 96> samples{};
        speaker.mix_speaker(samples);
        bool positive = false, negative = false;
        for (unsigned i = 0; i < samples.size(); i += 2) {
            positive |= samples[i] > 0;
            negative |= samples[i] < 0;
            require(samples[i] == samples[i + 1], "speaker stereo mismatch");
        }
        require(positive && negative, "short beep lost before audio submission");
        speaker.advance(1000);
        samples.fill(0);
        speaker.mix_speaker(samples);
        for (auto sample : samples)
            require(sample == 0, "disabled speaker is not silent");
        std::cout << "PIT checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

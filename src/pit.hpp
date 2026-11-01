#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace gateway {
// PIT channel programming, IRQ0 cadence, and the port-61h PC speaker.
class Pit {
  public:
    bool handles_port(std::uint16_t port) const {
        return (port >= 0x40 && port <= 0x43) || port == 0x61;
    }
    std::uint8_t in(std::uint16_t port);
    void out(std::uint16_t port, std::uint8_t value);
    void advance(std::uint64_t microseconds);
    // Time until channel 0's next timer edge, rounded up to whole microseconds.
    // A pending IRQ or a completed one-shot needs no finer scheduling.
    unsigned microseconds_until_irq() const;
    void mix_speaker(std::span<std::int16_t> stereo);
    bool pending() const { return pending_; }
    void acknowledge() { pending_ = false; }

  private:
    struct Channel {
        std::uint32_t reload = 65536, remaining = 65536;
        std::uint16_t write = 0, latched = 0;
        std::uint8_t access = 3, mode = 3;
        bool high_write = false, high_read = false, latch_valid = false;
        bool terminal = false;
    };
    std::array<Channel, 3> channels_{};
    std::uint64_t fraction_ = 0;
    bool pending_ = false;
    std::uint8_t speaker_control_ = 0;
    std::uint64_t audio_fraction_ = 0, refresh_ticks_ = 0;
    std::array<std::int16_t, 768> speaker_audio_{};
    unsigned audio_next_ = 0, audio_count_ = 0;
    bool speaker_output(std::uint64_t ticks) const;
};
} // namespace gateway

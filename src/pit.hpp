#pragma once
#include <array>
#include <cstdint>
namespace gateway {
// PIT channel programming and IRQ0 cadence; no speaker synthesis yet.
class Pit {
  public:
    bool handles_port(std::uint16_t port) const { return port >= 0x40 && port <= 0x43; }
    std::uint8_t in(std::uint16_t port);
    void out(std::uint16_t port, std::uint8_t value);
    void advance(std::uint64_t microseconds);
    bool pending() const { return pending_; }
    void acknowledge() { pending_ = false; }

  private:
    struct Channel {
        std::uint32_t reload = 65536, remaining = 65536;
        std::uint16_t write = 0, latched = 0;
        std::uint8_t access = 3, mode = 3;
        bool high_write = false, high_read = false, latch_valid = false;
    };
    std::array<Channel, 3> channels_{};
    std::uint64_t fraction_ = 0;
    bool pending_ = false;
};
} // namespace gateway

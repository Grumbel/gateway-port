#pragma once
#include <cstdint>
#include <memory>
#include <vector>
struct _opl3_chip;
namespace gateway {
class Opl {
  public:
    static constexpr unsigned sample_rate = 48000;
    Opl();
    ~Opl();
    bool handles_port(std::uint16_t port) const { return port == 0x388 || port == 0x389; }
    std::uint8_t in(std::uint16_t port);
    void out(std::uint16_t port, std::uint8_t value);
    void advance(std::uint64_t microseconds);
    std::vector<std::int16_t> render(unsigned frames); // Interleaved stereo.
  private:
    std::unique_ptr<_opl3_chip> chip_;
    std::uint8_t address_ = 0, control_ = 0;
    std::uint8_t timer_value_[2]{};
    std::uint64_t elapsed_[2]{};
    bool overflow_[2]{};
};
} // namespace gateway

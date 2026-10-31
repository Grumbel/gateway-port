#include "opl.hpp"
#include "opl3.h"
#include <stdexcept>
namespace gateway {
Opl::Opl() : chip_(std::make_unique<opl3_chip>()) { OPL3_Reset(chip_.get(), sample_rate); }
Opl::~Opl() = default;
void Opl::advance(std::uint64_t microseconds) {
    for (unsigned i = 0; i < 2; ++i) {
        if (!(control_ & (1 << i)))
            continue;
        elapsed_[i] += microseconds;
        const unsigned period = (256 - unsigned(timer_value_[i])) * (i == 0 ? 80 : 320);
        if (elapsed_[i] >= period) {
            overflow_[i] = true;
            elapsed_[i] %= period;
        }
    }
}
std::uint8_t Opl::in(std::uint16_t port) {
    advance(1); // Each hardware access consumes a minimal modeled bus interval.
    if (port == 0x389)
        return 255;
    if (port != 0x388)
        throw std::runtime_error("invalid AdLib port read");
    const bool a = overflow_[0] && !(control_ & 0x40);
    const bool b = overflow_[1] && !(control_ & 0x20);
    return 6 | (a ? 0x40 : 0) | (b ? 0x20 : 0) | ((a || b) ? 0x80 : 0);
}
void Opl::out(std::uint16_t port, std::uint8_t value) {
    advance(1);
    if (port == 0x388) {
        address_ = value;
        return;
    }
    if (port != 0x389)
        throw std::runtime_error("invalid AdLib port write");
    if (address_ == 2 || address_ == 3)
        timer_value_[address_ - 2] = value;
    if (address_ == 4) {
        if (value & 0x80)
            overflow_[0] = overflow_[1] = false;
        else {
            for (unsigned i = 0; i < 2; ++i) {
                if ((value & (1 << i)) && !(control_ & (1 << i)))
                    elapsed_[i] = 0;
                if (value & (0x40 >> i))
                    overflow_[i] = false;
            }
            control_ = value;
        }
    }
    OPL3_WriteReg(chip_.get(), address_, value);
}
std::vector<std::int16_t> Opl::render(unsigned frames) {
    if (frames > sample_rate)
        throw std::runtime_error("AdLib render block exceeds one second");
    std::vector<std::int16_t> samples(std::size_t(frames) * 2);
    OPL3_GenerateStream(chip_.get(), samples.data(), frames);
    return samples;
}
} // namespace gateway

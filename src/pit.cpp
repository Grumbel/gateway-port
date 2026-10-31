#include "pit.hpp"
#include <stdexcept>
namespace gateway {
void Pit::advance(std::uint64_t microseconds) {
    const auto scaled = microseconds * 1193182 + fraction_;
    const auto ticks = scaled / 1000000;
    fraction_ = scaled % 1000000;
    for (unsigned i = 0; i < channels_.size(); ++i) {
        auto &c = channels_[i];
        if (!ticks)
            continue;
        if (ticks >= c.remaining) {
            if (i == 0)
                pending_ = true;
            c.remaining = c.reload - ((ticks - c.remaining) % c.reload);
        } else
            c.remaining -= ticks;
    }
}
void Pit::out(std::uint16_t port, std::uint8_t value) {
    if (port == 0x43) {
        const auto index = value >> 6;
        if (index == 3)
            throw std::runtime_error("unsupported PIT read-back command");
        auto &c = channels_[index];
        const auto access = (value >> 4) & 3;
        if (!access) {
            c.latched = c.remaining;
            c.latch_valid = true;
            c.high_read = false;
            return;
        }
        if (value & 1)
            throw std::runtime_error("unsupported PIT BCD counter");
        auto mode = (value >> 1) & 7;
        if (mode >= 6)
            mode -= 4;
        if (mode != 2 && mode != 3)
            throw std::runtime_error("unsupported PIT counter mode");
        c.access = access;
        c.mode = mode;
        c.high_write = c.high_read = false;
        c.latch_valid = false;
        return;
    }
    if (port < 0x40 || port > 0x42)
        throw std::runtime_error("invalid PIT write port");
    auto &c = channels_[port - 0x40];
    if (c.access == 1)
        c.write = value;
    else if (c.access == 2)
        c.write = std::uint16_t(value) << 8;
    else {
        if (!c.high_write) {
            c.write = value;
            c.high_write = true;
            return;
        }
        c.write |= std::uint16_t(value) << 8;
        c.high_write = false;
    }
    c.reload = c.write ? c.write : 65536;
    c.remaining = c.reload;
}
std::uint8_t Pit::in(std::uint16_t port) {
    if (port < 0x40 || port > 0x42)
        throw std::runtime_error("invalid PIT read port");
    auto &c = channels_[port - 0x40];
    const auto value = c.latch_valid ? c.latched : static_cast<std::uint16_t>(c.remaining);
    if (c.access == 1) {
        c.latch_valid = false;
        return value & 255;
    }
    if (c.access == 2) {
        c.latch_valid = false;
        return value >> 8;
    }
    if (!c.high_read) {
        c.high_read = true;
        return value & 255;
    }
    c.high_read = false;
    c.latch_valid = false;
    return value >> 8;
}
} // namespace gateway

#include "pit.hpp"
#include <algorithm>
#include <stdexcept>
namespace gateway {
bool Pit::speaker_output(std::uint64_t ticks) const {
    const auto &c = channels_[2];
    if (!(speaker_control_ & 1))
        return true;
    const auto elapsed = c.reload - c.remaining + ticks;
    if (c.mode == 0 || c.mode == 1)
        return c.terminal || elapsed >= c.reload;
    if (c.mode == 4 || c.mode == 5)
        return elapsed != c.reload;
    const auto phase = elapsed % c.reload;
    return c.mode == 3 ? phase < (c.reload + 1) / 2 : phase != c.reload - 1;
}
void Pit::mix_speaker(std::span<std::int16_t> stereo) {
    const auto frames = stereo.size() / 2;
    const auto count = std::min<std::size_t>(audio_count_, frames);
    const auto start = (audio_next_ + speaker_audio_.size() - count) % speaker_audio_.size();
    for (std::size_t frame = 0; frame < count; ++frame)
        for (unsigned side = 0; side < 2; ++side) {
            auto &sample = stereo[(frames - count + frame) * 2 + side];
            sample =
                std::clamp(int(sample) + speaker_audio_[(start + frame) % speaker_audio_.size()],
                           -32768, 32767);
        }
    audio_count_ = 0;
}
unsigned Pit::microseconds_until_irq() const {
    const auto &channel = channels_[0];
    if (pending_ || (channel.terminal &&
                     (channel.mode == 0 || channel.mode == 1 || channel.mode == 4 ||
                      channel.mode == 5)))
        return 1000;
    const std::uint64_t needed = std::uint64_t(channel.remaining) * 1000000 - fraction_;
    return std::max<std::uint64_t>(1, (needed + 1193181) / 1193182);
}
void Pit::advance(std::uint64_t microseconds) {
    // Preserve sub-block beeps even if the guest switches the speaker off before
    // the next 16-ms SDL audio submission. Retain at most one presentation block.
    const auto scaled_audio = microseconds * 48000 + audio_fraction_;
    const auto frames = scaled_audio / 1000000;
    audio_fraction_ = scaled_audio % 1000000;
    for (auto frame = frames > 768 ? frames - 768 : 0; frame < frames; ++frame) {
        const auto ticks = frame * 1193182 / 48000;
        speaker_audio_[audio_next_] =
            (speaker_control_ & 3) == 3 ? (speaker_output(ticks) ? 1800 : -1800) : 0;
        audio_next_ = (audio_next_ + 1) % speaker_audio_.size();
        audio_count_ = std::min<unsigned>(speaker_audio_.size(), audio_count_ + 1);
    }
    const auto scaled = microseconds * 1193182 + fraction_;
    const auto ticks = scaled / 1000000;
    fraction_ = scaled % 1000000;
    refresh_ticks_ += ticks;
    for (unsigned i = 0; i < channels_.size(); ++i) {
        auto &c = channels_[i];
        if (i == 2 && !(speaker_control_ & 1))
            continue;
        if (!ticks)
            continue;
        if (ticks >= c.remaining) {
            if (i == 0 && !c.terminal)
                pending_ = true;
            if (c.mode == 0 || c.mode == 1 || c.mode == 4 || c.mode == 5) {
                c.terminal = true;
                c.remaining = 1;
            } else
                c.remaining = c.reload - ((ticks - c.remaining) % c.reload);
        } else
            c.remaining -= ticks;
    }
}
void Pit::out(std::uint16_t port, std::uint8_t value) {
    if (port == 0x61) {
        if (!(speaker_control_ & 1) && (value & 1))
            channels_[2].remaining = channels_[2].reload;
        speaker_control_ = value & 0x0f;
        return;
    }
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
        c.access = access;
        c.mode = mode;
        c.terminal = false;
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
    c.terminal = false;
}
std::uint8_t Pit::in(std::uint16_t port) {
    if (port == 0x61)
        return speaker_control_ | ((refresh_ticks_ / 18 & 1) << 4) | (speaker_output(0) ? 0x20 : 0);
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

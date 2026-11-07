// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "intro_mdi_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace gateway {
namespace {

constexpr unsigned first_payload = 60 * 2 * 12 + 1;
constexpr std::array<unsigned, 9> operators{0, 1, 2, 8, 9, 10, 16, 17, 18};
constexpr std::array<unsigned, 11> channels{0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7};
constexpr std::array<unsigned, 11> rhythm_bits{0, 0, 0, 0, 0, 0, 0x10, 8, 4, 2, 1};

std::uint16_t le16(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 2)
        throw std::runtime_error("truncated MUS 16-bit field");
    return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
}
std::uint32_t le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(le16(bytes, at)) | (std::uint32_t(le16(bytes, at + 2)) << 16);
}
std::uint16_t be16(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 2)
        throw std::runtime_error("truncated MIDI 16-bit field");
    return (unsigned(bytes[at]) << 8) | bytes[at + 1];
}
std::uint32_t be32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (std::uint32_t(be16(bytes, at)) << 16) | be16(bytes, at + 2);
}
unsigned vlq(std::span<const std::uint8_t> bytes, std::size_t &pos, std::size_t end) {
    unsigned number = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (pos == end)
            throw std::runtime_error("truncated MIDI variable-length field");
        const unsigned byte = bytes[pos++];
        number = (number << 7) | (byte & 127);
        if (byte < 128) return number;
    }
    throw std::runtime_error("overlong MIDI variable-length field");
}

} // namespace

std::vector<std::uint8_t> read_intro_mdi(const std::string &mus_path) {
    std::ifstream stream(mus_path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open original intro MUS file: " + mus_path);
    const std::vector<std::uint8_t> mus{std::istreambuf_iterator<char>(stream), {}};
    const std::span bytes{mus};
    if (bytes.size() < first_payload || bytes[first_payload - 1] != 0)
        throw std::runtime_error("invalid original MUS directory");
    // Descriptor 0 is the MT-32 version, descriptor 1 the AdLib MIDIPlay
    // version. Neither file size nor original payload bytes are hard-coded.
    const auto mt32_offset = le32(bytes, 0);
    const auto mt32_size = le16(bytes, 4);
    const auto adlib_offset = le32(bytes, 12);
    const auto adlib_size = le16(bytes, 16);
    if (mt32_offset != first_payload || le16(bytes, 6) != 1 ||
        adlib_offset != mt32_offset + mt32_size + 1 ||
        le16(bytes, 18) != 0 ||
        adlib_offset > bytes.size() || adlib_size > bytes.size() - adlib_offset ||
        adlib_size < 26 || bytes[adlib_offset] != 'M' ||
        bytes[adlib_offset + 1] != 'T')
        throw std::runtime_error("invalid intro AdLib MIDI descriptor");
    return {mus.begin() + adlib_offset, mus.begin() + adlib_offset + adlib_size};
}

IntroMdiPlayer::IntroMdiPlayer(std::span<const std::uint8_t> mdi) {
    if (mdi.size() < 26 || std::memcmp(mdi.data(), "MThd", 4) ||
        be32(mdi, 4) != 6 || be16(mdi, 8) != 0 || be16(mdi, 10) != 1)
        throw std::runtime_error("unsupported AdLib MIDIPlay file header");
    const unsigned ppqn = be16(mdi, 12);
    if (ppqn == 0 || ppqn & 0x8000 || std::memcmp(mdi.data() + 14, "MTrk", 4))
        throw std::runtime_error("invalid AdLib MIDI timebase or track header");
    const std::size_t track_end = 22u + be32(mdi, 18);
    if (track_end > mdi.size() || mdi.size() - track_end != 4 ||
        std::memcmp(mdi.data() + track_end, "\0\xff\x2f\0", 4))
        throw std::runtime_error("missing external AdLib MIDI end-of-track");

    std::size_t pos = 22;
    unsigned tempo = 500000;
    long double time_us = 0;
    unsigned running_status = 0;
    while (pos < track_end) {
        const auto delta = vlq(mdi, pos, track_end);
        time_us += static_cast<long double>(delta) * tempo / ppqn;
        if (pos == track_end) throw std::runtime_error("MIDI event lacks status");
        unsigned status = mdi[pos];
        if (status & 128) {
            ++pos;
            running_status = status < 0xf0 ? status : 0;
        } else {
            if (running_status < 0x80 || running_status > 0xef)
                throw std::runtime_error("MIDI running status without channel event");
            status = running_status;
        }
        Event event;
        event.sample = static_cast<std::uint64_t>(std::llround(
            time_us * Opl::sample_rate / 1000000));
        duration_samples_ = event.sample;
        if (status == 0xff) {
            if (pos == track_end) throw std::runtime_error("truncated MIDI meta event");
            const unsigned type = mdi[pos++];
            const auto length = vlq(mdi, pos, track_end);
            if (length > track_end - pos)
                throw std::runtime_error("MIDI meta event exceeds track");
            if (type == 0x51) {
                if (length != 3) throw std::runtime_error("invalid MIDI tempo event");
                tempo = (unsigned(mdi[pos]) << 16) | (unsigned(mdi[pos + 1]) << 8) |
                        mdi[pos + 2];
                if (tempo == 0) throw std::runtime_error("zero MIDI tempo");
            } else if (type == 0x7f && length >= 5 &&
                       std::memcmp(mdi.data() + pos, "\0\0\x3f", 3) == 0) {
                const auto op = (unsigned(mdi[pos + 3]) << 8) | mdi[pos + 4];
                if (op == 1) {
                    if (length != 34) throw std::runtime_error("invalid MDI FM patch");
                    event.kind = Kind::patch;
                    event.voice = mdi[pos + 5];
                    std::copy_n(mdi.begin() + pos + 6, 28, event.patch.begin());
                    events_.push_back(event);
                } else if (op == 2 || op == 3) {
                    if (length != 6) throw std::runtime_error("invalid MDI mode or pitch range");
                    event.kind = op == 2 ? Kind::mode : Kind::bend_range;
                    event.value = mdi[pos + 5];
                    events_.push_back(event);
                }
            }
            pos += length;
        } else if (status == 0xf0 || status == 0xf7) {
            const auto length = vlq(mdi, pos, track_end);
            if (length > track_end - pos)
                throw std::runtime_error("MIDI sysex exceeds track");
            pos += length;
        } else if (status >= 0x80 && status <= 0xef) {
            const auto command = status & 0xf0;
            const auto nbytes = command == 0xc0 || command == 0xd0 ? 1u : 2u;
            if (nbytes > track_end - pos ||
                std::any_of(mdi.begin() + pos, mdi.begin() + pos + nbytes,
                            [](std::uint8_t byte) { return byte & 128; }))
                throw std::runtime_error("truncated or invalid MIDI channel event");
            event.voice = status & 15;
            const int a = mdi[pos], b = nbytes == 2 ? mdi[pos + 1] : 0;
            if (command == 0x90 && b) {
                event.kind = Kind::note_on;
                event.value = a | (b << 8);
                events_.push_back(event);
                ++note_ons_;
            } else if (command == 0x80 || command == 0x90) {
                event.kind = Kind::note_off;
                events_.push_back(event);
            } else if (command == 0xa0 || command == 0xd0) {
                event.kind = Kind::volume;
                event.value = command == 0xd0 ? a : b;
                events_.push_back(event);
            } else if (command == 0xe0) {
                event.kind = Kind::pitch;
                event.value = a | (b << 7);
                events_.push_back(event);
            }
            pos += nbytes;
        } else {
            throw std::runtime_error("unsupported MIDI system event");
        }
    }
    // AdLib MIDIPlay uses a one-to-one mapping to the OPL channels; unlike
    // generic MIDI it does not allocate multiple voices per MIDI channel.
    write(1, 0x20); // OPL waveform select enable
    write(8, 0);
    write(0xbd, 0);
    for (unsigned ch = 0; ch < 9; ++ch) write(0xb0 + ch, 0);
}

void IntroMdiPlayer::write(unsigned reg, unsigned value) {
    opl_.out(0x388, static_cast<std::uint8_t>(reg));
    opl_.out(0x389, static_cast<std::uint8_t>(value));
}

void IntroMdiPlayer::level(unsigned voice) {
    const auto &v = voices_[voice];
    const auto ch = channels[voice];
    const auto mod = operators[ch], car = mod + 3;
    const auto set = [&](unsigned op, unsigned shift) {
        const unsigned tl = v.patch[shift + 8] & 63;
        const unsigned effective = tl + (63 - tl) * (127 - v.volume) / 127;
        write(0x40 + op, ((v.patch[shift] & 3) << 6) | effective);
    };
    if (!percussion_ || voice <= 6) {
        set(mod, 0);
        set(car, 13);
    } else if (voice == 7 || voice == 9) set(car, 13);
    else set(mod, 0);
}

void IntroMdiPlayer::program(unsigned voice) {
    const auto &p = voices_[voice].patch;
    const auto ch = channels[voice];
    const auto mod = operators[ch], car = mod + 3;
    const auto set = [&](unsigned op, unsigned shift, unsigned wave) {
        const auto f = [&](unsigned field) { return unsigned(p[shift + field]); };
        write(0x20 + op, (f(1) & 15) | (f(9) ? 0x80 : 0) |
                  (f(10) ? 0x40 : 0) | (f(5) ? 0x20 : 0) | (f(11) ? 0x10 : 0));
        write(0x60 + op, ((f(3) & 15) << 4) | (f(6) & 15));
        write(0x80 + op, ((f(4) & 15) << 4) | (f(7) & 15));
        write(0xe0 + op, p[wave] & 3);
    };
    if (!percussion_ || voice <= 6) {
        set(mod, 0, 26);
        set(car, 13, 27);
        write(0xc0 + ch, ((p[2] & 7) << 1) | (p[12] == 0 ? 1 : 0));
    } else if (voice == 7 || voice == 9) set(car, 13, 27);
    else set(mod, 0, 26);
    level(voice);
}

void IntroMdiPlayer::stop(unsigned voice) {
    auto &v = voices_[voice];
    if (!v.active) return;
    v.active = false;
    const auto ch = channels[voice];
    if (percussion_ && voice >= 6) {
        rhythm_keys_ &= ~rhythm_bits[voice];
        write(0xbd, 0x20 | rhythm_keys_);
    } else write(0xb0 + ch, v.block_frequency & ~0x20);
}

void IntroMdiPlayer::tune(unsigned voice) {
    auto &v = voices_[voice];
    const double midi = v.note + (v.pitch - 8192) * (double(bend_range_) / 8192);
    const unsigned block = std::clamp(int(midi / 12) - 1, 0, 7);
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12);
    const unsigned fnum = std::clamp(int(std::lround(hz * 1048576.0 /
                                  (49716.0 * double(1u << block)))), 0, 1023);
    const auto ch = channels[voice];
    write(0xa0 + ch, fnum & 255);
    v.block_frequency = ((fnum >> 8) & 3) | (block << 2) |
                        (percussion_ && voice >= 6 ? 0 : 0x20);
    write(0xb0 + ch, v.block_frequency);
    if (percussion_ && voice >= 6) {
        rhythm_keys_ |= rhythm_bits[voice];
        write(0xbd, 0x20 | rhythm_keys_);
    }
}

void IntroMdiPlayer::process(const Event &event) {
    if (event.kind == Kind::mode) {
        for (unsigned voice = 0; voice < voices_.size(); ++voice) stop(voice);
        percussion_ = event.value != 0;
        rhythm_keys_ = 0;
        write(0xbd, percussion_ ? 0x20 : 0);
        return;
    }
    if (event.kind == Kind::bend_range) {
        bend_range_ = std::clamp(event.value, 1, 12);
        return;
    }
    if (event.voice >= voices_.size()) return;
    auto &v = voices_[event.voice];
    switch (event.kind) {
    case Kind::patch:
        v.patch = event.patch;
        program(event.voice);
        break;
    case Kind::note_on:
        stop(event.voice);
        v.note = event.value & 255;
        v.volume = (event.value >> 8) & 127;
        v.active = true;
        level(event.voice);
        tune(event.voice);
        break;
    case Kind::note_off: stop(event.voice); break;
    case Kind::volume:
        v.volume = event.value;
        level(event.voice);
        break;
    case Kind::pitch:
        v.pitch = event.value;
        if (v.active) tune(event.voice);
        break;
    default: break;
    }
}

std::vector<std::int16_t> IntroMdiPlayer::render(unsigned frames) {
    if (frames > Opl::sample_rate)
        throw std::runtime_error("intro MIDI render block exceeds one second");
    std::vector<std::int16_t> result(std::size_t(frames) * 2);
    unsigned written = 0;
    while (written < frames) {
        while (event_index_ < events_.size() &&
               events_[event_index_].sample <= at_sample_)
            process(events_[event_index_++]);
        const auto until = event_index_ < events_.size()
            ? events_[event_index_].sample : at_sample_ + frames - written;
        const auto chunk = unsigned(std::min<std::uint64_t>(frames - written,
            std::max<std::uint64_t>(1, until - at_sample_)));
        auto samples = opl_.render(chunk);
        std::copy(samples.begin(), samples.end(), result.begin() + written * 2);
        written += chunk;
        at_sample_ += chunk;
    }
    return result;
}

} // namespace gateway

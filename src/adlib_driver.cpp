#include "adlib_driver.hpp"
#include <algorithm>
#include <cmath>

namespace gateway {
namespace {
constexpr std::array<unsigned, 9> modulator{0, 1, 2, 8, 9, 10, 16, 17, 18};
// SOUND.COM numbers percussion voices 6..10 as bass, snare, tom, cymbal, hat.
constexpr std::array<unsigned, 11> channel{0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7};
constexpr std::array<unsigned, 11> rhythm_bit{0, 0, 0, 0, 0, 0, 0x10, 8, 4, 2, 1};

unsigned byte_word(const std::array<std::uint8_t, 56> &p, unsigned word_index) {
    return p[word_index * 2];
}
} // namespace

std::uint16_t AdlibDriver::word(std::span<const std::uint8_t> data, unsigned offset) {
    if (offset + 2 > data.size())
        return 0;
    return std::uint16_t(data[offset] | (unsigned(data[offset + 1]) << 8));
}

double AdlibDriver::ticks(std::span<const std::uint8_t> args, unsigned offset) const {
    // SB-SOUND.COM's d79h routine converts the TWO words to a rational
    // beat count, numerator / denominator; they are not 16.16 fixed-point.
    // For example 01 00 08 00 is one eighth of a beat, not eight beats.
    const auto denominator = word(args, offset + 2);
    return denominator ? double(word(args, offset)) / denominator * ticks_per_beat_ : 0;
}

void AdlibDriver::write(unsigned reg, unsigned value) {
    opl_.out(0x388, static_cast<std::uint8_t>(reg));
    opl_.out(0x389, static_cast<std::uint8_t>(value));
}

void AdlibDriver::stop(unsigned voice) {
    if (voice >= voices_.size() || !voices_[voice].active)
        return;
    auto &v = voices_[voice];
    v.active = false;
    const auto ch = channel[voice];
    if (percussion_ && voice >= 6) {
        rhythm_keys_ &= ~rhythm_bit[voice];
        write(0xbd, 0x20 | rhythm_keys_);
    } else {
        write(0xb0 + ch, v.block_frequency & ~0x20);
    }
}

void AdlibDriver::reset() {
    playing_ = percussion_ = waveform_ = false;
    rhythm_keys_ = 0;
    tempo_ = 90;
    ticks_per_beat_ = 32;
    current_tick_ = 0;
    relative_origin_ = 0;
    next_voice_tick_.fill(0);
    active_voice_ = 0;
    next_order_ = played_notes_ = 0;
    events_ = {};
    voices_ = {};
    write(1, 0);
    write(8, 0);
    write(0xbd, 0);
    for (unsigned ch = 0; ch < 9; ++ch)
        write(0xb0 + ch, 0);
}

std::uint64_t AdlibDriver::enqueue(Event event) {
    event.order = next_order_++;
    const auto order = event.order;
    events_.push(std::move(event));
    return order;
}

void AdlibDriver::level(unsigned voice) {
    const auto &v = voices_[voice];
    const auto ch = channel[voice];
    const auto mod = modulator[ch];
    const auto car = mod + 3;
    const auto operator_level = [&](unsigned op, unsigned shift) {
        const unsigned ksl = byte_word(v.patch, shift) & 3;
        const unsigned raw_level = byte_word(v.patch, shift + 8) & 63;
        const auto adjusted = unsigned(std::clamp(std::lround(
            raw_level + (63 - raw_level) * (1 - v.volume)), 0l, 63l));
        write(0x40 + op, (ksl << 6) | adjusted);
    };
    if (!percussion_ || voice <= 6) {
        operator_level(mod, 0);
        operator_level(car, 13);
    } else if (voice == 7 || voice == 9) {
        operator_level(car, 13);
    } else {
        operator_level(mod, 0);
    }
}

void AdlibDriver::timbre(unsigned voice, const std::array<std::uint8_t, 56> &patch) {
    if (voice >= voices_.size())
        return;
    auto &v = voices_[voice];
    v.patch = patch;
    const unsigned ch = channel[voice];
    const auto mod = modulator[ch];
    const auto car = mod + 3;
    const auto operator_patch = [&](unsigned op, unsigned shift, unsigned wave) {
        const auto field = [&](unsigned f) { return byte_word(patch, shift + f); };
        write(0x20 + op, (field(1) & 15) | (field(9) ? 0x80 : 0) |
                             (field(10) ? 0x40 : 0) | (field(5) ? 0x20 : 0) |
                             (field(11) ? 0x10 : 0));
        write(0x60 + op, ((field(3) & 15) << 4) | (field(6) & 15));
        write(0x80 + op, ((field(4) & 15) << 4) | (field(7) & 15));
        write(0xe0 + op, waveform_ ? byte_word(patch, wave) & 3 : 0);
    };
    if (!percussion_ || voice <= 6) {
        operator_patch(mod, 0, 26);
        operator_patch(car, 13, 27);
        write(0xc0 + ch, ((byte_word(patch, 2) & 7) << 1) |
                               (byte_word(patch, 12) == 0 ? 1 : 0));
    } else if (voice == 7 || voice == 9) {
        operator_patch(car, 13, 27);
    } else {
        operator_patch(mod, 0, 26);
    }
    level(voice);
}

void AdlibDriver::start(unsigned voice, int note) {
    if (voice >= voices_.size())
        return;
    stop(voice);
    auto &v = voices_[voice];
    v.active = true;
    v.note = note;
    ++played_notes_;
    tune(voice);
}

void AdlibDriver::tune(unsigned voice) {
    const auto &v = voices_[voice];
    // Driver note number 0 is middle C (MIDI 60), not MIDI note zero.
    const int midi = std::clamp(v.note + 60, 0, 127);
    const unsigned block = std::clamp(midi / 12 - 1, 0, 7);
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    const int fnum = std::clamp(int(std::lround(hz * 1048576.0 /
                           (49716.0 * double(1u << block)))) + v.bend, 0, 1023);
    const unsigned ch = channel[voice];
    write(0xa0 + ch, fnum & 255);
    voices_[voice].block_frequency = ((fnum >> 8) & 3) | (block << 2) |
                                    (percussion_ && voice >= 6 ? 0 : 0x20);
    write(0xb0 + ch, voices_[voice].block_frequency);
    if (percussion_ && voice >= 6) {
        rhythm_keys_ |= rhythm_bit[voice];
        write(0xbd, 0x20 | rhythm_keys_);
    }
}

void AdlibDriver::process(const Event &event) {
    if (event.voice >= voices_.size())
        return;
    auto &voice = voices_[event.voice];
    switch (event.kind) {
    case Kind::note:
        start(event.voice, event.value);
        voice.note_order = event.order;
        break;
    case Kind::off:
        // A newer note on the same voice supersedes the earlier release.
        if (voice.note_order == event.note_order)
            stop(event.voice);
        break;
    case Kind::rest: break;
    case Kind::timbre: timbre(event.voice, event.patch); break;
    case Kind::volume:
        voice.volume = std::clamp(event.value / 65535.0, 0.0, 1.0);
        level(event.voice);
        break;
    case Kind::tempo: tempo_ = std::clamp(double(event.value), 1.0, 1000.0); break;
    case Kind::pitch:
        voice.bend = event.value;
        if (voice.active)
            tune(event.voice);
        break;
    }
}

std::uint16_t AdlibDriver::call(std::uint16_t function, std::span<const std::uint8_t> args,
                                std::uint16_t ax, std::span<const std::uint8_t> instrument) {
    function &= 255; // SOUND.COM ignores the high byte of SI.
    const auto accepted = [&] { return std::uint16_t((ax & 0xff00) | 0xff); };
    const auto event = [&](Kind kind, unsigned offset, int value, bool relative = false) {
        Event item;
        item.kind = kind;
        item.voice = active_voice_;
        item.tick = ticks(args, offset) + (relative ? relative_origin_ : 0);
        item.value = value;
        enqueue(std::move(item));
    };
    switch (function) {
    case 0x00: reset(); break;
    case 0x02:
        relative_origin_ = current_tick_ = ticks(args, 0);
        next_voice_tick_.fill(current_tick_);
        break;
    case 0x03:
        playing_ = word(args, 0) != 0;
        break;
    case 0x04: return playing_ ? 1 : 0;
    case 0x05:
        for (unsigned voice = 0; voice < voices_.size(); ++voice)
            stop(voice);
        events_ = {};
        playing_ = false;
        break;
    case 0x06:
        percussion_ = word(args, 0) != 0;
        rhythm_keys_ = 0;
        write(0xbd, percussion_ ? 0x20 : 0);
        break;
    case 0x07: return percussion_ ? 1 : 0;
    case 0x08: {
        const auto denominator = word(args, 2);
        const double ratio = denominator ? double(word(args, 0)) / denominator : 0.0;
        event(Kind::volume, 4, int(std::lround(std::clamp(ratio, 0.0, 1.0) * 65535)),
              true);
        return accepted();
    }
    case 0x09:
        event(Kind::tempo, 2, word(args, 0), true);
        return accepted();
    case 0x0a: break; // Global transpose is not used by the traced music.
    case 0x0c: active_voice_ = word(args, 0); break;
    case 0x0d: return active_voice_;
    case 0x0e: {
        if (active_voice_ >= voices_.size())
            return accepted();
        const auto duration = ticks(args, 2);
        const auto step = ticks(args, 6);
        const auto start_at = next_voice_tick_[active_voice_];
        // PlayNoteDel's first rational time is how long the note is held;
        // its second is the total interval until this voice's next note.
        // A zero hold is a rest, which still advances that voice's clock.
        if (duration > 0) {
            Event note;
            note.kind = Kind::note;
            note.voice = active_voice_;
            note.tick = start_at;
            note.value = std::int16_t(word(args, 0));
            const auto note_order = enqueue(std::move(note));
            Event off;
            off.kind = Kind::off;
            off.voice = active_voice_;
            off.tick = start_at + duration;
            off.note_order = note_order;
            enqueue(std::move(off));
        }
        if (step > duration) {
            Event rest;
            rest.kind = Kind::rest;
            rest.voice = active_voice_;
            rest.tick = start_at + step;
            enqueue(std::move(rest));
        }
        next_voice_tick_[active_voice_] += step;
        return accepted();
    }
    case 0x0f: {
        if (active_voice_ >= voices_.size())
            return accepted();
        const auto duration = ticks(args, 2);
        if (duration > 0) {
            Event note;
            note.kind = Kind::note;
            note.voice = active_voice_;
            note.tick = next_voice_tick_[active_voice_];
            note.value = std::int16_t(word(args, 0));
            const auto note_order = enqueue(std::move(note));
            Event off;
            off.kind = Kind::off;
            off.voice = active_voice_;
            off.tick = next_voice_tick_[active_voice_] + duration;
            off.note_order = note_order;
            enqueue(std::move(off));
            next_voice_tick_[active_voice_] += duration;
        }
        return accepted();
    }
    case 0x10: {
        Event item;
        item.kind = Kind::timbre;
        item.voice = active_voice_;
        item.tick = relative_origin_ + ticks(args, 4);
        std::copy_n(instrument.begin(), std::min(instrument.size(), item.patch.size()),
                    item.patch.begin());
        enqueue(std::move(item));
        return accepted();
    }
    case 0x11: {
        const auto denominator = word(args, 4);
        const auto ratio = denominator ? double(std::int16_t(word(args, 2))) /
                                         denominator : 0.0;
        event(Kind::pitch, 6, int(std::lround(std::clamp(ratio, -1.0, 1.0) * 64)),
              true);
        return accepted();
    }
    case 0x12: {
        unsigned ticks = word(args, 0);
        while (ticks && ticks <= 60)
            ticks *= 2;
        if (ticks)
            ticks_per_beat_ = ticks;
        break;
    }
    case 0x13: start(active_voice_, std::int16_t(word(args, 0))); break;
    case 0x14: stop(active_voice_); break;
    case 0x17:
        waveform_ = word(args, 0) != 0;
        write(1, waveform_ ? 0x20 : 0);
        break;
    default: break;
    }
    return ax;
}

void AdlibDriver::advance(std::uint64_t microseconds) {
    if (!playing_)
        return;
    // Advance in pieces so tempo changes in the event stream affect the
    // following notes, instead of retroactively scaling an entire frame.
    double remaining = double(microseconds);
    while (remaining > 0 && playing_) {
        const auto ticks_per_us = tempo_ * ticks_per_beat_ / 60000000.0;
        const auto next_tick = events_.empty() ? current_tick_ + remaining * ticks_per_us
                                                : events_.top().tick;
        const auto until_event = std::max(0.0, (next_tick - current_tick_) / ticks_per_us);
        const auto slice = std::min(remaining, until_event);
        current_tick_ += slice * ticks_per_us;
        remaining -= slice;
        if (events_.empty() || next_tick > current_tick_ + 1e-8)
            break;
        auto event = events_.top();
        events_.pop();
        process(event);
        if (events_.empty()) {
            // SOUND.COM drops SetState to zero once the scheduled score ends.
            playing_ = false;
        }
    }
}
} // namespace gateway

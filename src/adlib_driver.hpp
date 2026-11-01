#pragma once
#include "opl.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <span>
#include <vector>

namespace gateway {

// The Ad Lib SOUND.COM / SB-SOUND.COM INT 65h interface. The game supplies
// timed events and 56-byte INS timbres; the resident DOS driver normally
// schedules them on IRQ0 and writes OPL2 registers. This models that contract
// without executing a separate DOS TSR.
class AdlibDriver {
  public:
    explicit AdlibDriver(Opl &opl) : opl_(opl) {}
    // The caller resolves the far instrument pointer for SetTimbre (10h).
    // Other arguments are copied from ES:BX before the guest reuses its stack.
    std::uint16_t call(std::uint16_t function, std::span<const std::uint8_t> args,
                       std::uint16_t ax, std::span<const std::uint8_t> instrument = {});
    void advance(std::uint64_t microseconds);
    std::size_t pending_events() const { return events_.size(); }
    std::uint64_t played_notes() const { return played_notes_; }

  private:
    enum class Kind { note, off, rest, timbre, volume, tempo, pitch };
    struct Event {
        double tick = 0;
        std::uint64_t order = 0;
        std::uint64_t note_order = 0;
        Kind kind = Kind::note;
        unsigned voice = 0;
        int value = 0;
        std::array<std::uint8_t, 56> patch{};
    };
    struct Later {
        bool operator()(const Event &a, const Event &b) const {
            return a.tick != b.tick ? a.tick > b.tick : a.order > b.order;
        }
    };
    struct Voice {
        std::array<std::uint8_t, 56> patch{};
        double volume = 1;
        int bend = 0;
        int note = 0;
        bool active = false;
        std::uint8_t block_frequency = 0;
        std::uint64_t note_order = 0;
    };

    static std::uint16_t word(std::span<const std::uint8_t> data, unsigned offset);
    double ticks(std::span<const std::uint8_t> args, unsigned offset) const;
    std::uint64_t enqueue(Event event);
    void process(const Event &event);
    void reset();
    void stop(unsigned voice);
    void start(unsigned voice, int note);
    void tune(unsigned voice);
    void timbre(unsigned voice, const std::array<std::uint8_t, 56> &patch);
    void level(unsigned voice);
    void write(unsigned reg, unsigned value);

    Opl &opl_;
    std::array<Voice, 11> voices_{};
    std::priority_queue<Event, std::vector<Event>, Later> events_;
    std::uint64_t next_order_ = 0, played_notes_ = 0;
    double current_tick_ = 0;
    double relative_origin_ = 0;
    std::array<double, 11> next_voice_tick_{};
    double tempo_ = 90;
    unsigned ticks_per_beat_ = 32;
    unsigned active_voice_ = 0;
    bool playing_ = false, percussion_ = false, waveform_ = false;
    unsigned rhythm_keys_ = 0;
};

} // namespace gateway

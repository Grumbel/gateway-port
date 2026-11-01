#include "adlib_driver.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void word(std::array<std::uint8_t, 16> &data, unsigned byte, unsigned value) {
    data[byte] = value & 255;
    data[byte + 1] = (value >> 8) & 255;
}
void instrument_word(std::array<std::uint8_t, 56> &data, unsigned index, unsigned value) {
    data[index * 2] = value;
}
void init_instrument(std::array<std::uint8_t, 56> &patch) {
    for (unsigned base : {0u, 13u}) {
        instrument_word(patch, base + 1, 1); // frequency multiplier
        instrument_word(patch, base + 3, 15); // attack
        instrument_word(patch, base + 4, 4); // sustain
        instrument_word(patch, base + 5, 1); // sustain enabled
        instrument_word(patch, base + 6, 2); // decay
        instrument_word(patch, base + 7, 6); // release
    }
    instrument_word(patch, 8, 20); // modulator output level
    instrument_word(patch, 21, 0); // carrier output level
    instrument_word(patch, 2, 2); // feedback
    instrument_word(patch, 12, 1); // FM connection
}
} // namespace

int main() {
    try {
        gateway::Opl opl;
        gateway::AdlibDriver driver(opl);
        std::array<std::uint8_t, 16> args{};
        driver.call(0, args, 0);
        require(driver.call(4, args, 0xabcd) == 0, "initial driver idle state");
        word(args, 0, 8); // SetTickBeat rounds upward to 64 ticks/beat.
        driver.call(0x12, args, 0);
        word(args, 0, 1);
        driver.call(0x17, args, 0);
        driver.call(6, args, 0); // percussion mode
        require(driver.call(7, args, 0) == 1, "percussion mode not retained");
        word(args, 0, 0);
        driver.call(6, args, 0); // melodic mode
        driver.call(0x0c, args, 0);
        require(driver.call(0x0d, args, 0) == 0, "active voice not retained");

        // The queue starts at beat 8. SetTimbre and PlayNoteDel use absolute
        // fixed-point beat times and must wait for SetState=1 to run.
        args = {};
        word(args, 2, 8);
        driver.call(2, args, 0);
        std::array<std::uint8_t, 56> patch{};
        init_instrument(patch);
        args = {};
        word(args, 6, 8);
        require(driver.call(0x10, args, 0xab01, patch) == 0xabff,
                "SetTimbre must preserve AH and return AL=FFh");
        args = {};
        word(args, 4, 8);  // onset at beat 8
        word(args, 8, 9);  // release at beat 9
        require(driver.call(0x0e, args, 0x1234) == 0x12ff,
                "PlayNoteDel must acknowledge the queued event");
        require(driver.pending_events() == 3 && driver.played_notes() == 0,
                "music events were not queued");
        args = {};
        word(args, 0, 1);
        driver.call(3, args, 0);
        require(driver.call(4, args, 0) == 1, "playback must show busy state");
        driver.advance(1000);
        require(driver.played_notes() == 1 && driver.pending_events() == 1,
                "time-zero instrument and note events were not dispatched");
        auto samples = opl.render(4096);
        const auto [min, max] = std::minmax_element(samples.begin(), samples.end());
        require(*max - *min > 100, "queued INS timbre and note produced no FM sound");
        driver.advance(600000); // Beat 9 at 90 BPM is 666.7 ms after beat 8.
        require(driver.call(4, args, 0) == 1, "note released before its end tick");
        driver.advance(100000);
        require(driver.call(4, args, 0) == 0 && driver.pending_events() == 0,
                "note release did not complete the score");

        // Flush must cancel future events and report idle immediately.
        driver.call(0x0e, args, 0);
        driver.call(5, args, 0);
        require(driver.pending_events() == 0 && driver.call(4, args, 0) == 0,
                "Flush did not discard pending music");

        // A queued tempo change must affect later beat times; it must not
        // compress the already elapsed time. Pitch/volume events also report
        // the same acceptance status as timbre and note events.
        driver.call(0, args, 0);
        args = {};
        word(args, 0, 8);
        driver.call(0x12, args, 0);
        word(args, 0, 0);
        driver.call(2, args, 0);
        args = {};
        word(args, 0, 120);
        require(driver.call(9, args, 0x1234) == 0x12ff,
                "SetTempo was not acknowledged");
        args = {};
        word(args, 0, 1);
        word(args, 2, 2);
        require(driver.call(8, args, 0) == 0xff, "SetRelVolume was not acknowledged");
        args = {};
        word(args, 2, 1);
        word(args, 4, 2);
        require(driver.call(0x11, args, 0) == 0xff, "SetPitch was not acknowledged");
        args = {};
        word(args, 4, 1); // one beat after time zero
        word(args, 8, 2);
        driver.call(0x0e, args, 0);
        word(args, 0, 1);
        driver.call(3, args, 0);
        driver.advance(400000);
        require(driver.played_notes() == 0, "note preceded its adjusted beat");
        driver.advance(150000);
        require(driver.played_notes() == 1, "tempo change did not advance the note");
        driver.advance(500000);
        require(driver.call(4, args, 0) == 0, "tempo-adjusted score did not finish");

        std::cout << "AdLib SOUND.COM event and OPL synthesis checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#include "opl.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void write(gateway::Opl &opl, unsigned reg, unsigned value) {
    opl.out(0x388, reg);
    opl.out(0x389, value);
}
int main() {
    try {
        gateway::Opl opl;
        require((opl.in(0x388) & 0xe0) == 0, "initial timer flags");
        write(opl, 2, 255);
        write(opl, 4, 1);
        opl.advance(80);
        require((opl.in(0x388) & 0xe0) == 0xc0, "timer 1 overflow not latched");
        write(opl, 4, 0x80);
        require((opl.in(0x388) & 0xe0) == 0, "timer reset failed");
        write(opl, 4, 0x40);
        opl.advance(1000);
        require((opl.in(0x388) & 0xe0) == 0, "masked timer interrupted");
        write(opl, 3, 255);
        write(opl, 4, 2);
        opl.advance(320);
        require((opl.in(0x388) & 0xe0) == 0xa0, "timer 2 overflow failed");
        // Program a simple two-operator voice through the emulated AdLib ports.
        write(opl, 0x20, 1);
        write(opl, 0x23, 1);
        write(opl, 0x40, 0x10);
        write(opl, 0x43, 0);
        write(opl, 0x60, 0xf0);
        write(opl, 0x63, 0xf0);
        write(opl, 0x80, 0x77);
        write(opl, 0x83, 0x77);
        write(opl, 0xa0, 0x98);
        write(opl, 0xb0, 0x31);
        const auto samples = opl.render(4800);
        const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
        require(samples.size() == 9600 && *high - *low > 100, "voice produced no waveform");
        std::cout << "AdLib timer and synthesis checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#include "pit.hpp"
#include <iostream>
#include <stdexcept>
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        gateway::Pit pit;
        pit.advance(54000);
        require(!pit.pending(), "default tick arrived early");
        pit.advance(1000);
        require(pit.pending(), "default tick missing");
        pit.acknowledge();
        require(!pit.pending(), "IRQ acknowledge failed");
        pit.out(0x43, 0x36);
        pit.out(0x40, 0xa9);
        pit.out(0x40, 4); // 1193 ticks, ~1ms.
        pit.advance(900);
        require(!pit.pending(), "programmed tick arrived early");
        pit.out(0x43, 0); // Latch must survive elapsed time between low/high reads.
        const unsigned low = pit.in(0x40);
        pit.advance(50);
        const unsigned count = low | (unsigned(pit.in(0x40)) << 8);
        require(count >= 118 && count <= 120, "latched count changed");
        pit.advance(100);
        require(pit.pending(), "programmed tick missing");
        pit.acknowledge();
        pit.out(0x43, 0x36);
        pit.out(0x40, 0);
        pit.out(0x40, 0);
        pit.advance(54000);
        require(!pit.pending(), "zero reload not 65536");
        std::cout << "PIT checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

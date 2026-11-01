#include "bios_text.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::array<std::uint8_t, 4096> page{};
        for (unsigned row = 0; row < 25; ++row)
            for (unsigned column = 0; column < 80; ++column) {
                page[(row * 80 + column) * 2] = 'A' + row;
                page[(row * 80 + column) * 2 + 1] = 7;
            }
        gateway::scroll_text_window(page, false, 1, 0x1f, 1, 2, 3, 4);
        auto character = [&](unsigned row, unsigned column) {
            return page[(row * 80 + column) * 2];
        };
        require(character(1, 2) == 'C' && character(2, 4) == 'D', "scroll up lost cells");
        require(character(3, 2) == ' ' && page[(3 * 80 + 2) * 2 + 1] == 0x1f,
                "scroll up did not blank with attribute");
        require(character(1, 1) == 'B' && character(4, 2) == 'E', "scroll touched outside window");
        gateway::scroll_text_window(page, true, 1, 0x2e, 1, 2, 3, 4);
        require(character(1, 2) == ' ' && character(2, 2) == 'C', "scroll down lost cells");
        require(page[(1 * 80 + 2) * 2 + 1] == 0x2e, "scroll down fill attribute");
        gateway::scroll_text_window(page, false, 0, 0x70, 0, 0, 24, 79);
        require(character(0, 0) == ' ' && character(24, 79) == ' ', "clear full screen");
        require(page[1] == 0x70 && page[3999] == 0x70, "clear attribute");
        std::cout << "BIOS text checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

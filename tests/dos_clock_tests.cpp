// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "dos_clock.hpp"
#include <iostream>
#include <stdexcept>
void require(bool c, const char *message) {
    if (!c)
        throw std::runtime_error(message);
}
int main() {
    try {
        gateway::DosClock clock;
        require(clock.set_date(2024, 2, 29), "leap day rejected");
        require(clock.set_time(23, 59, 59, 99), "time rejected");
        clock.advance(20000);
        auto date = clock.local();
        require(date.tm_mon == 2 && date.tm_mday == 1 && date.tm_hour == 0 && date.tm_sec == 0 &&
                    clock.hundredths() == 1,
                "midnight rollover failed");
        require(!clock.set_date(2025, 2, 29) && !clock.set_date(1979, 12, 31) &&
                    !clock.set_date(2024, 0, 1),
                "invalid date accepted");
        require(!clock.set_time(24, 0, 0, 0) && !clock.set_time(1, 60, 0, 0) &&
                    !clock.set_time(0, 0, 60, 0) && !clock.set_time(0, 0, 0, 100),
                "invalid time accepted");
        require(clock.local().tm_mday == 1 && clock.hundredths() == 1,
                "failed setter changed clock");
        std::cout << "Virtual clock checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

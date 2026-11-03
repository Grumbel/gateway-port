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
        // Golden replays must not depend on the host date, time of day,
        // timezone or daylight-saving transition rules.
        auto deterministic = gateway::DosClock::deterministic();
        auto fixed = deterministic.local();
        require(fixed.tm_year + 1900 == 1992 && fixed.tm_mon == 0 && fixed.tm_mday == 1 &&
                    fixed.tm_hour == 12 && fixed.tm_min == 0 && fixed.tm_sec == 0 &&
                    deterministic.hundredths() == 0,
                "deterministic DOS epoch is not fixed UTC noon");
        deterministic.advance(1230000);
        require(deterministic.local().tm_sec == 1 && deterministic.hundredths() == 23,
                "deterministic DOS clock did not advance in guest time");
        require(deterministic.set_date(2024, 2, 29) &&
                    deterministic.local().tm_mday == 29 &&
                    deterministic.local().tm_hour == 12 &&
                    deterministic.hundredths() == 23,
                "deterministic DOS date setter lost the UTC time of day");
        require(deterministic.set_time(23, 59, 59, 99),
                "deterministic DOS time setter rejected a valid time");
        deterministic.advance(20000);
        fixed = deterministic.local();
        require(fixed.tm_year + 1900 == 2024 && fixed.tm_mon == 2 &&
                    fixed.tm_mday == 1 && fixed.tm_hour == 0 && fixed.tm_sec == 0 &&
                    deterministic.hundredths() == 1,
                "deterministic DOS leap-day rollover failed");
        std::cout << "Virtual clock checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

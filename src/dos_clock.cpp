// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "dos_clock.hpp"
#include <stdexcept>
namespace gateway {
DosClock DosClock::deterministic() {
    using namespace std::chrono;
    // Midday avoids midnight/date-boundary cases. UTC means the replay is
    // independent of the host's timezone, DST rules and current wall clock.
    return DosClock(sys_days{year{1992} / month{1} / day{1}} + hours{12}, true);
}
std::tm DosClock::local() const {
    const auto seconds = std::chrono::system_clock::to_time_t(now_);
    const auto *result = utc_ ? std::gmtime(&seconds) : std::localtime(&seconds);
    if (!result)
        throw std::runtime_error("cannot convert virtual DOS calendar");
    return *result;
}
unsigned DosClock::hundredths() const {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now_.time_since_epoch()).count();
    return unsigned((ms % 1000 + 1000) % 1000) / 10;
}
bool DosClock::set_date(unsigned year, unsigned month, unsigned day) {
    if (year < 1980 || year > 2099 || month > 12 || day > 31 ||
        !std::chrono::year_month_day{std::chrono::year(int(year)), std::chrono::month(month),
                                     std::chrono::day(day)}
             .ok())
        return false;
    auto calendar = local();
    if (utc_) {
        using namespace std::chrono;
        const auto fractional = now_ - floor<seconds>(now_);
        now_ = sys_days{std::chrono::year(int(year)) / std::chrono::month{month} /
                        std::chrono::day{day}} +
               hours{calendar.tm_hour} + minutes{calendar.tm_min} +
               seconds{calendar.tm_sec} + fractional;
        return true;
    }
    calendar.tm_year = year - 1900;
    calendar.tm_mon = month - 1;
    calendar.tm_mday = day;
    calendar.tm_isdst = -1;
    const auto fraction = now_ - std::chrono::floor<std::chrono::seconds>(now_);
    const auto seconds = std::mktime(&calendar);
    if (seconds == -1)
        return false;
    now_ = std::chrono::system_clock::from_time_t(seconds) + fraction;
    return true;
}
bool DosClock::set_time(unsigned hour, unsigned minute, unsigned second, unsigned hundredths) {
    if (hour > 23 || minute > 59 || second > 59 || hundredths > 99)
        return false;
    if (utc_) {
        using namespace std::chrono;
        now_ = floor<days>(now_) + hours{hour} + minutes{minute} +
               seconds{second} + milliseconds{hundredths * 10};
        return true;
    }
    auto calendar = local();
    calendar.tm_hour = hour;
    calendar.tm_min = minute;
    calendar.tm_sec = second;
    calendar.tm_isdst = -1;
    const auto seconds = std::mktime(&calendar);
    if (seconds == -1)
        return false;
    now_ = std::chrono::system_clock::from_time_t(seconds) +
           std::chrono::milliseconds(hundredths * 10);
    return true;
}
} // namespace gateway

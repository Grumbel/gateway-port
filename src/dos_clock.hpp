// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <chrono>
#include <ctime>
namespace gateway {
class DosClock {
  public:
    explicit DosClock(
        std::chrono::system_clock::time_point initial = std::chrono::system_clock::now())
        : now_(initial) {}
    void advance(std::uint64_t microseconds) { now_ += std::chrono::microseconds(microseconds); }
    std::tm local() const;
    unsigned hundredths() const;
    bool set_date(unsigned year, unsigned month, unsigned day);
    bool set_time(unsigned hour, unsigned minute, unsigned second, unsigned hundredths);

  private:
    std::chrono::system_clock::time_point now_;
};
} // namespace gateway

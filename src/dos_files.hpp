// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>
namespace gateway {
struct DosIoError : std::runtime_error {
    unsigned code;
    DosIoError(unsigned value, const char *message) : std::runtime_error(message), code(value) {}
};
class DosFile {
  public:
    DosFile(int fd, unsigned access);
    ~DosFile();
    DosFile(DosFile &&other) noexcept;
    DosFile &operator=(DosFile &&other) noexcept;
    DosFile(const DosFile &) = delete;
    DosFile &operator=(const DosFile &) = delete;
    std::size_t read(std::span<std::uint8_t> buffer);
    std::size_t write(std::span<const std::uint8_t> buffer);
    std::uint32_t seek(std::int32_t offset, unsigned origin);

  private:
    int fd_;
    unsigned access_;
};
struct DosDirectoryEntry {
    std::string name;
    std::uint32_t size;
    std::uint16_t date, time;
    std::uint8_t attributes;
};
// Flat, case-insensitive DOS drive: immutable assets plus an optional writable layer.
class DosDrive {
  public:
    DosDrive(std::filesystem::path assets, std::filesystem::path state = {});
    DosFile open(std::string_view name, unsigned access) const;
    DosFile create(std::string_view name, unsigned attributes) const;
    unsigned attributes(std::string_view name) const;
    std::vector<DosDirectoryEntry> list(std::string_view pattern, unsigned attributes) const;
    std::vector<std::uint8_t> read_all(std::string_view name) const;
    const std::filesystem::path &state_directory() const { return state_; }

  private:
    std::filesystem::path assets_, state_;
    static std::string key(std::string_view name);
    std::filesystem::path find(const std::filesystem::path &root, const std::string &name) const;
    void prepare_state() const;
};
} // namespace gateway

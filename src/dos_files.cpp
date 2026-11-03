// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "dos_files.hpp"
#include "dos_fcb.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace gateway {
namespace {
[[noreturn]] void io_error() {
    const auto code = errno == ENOENT                      ? 2
                      : errno == ENOTDIR                   ? 3
                      : errno == EMFILE || errno == ENFILE ? 4
                      : errno == EBADF                     ? 6
                                                           : 5;
    throw DosIoError(code, "DOS file I/O failed");
}
int checked_open(const std::filesystem::path &path, int flags, bool writable) {
    const int fd = ::open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        io_error();
    struct stat info{};
    if (::fstat(fd, &info) || !S_ISREG(info.st_mode) ||
        (writable && (info.st_nlink != 1 || !(info.st_mode & 0222)))) {
        ::close(fd);
        throw DosIoError(
            5, "DOS file must be a regular file; writable state cannot be linked or read-only");
    }
    return fd;
}
bool below(const std::filesystem::path &child, const std::filesystem::path &parent) {
    auto a = child.begin(), b = parent.begin();
    for (; b != parent.end(); ++a, ++b)
        if (a == child.end() || *a != *b)
            return false;
    return true;
}
} // namespace
DosFile::DosFile(int fd, unsigned access) : fd_(fd), access_(access) {}
DosFile::~DosFile() {
    if (fd_ >= 0)
        ::close(fd_);
}
DosFile::DosFile(DosFile &&other) noexcept
    : fd_(std::exchange(other.fd_, -1)), access_(other.access_) {}
DosFile &DosFile::operator=(DosFile &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = std::exchange(other.fd_, -1);
        access_ = other.access_;
    }
    return *this;
}
std::size_t DosFile::read(std::span<std::uint8_t> buffer) {
    if (access_ == 1)
        throw DosIoError(5, "write-only DOS handle");
    ssize_t count;
    do {
        count = ::read(fd_, buffer.data(), buffer.size());
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        io_error();
    return count;
}
std::size_t DosFile::write(std::span<const std::uint8_t> buffer) {
    if (!access_)
        throw DosIoError(5, "read-only DOS handle");
    if (buffer.empty()) {
        const auto position = ::lseek(fd_, 0, SEEK_CUR);
        if (position < 0 || ::ftruncate(fd_, position))
            io_error();
        return 0;
    }
    ssize_t count;
    do {
        count = ::write(fd_, buffer.data(), buffer.size());
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        io_error();
    return count;
}
std::uint32_t DosFile::seek(std::int32_t offset, unsigned origin) {
    if (origin > 2)
        throw DosIoError(1, "invalid DOS seek origin");
    const int modes[]{SEEK_SET, SEEK_CUR, SEEK_END};
    const auto position = ::lseek(fd_, offset, modes[origin]);
    if (position < 0 || std::uint64_t(position) > std::numeric_limits<std::uint32_t>::max())
        throw DosIoError(25, "DOS seek failed");
    return position;
}
DosDrive::DosDrive(std::filesystem::path assets, std::filesystem::path state)
    : assets_(std::filesystem::canonical(assets)) {
    if (!std::filesystem::is_directory(assets_))
        throw std::runtime_error("invalid game-data directory");
    if (!state.empty()) {
        state_ = std::filesystem::weakly_canonical(std::filesystem::absolute(state));
        if (below(state_, assets_))
            throw std::runtime_error("state directory must be outside original game data");
    }
}
std::string DosDrive::key(std::string_view name) {
    if (name.size() >= 2 && (name[0] == 'C' || name[0] == 'c') && name[1] == ':')
        name.remove_prefix(2);
    if (!name.empty() && name.front() == '\\')
        name.remove_prefix(1);
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != name.npos)
        throw DosIoError(5, "DOS drive requires a flat game-directory filename");
    std::string result(name);
    for (auto &c : result) {
        if (static_cast<unsigned char>(c) < 32)
            throw DosIoError(5, "invalid DOS filename");
        if (c >= 'a' && c <= 'z')
            c -= 32;
    }
    return result;
}
std::filesystem::path DosDrive::find(const std::filesystem::path &root,
                                     const std::string &name) const {
    if (root.empty())
        return {};
    std::error_code error;
    const bool exists = std::filesystem::exists(root, error);
    if (error)
        throw DosIoError(5, "cannot inspect DOS directory");
    if (!exists)
        return {};
    std::filesystem::directory_iterator entries(root, error);
    if (error)
        throw DosIoError(5, "cannot read DOS directory");
    for (const auto &entry : entries) {
        auto candidate = entry.path().filename().string();
        for (auto &c : candidate)
            if (c >= 'a' && c <= 'z')
                c -= 32;
        if (candidate == name)
            return entry.path();
    }
    return {};
}
void DosDrive::prepare_state() const {
    if (state_.empty())
        throw DosIoError(5, "DOS writes need a state directory");
    std::error_code error;
    std::filesystem::create_directories(state_, error);
    if (error)
        throw DosIoError(5, "cannot create DOS state directory");
}
DosFile DosDrive::open(std::string_view input, unsigned access) const {
    if (access > 2)
        throw DosIoError(12, "invalid DOS access mode");
    const auto name = key(input);
    if (name.find_first_of("*?") != name.npos)
        throw DosIoError(5, "wildcards are not file names");
    auto path = find(state_, name);
    const int modes[]{O_RDONLY, O_WRONLY, O_RDWR};
    if (!path.empty())
        return DosFile(checked_open(path, modes[access], access != 0), access);
    path = find(assets_, name);
    if (path.empty())
        throw DosIoError(2, "DOS file not found");
    if (!access)
        return DosFile(checked_open(path, O_RDONLY, false), 0);
    prepare_state();
    const auto destination = state_ / name;
    // Copy on first write; never open the original asset with write permissions.
    DosFile source(checked_open(path, O_RDONLY, false), 0);
    const int fd =
        ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        io_error();
    try {
        DosFile copy(fd, 1);
        std::array<std::uint8_t, 8192> buffer{};
        while (const auto count = source.read(buffer)) {
            std::size_t done = 0;
            while (done < count) {
                const auto written = copy.write(std::span(buffer).subspan(done, count - done));
                if (!written)
                    throw DosIoError(5, "short state-file copy");
                done += written;
            }
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        throw;
    }
    return DosFile(checked_open(destination, modes[access], true), access);
}
DosFile DosDrive::create(std::string_view input, unsigned attributes) const {
    const auto name = key(input);
    if (name.find_first_of("*?") != name.npos)
        throw DosIoError(5, "wildcards are not file names");
    if (attributes & ~0x21u)
        throw DosIoError(5, "unsupported DOS create attributes");
    prepare_state();
    auto path = find(state_, name);
    if (path.empty())
        path = state_ / name;
    const int fd = checked_open(path, O_RDWR | O_CREAT, true);
    DosFile result(fd, 2);
    if (::ftruncate(fd, 0))
        io_error();
    if ((attributes & 1) && ::fchmod(fd, 0400))
        io_error();
    return result;
}
unsigned DosDrive::attributes(std::string_view input) const {
    const auto name = key(input);
    auto path = find(state_, name);
    const bool state = !path.empty();
    if (!state)
        path = find(assets_, name);
    if (path.empty())
        throw DosIoError(2, "DOS file not found");
    struct stat info{};
    if (::lstat(path.c_str(), &info))
        io_error();
    if (!S_ISREG(info.st_mode))
        throw DosIoError(5, "not a regular DOS file");
    return state && info.st_nlink == 1 && (info.st_mode & 0222) ? 0x20 : 0x21;
}
std::vector<DosDirectoryEntry> DosDrive::list(std::string_view input,
                                              unsigned search_attributes) const {
    auto pattern = key(input);
    if (pattern == "*")
        pattern = "*.*";
    if (search_attributes & 8)
        return {}; // No volume-label entry in the virtual drive.
    const auto wildcard = parse_fcb(pattern, 0, {});
    std::map<std::string, std::filesystem::path> merged;
    for (const auto &root : {assets_, state_}) {
        if (root.empty())
            continue;
        std::error_code error;
        if (!std::filesystem::exists(root, error)) {
            if (error)
                throw DosIoError(5, "cannot inspect DOS search directory");
            continue;
        }
        for (const auto &entry : std::filesystem::directory_iterator(root)) {
            auto name = entry.path().filename().string();
            if (name.empty() || name[0] == '.')
                continue;
            for (auto &c : name)
                if (c >= 'a' && c <= 'z')
                    c -= 32;
            merged[name] = entry.path();
        }
    }
    std::vector<DosDirectoryEntry> result;
    for (const auto &[name, path] : merged) {
        const auto dot = name.find('.');
        const auto base = name.substr(0, dot);
        const auto extension = dot == name.npos ? std::string{} : name.substr(dot + 1);
        if (base.empty() || base.size() > 8 || extension.size() > 3 ||
            extension.find('.') != extension.npos)
            continue;
        const auto candidate = parse_fcb(name, 0, {});
        if (candidate.consumed != name.size())
            continue;
        bool match = true;
        for (unsigned i = 1; i < 12; ++i)
            if (wildcard.bytes[i] != '?' && wildcard.bytes[i] != candidate.bytes[i])
                match = false;
        if (!match)
            continue;
        struct stat info{};
        if (::lstat(path.c_str(), &info))
            io_error();
        if (!S_ISREG(info.st_mode))
            continue;
        const auto *local = std::localtime(&info.st_mtime);
        std::uint16_t date = 0x21, time = 0; // 1980-01-01 fallback.
        if (local) {
            const auto year = std::clamp(local->tm_year + 1900, 1980, 2107);
            date = ((year - 1980) << 9) | ((local->tm_mon + 1) << 5) | local->tm_mday;
            time = (local->tm_hour << 11) | (local->tm_min << 5) | (local->tm_sec / 2);
        }
        const bool in_state = path.parent_path() == state_;
        const std::uint8_t attr =
            in_state && info.st_nlink == 1 && (info.st_mode & 0222) ? 0x20 : 0x21;
        result.push_back(
            {name, static_cast<std::uint32_t>(std::min<std::uint64_t>(info.st_size, 0xffffffff)),
             date, time, attr});
    }
    return result;
}

std::vector<std::uint8_t> DosDrive::read_all(std::string_view name) const {
    auto file = open(name, 0);
    const auto size = file.seek(0, 2);
    if (size > 64 * 1024 * 1024)
        throw DosIoError(8, "DOS image too large");
    file.seek(0, 0);
    std::vector<std::uint8_t> bytes(size);
    std::size_t at = 0;
    while (at < bytes.size()) {
        const auto count = file.read(std::span(bytes).subspan(at));
        if (!count)
            throw DosIoError(5, "DOS image read was truncated");
        at += count;
    }
    return bytes;
}
} // namespace gateway

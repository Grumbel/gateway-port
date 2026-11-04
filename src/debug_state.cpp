// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "debug_state.hpp"
#include "object_flags.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gateway {
namespace {
std::uint8_t byte(std::span<const std::uint8_t> m, std::uint16_t segment,
                  std::uint32_t offset) {
    const auto address = std::size_t(segment) * 16 + offset;
    if (address >= m.size() || address >= 0xa0000)
        throw std::runtime_error("guest object pointer outside conventional memory");
    return m[address];
}
std::uint16_t word(std::span<const std::uint8_t> m, std::uint16_t seg, std::uint16_t off) {
    return byte(m, seg, off) | std::uint16_t(byte(m, seg, std::uint32_t(off) + 1)) << 8;
}
std::string token_name(std::span<const std::uint8_t> file, std::uint16_t token) {
    if (!token || file.size() < 2)
        return {};
    // The game's state name tokens are one-based offsets into OBJECT.DAT's
    // payload, not indexes into the separately enumerated RGN name list.
    const auto at = std::size_t(token) + 1;
    if (at >= file.size())
        return {};
    std::string name;
    for (std::size_t i = at; i < file.size() && file[i]; ++i) {
        const auto c = file[i];
        name += c >= 32 && c < 127 ? char(c) : '?';
    }
    return name;
}
} // namespace

std::optional<DebugGameState> inspect_gateway_state(std::span<const std::uint8_t> m,
                                                     std::uint16_t ds) {
    try {
        const auto count = word(m, word(m, ds, 0x8d54), 0x38dc);
        if (!count || count > 4096)
            return {};
        const auto directory = word(m, ds, 0x8d50);
        const auto descriptors = word(m, ds, 0x8d52);
        constexpr std::array<std::uint16_t, 8> handler_at{
            0x10, 0x22, 0x0a, 0x12, 0x14, 0x16, 0x1e, 0x16};
        constexpr std::array<std::uint16_t, 8> location_at{
            0, 0x0e, 0, 0, 0, 0x0a, 0x0c, 0x0c};
        const GuestByteReader read = [&](std::uint16_t seg, std::uint32_t off) {
            return byte(m, seg, off);
        };
        DebugGameState state;
        state.ds = ds;
        state.objects.reserve(count);
        for (unsigned id = 1; id <= count; ++id) {
            const auto dir_off = std::uint16_t(0xba + 6 * id);
            const auto type = byte(m, directory, dir_off);
            if (type < 1 || type > 8)
                return {};
            const auto offset = word(m, directory, std::uint16_t(dir_off + 2));
            const auto segment = word(m, directory, std::uint16_t(dir_off + 4));
            const auto length = byte(m, descriptors, type * 3 - 1);
            if (length < handler_at[type - 1] + 2 || length > 128)
                return {};
            DebugObjectState object{std::uint16_t(id), type, segment, offset, 0, 0, 0, {}, {}, {}};
            object.raw.reserve(length);
            for (unsigned i = 0; i < length; ++i)
                object.raw.push_back(byte(m, segment, std::uint16_t(offset + i)));
            object.name_token = word(m, segment, offset);
            object.secondary_name_token = word(m, segment, std::uint16_t(offset + 2));
            object.handler = word(m, segment, std::uint16_t(offset + handler_at[type - 1]));
            if (object.handler > 696) // Observed Gateway 1 handler table.
                return {};
            if (location_at[type - 1])
                object.location = word(m, segment,
                                       std::uint16_t(offset + location_at[type - 1]));
            try {
                // The write-address query only checks whether this property
                // is supported; it does not modify any guest memory.
                if (object_flag_write(read, ds, std::uint16_t(id), 7, false))
                    object.disclosed = object_flag(read, ds, std::uint16_t(id), 7);
            } catch (const std::runtime_error &) {
                // A nonstandard/missing flag table must not invalidate the
                // otherwise independently validated object-directory scan.
            }
            state.objects.push_back(std::move(object));
        }
        // These IDs were established by the first-room replay, not generalized
        // to other games. Optional because the intro has no Gateway object table.
        if (count >= 0x124) {
            if (const auto &book = state.objects[0x123 - 1]; book.location)
                state.book_location = book.location;
            try {
                state.drawer_open = object_flag(read, ds, 0x124, 0x0c);
            } catch (const std::runtime_error &) {
            }
        }
        return state;
    } catch (const std::runtime_error &) {
        return {};
    }
}

std::string debug_objects_tsv(const DebugGameState &state,
                              std::span<const std::uint8_t> object_dat) {
    std::ostringstream out;
    out << "engine_id\ttype\tsegment\toffset\thandler\tlocation\tname_token\tname"
           "\tsecondary_name\traw_hex\n";
    for (const auto &object : state.objects) {
        out << std::dec << object.id << '\t' << object.type << '\t' << std::hex << std::setfill('0')
            << std::setw(4) << object.segment << '\t' << std::setw(4) << object.offset << '\t'
            << std::dec << object.handler << '\t';
        if (object.location)
            out << std::hex << *object.location;
        out << '\t' << std::dec << object.name_token << '\t'
            << token_name(object_dat, object.name_token) << '\t'
            << token_name(object_dat, object.secondary_name_token) << '\t';
        for (auto value : object.raw)
            out << std::hex << std::setw(2) << unsigned(value);
        out << '\n';
    }
    return out.str();
}

std::vector<std::string> debug_engine_object_names(
    const DebugGameState &state, std::span<const std::uint8_t> object_dat) {
    std::vector<std::string> names(state.objects.size() + 1);
    for (const auto &object : state.objects) {
        if (!object.id || object.id >= names.size())
            continue;
        names[object.id] = token_name(object_dat, object.name_token);
        if (names[object.id].empty())
            names[object.id] = token_name(object_dat, object.secondary_name_token);
    }
    return names;
}

std::optional<DebugNameTable> inspect_legend_name_table(
    std::span<const std::uint8_t> memory, std::span<const std::uint8_t> object_dat,
    unsigned required_id, std::optional<std::size_t> directory_hint) {
    constexpr std::size_t limit = 0xa0000;
    constexpr unsigned minimum_objects = 256;
    if (memory.size() < limit || object_dat.size() < 4 ||
        (object_dat[0] | unsigned(object_dat[1]) << 8) != object_dat.size() - 2 ||
        required_id > 2048)
        return {};

    // Store only NUL-delimited starts. A random word in guest RAM must not be
    // accepted merely because it happens to lie somewhere inside OBJECT.DAT.
    std::vector<std::string> token_names(65536);
    for (std::size_t at = 2; at < object_dat.size();) {
        const auto start = at;
        while (at < object_dat.size() && object_dat[at])
            ++at;
        if (at == object_dat.size() || start - 1 >= token_names.size())
            return {};
        token_names[start - 1] = token_name(object_dat, std::uint16_t(start - 1));
        ++at;
    }
    if (token_names[1].empty())
        return {};

    const auto read_word = [&](std::size_t at) -> unsigned {
        return memory[at] | unsigned(memory[at + 1]) << 8;
    };
    const auto valid_token = [&](unsigned token) {
        return token == 0 || (token < token_names.size() && !token_names[token].empty());
    };
    const auto inspect = [&](std::size_t directory) -> std::optional<DebugNameTable> {
        DebugNameTable result;
        result.directory_address = directory;
        result.names.push_back({});
        unsigned with_names = 0;
        for (unsigned id = 1; id <= 2048; ++id) {
            const auto at = directory + (id - 1) * 6;
            if (at + 6 > limit || memory[at] < 1 || memory[at] > 8)
                break;
            const auto record = std::size_t(read_word(at + 4)) * 16 + read_word(at + 2);
            if (record + 4 > limit)
                break;
            const auto primary = read_word(record);
            const auto secondary = read_word(record + 2);
            if (!valid_token(primary) || !valid_token(secondary))
                break;
            if (id == 1 && primary != 1)
                break; // Anchor the beginning of the object ID sequence.
            const auto token = primary ? primary : secondary;
            result.names.push_back(token ? token_names[token] : std::string{});
            with_names += token != 0;
        }
        const auto count = result.names.size() - 1;
        if (count < minimum_objects || count < required_id || with_names < count * 3 / 4)
            return {};
        return result;
    };

    if (directory_hint && *directory_hint < limit) {
        if (auto found = inspect(*directory_hint))
            return found;
    }
    // The directory can live at *any byte address* in conventional memory.
    // Gateway II's verified table begins at linear 0x38b1d (odd!), so scanning
    // only word-aligned addresses silently missed all 521 of its objects.
    // A successful candidate still validates hundreds of far pointers and
    // NUL-delimited name tokens, and is cached as a hint for later frames.
    for (std::size_t at = 0x10000; at < limit - minimum_objects * 6; ++at) {
        if (memory[at] < 1 || memory[at] > 8)
            continue;
        const auto record = std::size_t(read_word(at + 4)) * 16 + read_word(at + 2);
        if (record + 4 > limit || read_word(record) != 1)
            continue;
        if (auto found = inspect(at))
            return found;
    }
    return {};
}
} // namespace gateway

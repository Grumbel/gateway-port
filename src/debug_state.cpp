#include "debug_state.hpp"
#include "object_flags.hpp"
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
            DebugObjectState object{std::uint16_t(id), type, segment, offset, 0, 0, 0, {}, {}};
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
            state.objects.push_back(std::move(object));
        }
        // These IDs were established by the first-room replay, not generalized
        // to other games. Optional because the intro has no Gateway object table.
        if (count >= 0x124) {
            if (const auto &book = state.objects[0x123 - 1]; book.location)
                state.book_location = book.location;
            const GuestByteReader read = [&](std::uint16_t seg, std::uint32_t off) {
                return byte(m, seg, off);
            };
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
} // namespace gateway

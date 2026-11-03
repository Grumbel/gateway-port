// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "object_flags.hpp"

namespace gateway {
namespace {
std::uint16_t word(const GuestByteReader &read, std::uint16_t segment, std::uint16_t offset) {
    return std::uint16_t(read(segment, offset) |
                         (std::uint16_t(read(segment, std::uint32_t(offset) + 1)) << 8));
}
struct Object {
    std::uint8_t type;
    std::uint16_t offset, segment;
};
std::optional<Object> object_record(const GuestByteReader &read, std::uint16_t ds,
                                    std::uint16_t object) {
    if (!object || object >= 0x8000 || object > word(read, word(read, ds, 0x8d54), 0x38dc))
        return {};
    const auto directory = word(read, ds, 0x8d50);
    const auto record = std::uint16_t(object * 6 + 0xba);
    const auto type = read(directory, record);
    if (type < 1 || type > 8)
        return {};
    return Object{type, word(read, directory, std::uint16_t(record + 2)),
                  word(read, directory, std::uint16_t(record + 4))};
}
struct Flag {
    std::uint16_t segment, offset;
    std::uint8_t mask;
};
std::optional<Flag> flag_address(const GuestByteReader &read, std::uint16_t ds,
                                 std::uint16_t object, std::uint16_t property) {
    if (!property)
        return {};
    const auto record = object_record(read, ds, object);
    if (!record)
        return {};
    const auto properties = word(read, ds, 0x8d56);
    const auto index = std::uint16_t(property * 4);
    if (!(word(read, properties, std::uint16_t(index + 0x16)) & (1u << (record->type - 1))))
        return {};
    const auto bit = read(properties, std::uint16_t(index + 0x14));
    return Flag{record->segment, std::uint16_t(record->offset + 4 + bit / 8),
                std::uint8_t(1u << (bit & 7))};
}
std::optional<Object> location_address(const GuestByteReader &read, std::uint16_t ds,
                                       std::uint16_t object) {
    auto record = object_record(read, ds, object);
    if (!record)
        return {};
    unsigned field;
    switch (record->type) {
    case 2:
        field = 0x0e;
        break;
    case 6:
        field = 0x0a;
        break;
    case 7:
    case 8:
        field = 0x0c;
        break;
    default:
        return {};
    }
    record->offset = std::uint16_t(record->offset + field);
    return record;
}
} // namespace
bool object_flag(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object,
                 std::uint16_t property) {
    const auto flag = flag_address(read, ds, object, property);
    return flag && (read(flag->segment, flag->offset) & flag->mask);
}
std::optional<GuestWrite> object_flag_write(const GuestByteReader &read, std::uint16_t ds,
                                            std::uint16_t object, std::uint16_t property,
                                            bool value) {
    const auto flag = flag_address(read, ds, object, property);
    if (!flag)
        return {};
    const auto byte = read(flag->segment, flag->offset);
    return GuestWrite{flag->segment,
                      flag->offset,
                      {std::uint8_t(value ? byte | flag->mask : byte & ~flag->mask), 0},
                      1};
}
std::uint16_t object_location(const GuestByteReader &read, std::uint16_t ds, std::uint16_t object) {
    const auto field = location_address(read, ds, object);
    return field ? word(read, field->segment, field->offset) : 0;
}
std::optional<GuestWrite> object_location_write(const GuestByteReader &read, std::uint16_t ds,
                                                std::uint16_t object, std::uint16_t location) {
    const auto field = location_address(read, ds, object);
    if (!field)
        return {};
    return GuestWrite{
        field->segment, field->offset, {std::uint8_t(location), std::uint8_t(location >> 8)}, 2};
}
} // namespace gateway

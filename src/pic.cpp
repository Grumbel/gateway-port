#include "pic.hpp"
#include <array>
#include <stdexcept>

namespace {
using Bytes = std::span<const std::uint8_t>;
using gateway::PicEntry;
using gateway::Rgb8;

std::uint16_t u16(Bytes b, std::size_t at) {
    if (at > b.size() || b.size() - at < 2)
        throw std::runtime_error("PIC field beyond end of file");
    return static_cast<std::uint16_t>(b[at] | b[at + 1] << 8);
}
std::uint32_t u32(Bytes b, std::size_t at) {
    return u16(b, at) | static_cast<std::uint32_t>(u16(b, at + 2)) << 16;
}
Bytes bytes_at(Bytes b, std::size_t at, std::size_t size) {
    if (at > b.size() || b.size() - at < size)
        throw std::runtime_error("PIC data beyond end of file");
    return b.subspan(at, size);
}

// Most significant bit first, used by the Spellcasting 101 and 201 codecs.
class MsbBits {
  public:
    explicit MsbBits(Bytes data) : data_(data) {}
    bool can_read(int count) const { return data_.size() * 8 - bit_ >= std::size_t(count); }
    unsigned read(int count) {
        if (!can_read(count))
            throw std::runtime_error("compressed PIC stream ended early");
        unsigned value = 0;
        for (int i = 0; i < count; ++i, ++bit_)
            value = value << 1 | ((data_[bit_ / 8] >> (7 - bit_ % 8)) & 1);
        return value;
    }

  private:
    Bytes data_;
    std::size_t bit_ = 0;
};

// Least significant bit first, as PKWARE DCL defines it.
class LsbBits {
  public:
    explicit LsbBits(Bytes data) : data_(data) {}
    unsigned read(int count) {
        unsigned value = 0;
        for (int i = 0; i < count; ++i, ++bit_) {
            if (bit_ / 8 >= data_.size())
                throw std::runtime_error("DCL stream ended early");
            value |= static_cast<unsigned>((data_[bit_ / 8] >> (bit_ % 8)) & 1) << i;
        }
        return value;
    }

  private:
    Bytes data_;
    std::size_t bit_ = 0;
};

struct DclCode {
    std::array<int, 14> count{};
    std::vector<int> symbols;
};

// Each byte holds (repeat - 1) << 4 | code length; codes are canonical with inverted bits.
DclCode build_dcl_code(Bytes compact) {
    std::vector<int> lengths;
    for (auto item : compact)
        lengths.insert(lengths.end(), (item >> 4) + 1, item & 15);
    DclCode code;
    for (auto length : lengths)
        ++code.count[length];
    std::array<int, 15> next{};
    for (int length = 1; length < 14; ++length)
        next[length + 1] = next[length] + code.count[length];
    code.symbols.resize(lengths.size());
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
        if (lengths[symbol])
            code.symbols[next[lengths[symbol]]++] = static_cast<int>(symbol);
    return code;
}

int read_dcl_symbol(LsbBits &bits, const DclCode &table) {
    int code = 0, first = 0, index = 0;
    for (int length = 1; length < 14; ++length) {
        code |= static_cast<int>(bits.read(1)) ^ 1;
        const int count = table.count[length];
        if (code - count < first)
            return table.symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    throw std::runtime_error("invalid DCL code");
}

constexpr std::array<std::uint8_t, 98> dcl_literal{
    11, 124, 8,   7,   28,  7,   188, 13, 76, 4,  10, 8,  12, 10, 12, 10, 8,  23, 8,  9,
    7,  6,   7,   8,   7,   6,   55,  8,  23, 24, 12, 11, 7,  9,  11, 12, 6,  7,  22, 5,
    7,  24,  6,   11,  9,   6,   7,   22, 7,  11, 38, 7,  9,  8,  25, 11, 8,  11, 9,  12,
    8,  12,  5,   38,  5,   38,  5,   11, 7,  5,  6,  21, 6,  10, 53, 8,  7,  24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45, 44, 173};
constexpr std::array<std::uint8_t, 6> dcl_length{2, 35, 36, 53, 38, 23};
constexpr std::array<std::uint8_t, 7> dcl_distance{2, 20, 53, 230, 247, 151, 248};
constexpr std::array<int, 16> dcl_base{3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
constexpr std::array<int, 16> dcl_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr int dcl_end_length = 519;

std::vector<std::uint8_t> unpack_rows(Bytes data, int width, int height, int bits) {
    const std::size_t row = (static_cast<std::size_t>(width) * bits + 7) / 8;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const auto line = data.subspan(row * y, row);
        auto *out = pixels.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            if (bits == 8)
                out[x] = line[x];
            else if (bits == 4) // The low nibble is the left pixel.
                out[x] = (line[x / 2] >> (x & 1 ? 4 : 0)) & 15;
            else
                out[x] = (line[x / 8] >> (7 - x % 8)) & 1;
        }
    }
    return pixels;
}

std::vector<std::uint8_t> unpack_planes(Bytes data, int width, int height) {
    const std::size_t row = (static_cast<std::size_t>(width) + 7) / 8;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int plane = 0; plane < 4; ++plane) {
            const auto line = data.subspan(row * (y * std::size_t(4) + plane), row);
            for (int x = 0; x < width; ++x)
                pixels[static_cast<std::size_t>(y) * width + x] |=
                    ((line[x / 8] >> (7 - x % 8)) & 1) << plane;
        }
    return pixels;
}

std::vector<Rgb8> decode_palette(Bytes data) {
    std::vector<Rgb8> palette;
    if (data.size() == 16) {
        for (auto reg : data)
            palette.push_back(gateway::ega_color(reg));
        return palette;
    }
    const auto expand = [](std::uint8_t v) {
        v &= 63;
        return static_cast<std::uint8_t>(v << 2 | v >> 4);
    };
    for (std::size_t i = 0; i + 2 < data.size(); i += 3)
        palette.push_back({expand(data[i]), expand(data[i + 1]), expand(data[i + 2])});
    return palette;
}

enum class Codec { lzw, huffman, raw_1, raw_planes, dcl };
struct Layout {
    Codec codec = Codec::dcl;
    std::size_t palette_bytes = 0;
};

Layout s101_layout(std::uint16_t flags) {
    const int high = flags >> 8;
    const bool palette = !(high & 1);
    switch (high >> 1) {
    case 0:
        return {Codec::lzw, palette ? 16u : 0u};
    case 1:
        return {Codec::huffman, 0};
    case 4:
        return {Codec::raw_1, 0};
    case 5:
        return {Codec::raw_planes, palette ? 16u : 0u};
    default:
        throw std::runtime_error("unknown S101 PIC flags");
    }
}

Layout s201_layout(std::uint16_t flags, bool empty) {
    const std::size_t palette = flags & 0x0100 ? 16 : 0;
    const int codec = flags & 0x2600;
    if (empty)
        return {Codec::raw_1, palette};
    if (codec == 0x0200)
        return {Codec::lzw, palette};
    if (codec == 0x0400)
        return {Codec::huffman, palette};
    if (codec == 0x2000)
        return {Codec::raw_1, palette};
    throw std::runtime_error("unknown S201 PIC flags");
}

Layout gateway_layout(std::uint16_t flags) {
    if (!(flags & 0x1000))
        return {Codec::dcl, 0};
    switch ((flags >> 8) & 15) {
    case 0:
        return {Codec::dcl, 0};
    case 1:
        return {Codec::dcl, 16};
    case 2:
    case 4:
        return {Codec::dcl, 48};
    case 3:
        return {Codec::dcl, 768};
    default:
        throw std::runtime_error("unknown Gateway PIC palette class");
    }
}

// The decoded size decides the depth. Class bits only break ties on very narrow images, because
// they disagree with the data in a few Gateway entries.
int gateway_depth(std::size_t size, int width, int height, const PicEntry &entry,
                  std::size_t palette_bytes) {
    const auto fits = [&](int bits) {
        return ((static_cast<std::size_t>(width) * bits + 7) / 8) * height == size;
    };
    constexpr std::array<int, 5> class_depth{1, 4, 4, 8, 4};
    const int display_class = (entry.flags >> 8) & 15;
    int hint = display_class < 5 ? class_depth[display_class] : 8;
    if (palette_bytes)
        hint = palette_bytes == 768 ? 8 : 4;
    if (fits(hint))
        return hint;
    for (int bits : {8, 4, 1})
        if (fits(bits))
            return bits;
    throw std::runtime_error("Gateway PIC size does not match dimensions");
}
} // namespace

namespace gateway {
std::string_view pic_dialect_name(PicDialect dialect) {
    switch (dialect) {
    case PicDialect::s101:
        return "s101";
    case PicDialect::s201:
        return "s201";
    case PicDialect::gateway:
        return "gateway";
    }
    return "unknown";
}

std::vector<PicEntry> parse_pic_directory(Bytes file) {
    // Offsets are not always ascending and unused entries are zero, so the directory ends at the
    // lowest entry offset rather than simply at the first one.
    std::size_t end = file.size();
    std::vector<PicEntry> entries;
    for (std::size_t at = 0; at + 12 <= end; at += 12) {
        PicEntry entry{u32(file, at), u16(file, at + 4), u16(file, at + 6), u16(file, at + 8),
                       u16(file, at + 10)};
        if (entry.offset && entry.offset < end)
            end = entry.offset;
        entries.push_back(entry);
    }
    if (entries.empty() || end % 12 != 0 || end != entries.size() * 12)
        throw std::runtime_error("invalid PIC directory");
    return entries;
}

std::vector<McPicEntry> parse_mc_pic_directory(Bytes file) {
    constexpr std::size_t directory_bytes = 0x1400;
    constexpr std::size_t entry_bytes = 20;
    constexpr std::size_t palette_bytes = 256 * 3;
    if (file.size() < directory_bytes || u32(file, 0) != directory_bytes)
        throw std::runtime_error("invalid Mission Critical PIC directory");

    std::vector<McPicEntry> entries;
    entries.reserve(directory_bytes / entry_bytes);
    std::size_t previous_end = directory_bytes;
    for (std::size_t at = 0; at < directory_bytes; at += entry_bytes) {
        McPicEntry entry;
        entry.offset = u32(file, at);
        entry.compressed_size = u32(file, at + 4);
        entry.flags = u16(file, at + 8);
        entry.x = u16(file, at + 10);
        entry.y = u16(file, at + 12);
        entry.width = u16(file, at + 14);
        entry.height = u16(file, at + 16);
        entry.extra = u16(file, at + 18);
        if (!entry.offset) {
            if (entry.compressed_size || entry.flags || entry.width || entry.height)
                throw std::runtime_error("invalid empty Mission Critical PIC entry");
            entries.push_back(entry);
            continue;
        }
        if (entry.offset < previous_end || entry.offset > file.size())
            throw std::runtime_error("invalid Mission Critical PIC data offset");
        std::size_t data_at = entry.offset;
        if (entry.flags == 0xff00) {
            entry.palette_start = u16(file, data_at);
            entry.palette_size = u16(file, data_at + 2);
            data_at += 4;
            if (entry.palette_start % 3 || entry.palette_size % 3 ||
                entry.palette_start + entry.palette_size > palette_bytes)
                throw std::runtime_error("invalid Mission Critical PIC palette range");
            bytes_at(file, data_at, entry.palette_size);
            data_at += entry.palette_size;
        }
        bytes_at(file, data_at, entry.compressed_size);
        entry.data_offset = static_cast<std::uint32_t>(data_at);
        previous_end = data_at + entry.compressed_size;
        entries.push_back(entry);
    }
    if (previous_end != file.size())
        throw std::runtime_error("Mission Critical PIC data does not end at EOF");
    return entries;
}

std::vector<std::uint8_t> explode_dcl(Bytes data, std::size_t max_size) {
    static const auto literal_code = build_dcl_code(dcl_literal);
    static const auto length_code = build_dcl_code(dcl_length);
    static const auto distance_code = build_dcl_code(dcl_distance);

    LsbBits bits(data);
    const unsigned coded_literals = bits.read(8);
    const unsigned dictionary_bits = bits.read(8);
    if (coded_literals > 1 || dictionary_bits < 4 || dictionary_bits > 6)
        throw std::runtime_error("invalid DCL header");

    std::vector<std::uint8_t> out;
    while (true) {
        if (!bits.read(1)) {
            if (out.size() == max_size)
                throw std::runtime_error("DCL output larger than expected");
            out.push_back(static_cast<std::uint8_t>(
                coded_literals ? read_dcl_symbol(bits, literal_code) : bits.read(8)));
            continue;
        }
        const int symbol = read_dcl_symbol(bits, length_code);
        const int length = dcl_base[symbol] + static_cast<int>(bits.read(dcl_extra[symbol]));
        if (length == dcl_end_length)
            return out;
        const int shift = length == 2 ? 2 : static_cast<int>(dictionary_bits);
        const std::size_t distance =
            (static_cast<std::size_t>(read_dcl_symbol(bits, distance_code)) << shift) +
            bits.read(shift) + 1;
        if (distance > out.size())
            throw std::runtime_error("DCL distance before start of output");
        if (max_size - out.size() < static_cast<std::size_t>(length))
            throw std::runtime_error("DCL output larger than expected");
        for (int i = 0; i < length; ++i)
            out.push_back(out[out.size() - distance]);
    }
}

// Traced from S101.EXE (LZW loop 0x9714, reset 0x9c2f, code reader 0x9c55 in the load image).
std::vector<std::uint8_t> decode_pic_lzw(Bytes data, std::size_t pixels) {
    constexpr unsigned widen = 16, reset = 17, first_code = 18;
    constexpr int start_width = 5, max_width = 16;
    MsbBits bits(data);
    std::vector<std::uint8_t> out;
    std::vector<std::uint16_t> prefix(std::size_t(1) << max_width);
    std::vector<std::uint8_t> suffix(std::size_t(1) << max_width);
    std::vector<std::uint8_t> string;
    int width = start_width;
    unsigned next = first_code, previous = 0;
    std::uint8_t first = 0;

    const auto expand = [&](unsigned code) {
        string.clear();
        for (; code >= first_code; code = prefix[code])
            string.push_back(suffix[code]);
        if (code >= widen)
            throw std::runtime_error("LZW control code in string");
        string.push_back(static_cast<std::uint8_t>(code));
    };
    const auto emit = [&] {
        if (pixels - out.size() < string.size())
            throw std::runtime_error("LZW output larger than expected");
        out.insert(out.end(), string.rbegin(), string.rend());
    };
    const auto read_code = [&] {
        while (true) {
            const unsigned code = bits.read(width);
            if (code != widen)
                return code;
            if (++width > max_width)
                throw std::runtime_error("LZW code too wide");
        }
    };
    const auto restart = [&] {
        width = start_width;
        next = first_code;
        previous = read_code();
        if (previous >= widen)
            throw std::runtime_error("LZW restart without a literal");
        expand(previous);
        first = string.back();
        emit();
    };

    restart();
    // Finish at the final pixel: an end code usually follows, but S101_E3 #114 omits it and the
    // next entry starts straight after the partial code.
    while (out.size() < pixels) {
        const unsigned code = read_code();
        if (code == reset) {
            restart();
            continue;
        }
        if (code == (1u << width) - 1)
            break;
        if (code > next)
            throw std::runtime_error("LZW code beyond table");
        if (next >= prefix.size())
            throw std::runtime_error("LZW table full");
        if (code == next) { // The code being defined: previous string plus its own first pixel.
            expand(previous);
            string.insert(string.begin(), first);
        } else {
            expand(code);
            first = string.back();
        }
        prefix[next] = static_cast<std::uint16_t>(previous);
        suffix[next] = first;
        ++next;
        previous = code;
        emit();
    }
    if (out.size() != pixels)
        throw std::runtime_error("LZW output smaller than expected");
    return out;
}

// Traced from S101.EXE (table reader 0x9574, tree walk 0x8fd5).
std::vector<std::uint8_t> decode_pic_huffman(Bytes data, std::size_t size) {
    struct Node {
        std::array<int, 2> child{-1, -1};
        int symbol = -1;
    };
    std::vector<Node> tree(1);
    std::size_t at = 0;
    const auto next_byte = [&] {
        if (at >= data.size())
            throw std::runtime_error("Huffman table beyond end of data");
        return data[at++];
    };
    const int records = next_byte() + 1;
    for (int i = 0; i < records; ++i) {
        const int length = next_byte();
        const std::uint8_t symbol = next_byte();
        unsigned code = static_cast<unsigned>(next_byte()) << 8;
        if (length > 8)
            code |= next_byte();
        if (length > 16)
            throw std::runtime_error("Huffman code too long");
        int node = 0;
        for (int bit = 0; bit < length; ++bit) {
            const int branch = (code >> (15 - bit)) & 1;
            if (tree[node].symbol >= 0)
                throw std::runtime_error("Huffman code conflict");
            if (tree[node].child[branch] < 0) {
                tree[node].child[branch] = static_cast<int>(tree.size());
                tree.emplace_back();
            }
            node = tree[node].child[branch];
        }
        if (tree[node].symbol >= 0 || tree[node].child[0] >= 0 || tree[node].child[1] >= 0)
            throw std::runtime_error("Huffman code conflict");
        tree[node].symbol = symbol;
    }

    // A one-symbol table has a zero-length code, so a solid image has no bit stream at all.
    if (tree[0].symbol >= 0)
        return std::vector<std::uint8_t>(size, static_cast<std::uint8_t>(tree[0].symbol));

    MsbBits bits(data.subspan(at));
    std::vector<std::uint8_t> out;
    out.reserve(size);
    while (out.size() < size) {
        int node = 0;
        while (tree[node].symbol < 0) {
            node = tree[node].child[bits.read(1)];
            if (node < 0)
                throw std::runtime_error("invalid Huffman code");
        }
        out.push_back(static_cast<std::uint8_t>(tree[node].symbol));
    }
    return out;
}

PicImage decode_pic(Bytes file, const PicEntry &entry, PicDialect dialect) {
    PicImage image;
    image.width = entry.width;
    image.height = entry.height;
    const bool empty = !entry.width || !entry.height;
    const bool palette_only = (dialect == PicDialect::s201 && entry.flags & 0x0100) ||
                              (dialect == PicDialect::gateway && entry.flags & 0x1000);
    if (!entry.offset || (empty && !palette_only))
        return image;

    const Layout layout = dialect == PicDialect::s101   ? s101_layout(entry.flags)
                          : dialect == PicDialect::s201 ? s201_layout(entry.flags, empty)
                                                        : gateway_layout(entry.flags);
    std::size_t at = entry.offset;
    const std::size_t frames = entry.flags & 0xff;
    if (frames) { // Each frame is a screen position for the same bitmap.
        image.x = u16(file, at);
        image.y = u16(file, at + 2);
    }
    at += frames * 4;
    image.palette = decode_palette(bytes_at(file, at, layout.palette_bytes));
    at += layout.palette_bytes;
    if (empty)
        return image;

    if (at > file.size())
        throw std::runtime_error("PIC data beyond end of file");
    const auto stream = file.subspan(at);
    const std::size_t pixels = static_cast<std::size_t>(entry.width) * entry.height;
    const std::size_t row = (static_cast<std::size_t>(entry.width) + 7) / 8;
    switch (layout.codec) {
    case Codec::lzw:
        image.bits = 4;
        image.pixels = decode_pic_lzw(stream, pixels);
        break;
    case Codec::huffman:
        image.bits = 1;
        image.pixels = unpack_rows(decode_pic_huffman(stream, row * entry.height), entry.width,
                                   entry.height, 1);
        break;
    case Codec::raw_1:
        image.bits = 1;
        image.pixels =
            unpack_rows(bytes_at(stream, 0, row * entry.height), entry.width, entry.height, 1);
        break;
    case Codec::raw_planes:
        image.bits = 4;
        image.pixels =
            unpack_planes(bytes_at(stream, 0, row * 4 * entry.height), entry.width, entry.height);
        break;
    case Codec::dcl: {
        const auto data = explode_dcl(stream, pixels);
        image.bits =
            gateway_depth(data.size(), entry.width, entry.height, entry, layout.palette_bytes);
        image.pixels = unpack_rows(data, entry.width, entry.height, image.bits);
        image.overlay = entry.flags & 0x4000;
        break;
    }
    }
    return image;
}

Rgb8 ega_color(std::uint8_t reg) {
    const auto level = [&](int high, int low) {
        return static_cast<std::uint8_t>((((reg >> high) & 1) * 2 + ((reg >> low) & 1)) * 85);
    };
    return {level(2, 5), level(1, 4), level(0, 3)};
}

std::vector<Rgb8> default_ega_palette() {
    std::vector<Rgb8> palette;
    for (std::uint8_t reg : {0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63})
        palette.push_back(ega_color(reg));
    return palette;
}
} // namespace gateway

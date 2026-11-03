// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "crusher.hpp"
#include "pic.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <typename Function> void rejects(Function function, const char *message) {
    try {
        function();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error(message);
}

class BitWriter {
  public:
    void put(unsigned value, int width) {
        for (int bit = width - 1; bit >= 0; --bit) {
            if (used_ % 8 == 0)
                bytes_.push_back(0);
            bytes_.back() |= ((value >> bit) & 1) << (7 - used_ % 8);
            ++used_;
        }
    }
    const Bytes &bytes() const { return bytes_; }

  private:
    Bytes bytes_;
    std::size_t used_ = 0;
};

void put_u16(Bytes &b, std::uint16_t value) {
    b.push_back(value & 255);
    b.push_back(value >> 8);
}
void put_u32(Bytes &b, std::uint32_t value) {
    put_u16(b, static_cast<std::uint16_t>(value));
    put_u16(b, static_cast<std::uint16_t>(value >> 16));
}
void put_entry(Bytes &b, std::uint32_t offset, std::uint16_t flags, std::uint16_t width,
               std::uint16_t height) {
    put_u16(b, offset & 0xffff);
    put_u16(b, offset >> 16);
    put_u16(b, flags);
    put_u16(b, width);
    put_u16(b, height);
    put_u16(b, 0);
}
// blast.c's reference vector, which decodes to "AIAIAIAIAIAIA".
const Bytes dcl_sample{0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f};

void test_directory() {
    Bytes file;
    put_entry(file, 36, 0x2300, 1, 1);
    put_entry(file, 0, 0, 0, 0);
    put_entry(file, 37, 0x2300, 1, 1);
    file.resize(40);
    const auto entries = gateway::parse_pic_directory(file);
    require(entries.size() == 3 && entries[2].offset == 37, "zero entries stay in the directory");

    Bytes unordered;
    put_entry(unordered, 25, 0x2300, 1, 1);
    put_entry(unordered, 24, 0x2300, 1, 1);
    unordered.resize(26);
    require(gateway::parse_pic_directory(unordered).size() == 2, "lowest offset ends directory");

    Bytes misaligned;
    put_entry(misaligned, 5, 0, 1, 1);
    misaligned.resize(20);
    rejects([&] { gateway::parse_pic_directory(misaligned); }, "misaligned directory accepted");
}

void test_mission_critical_directory() {
    Bytes file(0x1400);
    const auto put_mc_entry = [&](std::size_t index, std::uint32_t offset, std::uint32_t size,
                                  std::uint16_t flags, std::uint16_t x, std::uint16_t y,
                                  std::uint16_t width, std::uint16_t height, std::uint16_t extra) {
        Bytes record;
        put_u32(record, offset);
        put_u32(record, size);
        for (const auto value : {flags, x, y, width, height, extra})
            put_u16(record, value);
        std::copy(record.begin(), record.end(), file.begin() + index * 20);
    };
    // First payload updates two RGB entries starting at palette index 64, followed by 2 bytes
    // of Crusher data. The next entry shares the palette but supplies a separate image stream.
    put_mc_entry(0, 0x1400, 2, 0xff00, 0, 0, 2, 1, 0);
    put_mc_entry(1, 0x140c, 4, 3, 7, 9, 2, 2, 2);
    file.insert(file.end(), {0xc0, 0, 6, 0, 1, 2, 3, 4, 5, 6, 0xaa, 0xbb});
    file.insert(file.end(), {0xde, 0xad, 0xbe, 0xef});
    const auto entries = gateway::parse_mc_pic_directory(file);
    require(entries.size() == 256 && entries[2].offset == 0, "MC fixed-size directory");
    require(entries[0].palette_start == 192 && entries[0].palette_size == 6 &&
                entries[0].data_offset == 0x140a && entries[0].compressed_size == 2,
            "MC partial palette and compressed size");
    require(entries[1].flags == 3 && entries[1].x == 7 && entries[1].y == 9 &&
                entries[1].width == 2 && entries[1].height == 2 && entries[1].extra == 2 &&
                entries[1].data_offset == 0x140c,
            "MC positioned animation entry");
    require(gateway::decode_mc_pic_pixels(file, entries[0]) == Bytes({0xaa, 0xbb}),
            "MC uses raw image bytes when stored size equals pixel count");
    require(gateway::decode_mc_pic_pixels(file, entries[1]) == Bytes({0xde, 0xad, 0xbe, 0xef}),
            "MC raw image with non-palette flags");

    Bytes high_flag = file;
    high_flag[9] = 1; // The original loader tests only whether the high flag byte is nonzero.
    const auto alternate = gateway::parse_mc_pic_directory(high_flag);
    require(alternate[0].palette_size == 6 && alternate[0].data_offset == 0x140a,
            "MC palette updates with non-ff high flag bytes");

    Bytes trailer = file;
    trailer.insert(trailer.end(), {0xe1, 0xff, 0x02});
    require(gateway::parse_mc_pic_directory(trailer).size() == 256,
            "MC archive permits trailing unindexed data");

    Bytes bad = file;
    bad[0x1402] = 7; // Palette byte length must be RGB triples.
    rejects([&] { gateway::parse_mc_pic_directory(bad); }, "invalid MC palette length accepted");
    bad = file;
    bad[0x1400] = 0x00; // Palette start plus length must fit 768 bytes.
    bad[0x1401] = 3;
    rejects([&] { gateway::parse_mc_pic_directory(bad); }, "invalid MC palette range accepted");
    bad = file;
    bad[20] = 0x05; // The second record would overlap the first.
    rejects([&] { gateway::parse_mc_pic_directory(bad); }, "overlapping MC entries accepted");
    bad = file;
    bad.pop_back();
    rejects([&] { gateway::parse_mc_pic_directory(bad); }, "truncated MC stream accepted");
    rejects([&] { gateway::parse_mc_pic_directory(Bytes(0x1400)); },
            "empty MC directory accepted");
}

void test_pic_xor_frames() {
    gateway::PicImage base;
    base.bits = 8;
    base.x = 10;
    base.y = 20;
    base.width = 4;
    base.height = 3;
    base.pixels = {10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33};

    gateway::PicImage first;
    first.bits = 8;
    first.x = 11;
    first.y = 21;
    first.width = 2;
    first.height = 2;
    first.pixels = {1, 2, 4, 8};
    gateway::apply_pic_xor(first, base);
    require(first.x == 10 && first.y == 20 && first.width == 4 && first.height == 3,
            "XOR frame must have base coordinates and dimensions");
    require(first.pixels == Bytes({10, 11, 12, 13, 20, 20, 20, 23, 30, 27, 40, 33}),
            "XOR frame must modify only its rectangle");

    // Separate animation entries refer to the *same* full picture, not the previous frame.
    gateway::PicImage second;
    second.bits = 8;
    second.x = 11;
    second.y = 21;
    second.width = 2;
    second.height = 2;
    second.pixels = {16, 32, 64, 128};
    gateway::apply_pic_xor(second, base);
    require(second.pixels == Bytes({10, 11, 12, 13, 20, 5, 54, 23, 30, 95, 160, 33}),
            "XOR siblings must not be accumulated");
    require(base.pixels[5] == 21, "XOR frame modified its base");

    gateway::PicImage beyond = second;
    beyond.x = 20;
    rejects([&] { gateway::apply_pic_xor(beyond, base); },
            "XOR frame outside base picture accepted");
    gateway::PicImage malformed = second;
    malformed.pixels.pop_back();
    rejects([&] { gateway::apply_pic_xor(malformed, base); },
            "XOR frame with truncated pixels accepted");
}

void test_crusher() {
    // A Crusher block with a fixed literal symbol needs no per-pixel bits. Both the literal
    // code and the unused distance tree have zero code-length records.
    BitWriter constant;
    constant.put(4, 16);  // Number of tokens in this block.
    constant.put(0, 5);   // Meta tree: fixed symbol.
    constant.put(0, 5);
    constant.put(0, 9);   // Literal tree: fixed symbol '*'.
    constant.put('*', 9);
    constant.put(0, 4);   // Distance tree: fixed class 0.
    constant.put(0, 4);
    require(gateway::decode_crusher(constant.bytes(), 4) == Bytes(4, '*'),
            "Crusher fixed-symbol block");
    gateway::McPicEntry compressed;
    compressed.compressed_size = static_cast<std::uint32_t>(constant.bytes().size());
    compressed.width = 2;
    compressed.height = 2;
    require(gateway::decode_mc_pic_pixels(constant.bytes(), compressed) == Bytes(4, '*'),
            "MC dispatches smaller stored streams to Crusher");
    rejects([&] { gateway::decode_crusher(constant.bytes(), 5); },
            "Crusher missing next block accepted");
    rejects([&] { gateway::decode_crusher(constant.bytes(), 3); },
            "Crusher extra whole bytes accepted");

    // The literal tree has two one-bit codes (symbols 0 and 1). Meta tree is fixed symbol 3,
    // which expands to a length-one literal code each time it is read.
    BitWriter variable;
    variable.put(4, 16);
    variable.put(0, 5);
    variable.put(3, 5);
    variable.put(2, 9);
    variable.put(0, 4);
    variable.put(0, 4);
    for (unsigned symbol : {0, 1, 1, 0})
        variable.put(symbol, 1);
    require(gateway::decode_crusher(variable.bytes(), 4) == Bytes({0, 1, 1, 0}),
            "Crusher canonical Huffman symbols");

    // One literal followed by a three-byte distance-zero LZ match. The meta tree has four
    // two-bit symbols; the long-zero code fills holes in the 510-symbol literal alphabet.
    BitWriter match;
    match.put(2, 16);   // Two tokens produce four output bytes.
    match.put(4, 5);    // Four meta-tree code lengths follow.
    for (int i = 0; i < 3; ++i)
        match.put(2, 3);
    match.put(0, 2);    // No zero run after the third code length.
    match.put(2, 3);
    match.put(257, 9);  // Literal codes 0..256 are defined.
    match.put(2, 2);    // Meta symbol 2: long run of zero lengths.
    match.put(45, 9);   // 65 absent symbols.
    match.put(3, 2);    // Symbol 65: one-bit code for 'A'.
    match.put(2, 2);
    match.put(170, 9);  // 190 absent symbols.
    match.put(3, 2);    // Symbol 256: one-bit three-byte match.
    match.put(0, 4);    // Constant distance class 0.
    match.put(0, 4);
    match.put(0, 1);    // 'A'.
    match.put(1, 1);    // Match at distance 1.
    require(gateway::decode_crusher(match.bytes(), 4) == Bytes(4, 'A'),
            "Crusher LZ back-reference and long code-length run");

    rejects([] { gateway::decode_crusher(Bytes{0}, 3); }, "truncated Crusher accepted");
    rejects([] { gateway::decode_crusher(Bytes{}, 65 * 1024 * 1024); },
            "unbounded Crusher allocation accepted");
}

void test_dcl() {
    const auto out = gateway::explode_dcl(dcl_sample, 13);
    require(std::string(out.begin(), out.end()) == "AIAIAIAIAIAIA", "DCL reference vector");
    rejects([] { gateway::explode_dcl(dcl_sample, 12); }, "DCL output limit ignored");
    rejects([] { gateway::explode_dcl(Bytes{0x00, 0x04, 0x82}, 13); }, "truncated DCL accepted");
    rejects([] { gateway::explode_dcl(Bytes{0x02, 0x04, 0, 0}, 13); }, "bad DCL header accepted");
}

void test_lzw() {
    BitWriter bits;
    bits.put(3, 5);  // Pixel 3.
    bits.put(18, 5); // The entry being defined: 3 3.
    bits.put(16, 5); // Widen to six bits.
    bits.put(19, 6); // Again the entry being defined: 3 3 3.
    bits.put(5, 6);  // Pixel 5, defining 20 = 3 3 3 + ... with 21 next.
    bits.put(63, 6); // End code at the current width.
    const auto out = gateway::decode_pic_lzw(bits.bytes(), 7);
    require(out == Bytes({3, 3, 3, 3, 3, 3, 5}), "LZW self-referencing codes");
    rejects([&] { gateway::decode_pic_lzw(bits.bytes(), 8); }, "short LZW image accepted");
    rejects([&] { gateway::decode_pic_lzw(bits.bytes(), 5); }, "long LZW image accepted");

    BitWriter restart;
    restart.put(1, 5);
    restart.put(17, 5); // Reset, then a new first literal.
    restart.put(2, 5);
    restart.put(31, 5);
    require(gateway::decode_pic_lzw(restart.bytes(), 2) == Bytes({1, 2}), "LZW reset");

    BitWriter unterminated; // The final pixel is followed by unrelated bytes, not an end code.
    unterminated.put(1, 5);
    unterminated.put(2, 5);
    unterminated.put(28, 5);
    require(gateway::decode_pic_lzw(unterminated.bytes(), 2) == Bytes({1, 2}),
            "LZW without a complete end code");

    BitWriter ahead;
    ahead.put(1, 5);
    ahead.put(25, 5);
    rejects([&] { gateway::decode_pic_lzw(ahead.bytes(), 2); }, "LZW code beyond table accepted");
}

void test_huffman() {
    Bytes data{1, 1, 0xaa, 0x00, 2, 0x55, 0x80};
    BitWriter bits;
    bits.put(0, 1);
    bits.put(2, 2);
    bits.put(0, 1);
    data.insert(data.end(), bits.bytes().begin(), bits.bytes().end());
    require(gateway::decode_pic_huffman(data, 3) == Bytes({0xaa, 0x55, 0xaa}), "Huffman codes");
    rejects([&] { gateway::decode_pic_huffman(data, 9); }, "truncated Huffman data accepted");
    require(gateway::decode_pic_huffman(Bytes{0, 0, 0xff, 0}, 4) == Bytes(4, 0xff),
            "zero-length Huffman code fills the output");
    rejects([] { gateway::decode_pic_huffman(Bytes{1, 1, 1, 0, 1, 2, 0}, 1); },
            "conflicting Huffman codes accepted");
}

void test_gateway_entry() {
    Bytes file;
    put_entry(file, 12, 0x1101, 26, 1);
    put_u16(file, 7);
    put_u16(file, 9);
    for (std::uint8_t reg = 0; reg < 16; ++reg)
        file.push_back(reg == 4 ? 0x3f : 0);
    file.insert(file.end(), dcl_sample.begin(), dcl_sample.end());
    const auto image = gateway::decode_pic(file, gateway::parse_pic_directory(file)[0],
                                           gateway::PicDialect::gateway);
    require(image.bits == 4 && image.x == 7 && image.y == 9, "Gateway header fields");
    require(image.pixels[0] == 1 && image.pixels[1] == 4, "low nibble is the left pixel");
    require(image.palette.size() == 16 && image.palette[4].r == 255, "EGA palette decoded");

    gateway::PicEntry eight{12 + 4 + 16, 0x4300, 13, 1, 0};
    const auto byte_image = gateway::decode_pic(file, eight, gateway::PicDialect::gateway);
    require(byte_image.bits == 8 && byte_image.overlay && byte_image.pixels[0] == 'A',
            "size selects eight-bit depth and flag selects XOR");

    Bytes frames;
    for (std::uint16_t i = 0; i < 16; ++i)
        put_u16(frames, 100 + i);
    frames.insert(frames.end(), dcl_sample.begin(), dcl_sample.end());
    gateway::PicEntry framed_entry{1, 0x2308, 13, 1, 0};
    Bytes shifted{0};
    shifted.insert(shifted.end(), frames.begin(), frames.end());
    const auto first_frame =
        gateway::decode_pic(shifted, framed_entry, gateway::PicDialect::gateway);
    require(first_frame.x == 100 && first_frame.y == 101 && first_frame.pixels[0] == 'A',
            "low flag byte counts four-byte frame positions");

    Bytes class_4(1, 0);
    class_4.resize(49);
    class_4.insert(class_4.end(), dcl_sample.begin(), dcl_sample.end());
    const auto wide = gateway::decode_pic(class_4, gateway::PicEntry{1, 0x1400, 26, 1, 0},
                                          gateway::PicDialect::gateway);
    require(wide.bits == 4 && wide.palette.size() == 16, "class 4 palette has 48 bytes");

    gateway::PicEntry wrong{12 + 4 + 16, 0x2300, 13, 2, 0};
    rejects([&] { gateway::decode_pic(file, wrong, gateway::PicDialect::gateway); },
            "mismatched Gateway size accepted");
}

void test_spellcasting_entries() {
    Bytes file(12);
    file.insert(file.end(), {0b10100000, 0b01000000});
    const gateway::PicEntry mono{12, 0x0901, 3, 2, 0};
    file[12] = 0;
    file.insert(file.begin() + 12, {1, 0, 2, 0});
    const auto image = gateway::decode_pic(file, mono, gateway::PicDialect::s101);
    require(image.bits == 1 && image.x == 1 && image.y == 2, "raw one-bit header");
    require(image.pixels == Bytes({0, 0, 0, 0, 1, 0}), "raw one-bit rows, MSB left");

    const Bytes planes{0x80, 0x00, 0x80, 0x00};
    const gateway::PicEntry planar{0, 0x0b00, 1, 1, 0};
    Bytes planar_file{0xff};
    planar_file.insert(planar_file.end(), planes.begin(), planes.end());
    auto entry = planar;
    entry.offset = 1;
    require(gateway::decode_pic(planar_file, entry, gateway::PicDialect::s101).pixels == Bytes({5}),
            "raw planes, first plane is bit 0");

    Bytes palette(32, 0);
    palette[12] = 0x3f;
    const gateway::PicEntry palette_only{12, 0x4100, 0, 0, 0};
    const auto only = gateway::decode_pic(palette, palette_only, gateway::PicDialect::s201);
    require(only.pixels.empty() && only.palette.size() == 16, "S201 palette-only entry");
    rejects(
        [&] {
            gateway::decode_pic(file, gateway::PicEntry{12, 0x0800, 3, 2, 0},
                                gateway::PicDialect::s201);
        },
        "unknown S201 flags accepted");
}

void test_palette() {
    const auto brown = gateway::ega_color(20);
    require(brown.r == 170 && brown.g == 85 && brown.b == 0, "EGA colour bits");
    const auto palette = gateway::default_ega_palette();
    require(palette.size() == 16 && palette[15].g == 255, "default EGA palette");
}

void test_png() {
    const Bytes pixels{1, 0, 1, 2, 3, 0};
    const std::vector<gateway::Rgb8> palette{{0, 0, 0}, {1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    const auto png = gateway::encode_indexed_png(3, 2, 4, pixels, palette);
    require(Bytes(png.begin() + 1, png.begin() + 4) == Bytes({'P', 'N', 'G'}), "PNG signature");
    require(png[16 + 3] == 3 && png[20 + 3] == 2, "PNG dimensions");
    require(png[24] == 4 && png[25] == 3, "PNG depth and colour type");
    require(Bytes(png.end() - 4, png.end()) == Bytes({0xae, 0x42, 0x60, 0x82}), "PNG IEND CRC");
    const Bytes row{0, 0x10, 0x10};
    require(std::search(png.begin(), png.end(), row.begin(), row.end()) != png.end(),
            "PNG packs the left pixel first");
    rejects([&] { gateway::encode_indexed_png(3, 2, 1, pixels, palette); },
            "pixel outside depth accepted");
    rejects([&] { gateway::encode_indexed_png(4, 2, 4, pixels, palette); },
            "PNG pixel count mismatch accepted");
}

int main() {
    try {
        test_directory();
        test_mission_critical_directory();
        test_pic_xor_frames();
        test_crusher();
        test_dcl();
        test_lzw();
        test_huffman();
        test_gateway_entry();
        test_spellcasting_entries();
        test_palette();
        test_png();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "PIC decoder checks passed\n";
}

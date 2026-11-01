#pragma once
#include "png.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gateway {
// Early Legend games share the same PIC directory but use three different entry encodings.
enum class PicDialect { s101, s201, gateway };

std::string_view pic_dialect_name(PicDialect dialect);

struct PicEntry {
    std::uint32_t offset = 0;
    std::uint16_t flags = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t reserved = 0;
};

// Mission Critical has a different 256-slot, 20-byte PIC directory. Its image streams use
// Crusher! compression and may update the palette independently of pixel data.
struct McPicEntry {
    std::uint32_t offset = 0;
    std::uint32_t compressed_size = 0;
    std::uint16_t flags = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t extra = 0;
    std::uint16_t palette_start = 0; // Byte offset into the 768-byte VGA DAC palette.
    std::uint16_t palette_size = 0;  // Bytes, in RGB triples.
    std::uint32_t data_offset = 0;   // Compressed stream starts after an optional palette update.
};

std::vector<McPicEntry> parse_mc_pic_directory(std::span<const std::uint8_t> file);

struct PicImage {
    int width = 0;
    int height = 0;
    int bits = 0; // 1, 4, or 8; 0 for an entry without pixels.
    int x = 0;
    int y = 0;
    bool overlay = false;             // Pixels are XORed onto the current image.
    std::vector<std::uint8_t> pixels; // One palette index per pixel, row by row.
    std::vector<Rgb8> palette;        // Empty when the entry carries no palette.
};

std::vector<PicEntry> parse_pic_directory(std::span<const std::uint8_t> file);
PicImage decode_pic(std::span<const std::uint8_t> file, const PicEntry &entry, PicDialect dialect);

// Codecs, exposed so that they can be tested without game data.
std::vector<std::uint8_t> explode_dcl(std::span<const std::uint8_t> data, std::size_t max_size);
std::vector<std::uint8_t> decode_pic_lzw(std::span<const std::uint8_t> data, std::size_t pixels);
std::vector<std::uint8_t> decode_pic_huffman(std::span<const std::uint8_t> data, std::size_t size);

Rgb8 ega_color(std::uint8_t reg);
std::vector<Rgb8> default_ega_palette();
} // namespace gateway

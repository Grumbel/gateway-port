#include "pic.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using gateway::PicDialect;
using gateway::PicEntry;
using gateway::PicImage;
using gateway::Rgb8;

namespace {
struct Decoded {
    PicDialect dialect = PicDialect::gateway;
    std::vector<std::optional<PicImage>> images;
    std::vector<std::string> errors;
};

Decoded decode_all(std::span<const std::uint8_t> file, const std::vector<PicEntry> &entries,
                   PicDialect dialect) {
    Decoded result;
    result.dialect = dialect;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        try {
            result.images.push_back(gateway::decode_pic(file, entries[i], dialect));
        } catch (const std::runtime_error &error) {
            result.images.push_back(std::nullopt);
            result.errors.push_back("entry " + std::to_string(i) + ": " + error.what());
        }
    }
    return result;
}

Decoded decode_best(std::span<const std::uint8_t> file, const std::vector<PicEntry> &entries) {
    std::optional<Decoded> best;
    for (auto dialect : {PicDialect::gateway, PicDialect::s201, PicDialect::s101}) {
        auto decoded = decode_all(file, entries, dialect);
        if (decoded.errors.empty())
            return decoded;
        if (!best || decoded.errors.size() < best->errors.size())
            best = std::move(decoded);
    }
    return *best;
}

std::vector<Rgb8> mono_palette() { return {{0, 0, 0}, {255, 255, 255}}; }

std::vector<Rgb8> grey_palette() {
    std::vector<Rgb8> palette(256);
    for (int i = 0; i < 256; ++i)
        palette[i] = {std::uint8_t(i), std::uint8_t(i), std::uint8_t(i)};
    return palette;
}

// Overlays carry screen positions but no reference to the picture they change. The latest full
// picture that covers an overlay's rectangle is the best available choice; a smaller sprite may be
// stored between a room and its overlays (GATE_102 #3).
const PicImage *find_base(const std::vector<PicImage> &bases, const PicImage &image) {
    for (auto base = bases.rbegin(); base != bases.rend(); ++base)
        if (base->bits == image.bits && image.x >= base->x && image.y >= base->y &&
            image.x + image.width <= base->x + base->width &&
            image.y + image.height <= base->y + base->height)
            return &*base;
    return nullptr;
}

void apply_overlay(PicImage &image, const PicImage &base) {
    auto pixels = base.pixels;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            pixels[std::size_t(image.y - base.y + y) * base.width + image.x - base.x + x] ^=
                image.pixels[std::size_t(y) * image.width + x];
    image.pixels = std::move(pixels);
    image.width = base.width;
    image.height = base.height;
}

void write_file(const fs::path &path, std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

std::vector<std::uint8_t> read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void usage(std::ostream &out) {
    out << "usage: gateway-extract [--dialect auto|s101|s201|gateway|mc] "
           "[--raw-overlays] [--raw-chunks] -o DIR "
           "FILE.PIC...\n";
}
} // namespace

int main(int argc, char **argv) try {
    std::optional<PicDialect> forced;
    bool forced_mc = false;
    bool raw_overlays = false;
    bool raw_chunks = false;
    fs::path output;
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return 0;
        } else if (arg == "--raw-overlays") {
            raw_overlays = true;
        } else if (arg == "--raw-chunks") {
            raw_chunks = true;
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--dialect" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (name == "s101")
                forced = PicDialect::s101;
            else if (name == "s201")
                forced = PicDialect::s201;
            else if (name == "gateway")
                forced = PicDialect::gateway;
            else if (name == "mc") {
                forced = std::nullopt;
                forced_mc = true;
            } else if (name != "auto")
                throw std::runtime_error("unknown dialect " + name);
            if (name != "mc")
                forced_mc = false;
        } else if (arg.starts_with("-")) {
            usage(std::cerr);
            return 2;
        } else {
            inputs.emplace_back(arg);
        }
    }
    if (output.empty() || inputs.empty()) {
        usage(std::cerr);
        return 2;
    }

    std::size_t written = 0, failed = 0, unpaletted = 0, unplaced = 0, raw_written = 0;
    std::vector<Rgb8> palette_8; // Some 8-bit files rely on a palette loaded from an earlier one.
    std::array<std::uint8_t, 768> mc_palette{};
    bool have_mc_palette = false; // Mission Critical also reuses palettes between files.
    for (const auto &input : inputs) {
        const auto file = read_file(input);
        const bool mc = forced_mc ||
                        (!forced && file.size() >= 0x1400 && file[0] == 0 && file[1] == 0x14 &&
                         file[2] == 0 && file[3] == 0);
        if (mc) {
            const auto entries = gateway::parse_mc_pic_directory(file);
            const std::size_t populated =
                std::count_if(entries.begin(), entries.end(), [](const auto &e) { return e.offset; });
            const std::size_t chunks = std::count_if(entries.begin(), entries.end(),
                                                     [](const auto &e) { return e.compressed_size; });
            if (!raw_chunks) {
                failed += chunks;
                std::cerr << input.filename().string() << ": Mission Critical PIC (" << populated
                          << " entries, " << chunks << " Crusher-compressed images); native Crusher "
                             "decoding unavailable. Use --raw-chunks to export streams.\n";
                continue;
            }
            const auto directory = output / input.stem();
            fs::create_directories(directory);
            std::ofstream manifest(directory / "index.tsv");
            if (!manifest)
                throw std::runtime_error("cannot write Mission Critical manifest");
            manifest << "index\toffset\tcompressed_size\tflags\tx\ty\twidth\theight\textra"
                        "\tpalette_start\tpalette_size\n";
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto &entry = entries[i];
                if (!entry.offset)
                    continue;
                manifest << i << '\t' << entry.offset << '\t' << entry.compressed_size << '\t'
                         << entry.flags << '\t' << entry.x << '\t' << entry.y << '\t'
                         << entry.width << '\t' << entry.height << '\t' << entry.extra << '\t'
                         << entry.palette_start << '\t' << entry.palette_size << '\n';
                if (entry.flags == 0xff00 && entry.palette_size) {
                    std::copy_n(file.begin() + entry.offset + 4, entry.palette_size,
                                mc_palette.begin() + entry.palette_start);
                    have_mc_palette = true;
                }
                char name[32];
                if (entry.compressed_size) {
                    std::snprintf(name, sizeof name, "%03zu.crusher", i);
                    write_file(directory / name,
                               std::span<const std::uint8_t>(file).subspan(entry.data_offset,
                                                                           entry.compressed_size));
                    ++raw_written;
                }
                if (have_mc_palette) {
                    std::snprintf(name, sizeof name, "%03zu.pal", i);
                    write_file(directory / name, mc_palette);
                }
            }
            if (!manifest)
                throw std::runtime_error("cannot write Mission Critical manifest");
            std::cout << input.filename().string() << ": mission-critical, " << populated
                      << " entries, " << chunks << " compressed streams exported (not decoded)\n";
            continue;
        }
        if (raw_chunks)
            throw std::runtime_error("--raw-chunks supports Mission Critical PIC files only");
        const auto entries = gateway::parse_pic_directory(file);
        auto decoded = forced ? decode_all(file, entries, *forced) : decode_best(file, entries);
        const auto directory = output / input.stem();
        fs::create_directories(directory);

        std::vector<Rgb8> palette_4;
        std::vector<PicImage> bases;
        std::size_t images = 0;
        for (std::size_t i = 0; i < decoded.images.size(); ++i) {
            if (!decoded.images[i])
                continue;
            auto &image = *decoded.images[i];
            if (image.palette.size() == 16)
                palette_4 = image.palette;
            else if (image.palette.size() == 256)
                palette_8 = image.palette;
            if (image.pixels.empty())
                continue;

            std::vector<Rgb8> palette;
            if (image.bits == 1) {
                palette = mono_palette();
            } else if (image.bits == 4) {
                palette = palette_4.empty() ? gateway::default_ega_palette() : palette_4;
            } else if (palette_8.empty()) {
                palette = grey_palette();
                ++unpaletted;
            } else {
                palette = palette_8;
            }

            if (!image.overlay) {
                bases.push_back(image);
            } else if (!raw_overlays) {
                if (const auto *base = find_base(bases, image))
                    apply_overlay(image, *base);
                else
                    ++unplaced;
            }

            char name[32];
            std::snprintf(name, sizeof name, "%03zu.png", i);
            write_file(directory / name,
                       gateway::encode_indexed_png(image.width, image.height, image.bits,
                                                   image.pixels, palette));
            ++images;
        }
        written += images;
        failed += decoded.errors.size();
        std::cout << input.filename().string() << ": " << gateway::pic_dialect_name(decoded.dialect)
                  << ", " << entries.size() << " entries, " << images << " images, "
                  << decoded.errors.size() << " failed\n";
        for (const auto &error : decoded.errors)
            std::cerr << input.filename().string() << ": " << error << '\n';
    }
    if (!raw_chunks || written)
        std::cout << written << " images written to " << output.string() << ", " << failed
                  << " failed\n";
    if (unplaced)
        std::cerr << unplaced << " overlays have no covering picture and stay as raw deltas\n";
    if (unpaletted)
        std::cerr << unpaletted << " 8-bit images had no palette and use a grey ramp\n";
    if (raw_written)
        std::cout << raw_written << " Crusher streams (plus available palette snapshots) written "
                  << "to " << output.string() << "\n";
    return failed ? 1 : 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-extract: " << error.what() << '\n';
    return 1;
}

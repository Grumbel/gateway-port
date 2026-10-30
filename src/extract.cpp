#include "pic.hpp"
#include <algorithm>
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
    out << "usage: gateway-extract [--dialect auto|s101|s201|gateway] [--raw-overlays] -o DIR "
           "FILE.PIC...\n";
}
} // namespace

int main(int argc, char **argv) try {
    std::optional<PicDialect> forced;
    bool raw_overlays = false;
    fs::path output;
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return 0;
        } else if (arg == "--raw-overlays") {
            raw_overlays = true;
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
            else if (name != "auto")
                throw std::runtime_error("unknown dialect " + name);
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

    std::size_t written = 0, failed = 0, unpaletted = 0, unplaced = 0;
    std::vector<Rgb8> palette_8; // Some 8-bit files rely on a palette loaded from an earlier one.
    for (const auto &input : inputs) {
        const auto file = read_file(input);
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
    std::cout << written << " images written to " << output.string() << ", " << failed
              << " failed\n";
    if (unplaced)
        std::cerr << unplaced << " overlays have no covering picture and stay as raw deltas\n";
    if (unpaletted)
        std::cerr << unpaletted << " 8-bit images had no palette and use a grey ramp\n";
    return failed ? 1 : 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-extract: " << error.what() << '\n';
    return 1;
}

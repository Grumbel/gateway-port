// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

// Headless driver for the live source-C intro hooks. It runs the same
// gate_source_c_full_run() entry as gateway-intro-source-c-live, but with
// a virtual clock, so the whole intro runs in seconds. Every distinct
// published frame can be written as a PPM, and Escape can be injected at a
// virtual time to check the interruption path terminates.
#include "intro_source_c_full.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Frames {
    std::uint64_t now = 0;
    std::uint64_t escape_at = UINT64_MAX;
    bool escape_pending = false;
    bool escape_sent = false;
    std::uint64_t dump_from = 0, dump_to = UINT64_MAX;
    std::string directory;
    unsigned written = 0, published = 0;
    unsigned key_reads = 0;
    std::vector<std::uint8_t> last_rgb, last_raw;
    std::uint16_t last_width = 0;
    // Flash check: a non-black frame shown for under 10ms between two
    // black frames (as the interrupt-vector palette was at each fade-out).
    bool check_flashes = false;
    unsigned flashes = 0;
    int previous_black = -1, current_black = -1;
    std::uint64_t current_since = 0;

    void tick(std::uint64_t ms) {
        now += ms;
        if (!escape_sent && now >= escape_at) {
            escape_pending = true;
            escape_sent = true;
        }
    }
};

std::uint32_t clock_ms(void *userdata) {
    auto &f = *static_cast<Frames *>(userdata);
    f.tick(1);
    return static_cast<std::uint32_t>(f.now);
}

void delay_ms(void *userdata, std::uint32_t ms) { static_cast<Frames *>(userdata)->tick(ms); }

std::uint16_t key_pending(void *userdata) {
    return static_cast<Frames *>(userdata)->escape_pending;
}

std::uint16_t read_key(void *userdata) {
    auto &f = *static_cast<Frames *>(userdata);
    ++f.key_reads;
    if (f.escape_pending) {
        f.escape_pending = false;
        return 27;
    }
    return 0;
}

void publish(Frames &f, const std::uint8_t *indices, const std::uint8_t dac[256][3],
             std::uint16_t width, std::uint16_t height) {
    ++f.published;
    const bool dumping = !f.directory.empty() && f.now >= f.dump_from && f.now <= f.dump_to;
    if (!dumping && !f.check_flashes)
        return;
    // Most callbacks republish an unchanged index/DAC state.
    const std::size_t pixels = std::size_t(width) * height;
    if (width == f.last_width && f.last_raw.size() == pixels + 768u &&
        std::memcmp(f.last_raw.data(), indices, pixels) == 0 &&
        std::memcmp(f.last_raw.data() + pixels, dac, 768u) == 0)
        return;
    f.last_raw.assign(indices, indices + pixels);
    f.last_raw.insert(f.last_raw.end(), &dac[0][0], &dac[0][0] + 768u);
    std::vector<std::uint8_t> rgb(std::size_t(width) * height * 3u);
    for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
        for (unsigned c = 0; c < 3; ++c) {
            const std::uint8_t s = dac[indices[i]][c];
            rgb[3 * i + c] = static_cast<std::uint8_t>((s << 2) | (s >> 4));
        }
    if (width == f.last_width && rgb == f.last_rgb)
        return;
    f.last_rgb = rgb;
    f.last_width = width;
    const bool black = std::all_of(rgb.begin(), rgb.end(), [](std::uint8_t v) { return v == 0; });
    if (black && f.current_black == 0 && f.previous_black == 1 && f.now - f.current_since < 10) {
        ++f.flashes;
        std::cerr << "isolated " << f.now - f.current_since << "ms non-black frame at "
                  << f.current_since << "ms\n";
    }
    f.previous_black = f.current_black;
    f.current_black = black;
    f.current_since = f.now;
    if (!dumping)
        return;
    char name[64];
    std::snprintf(name, sizeof name, "/%05u-%07llu.ppm", f.written++,
                  static_cast<unsigned long long>(f.now));
    std::FILE *out = std::fopen((f.directory + name).c_str(), "wb");
    if (!out) {
        std::perror("fopen");
        std::exit(1);
    }
    std::fprintf(out, "P6\n%u %u\n255\n", width, height);
    std::fwrite(rgb.data(), 1, rgb.size(), out);
    std::fclose(out);
}

void frame_ready(void *userdata, const std::uint8_t *indices, const std::uint8_t dac[256][3]) {
    publish(*static_cast<Frames *>(userdata), indices, dac, 320, 200);
}

void frame_ready_vga16(void *userdata, const std::uint8_t planes[4][0x10000],
                       const std::uint8_t dac[256][3], const std::uint8_t attributes[32]) {
    static std::array<std::uint8_t, 640u * 480u> indices;
    for (std::size_t y = 0; y < 480; ++y)
        for (std::size_t x = 0; x < 640; ++x) {
            const auto offset = y * 80u + x / 8u;
            const auto bit = static_cast<std::uint8_t>(0x80u >> (x & 7u));
            std::uint8_t index = 0;
            for (unsigned plane = 0; plane < 4; ++plane)
                if (planes[plane][offset] & bit)
                    index |= static_cast<std::uint8_t>(1u << plane);
            indices[y * 640u + x] = attributes[index];
        }
    publish(*static_cast<Frames *>(userdata), indices.data(), dac, 640, 480);
}

} // namespace

int main(int argc, char **argv) {
    Frames f;
    std::vector<std::string_view> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--escape-at" && i + 1 < argc)
            f.escape_at = std::strtoull(argv[++i], nullptr, 0);
        else if (arg == "--dump" && i + 1 < argc)
            f.directory = argv[++i];
        else if (arg == "--from" && i + 1 < argc)
            f.dump_from = std::strtoull(argv[++i], nullptr, 0);
        else if (arg == "--to" && i + 1 < argc)
            f.dump_to = std::strtoull(argv[++i], nullptr, 0);
        else if (arg == "--check-flashes")
            f.check_flashes = true;
        else
            positional.push_back(arg);
    }
    if (positional.size() != 1) {
        std::cerr << "usage: gateway-intro-source-c-frames DOS-DIR [--escape-at MS] "
                     "[--dump DIR [--from MS] [--to MS]] [--check-flashes]\n"
                     "Runs the live source-C intro on a virtual clock and reports "
                     "its virtual duration.\n";
        return 2;
    }
    GateSourceCFullHooks hooks{};
    hooks.userdata = &f;
    hooks.clock_ms = clock_ms;
    hooks.delay_ms = delay_ms;
    hooks.key_pending = key_pending;
    hooks.read_key = read_key;
    hooks.frame_ready = frame_ready;
    hooks.frame_ready_vga16 = frame_ready_vga16;
    hooks.original_video_start = 1;
    const std::string dos(positional[0]);
    const int result = gate_source_c_full_run("--from-original", dos.c_str(), &hooks);
    std::cout << "result=" << result << " virtual_ms=" << f.now << " published=" << f.published
              << " written=" << f.written << " escape=" << (f.escape_sent ? "sent" : "none")
              << " key_reads=" << f.key_reads;
    if (f.check_flashes)
        std::cout << " flashes=" << f.flashes;
    std::cout << '\n';
    if (f.escape_at != UINT64_MAX && f.now > f.escape_at + 2000) {
        std::cerr << "intro kept running after Escape\n";
        return 1;
    }
    return result != 0 ? result : f.flashes != 0;
}

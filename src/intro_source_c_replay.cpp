// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

// Display frames produced by the original C EXE:1302 sequence. The recording
// contains the native source-C framebuffer and VGA DAC, not a scene script.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr int width = 320;
constexpr int height = 200;

struct Frame {
    std::uint32_t at_ms = 0;
    std::array<std::uint8_t, width * height> indices{};
    std::array<std::uint8_t, 256 * 3> dac{};
};

void require(bool condition) {
    if (!condition) throw std::runtime_error(SDL_GetError());
}

std::vector<Frame> load(const char *path) {
    std::ifstream input(path, std::ios::binary);
    char signature[4];
    if (!input.read(signature, sizeof signature) ||
        std::string_view(signature, 4) != "GVR1")
        throw std::runtime_error("not a Gateway source-C replay (GVR1)");

    std::vector<Frame> frames;
    for (;;) {
        std::uint8_t stamp[4];
        if (!input.read(reinterpret_cast<char *>(stamp), sizeof stamp)) {
            if (input.eof() && input.gcount() == 0) break;
            throw std::runtime_error("truncated replay timestamp");
        }
        Frame frame;
        for (unsigned i = 0; i < 4; ++i)
            frame.at_ms |= std::uint32_t(stamp[i]) << (8 * i);
        if ((!frames.empty() && frame.at_ms < frames.back().at_ms) ||
            frames.size() >= 30000)
            throw std::runtime_error("invalid replay sequence");
        if (!input.read(reinterpret_cast<char *>(frame.indices.data()),
                        frame.indices.size()) ||
            !input.read(reinterpret_cast<char *>(frame.dac.data()),
                        frame.dac.size()))
            throw std::runtime_error("truncated replay frame");
        frames.push_back(std::move(frame));
    }
    if (frames.empty()) throw std::runtime_error("empty replay");
    return frames;
}

std::uint8_t rgb8(std::uint8_t sample) {
    return static_cast<std::uint8_t>((sample << 2) | (sample >> 4));
}

void expand(const Frame &frame, std::vector<std::uint32_t> &pixels) {
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const std::size_t color = 3u * frame.indices[i];
        pixels[i] = 0xff000000u |
            (std::uint32_t(rgb8(frame.dac[color])) << 16) |
            (std::uint32_t(rgb8(frame.dac[color + 1])) << 8) |
            rgb8(frame.dac[color + 2]);
    }
}

} // namespace

int main(int argc, char **argv) try {
    if (argc != 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "usage: gateway-intro-source-c-replay FILE.gvr\n"
                     "Space/P: pause; Left/Right: seek 5 seconds; Home/End: "
                     "beginning/end; F9: filter; Esc: exit.\n"
                     "Replay is silent; source-C original game handoff is "
                     "not implemented.\n";
        return argc == 2 ? 0 : 2;
    }
    const auto frames = load(argv[1]);
    require(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    const auto cleanup = [&] {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    };
    try {
        require(SDL_CreateWindowAndRenderer("Gateway original C intro", 960, 720,
                                           SDL_WINDOW_RESIZABLE, &window, &renderer));
        require(SDL_SetRenderLogicalPresentation(renderer, 640, 480,
                                                SDL_LOGICAL_PRESENTATION_LETTERBOX));
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
        require(texture != nullptr);
        require(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));

        bool running = true, paused = false, nearest = true;
        std::int64_t start = static_cast<std::int64_t>(SDL_GetTicks());
        std::int64_t pause_at = 0;
        std::size_t previous = frames.size();
        std::uint32_t title_second = UINT32_MAX;
        std::vector<std::uint32_t> pixels(width * height);
        const std::int64_t duration = frames.back().at_ms;
        while (running) {
            const std::int64_t now = static_cast<std::int64_t>(SDL_GetTicks());
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key == SDLK_F9) {
                    nearest = !nearest;
                    require(SDL_SetTextureScaleMode(texture,
                        nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
                }
                if (event.key.key == SDLK_SPACE || event.key.key == SDLK_P) {
                    if (paused) start += now - pause_at;
                    else pause_at = now;
                    paused = !paused;
                }
                if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT ||
                    event.key.key == SDLK_HOME || event.key.key == SDLK_END) {
                    const auto position = (paused ? pause_at : now) - start;
                    std::int64_t target = position;
                    if (event.key.key == SDLK_LEFT) target -= 5000;
                    if (event.key.key == SDLK_RIGHT) target += 5000;
                    if (event.key.key == SDLK_HOME) target = 0;
                    if (event.key.key == SDLK_END) target = duration;
                    target = std::clamp(target, std::int64_t{0}, duration);
                    start = (paused ? pause_at : now) - target;
                }
            }
            const std::int64_t current = std::clamp(
                (paused ? pause_at : now) - start, std::int64_t{0}, duration);
            const auto iterator = std::upper_bound(frames.begin(), frames.end(),
                current, [](std::int64_t ms, const Frame &f) { return ms < f.at_ms; });
            const std::size_t index = iterator == frames.begin() ? 0 :
                static_cast<std::size_t>(iterator - frames.begin() - 1);
            if (index != previous) {
                expand(frames[index], pixels);
                require(SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4));
                previous = index;
            }
            if (title_second != static_cast<std::uint32_t>(current / 1000)) {
                title_second = static_cast<std::uint32_t>(current / 1000);
                char title[120];
                std::snprintf(title, sizeof title,
                    "Gateway source-C intro %02u:%02u / %02u:%02u%s",
                    title_second / 60, title_second % 60,
                    static_cast<unsigned>(duration / 60000),
                    static_cast<unsigned>(duration / 1000 % 60),
                    paused ? " [paused]" : "");
                require(SDL_SetWindowTitle(window, title));
            }
            require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
            require(SDL_RenderClear(renderer));
            require(SDL_RenderTexture(renderer, texture, nullptr, nullptr));
            require(SDL_RenderPresent(renderer));
            SDL_Delay(16);
        }
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    return 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-intro-source-c-replay: " << error.what() << '\n';
    return 1;
}

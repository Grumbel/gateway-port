// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

// Run the original source-C EXE:1302 concurrently with an SDL3 presenter.
// Only the host clock, keyboard and video boundaries are native; the scene
// order, caption layout, animation and palette C remain in decomp/launcher.
#include "intro_source_c_full.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct LiveState {
    std::mutex frame_mutex;
    std::array<std::uint8_t, 320u * 200u> indices{};
    std::array<std::uint8_t, 256u * 3u> palette{};
    std::uint64_t generation = 0;

    std::mutex time_mutex;
    Clock::time_point origin = Clock::now();
    Clock::time_point paused_at{};
    Clock::duration paused_total{};
    bool paused = false;

    std::atomic<bool> escape_pending{false};
    std::atomic<bool> closing{false};
    std::atomic<bool> completed{false};
    std::atomic<int> result{0};

    std::mutex input_mutex;
    std::deque<std::uint16_t> keys;

    /* Accessed only on the source-C execution thread. */
    Clock::time_point last_publish{};
    std::array<std::uint8_t, 320u * 200u> last_indices{};
    std::array<std::uint8_t, 256u * 3u> last_palette{};
    bool have_last = false;
};

std::uint32_t elapsed_ms(LiveState &state) {
    std::lock_guard lock(state.time_mutex);
    const auto now = state.paused ? state.paused_at : Clock::now();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.origin - state.paused_total).count());
}

void toggle_pause(LiveState &state) {
    std::lock_guard lock(state.time_mutex);
    const auto now = Clock::now();
    if (state.paused) state.paused_total += now - state.paused_at;
    else state.paused_at = now;
    state.paused = !state.paused;
}

std::uint32_t sample_clock(void *userdata) {
    auto &state = *static_cast<LiveState *>(userdata);
    std::this_thread::sleep_for(1ms);
    return elapsed_ms(state);
}

void wait_ms(void *userdata, std::uint32_t duration) {
    auto &state = *static_cast<LiveState *>(userdata);
    const auto until = static_cast<std::uint64_t>(elapsed_ms(state)) + duration;
    while (!state.escape_pending.load() && !state.closing.load() &&
           elapsed_ms(state) < until)
        std::this_thread::sleep_for(5ms);
}

std::uint16_t key_pending(void *userdata) {
    auto &state = *static_cast<LiveState *>(userdata);
    if (state.escape_pending.load() || state.closing.load()) return 1;
    std::lock_guard lock(state.input_mutex);
    return !state.keys.empty();
}

std::uint16_t read_key(void *userdata) {
    auto &state = *static_cast<LiveState *>(userdata);
    if (state.escape_pending.exchange(false) || state.closing.load()) return 27;
    std::lock_guard lock(state.input_mutex);
    if (state.keys.empty()) return 0;
    const auto key = state.keys.front();
    state.keys.pop_front();
    return key;
}

void queue_key(LiveState &state, std::uint16_t key) {
    std::lock_guard lock(state.input_mutex);
    if (state.keys.size() < 32) state.keys.push_back(key);
}

void frame_ready(void *userdata, const std::uint8_t *indices,
                 const std::uint8_t dac[256][3]) {
    auto &state = *static_cast<LiveState *>(userdata);
    const auto now = Clock::now();
    // The original palette service can upload hundreds of palettes in a
    // fade. Present at most 60fps, copying a fully composed VGA snapshot.
    if (state.have_last && now - state.last_publish < 16ms) return;
    if (state.have_last &&
        std::memcmp(indices, state.last_indices.data(), state.last_indices.size()) == 0 &&
        std::memcmp(dac, state.last_palette.data(), state.last_palette.size()) == 0)
        return;
    std::memcpy(state.last_indices.data(), indices, state.last_indices.size());
    std::memcpy(state.last_palette.data(), dac, state.last_palette.size());
    state.have_last = true;
    state.last_publish = now;
    {
        std::lock_guard lock(state.frame_mutex);
        state.indices = state.last_indices;
        state.palette = state.last_palette;
        ++state.generation;
    }
}

void require(bool ok) {
    if (!ok) throw std::runtime_error(SDL_GetError());
}

void copy_to_argb(const std::array<std::uint8_t, 320u * 200u> &indices,
                  const std::array<std::uint8_t, 256u * 3u> &palette,
                  std::vector<std::uint32_t> &argb) {
    for (std::size_t i = 0; i < argb.size(); ++i) {
        const std::size_t palette_index = 3u * indices[i];
        const auto rgb8 = [](std::uint8_t s) {
            return static_cast<std::uint8_t>((s << 2) | (s >> 4));
        };
        argb[i] = 0xff000000u |
            (std::uint32_t(rgb8(palette[palette_index])) << 16) |
            (std::uint32_t(rgb8(palette[palette_index + 1])) << 8) |
            rgb8(palette[palette_index + 2]);
    }
}

} // namespace

int main(int argc, char **argv) try {
    if (argc != 3 || std::string_view(argv[1]) == "--help") {
        std::cout << "usage: gateway-intro-source-c-live guest-22s.bin DOS-DIR\n"
                     "Original live source-C intro (silent). Space/Return/letters: "
                     "original C keys, P: pause, F9: filter, Escape: interrupt.\n";
        return argc == 2 ? 0 : 2;
    }
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
        require(SDL_CreateWindowAndRenderer("Gateway live original C intro", 960, 720,
                                           SDL_WINDOW_RESIZABLE, &window, &renderer));
        require(SDL_SetRenderLogicalPresentation(renderer, 640, 480,
                                                SDL_LOGICAL_PRESENTATION_LETTERBOX));
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, 320, 200);
        require(texture != nullptr);
        require(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));

        LiveState state;
        GateSourceCFullHooks hooks{};
        hooks.userdata = &state;
        hooks.clock_ms = sample_clock;
        hooks.delay_ms = wait_ms;
        hooks.key_pending = key_pending;
        hooks.read_key = read_key;
        hooks.frame_ready = frame_ready;
        std::thread worker([&] {
            state.result.store(gate_source_c_full_run(argv[1], argv[2], &hooks));
            state.completed.store(true);
        });

        bool running = true, nearest = true;
        std::uint64_t previous_generation = 0;
        std::uint32_t previous_second = UINT32_MAX;
        bool previous_paused = false, previous_completed = false;
        std::array<std::uint8_t, 320u * 200u> indices{};
        std::array<std::uint8_t, 256u * 3u> palette{};
        std::vector<std::uint32_t> argb(320u * 200u, 0xff000000u);
        try {
            while (running) {
                SDL_Event event{};
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_EVENT_QUIT) running = false;
                    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
                    if (event.key.key == SDLK_ESCAPE) {
                        state.escape_pending.store(true);
                        running = false;
                    } else if (event.key.key == SDLK_P) {
                        toggle_pause(state);
                    } else if (event.key.key == SDLK_F9) {
                        nearest = !nearest;
                        require(SDL_SetTextureScaleMode(texture,
                            nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
                    } else if (event.key.key == SDLK_RETURN) {
                        queue_key(state, '\r');
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        queue_key(state, '\b');
                    } else if (event.key.key >= 32 && event.key.key <= 126) {
                        queue_key(state, static_cast<std::uint16_t>(event.key.key));
                    }
                }
                bool dirty = false;
                {
                    std::lock_guard lock(state.frame_mutex);
                    if (state.generation != previous_generation) {
                        indices = state.indices;
                        palette = state.palette;
                        previous_generation = state.generation;
                        dirty = true;
                    }
                }
                if (dirty) {
                    copy_to_argb(indices, palette, argb);
                    require(SDL_UpdateTexture(texture, nullptr, argb.data(), 320 * 4));
                }
                const auto second = elapsed_ms(state) / 1000;
                const bool completed = state.completed.load();
                if (second != previous_second || state.paused != previous_paused ||
                    completed != previous_completed) {
                    char title[140];
                    std::snprintf(title, sizeof title,
                        "Gateway live source-C intro %02u:%02u%s%s",
                        second / 60, second % 60,
                        completed ? " [finished]" : "",
                        state.paused ? " [paused]" : "");
                    require(SDL_SetWindowTitle(window, title));
                    previous_second = second;
                    previous_paused = state.paused;
                    previous_completed = completed;
                }
                require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
                require(SDL_RenderClear(renderer));
                require(SDL_RenderTexture(renderer, texture, nullptr, nullptr));
                require(SDL_RenderPresent(renderer));
                SDL_Delay(16);
            }
        } catch (...) {
            state.closing.store(true);
            state.escape_pending.store(true);
            if (worker.joinable()) worker.join();
            throw;
        }
        state.closing.store(true);
        state.escape_pending.store(true);
        worker.join();
        if (state.result.load() != 0)
            throw std::runtime_error("source-C intro failed to initialize");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    return 0;
} catch (const std::exception &error) {
    std::cerr << "gateway-intro-source-c-live: " << error.what() << '\n';
    return 1;
}

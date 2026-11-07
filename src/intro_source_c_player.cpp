// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "intro_source_c_captions.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
struct CaptionCloser {
    void operator()(GateSourceCCaptions *player) const {
        gate_source_c_captions_close(player);
    }
};

std::uint32_t diagnostic_color(std::uint8_t index) {
    // Diagnostic index mapping, NOT the original VGA DAC palette.
    const auto gray = static_cast<std::uint8_t>(index == 0 ? 0 :
                       index == 1 ? 188 : index == 2 ? 110 :
                       index == 255 ? 245 : index);
    return 0xff000000u | (std::uint32_t(gray) << 16) |
           (std::uint32_t(gray) << 8) | gray;
}
}

int main(int argc, char **argv) {
    if (argc != 3 || std::string_view(argv[1]) == "--help") {
        std::cout << "usage: gateway-intro-source-c-captions guest-22s.bin DOS-DIR\n"
                     "Original source-C caption renderer, eighteen text pages.\n"
                     "Space/Return: next; Backspace: previous; P: pause; "
                     "F9: filtering; Escape: quit.\n"
                     "This is NOT the complete intro: images, animation and "
                     "original palette are not connected yet.\n";
        return argc == 2 ? 0 : 2;
    }
    std::unique_ptr<GateSourceCCaptions, CaptionCloser> player(
        gate_source_c_captions_open(argv[1], argv[2]));
    if (!player) {
        std::cerr << "gateway-intro-source-c-captions: invalid guest RAM, "
                     "mode, or path\n";
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    bool ok = SDL_CreateWindowAndRenderer("Gateway source-C captions", 960, 720,
                                          SDL_WINDOW_RESIZABLE, &window, &renderer);
    if (ok)
        ok = SDL_SetRenderLogicalPresentation(renderer, 640, 480,
                                               SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (ok)
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, 320, 200);
    ok = ok && texture;
    if (!ok) {
        std::cerr << SDL_GetError() << '\n';
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    unsigned page = 0;
    bool running = true, paused = false, nearest = true;
    bool dirty = true;
    std::uint64_t deadline = 0;
    std::uint64_t paused_at = 0;
    std::vector<std::uint32_t> pixels(320u * 200u);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
                continue;
            switch (event.key.key) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_P:
                paused = !paused;
                if (paused)
                    paused_at = SDL_GetTicks();
                else
                    deadline += SDL_GetTicks() - paused_at;
                break;
            case SDLK_F9:
                nearest = !nearest;
                SDL_SetTextureScaleMode(texture, nearest ? SDL_SCALEMODE_NEAREST
                                                         : SDL_SCALEMODE_LINEAR);
                break;
            case SDLK_BACKSPACE:
                page = page == 0 ? 0 : page - 1;
                dirty = true;
                break;
            case SDLK_SPACE:
            case SDLK_RETURN:
                if (page + 1 < gate_source_c_captions_count()) ++page;
                dirty = true;
                break;
            default: break;
            }
        }
        if (dirty) {
            if (!gate_source_c_captions_render(player.get(), page)) {
                running = false;
                break;
            }
            const auto *indices = gate_source_c_captions_pixels(player.get());
            for (std::size_t i = 0; i < pixels.size(); ++i)
                pixels[i] = diagnostic_color(indices[i]);
            if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4)) {
                std::cerr << SDL_GetError() << '\n';
                running = false;
                break;
            }
            deadline = SDL_GetTicks() + gate_source_c_captions_duration(player.get());
            if (paused)
                paused_at = SDL_GetTicks();
            dirty = false;
        }
        if (!paused && SDL_GetTicks() >= deadline &&
            page + 1 < gate_source_c_captions_count()) {
            ++page;
            dirty = true;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

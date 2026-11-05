// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "story/intro_presentation.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool ok) {
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}
} // namespace

int main(int argc, char **argv) try {
    if (argc != 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "usage: gateway-intro DOS-DIR\n"
                     "Native introduction reconstruction (all eighteen narrative pages).\n"
                     "Space: next page; Escape: exit preview; N: toggle filtering.\n"
                     "No game launch or music playback yet.\n";
        return argc == 2 ? 0 : 2;
    }
    const gateway::IntroPresentation presentation(argv[1]);
    require(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    const auto cleanup = [&] {
        if (texture)
            SDL_DestroyTexture(texture);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    };
    try {
        require(SDL_CreateWindowAndRenderer("Gateway native intro preview", 960, 720,
                                           SDL_WINDOW_RESIZABLE, &window, &renderer));
        require(SDL_SetRenderLogicalPresentation(renderer, 640, 480,
                                                 SDL_LOGICAL_PRESENTATION_LETTERBOX));
        const auto start = SDL_GetTicks();
        std::optional<std::size_t> forced_page;
        bool nearest = true;
        bool running = true;
        int tex_width = 0, tex_height = 0;
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
                if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
                    continue;
                if (event.key.key == SDLK_ESCAPE)
                    running = false;
                if (event.key.key == SDLK_SPACE) {
                    const auto current = forced_page.value_or(
                        presentation.page_at(static_cast<std::uint32_t>(SDL_GetTicks() - start)));
                    const auto target = forced_page ? current + 1
                        : current + (SDL_GetTicks() - start >= 20128 ? 1 : 0);
                    forced_page = std::min(target, presentation.verified_pages() - 1);
                }
                if (event.key.key == SDLK_N) {
                    nearest = !nearest;
                    if (texture)
                        require(SDL_SetTextureScaleMode(texture,
                                   nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
                }
            }
            const auto time = static_cast<std::uint32_t>(SDL_GetTicks() - start);
            const auto frame = forced_page ? presentation.page_frame(*forced_page)
                                           : presentation.frame_at(time);
            if (!texture || frame.width != tex_width || frame.height != tex_height) {
                if (texture)
                    SDL_DestroyTexture(texture);
                tex_width = frame.width;
                tex_height = frame.height;
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, tex_width, tex_height);
                require(texture != nullptr);
                require(SDL_SetTextureScaleMode(texture,
                            nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
            }
            require(SDL_UpdateTexture(texture, nullptr, frame.argb.data(), tex_width * 4));
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
    std::cerr << "gateway-intro: " << error.what() << '\n';
    return 1;
}

// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    bool smoke = false;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke-test")
        smoke = true;
    else if (argc != 1) {
        std::cout << "Usage: gateway-host [--smoke-test]\n"
                     "SDL3 graphics/audio diagnostic; does not execute the game.\n";
        return std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_AudioStream *audio = nullptr;
    auto cleanup = [&] {
        if (audio)
            SDL_DestroyAudioStream(audio);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    };
    auto fail = [&] {
        std::cerr << SDL_GetError() << '\n';
        cleanup();
        return 1;
    };
    if (!SDL_CreateWindowAndRenderer("Gateway host diagnostic — Escape to quit", 640, 400, 0,
                                     &window, &renderer))
        return fail();
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio)
        return fail();
    std::array<float, 4800> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.08f * std::sin(2.0f * 3.14159265f * 440.0f * float(i) / 48000.0f);
    if (!SDL_PutAudioStreamData(audio, samples.data(), int(sizeof(samples))) ||
        !SDL_ResumeAudioStreamDevice(audio))
        return fail();
    const auto start = SDL_GetTicks();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
                running = false;
        }
        if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) || !SDL_RenderClear(renderer))
            return fail();
        for (int i = 0; i < 16; ++i) {
            SDL_FRect rect{float(i * 40), 0, 40, 400};
            if (!SDL_SetRenderDrawColor(renderer, (i & 4) ? 170 : 0, (i & 2) ? 170 : 0,
                                        (i & 1) ? 170 : 0, 255) ||
                !SDL_RenderFillRect(renderer, &rect))
                return fail();
        }
        if (!SDL_RenderPresent(renderer))
            return fail();
        if (smoke && SDL_GetTicks() - start >= 200)
            running = false;
        SDL_Delay(16);
    }
    cleanup();
    return 0;
}

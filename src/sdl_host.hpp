#pragma once
#include "mouse.hpp"
#include "vga.hpp"
#include <cstdint>
#include <deque>
#include <span>
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_AudioStream;
namespace gateway {
struct DisplayAspect {
    unsigned width = 4, height = 3; // Zero width selects native square pixels.
};
class SdlHost {
  public:
    explicit SdlHost(DisplayAspect aspect = {});
    ~SdlHost();
    SdlHost(const SdlHost &) = delete;
    SdlHost &operator=(const SdlHost &) = delete;
    bool poll(std::deque<std::uint16_t> &keys, Mouse &mouse, const Vga &vga);
    void present(const Vga &vga, const Mouse &mouse, std::span<const std::int16_t> samples);

  private:
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_AudioStream *audio_ = nullptr;
    int width_ = 0, height_ = 0;
    int presentation_height_ = 0;
    DisplayAspect aspect_;
    bool fullscreen_ = false;
    void close();
};
} // namespace gateway

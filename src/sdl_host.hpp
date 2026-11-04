// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#pragma once
#include "debug_ui.hpp"
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
enum class UiLayoutMode { original, widescreen };
class SdlHost {
  public:
    explicit SdlHost(DisplayAspect aspect = {}, UiLayoutMode layout = UiLayoutMode::original);
    ~SdlHost();
    SdlHost(const SdlHost &) = delete;
    SdlHost &operator=(const SdlHost &) = delete;
    bool poll(std::deque<std::uint16_t> &keys, Mouse &mouse, const Vga &vga,
              DebugActions *actions = nullptr);
    void present(const Vga &vga, const Mouse &mouse, std::span<const std::int16_t> samples,
                 const DebugOverlayData &debug = {});
    bool debug_open() const { return debug_open_; }
    bool hitboxes_visible() const { return hitboxes_visible_; }
    bool widescreen_ui() const { return layout_ == UiLayoutMode::widescreen; }
    DebugMenuPosition debug_position() const { return debug_position_; }

  private:
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_AudioStream *audio_ = nullptr;
    int width_ = 0, height_ = 0;
    int presentation_height_ = 0;
    DisplayAspect aspect_;
    UiLayoutMode layout_;
    bool fullscreen_ = false;
    bool debug_open_ = false, hitboxes_visible_ = false;
    bool pointer_over_panel_ = false;
    DebugMenuPosition debug_position_;
    bool debug_dragging_ = false;
    int debug_drag_offset_x_ = 0, debug_drag_offset_y_ = 0;
    unsigned debug_row_ = 0;
    void debug_activate(unsigned row, DebugActions *actions);
    void close();
};
} // namespace gateway

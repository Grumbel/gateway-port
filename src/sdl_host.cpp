// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "sdl_host.hpp"
#include "mouse_cursor.hpp"
#include "opl.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
namespace gateway {
namespace {
void checked(bool ok) {
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}
std::uint16_t bios_key(const SDL_KeyboardEvent &event) {
    unsigned ascii = 0, scan = 0;
    const auto key = event.key;
    if (key >= SDLK_A && key <= SDLK_Z) {
        constexpr std::uint8_t codes[]{0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                       0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                       0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
        scan = codes[key - SDLK_A];
        const bool upper = bool(event.mod & SDL_KMOD_SHIFT) != bool(event.mod & SDL_KMOD_CAPS);
        ascii = (upper ? 'A' : 'a') + key - SDLK_A;
        if (event.mod & SDL_KMOD_CTRL)
            ascii = key - SDLK_A + 1;
    } else if (key >= SDLK_1 && key <= SDLK_9) {
        scan = 2 + key - SDLK_1;
        ascii = event.mod & SDL_KMOD_SHIFT ? "!@#$%^&*("[key - SDLK_1] : '1' + key - SDLK_1;
    } else {
        switch (key) {
        case SDLK_0:
            scan = 0x0b;
            ascii = event.mod & SDL_KMOD_SHIFT ? ')' : '0';
            break;
        case SDLK_ESCAPE:
            scan = 1;
            ascii = 27;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            scan = 0x1c;
            ascii = 13;
            break;
        case SDLK_SPACE:
            scan = 0x39;
            ascii = 32;
            break;
        case SDLK_BACKSPACE:
            scan = 0x0e;
            ascii = 8;
            break;
        case SDLK_TAB:
            scan = 0x0f;
            ascii = 9;
            break;
        case SDLK_UP:
            scan = 0x48;
            break;
        case SDLK_DOWN:
            scan = 0x50;
            break;
        case SDLK_LEFT:
            scan = 0x4b;
            break;
        case SDLK_RIGHT:
            scan = 0x4d;
            break;
        case SDLK_HOME:
            scan = 0x47;
            break;
        case SDLK_END:
            scan = 0x4f;
            break;
        case SDLK_PAGEUP:
            scan = 0x49;
            break;
        case SDLK_PAGEDOWN:
            scan = 0x51;
            break;
        case SDLK_DELETE:
            scan = 0x53;
            break;
        case SDLK_INSERT:
            scan = 0x52;
            break;
        case SDLK_PERIOD:
            scan = 0x34;
            ascii = event.mod & SDL_KMOD_SHIFT ? '>' : '.';
            break;
        case SDLK_COMMA:
            scan = 0x33;
            ascii = event.mod & SDL_KMOD_SHIFT ? '<' : ',';
            break;
        case SDLK_SLASH:
            scan = 0x35;
            ascii = event.mod & SDL_KMOD_SHIFT ? '?' : '/';
            break;
        case SDLK_MINUS:
            scan = 0x0c;
            ascii = event.mod & SDL_KMOD_SHIFT ? '_' : '-';
            break;
        case SDLK_EQUALS:
            scan = 0x0d;
            ascii = event.mod & SDL_KMOD_SHIFT ? '+' : '=';
            break;
        case SDLK_SEMICOLON:
            scan = 0x27;
            ascii = event.mod & SDL_KMOD_SHIFT ? ':' : ';';
            break;
        case SDLK_APOSTROPHE:
            scan = 0x28;
            ascii = event.mod & SDL_KMOD_SHIFT ? '"' : '\'';
            break;
        default:
            if (key >= SDLK_F1 && key <= SDLK_F10)
                scan = 0x3b + key - SDLK_F1;
            break;
        }
    }
    return (scan << 8) | ascii;
}
} // namespace
SdlHost::SdlHost(DisplayAspect aspect) : aspect_(aspect) {
    try {
        checked(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
        checked(SDL_CreateWindowAndRenderer("Gateway — original-code Linux runtime", 960, 720,
                                            SDL_WINDOW_RESIZABLE, &window_, &renderer_));
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, Opl::sample_rate};
        audio_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        checked(audio_ != nullptr);
        checked(SDL_ResumeAudioStreamDevice(audio_));
    } catch (...) {
        close();
        throw;
    }
}
SdlHost::~SdlHost() { close(); }
void SdlHost::debug_activate(unsigned row, DebugActions *actions) {
    switch (row) {
    case 0:
        hitboxes_visible_ = !hitboxes_visible_;
        break;
    case 1:
        if (actions)
            actions->toggle_auto_follow = !actions->toggle_auto_follow;
        break;
    case 2:
    case 3:
        if (actions)
            actions->scene_step += row == 2 ? -1 : 1;
        break;
    case 4:
        if (actions)
            actions->dump_state = true;
        break;
    case 5:
        debug_open_ = false;
        break;
    }
}
void SdlHost::close() {
    if (audio_)
        SDL_DestroyAudioStream(audio_);
    if (texture_)
        SDL_DestroyTexture(texture_);
    if (renderer_)
        SDL_DestroyRenderer(renderer_);
    if (window_)
        SDL_DestroyWindow(window_);
    audio_ = nullptr;
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    SDL_Quit();
}
bool SdlHost::poll(std::deque<std::uint16_t> &keys, Mouse &mouse, const Vga &vga,
                   DebugActions *actions) {
    const int presentation_height = presentation_height_ ? presentation_height_ : vga.height();
    if (debug_open_ && !SDL_CursorVisible())
        checked(SDL_ShowCursor());
    else if (!debug_open_ && SDL_CursorVisible())
        checked(SDL_HideCursor());
    if (!debug_open_ && mouse.warp_pending && texture_) {
        float x, y;
        const int rows = vga.text_mode() ? 200 : vga.height();
        checked(SDL_RenderCoordinatesToWindow(renderer_, float(mouse.x) * vga.width() / 640,
                                              float(mouse.y) * presentation_height_ / rows,
                                              &x, &y));
        SDL_WarpMouseInWindow(window_, x, y);
        mouse.warp_pending = false;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
            return false;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F10) {
            if (!event.key.repeat) {
                debug_open_ = !debug_open_;
                debug_dragging_ = false;
                if (debug_open_)
                    for (unsigned i = 0; i < 3; ++i)
                        mouse.button(i, false);
            }
            continue; // F10 is a host command, never a guest BIOS keystroke.
        }
        if (debug_open_ && (event.type == SDL_EVENT_MOUSE_MOTION ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
            checked(SDL_ConvertEventToRenderCoordinates(renderer_, &event));
            if (event.type == SDL_EVENT_MOUSE_MOTION && debug_dragging_) {
                const auto bounds = debug_menu_bounds(vga.width(), vga.height());
                debug_position_.x = std::clamp(int(event.motion.x) - debug_drag_offset_x_, 0,
                                                std::max(0, vga.width() - bounds.width));
                debug_position_.y = std::clamp(int(event.motion.y) - debug_drag_offset_y_, 0,
                                                std::max(0, vga.height() - bounds.height));
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_LEFT)
                debug_dragging_ = false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                const int x = int(event.button.x), y = int(event.button.y);
                if (debug_menu_title_at(x, y, vga.width(), vga.height(), debug_position_)) {
                    const auto bounds = debug_menu_bounds(vga.width(), vga.height(),
                                                          debug_position_);
                    debug_dragging_ = true;
                    debug_drag_offset_x_ = x - bounds.x;
                    debug_drag_offset_y_ = y - bounds.y;
                } else {
                    const auto row = debug_menu_row_at(x, y, vga.width(), vga.height(),
                                                       debug_position_);
                    if (row >= 0) {
                        debug_row_ = unsigned(row);
                        debug_activate(debug_row_, actions);
                    }
                }
            }
            continue; // Never pass debug-menu clicks to the DOS mouse driver.
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            checked(SDL_ConvertEventToRenderCoordinates(renderer_, &event));
            const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
            const float x = motion ? event.motion.x : event.button.x;
            const float y = motion ? event.motion.y : event.button.y;
            const int rows = vga.text_mode() ? 200 : vga.height();
            mouse.move(int(x * 640 / vga.width()), int(y * rows / presentation_height));
            const bool outside = x < 0 || y < 0 || x >= vga.width() ||
                                 y >= presentation_height;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && outside)
                continue;
            if (!motion) {
                const auto button = event.button.button;
                const unsigned index = button == SDL_BUTTON_LEFT    ? 0
                                       : button == SDL_BUTTON_RIGHT ? 1
                                                                    : 2;
                if (button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT ||
                    button == SDL_BUTTON_MIDDLE)
                    mouse.button(index, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            debug_dragging_ = false;
            for (unsigned i = 0; i < 3; ++i)
                mouse.button(i, false);
        }
        if (event.type == SDL_EVENT_KEY_DOWN &&
            (event.key.key == SDLK_F11 ||
             ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
              (event.key.mod & SDL_KMOD_ALT)))) {
            if (!event.key.repeat) {
                checked(SDL_SetWindowFullscreen(window_, !fullscreen_));
                fullscreen_ = !fullscreen_;
            }
            continue;
        }
        if (debug_open_ && event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_ESCAPE) {
                debug_open_ = false;
                debug_dragging_ = false;
            } else if (event.key.key == SDLK_UP) {
                debug_row_ = (debug_row_ + debug_menu_rows - 1) % debug_menu_rows;
            } else if (event.key.key == SDLK_DOWN) {
                debug_row_ = (debug_row_ + 1) % debug_menu_rows;
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ||
                       event.key.key == SDLK_SPACE) {
                if (!event.key.repeat)
                    debug_activate(debug_row_, actions);
            } else if (event.key.key == SDLK_H) {
                if (!event.key.repeat)
                    debug_activate(0, actions);
            } else if (event.key.key == SDLK_A) {
                if (!event.key.repeat)
                    debug_activate(1, actions);
            } else if (event.key.key == SDLK_D) {
                if (!event.key.repeat)
                    debug_activate(4, actions);
            } else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_PAGEUP) {
                if (!event.key.repeat)
                    debug_activate(2, actions);
            } else if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_PAGEDOWN) {
                if (!event.key.repeat)
                    debug_activate(3, actions);
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && keys.size() < 16) {
            const auto key = bios_key(event.key);
            if (key)
                keys.push_back(key);
        }
    }
    return true;
}
void SdlHost::present(const Vga &vga, const Mouse &mouse,
                      std::span<const std::int16_t> samples, const DebugOverlayData &debug) {
    if (!texture_ || width_ != vga.width() || height_ != vga.height()) {
        if (texture_)
            SDL_DestroyTexture(texture_);
        width_ = vga.width();
        height_ = vga.height();
        presentation_height_ = aspect_.width
                                   ? std::max(1, int((std::uint64_t(width_) * aspect_.height +
                                                       aspect_.width / 2) / aspect_.width))
                                   : height_;
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, width_, height_);
        checked(texture_ != nullptr);
        checked(SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST));
        checked(SDL_SetRenderLogicalPresentation(renderer_, width_, presentation_height_,
                                                 SDL_LOGICAL_PRESENTATION_LETTERBOX));
    }
    auto pixels = vga.pixels();
    if (debug_open_ || hitboxes_visible_)
        draw_debug_overlay(pixels, width_, height_, mouse, debug, debug_open_, hitboxes_visible_,
                           debug_row_, debug_position_);
    draw_mouse_cursor(pixels, width_, height_, mouse, vga.text_mode());
    checked(SDL_UpdateTexture(texture_, nullptr, pixels.data(), width_ * 4));
    checked(SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255));
    checked(SDL_RenderClear(renderer_));
    checked(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
    checked(SDL_RenderPresent(renderer_));
    const auto queued = SDL_GetAudioStreamQueued(audio_);
    checked(queued >= 0);
    if (queued < int(Opl::sample_rate / 4 * 4) && !samples.empty())
        checked(SDL_PutAudioStreamData(audio_, samples.data(), int(samples.size_bytes())));
}
} // namespace gateway

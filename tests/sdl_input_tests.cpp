// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "sdl_host.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        // Quit and window-close are terminal requests. Exercise them in
        // separate processes: the host's caller stops polling after either
        // one returns false, and SDL need not preserve its event queue for
        // continued use of a host that was already asked to terminate.
        if (argc == 2 && std::string_view(argv[1]) == "--quit-only") {
            gateway::SdlHost host;
            gateway::Vga vga;
            vga.set_mode(0x12);
            gateway::Mouse mouse;
            std::deque<std::uint16_t> keys;
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            event.common.timestamp = SDL_GetTicksNS();
            require(SDL_PushEvent(&event), "push quit request");
            require(!host.poll(keys, mouse, vga), "SDL quit must stop emulation");
            std::cout << "SDL global quit stopped emulation\n";
            return EXIT_SUCCESS;
        }
        require(argc == 1, "usage: sdl-input-tests [--quit-only]");
        gateway::SdlHost host;
        const char *signal_hint = SDL_GetHint(SDL_HINT_NO_SIGNAL_HANDLERS);
        require(signal_hint && std::string_view(signal_hint) == "1",
                "Ctrl-C must retain its default OS signal handler");
        gateway::Vga vga;
        vga.set_mode(0x12);
        gateway::Mouse mouse;
        host.present(vga, mouse, {});
        std::deque<std::uint16_t> keys;
        host.poll(keys, mouse, vga);
        require(!SDL_CursorVisible(), "hidden DOS cursor");
        mouse.visibility = 0;
        host.poll(keys, mouse, vga);
        require(!SDL_CursorVisible(), "native cursor should stay hidden");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "runtime window");
        const auto window_id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        auto toggle = [&](SDL_Keycode key, SDL_Keymod modifiers, bool repeat, bool expected) {
            SDL_Event shortcut{};
            shortcut.type = SDL_EVENT_KEY_DOWN;
            shortcut.key.windowID = window_id;
            shortcut.key.key = key;
            shortcut.key.mod = modifiers;
            shortcut.key.repeat = repeat;
            require(SDL_PushEvent(&shortcut), "push fullscreen shortcut");
            host.poll(keys, mouse, vga);
            auto window = SDL_GetWindowFromID(window_id);
            require(SDL_SyncWindow(window), "sync fullscreen request");
            require(bool(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == expected,
                    "fullscreen shortcut state");
            require(keys.empty(), "fullscreen shortcut leaked into guest input");
        };
        toggle(SDLK_F11, SDL_KMOD_NONE, false, true);
        toggle(SDLK_F11, SDL_KMOD_NONE, true, true);
        toggle(SDLK_RETURN, SDL_KMOD_LALT, false, false);
        toggle(SDLK_KP_ENTER, SDL_KMOD_RALT, false, true);
        toggle(SDLK_KP_ENTER, SDL_KMOD_RALT, true, true);
        toggle(SDLK_F11, SDL_KMOD_NONE, false, false);
        auto send_key = [&](SDL_Keycode key, bool repeat = false) {
            SDL_Event shortcut{};
            shortcut.type = SDL_EVENT_KEY_DOWN;
            shortcut.key.windowID = window_id;
            shortcut.key.key = key;
            shortcut.key.repeat = repeat;
            require(SDL_PushEvent(&shortcut), "push debug key");
        };
        gateway::DebugActions actions;
        send_key(SDLK_F10);
        require(host.poll(keys, mouse, vga, &actions) && host.debug_open(),
                "F10 opens native debug menu");
        require(keys.empty(), "F10 key leaked into guest input");
        host.poll(keys, mouse, vga, &actions);
        require(SDL_CursorVisible(), "menu uses native pointer");
        SDL_Event drag{};
        drag.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        drag.button.windowID = window_id;
        drag.button.button = SDL_BUTTON_LEFT;
        drag.button.x = 750; // 640x480 logical (500,243): title bar in bottom-right
        drag.button.y = 365;
        require(SDL_PushEvent(&drag), "press debug title bar");
        drag = {};
        drag.type = SDL_EVENT_MOUSE_MOTION;
        drag.motion.windowID = window_id;
        drag.motion.x = 400;
        drag.motion.y = 280;
        require(SDL_PushEvent(&drag), "drag debug window");
        drag = {};
        drag.type = SDL_EVENT_MOUSE_BUTTON_UP;
        drag.button.windowID = window_id;
        drag.button.button = SDL_BUTTON_LEFT;
        drag.button.x = 400;
        drag.button.y = 280;
        require(SDL_PushEvent(&drag), "release debug title bar");
        host.poll(keys, mouse, vga, &actions);
        require(host.debug_position().x >= 0 && host.debug_position().y >= 0 &&
                    host.debug_position().y < 232,
                "dragging the title bar should move the window upward");
        require(mouse.history[0].presses == 0 && keys.empty(),
                "debug window drag leaked guest mouse or keyboard input");
        send_key(SDLK_F10, true);
        host.poll(keys, mouse, vga, &actions);
        require(host.debug_open(), "F10 repeat closed menu");
        actions = {};
        send_key(SDLK_H);
        send_key(SDLK_R);
        send_key(SDLK_D);
        send_key(SDLK_RIGHT);
        send_key(SDLK_A);
        host.poll(keys, mouse, vga, &actions);
        require(host.hitboxes_visible() && host.raw_hitboxes() && actions.dump_state &&
                    actions.scene_step == 1 &&
                    actions.toggle_auto_follow,
                "debug actions did not reach host request queue");
        require(keys.empty(), "modal debugger leaked control keys to DOS");
        send_key(SDLK_ESCAPE);
        host.poll(keys, mouse, vga, &actions);
        host.poll(keys, mouse, vga, &actions);
        require(!host.debug_open() && !SDL_CursorVisible(), "Escape closes debug menu");
        require(keys.empty(), "debug-menu Escape leaked to DOS");
        send_key(SDLK_A);
        host.poll(keys, mouse, vga, &actions);
        require(keys.size() == 1 && keys.front() == 0x1e61,
                "ordinary guest keyboard input stopped after debug menu");
        keys.clear();
        host.present(vga, mouse, {});
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id;
        event.motion.x = 480;
        event.motion.y = 360;
        require(SDL_PushEvent(&event), "push motion");
        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = window_id;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = 480;
        event.button.y = 360;
        require(SDL_PushEvent(&event), "push press");
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        require(SDL_PushEvent(&event), "push release");
        require(host.poll(keys, mouse, vga), "poll");
        require(mouse.x == 320 && mouse.y == 240, "window-to-guest scaling");
        require(mouse.buttons == 0 && mouse.history[0].presses == 1 &&
                    mouse.history[0].releases == 1,
                "short click retains press/release history");
        require(mouse.history[0].press_x == 320 && mouse.history[0].press_y == 240,
                "press coordinates");
        require(SDL_SetWindowSize(SDL_GetWindowFromID(window_id), 1280, 720), "resize");
        SDL_PumpEvents();
        host.present(vga, mouse, {});
        host.poll(keys, mouse, vga);
        event = {};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id;
        event.motion.x = 310;
        event.motion.y = 150;
        require(SDL_PushEvent(&event), "push letterboxed motion");
        host.poll(keys, mouse, vga);
        require(mouse.x == 100 && mouse.y == 100, "letterbox offset and scaling");
        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = window_id;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = 20;
        event.button.y = 150;
        require(SDL_PushEvent(&event), "push click in black bar");
        host.poll(keys, mouse, vga);
        require(mouse.buttons == 0, "black-bar click reached guest UI");
        mouse.button(1, true);
        event = {};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        require(SDL_PushEvent(&event), "push focus loss");
        host.poll(keys, mouse, vga);
        require(mouse.buttons == 0 && mouse.history[1].releases == 1,
                "focus loss releases held buttons");
        mouse.motion_x = mouse.motion_y = 0;
        mouse.min_x = 10;
        mouse.max_x = 100;
        mouse.position(500, 0);
        require(mouse.x == 100, "position clamps to guest range");
        mouse.move(-100, 20);
        require(mouse.x == 10 && mouse.motion_x == -90 && mouse.motion_y == 20,
                "movement counters and range clamp");
        // In 640x350 mode the default 4:3 presentation stretches to 640x480,
        // but coordinates still map back to the actual 350-line guest image.
        mouse.min_x = 0;
        mouse.max_x = 639;
        mouse.min_y = 0;
        mouse.max_y = 349;
        vga.set_mode(0x10);
        host.present(vga, mouse, {});
        event = {};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id;
        event.motion.x = 640;
        event.motion.y = 360;
        require(SDL_PushEvent(&event), "push aspect-ratio motion");
        host.poll(keys, mouse, vga);
        require(mouse.x == 320 && mouse.y == 175, "4:3 aspect input mapping");
        // A synthetic window-close request must work even without SDL's
        // optional automatic SDL_EVENT_QUIT on last-window close.
        SDL_FlushEvent(SDL_EVENT_QUIT);
        event = {};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.common.timestamp = SDL_GetTicksNS();
        event.window.windowID = window_id;
        require(SDL_PushEvent(&event), "push window close request");
        require(!host.poll(keys, mouse, vga), "window close must stop emulation");
        std::cout << "SDL mouse coordinates, click history, and focus checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

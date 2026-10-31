#include "sdl_host.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        gateway::SdlHost host;
        gateway::Vga vga;
        vga.set_mode(0x12);
        host.present(vga, {});
        gateway::Mouse mouse;
        std::deque<std::uint16_t> keys;
        host.poll(keys, mouse, vga);
        require(!SDL_CursorVisible(), "hidden DOS cursor");
        mouse.visibility = 0;
        host.poll(keys, mouse, vga);
        require(SDL_CursorVisible(), "visible DOS cursor");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "runtime window");
        const auto window_id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
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
        host.present(vga, {});
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
        std::cout << "SDL mouse coordinates, click history, and focus checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

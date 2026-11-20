// SDL3 adapter of the in-game menu: the window's events to menu events (menu.h), as ui/menu_win.cpp does for Win32.
// Window thread (gfx_window_sdl.c's event loop, after input_sdl_event).
//
// F1 opens and closes the menu, Esc opens it (with the menu open, Esc goes to the menu, which closes). Esc also releases
// the cursor (input_sdl.c sees the event first; closing the menu gives the capture back). While the menu is open, its
// keys, the mouse and focus changes go to it; the game reads the keyboard through input_sdl.c and gets nothing while
// the menu is open (input_menu). Mouse positions come in window units and go to the menu in pixels.
//
// While the software keyboard overlay is open (swkbd_sdl.c), the same events go there instead, including TEXT_INPUT
// for typed characters and the on-screen keyboard.
#include <SDL3/SDL.h>

#include "imgui.h"

extern "C" {
#include "input.h"
#include "input_sdl.h"
#include "swkbd.h"
#include "ui/menu.h"
}

static ImGuiKey key_of(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return ImGuiKey_Enter;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;   // clears a cell of the remapping rows
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_Q: return ImGuiKey_Q;   // previous tab
    case SDL_SCANCODE_E: return ImGuiKey_E;   // next tab
    default: return ImGuiKey_None;
    }
}

static int ui_open(void) { return menu_is_open() || swkbd_ui_active(); }

extern "C" void menu_sdl_event(const SDL_Event *e) {
    float d = (float)gfx_window_sdl_density();
    int swkbd = swkbd_ui_active();
    switch (e->type) {
    case SDL_EVENT_TEXT_INPUT:
        if (swkbd) swkbd_ui_event_text(e->text.text);
        return;
    case SDL_EVENT_KEY_DOWN:
        // Esc with the menu open (opened by F1 too) is not a toggle here: it goes to the menu, which closes itself
        if (!e->key.repeat && !swkbd && (e->key.scancode == SDL_SCANCODE_F1 ||
                               (e->key.scancode == SDL_SCANCODE_ESCAPE && !menu_is_open()))) {
            menu_toggle();
            return;
        }
        if (e->key.scancode == SDL_SCANCODE_F10) {
            if (!e->key.repeat) menu_perf_cycle();
            return;
        }
        if (swkbd && e->key.scancode == SDL_SCANCODE_ESCAPE && !e->key.repeat) {
            swkbd_ui_finish(0);
            return;
        }
        [[fallthrough]];
    case SDL_EVENT_KEY_UP:
        if (!ui_open()) return;
        if (ImGuiKey k = key_of(e->key.scancode)) {
            if (swkbd) swkbd_ui_event_key(k, e->type == SDL_EVENT_KEY_DOWN);
            else menu_event_key(k, e->type == SDL_EVENT_KEY_DOWN);
        }
        return;
    case SDL_EVENT_MOUSE_MOTION:
        if (swkbd) swkbd_ui_event_mouse_pos(e->motion.x * d, e->motion.y * d);
        else if (menu_is_open()) menu_event_mouse_pos(e->motion.x * d, e->motion.y * d);
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!ui_open()) return;
        int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1
              : e->button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        if (b < 0) return;
        float x = e->button.x * d, y = e->button.y * d;
        int down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (swkbd) {
            swkbd_ui_event_mouse_pos(x, y);
            swkbd_ui_event_mouse_button(b, down);
        } else {
            menu_event_mouse_pos(x, y);
            menu_event_mouse_button(b, down);
        }
        return;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (menu_is_open() && !swkbd) menu_event_wheel(e->wheel.x, e->wheel.y);
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        menu_event_focus(e->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        return;
    }
}

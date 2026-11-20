// ImGui modal for nn::swkbd::ShowKeyboard on SDL hosts (pairs with swkbd_sdl.c). Present path only.
#include <stdio.h>
#include <string.h>

#include "imgui.h"

extern "C" {
#include "host.h"
#include "input.h"
#include "input_map.h"
#include "platform/platform.h"
#include "swkbd.h"
#include "ui_text.h"
}

enum { EV_KEY, EV_POS, EV_BUTTON, EV_TEXT, EV_MAX = 64 };
struct Ev {
    int type, a, b;
    float x, y;
    char text[32];
};
static PlatMutex ev_lock = PLAT_MUTEX_INIT;
static Ev ev_q[EV_MAX];
static int ev_n;
static uint64_t pad_prev;
static int focus_once;
static int armed_ok;   // A / Enter seen released since open

static void push(Ev e) {
    if (!swkbd_ui_active()) return;
    plat_mutex_lock(&ev_lock);
    if (e.type == EV_POS && ev_n && ev_q[ev_n - 1].type == EV_POS) ev_q[ev_n - 1] = e;
    else if (ev_n < EV_MAX) ev_q[ev_n++] = e;
    plat_mutex_unlock(&ev_lock);
}

extern "C" void swkbd_ui_event_key(int imgui_key, int down) { push({EV_KEY, imgui_key, down, 0, 0, {0}}); }
extern "C" void swkbd_ui_event_mouse_pos(float x, float y) { push({EV_POS, 0, 0, x, y, {0}}); }
extern "C" void swkbd_ui_event_mouse_button(int button, int down) { push({EV_BUTTON, button, down, 0, 0, {0}}); }
extern "C" void swkbd_ui_event_text(const char *utf8) {
    Ev e = {EV_TEXT, 0, 0, 0, 0, {0}};
    if (utf8) {
        size_t n = 0;
        while (utf8[n] && n + 1 < sizeof e.text) n++;
        memcpy(e.text, utf8, n);
        e.text[n] = 0;
    }
    push(e);
}

static void drain(ImGuiIO &io) {
    Ev q[EV_MAX];
    plat_mutex_lock(&ev_lock);
    int n = ev_n;
    for (int i = 0; i < n; i++) q[i] = ev_q[i];
    ev_n = 0;
    plat_mutex_unlock(&ev_lock);
    for (int i = 0; i < n; i++) {
        const Ev &e = q[i];
        switch (e.type) {
        case EV_KEY: io.AddKeyEvent((ImGuiKey)e.a, e.b != 0); break;
        case EV_POS: io.AddMousePosEvent(e.x, e.y); break;
        case EV_BUTTON: io.AddMouseButtonEvent(e.a, e.b != 0); break;
        case EV_TEXT: io.AddInputCharactersUTF8(e.text); break;
        }
    }
}

static int filter_char(ImGuiInputTextCallbackData *data) {
    const SwkbdRequest *req = (const SwkbdRequest *)data->UserData;
    if (data->EventFlag != ImGuiInputTextFlags_CallbackCharFilter) return 0;
    unsigned c = data->EventChar;
    if (c > 0xffff || !swkbd_allowed((uint16_t)c, req->disabled, req->numeric)) return 1;
    return 0;
}

extern "C" void swkbd_ui_layout(float scale) {
    if (!swkbd_ui_active()) return;
    const SwkbdRequest *req = nullptr;
    char *buf = nullptr;
    size_t cap = 0;
    int test = 0;
    swkbd_ui_get_request(&req, &buf, &cap, &test);
    if (!req || !buf) return;

    ImGuiIO &io = ImGui::GetIO();
    drain(io);

    // Gamepad: A confirms, B cancels (after a release since open, as on Win32).
    uint64_t b = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    input_poll_ui(&b, &lx, &ly, &rx, &ry);
    uint64_t pressed = b & ~pad_prev;
    pad_prev = b;
    if (!armed_ok) {
        if (!(b & (1ull << B_A | 1ull << B_PLUS | 1ull << B_B))) armed_ok = 1;
    } else {
        if (pressed & (1ull << B_A | 1ull << B_PLUS)) {
            swkbd_ui_finish(1);
            focus_once = armed_ok = 0;
            pad_prev = 0;
            return;
        }
        if (pressed & (1ull << B_B)) {
            swkbd_ui_finish(0);
            focus_once = armed_ok = 0;
            pad_prev = 0;
            return;
        }
    }

    if (test) {
        swkbd_ui_finish(test > 0);
        focus_once = armed_ok = 0;
        pad_prev = 0;
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.42f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f * scale, 0), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin(ui_text(UI_SWKBD_TITLE), nullptr, flags)) {
        ImGui::End();
        return;
    }

    char prompt[0x102 * 3];
    if (req->prompt[0]) {
        size_t n = 0;
        while (n < 0x100 && req->prompt[n]) n++;
        swkbd_utf16_to_utf8(req->prompt, n, prompt, sizeof prompt);
    } else {
        snprintf(prompt, sizeof prompt, "%s", ui_text(UI_SWKBD_PROMPT));
    }
    ImGui::TextWrapped("%s", prompt);
    ImGui::Spacing();

    ImGuiInputTextFlags tflags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter;
    if (req->password) tflags |= ImGuiInputTextFlags_Password;
    if (req->numeric) tflags |= ImGuiInputTextFlags_CharsDecimal;

    if (!focus_once) {
        ImGui::SetKeyboardFocusHere();
        focus_once = 1;
    }
    bool enter = ImGui::InputText("##swkbd", buf, (int)cap, tflags, filter_char, (void *)req);
    ImGui::TextDisabled("%s", ui_text(UI_SWKBD_PAD_HINT));
    ImGui::Spacing();

    float bw = 120.0f * scale;
    if (ImGui::Button(ui_text(UI_SWKBD_OK), ImVec2(bw, 0)) || enter) {
        swkbd_ui_finish(1);
        focus_once = armed_ok = 0;
        pad_prev = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button(ui_text(UI_SWKBD_CANCEL), ImVec2(bw, 0))) {
        swkbd_ui_finish(0);
        focus_once = armed_ok = 0;
        pad_prev = 0;
    }
    ImGui::End();
}

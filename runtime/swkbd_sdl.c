// The software keyboard's front end for SDL windows (swkbd.h). Draws an ImGui modal on the present path
// (ui/swkbd_overlay.cpp) and blocks the guest thread until the player confirms or cancels — same contract as
// swkbd_win.c. AC_SWKBD_TEST=ok|cancel auto-answers after the overlay opens (probes).
//
// AC_SWKBD_OSK=0 skips SDL_StartTextInput (system on-screen keyboard). Default is on for handhelds; the start is
// asynchronous so it cannot deadlock the Wayland compositor against the window thread.
#include <stdatomic.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "input_sdl.h"
#include "platform/platform.h"
#include "swkbd.h"

enum { SWKBD_IDLE = 0, SWKBD_OPEN = 1, SWKBD_DONE = 2 };

typedef struct {
    SwkbdRequest req;
    char utf8[KB_LIMIT * 4 + 1];
    size_t n_utf16;
    uint16_t utf16[KB_LIMIT * 2 + 1];
    int result;   // 1 OK, -1 cancel
    int test;     // AC_SWKBD_TEST: 1 OK, -1 cancel, 0 none
    atomic_int state;
    PlatMutex lock;
    PlatCond done;
    int lock_inited;
} SwkbdUi;

static SwkbdUi g = {.lock = PLAT_MUTEX_INIT, .done = PLAT_COND_INIT};

static void ensure_sync(void) {
    if (g.lock_inited) return;
    plat_mutex_init(&g.lock);
    plat_cond_init(&g.done);
    g.lock_inited = 1;
}

static size_t clean_utf16(uint16_t *t, size_t n, size_t max, uint32_t disabled, int digits_only) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        int pair = t[i] >= 0xd800 && t[i] < 0xdc00 && i + 1 < n;
        size_t k = pair ? 2 : 1;
        if (!pair && !swkbd_allowed(t[i], disabled, digits_only)) continue;
        if (o + k > max) break;
        t[o++] = t[i];
        if (pair) t[o++] = t[++i];
    }
    return o;
}

static int osk_wanted(void) {
    const char *v = cfg_get("AC_SWKBD_OSK");
    if (v && *v) return strcmp(v, "0") != 0 && strcmp(v, "false") != 0;
    return 1;   // handheld default: show the compositor keyboard when the field focuses
}

int swkbd_ui_active(void) {
    return atomic_load_explicit(&g.state, memory_order_acquire) == SWKBD_OPEN;
}

void swkbd_ui_get_request(const SwkbdRequest **req, char **utf8_buf, size_t *utf8_cap, int *test) {
    *req = &g.req;
    *utf8_buf = g.utf8;
    *utf8_cap = sizeof g.utf8;
    *test = g.test;
}

void swkbd_ui_finish(int ok) {
    ensure_sync();
    plat_mutex_lock(&g.lock);
    if (atomic_load_explicit(&g.state, memory_order_relaxed) != SWKBD_OPEN) {
        plat_mutex_unlock(&g.lock);
        return;
    }
    g.result = ok ? 1 : -1;
    if (ok) {
        g.n_utf16 = swkbd_utf8_to_utf16(g.utf8, g.utf16, KB_LIMIT * 2);
        g.n_utf16 = clean_utf16(g.utf16, g.n_utf16, (size_t)g.req.max_len, g.req.disabled, g.req.numeric);
        g.utf16[g.n_utf16] = 0;
    } else {
        g.n_utf16 = 0;
    }
    atomic_store_explicit(&g.state, SWKBD_DONE, memory_order_release);
    plat_cond_signal(&g.done);
    plat_mutex_unlock(&g.lock);
}

int text_entry_run(const SwkbdRequest *req, uint16_t *out, size_t cap, size_t *n) {
    ensure_sync();
    const char *test = cfg_get("AC_SWKBD_TEST");
    plat_mutex_lock(&g.lock);
    g.req = *req;
    g.result = 0;
    g.test = (test && *test) ? (!strcmp(test, "cancel") ? -1 : 1) : 0;
    g.n_utf16 = 0;
    size_t k = 0;
    while (k < KB_LIMIT && req->initial[k]) k++;
    swkbd_utf16_to_utf8(req->initial, k, g.utf8, sizeof g.utf8);
    atomic_store_explicit(&g.state, SWKBD_OPEN, memory_order_release);
    plat_mutex_unlock(&g.lock);

    // Async only — never run_on_window/wait here (Wayland StartTextInput deadlock).
    if (osk_wanted()) gfx_window_text_input(1);

    plat_mutex_lock(&g.lock);
    while (atomic_load_explicit(&g.state, memory_order_acquire) == SWKBD_OPEN)
        plat_cond_wait(&g.done, &g.lock);
    int ok = g.result > 0;
    *n = ok && g.n_utf16 < cap ? g.n_utf16 : (ok ? cap : 0);
    if (ok && *n) memcpy(out, g.utf16, *n * 2);
    atomic_store_explicit(&g.state, SWKBD_IDLE, memory_order_release);
    plat_mutex_unlock(&g.lock);

    if (osk_wanted()) gfx_window_text_input(0);
    return ok;
}

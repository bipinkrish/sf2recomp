#include "touch.h"
#include "../hle.h"
#include "../settings.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

typedef struct {
    SDL_TouchID device;
    SDL_FingerID finger;
    float x, y, start_x, start_y, previous_x, previous_y, pressure;
    uint64_t previous_ns;
    int terminal;
    bool used, began;
} Contact;
static SDL_SpinLock touch_lock;
static Contact contacts[MENU_TOUCH_MAX];
static float window_width, window_height;

static void cancel_contacts(void) {
    for (unsigned i = 0; i < MENU_TOUCH_MAX; ++i) {
        Contact *p = &contacts[i];
        if (!p->used) continue;
        if (!p->began) memset(p, 0, sizeof *p);
        else p->terminal = TOUCH_CANCELED;
    }
}
void menu_touch_cancel(void) {
    SDL_LockSpinlock(&touch_lock);
    cancel_contacts();
    SDL_UnlockSpinlock(&touch_lock);
}
RT_ON_RESET(reset_menu_touch) {
    SDL_LockSpinlock(&touch_lock);
    memset(contacts, 0, sizeof contacts);
    SDL_UnlockSpinlock(&touch_lock);
}

void menu_touch_event(const SDL_Event *e) {
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST || e->type == SDL_EVENT_WINDOW_MINIMIZED) {
        menu_touch_cancel();
        return;
    }
    if (e->type != SDL_EVENT_FINGER_DOWN && e->type != SDL_EVENT_FINGER_MOTION &&
        e->type != SDL_EVENT_FINGER_UP && e->type != SDL_EVENT_FINGER_CANCELED &&
        e->type != SDL_EVENT_WINDOW_RESIZED) return;
    SDL_Window *window = SDL_GetWindowFromEvent(e);
    int w = 0, h = 0;
    if (!window || !SDL_GetWindowSize(window, &w, &h) || w <= 0 || h <= 0) return;
    SDL_LockSpinlock(&touch_lock);
    window_width = w; window_height = h;
    if (e->type == SDL_EVENT_WINDOW_RESIZED) goto done;
    if (!isfinite(e->tfinger.x) || !isfinite(e->tfinger.y)) goto done;
    Contact *p = NULL, *empty = NULL;
    for (unsigned i = 0; i < MENU_TOUCH_MAX; ++i) {
        Contact *q = &contacts[i];
        if (!q->used) empty = empty ? empty : q;
        else if (q->terminal < 0 && q->device == e->tfinger.touchID && q->finger == e->tfinger.fingerID) p = q;
    }
    if (e->type == SDL_EVENT_FINGER_DOWN) {
        if (p || !empty) goto done;
        p = empty;
        *p = (Contact){.used = true, .device = e->tfinger.touchID, .finger = e->tfinger.fingerID,
                       .start_x = e->tfinger.x, .start_y = e->tfinger.y, .terminal = -1};
    }
    if (!p || p->terminal >= 0) goto done;
    p->x = e->tfinger.x; p->y = e->tfinger.y;
    p->pressure = isfinite(e->tfinger.pressure) ? fminf(1, fmaxf(0, e->tfinger.pressure)) : 1;
    if (e->type == SDL_EVENT_FINGER_UP) p->terminal = TOUCH_ENDED;
    if (e->type == SDL_EVENT_FINGER_CANCELED) p->terminal = TOUCH_CANCELED;
done:
    SDL_UnlockSpinlock(&touch_lock);
}

unsigned menu_touch_frame(unsigned width, unsigned height, bool captured, MenuTouch out[MENU_TOUCH_MAX]) {
    unsigned count = 0;
    SDL_LockSpinlock(&touch_lock);
    if (captured) cancel_contacts();
    float base_width = settings_screen_width(1080);
    float scale = fminf(window_width / base_width, window_height / 1080.f);
    uint64_t now = SDL_GetTicksNS();
    for (unsigned i = 0; scale > 0 && width && height && i < MENU_TOUCH_MAX; ++i) {
        Contact *p = &contacts[i];
        if (!p->used) continue;
        bool began = !p->began;
        float x = began ? p->start_x : p->x, y = began ? p->start_y : p->y;
        // Same fitted viewport as mouse input; leave black-bar contacts outside.
        x = (x * window_width - (window_width - base_width * scale) * .5f) / (base_width * scale) * width;
        y = (1 - (y * window_height - (window_height - 1080 * scale) * .5f) / (1080 * scale)) * height;
        int phase = began ? TOUCH_BEGAN : p->terminal >= 0 ? p->terminal :
                    x != p->previous_x || y != p->previous_y ? TOUCH_MOVED : TOUCH_STATIONARY;
        out[count++] = (MenuTouch){.finger = i, .x = x, .y = y, .raw_x = x, .raw_y = y,
            .dx = began ? 0 : x - p->previous_x, .dy = began ? 0 : y - p->previous_y,
            .dt = began ? 0 : (now - p->previous_ns) / 1e9f, .taps = 1, .phase = phase,
            .pressure = p->pressure, .maximum_pressure = 1};
        p->began = true; p->previous_x = x; p->previous_y = y; p->previous_ns = now;
        // A short tap still produces Began and Ended on separate guest updates.
        // Do not recycle its ID before the release reaches Unity.
        if (phase == TOUCH_ENDED || phase == TOUCH_CANCELED) memset(p, 0, sizeof *p);
    }
    SDL_UnlockSpinlock(&touch_lock);
    return count;
}

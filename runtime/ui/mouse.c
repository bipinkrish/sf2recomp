#include "../constants/ui.h"
#include "mouse.h"
#include "../hle.h"
#include "../settings.h"
#include <SDL3/SDL.h>
#include <math.h>

static SDL_SpinLock mouse_lock;
static struct {
    float x, y, width, height, scroll_x, scroll_y;
    uint8_t held, pressed, released;
    bool active;
} pending;

void menu_mouse_reset(void) {
    SDL_LockSpinlock(&mouse_lock);
    pending.active = false;
    pending.released |= pending.held;
    pending.held = pending.pressed = 0;
    pending.scroll_x = pending.scroll_y = 0;
    SDL_UnlockSpinlock(&mouse_lock);
}
RT_ON_RESET(reset_menu_mouse) { menu_mouse_reset(); }

static uint8_t mouse_bit(uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return 1;
    case SDL_BUTTON_RIGHT: return SF2C_MOUSE_N_2_D473;
    case SDL_BUTTON_MIDDLE: return SF2C_MOUSE_N_4_4B22;
    default: return 0;
    }
}

void menu_mouse_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_MINIMIZED:
        menu_mouse_reset();
        return;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.scancode >= SDL_SCANCODE_LCTRL && e->key.scancode <= SDL_SCANCODE_RGUI) return;
        menu_mouse_reset();
        return;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (abs(e->gaxis.value) <= SF2C_MOUSE_N_12000_C5D1) return;
        // fall through
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        menu_mouse_reset();
        return;
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL: case SDL_EVENT_WINDOW_MOUSE_ENTER: case SDL_EVENT_WINDOW_RESIZED:
        break;
    default: return;
    }
    SDL_Window *window = SDL_GetWindowFromEvent(e);
    if (!window) window = SDL_GetKeyboardFocus();  // scripted events have no window ID
    bool scripted_mouse = (e->type == SDL_EVENT_MOUSE_MOTION && e->motion.which == SF2C_MOUSE_N_0X7E57U_3EAD) ||
        ((e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) && e->button.which == SF2C_MOUSE_N_0X7E57U_3EAD);
    // Hidden diagnostic windows have no focus. Resolve only our synthetic
    // mouse to the sole game window, on SDL's event/window thread.
    if (!window && scripted_mouse) {
        int count=0;SDL_Window **windows=SDL_GetWindows(&count);
        if(windows && count==1)window=windows[0];
        SDL_free(windows);
    }
    int width = 0, height = 0;
    if (!window || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return;
    float x, y;
    SDL_GetMouseState(&x, &y);
    if (e->type == SDL_EVENT_MOUSE_MOTION) x = e->motion.x, y = e->motion.y;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)
        x = e->button.x, y = e->button.y;

    SDL_LockSpinlock(&mouse_lock);
    pending.x = x; pending.y = y;
    pending.width = width; pending.height = height;
    if (ENV(SF2C_MOUSE_S_LARECOMP_MOUSE_TRACE_86DC) && e->type != SDL_EVENT_WINDOW_RESIZED)
        LOG(SF2C_MOUSE_S_MOUSE_SDL_EVENT_X_POSITION_1_B53F, e->type, x, y, width, height);
    if (e->type != SDL_EVENT_WINDOW_RESIZED) pending.active = true;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        uint8_t bit = mouse_bit(e->button.button);
        pending.pressed |= bit & ~pending.held;
        pending.held |= bit;
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        uint8_t bit = mouse_bit(e->button.button);
        pending.released |= bit & pending.held;
        pending.held &= ~bit;
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        float direction = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        pending.scroll_x += e->wheel.x * direction;
        pending.scroll_y += e->wheel.y * direction;
    }
    SDL_UnlockSpinlock(&mouse_lock);
}

MenuMouse menu_mouse_frame(unsigned width, unsigned height, bool captured) {
    MenuMouse result = {.x = -SF2C_MOUSE_N_10000_39E5, .y = -SF2C_MOUSE_N_10000_39E5};
    SDL_LockSpinlock(&mouse_lock);
    if (captured) {
        pending.active = false;
        pending.released |= pending.held;
        pending.held = pending.pressed = 0;
        pending.scroll_x = pending.scroll_y = 0;
    }
    result.held = pending.held;
    result.pressed = pending.pressed;
    result.released = pending.released;
    result.scroll_x = pending.scroll_x;
    result.scroll_y = pending.scroll_y;
    pending.pressed = pending.released = 0;
    pending.scroll_x = pending.scroll_y = 0;
    // Presentation fits the configured aspect to the window. SDL reports window
    // points, so this also works on HiDPI screens regardless of render scale.
    float base_width = settings_screen_width(SF2C_MOUSE_N_1080_32EB);
    float scale = fminf(pending.width / base_width, pending.height / SF2C_MOUSE_N_1080_F_E3B8);
    if (pending.active && scale > 0 && width && height) {
        float view_w = base_width * scale, view_h = SF2C_MOUSE_N_1080_32EB * scale;
        float x = (pending.x - (pending.width - view_w) / SF2C_MOUSE_N_2_D473) / view_w;
        float y = (pending.y - (pending.height - view_h) / SF2C_MOUSE_N_2_D473) / view_h;
        // Keep positions outside the viewport outside: clamping would make black
        // bars clickable. Unity uses a bottom-left origin.
        result.x = x * width;
        result.y = (1 - y) * height;
    }
    SDL_UnlockSpinlock(&mouse_lock);
    return result;
}

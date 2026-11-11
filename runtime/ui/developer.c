#include "../constants/ui.h"
// SDL events are collected on the window thread, then consumed by guest code on
// its own update thread. Never invoke Unity/game methods from the window thread.
#include "../developer.h"
#include "../settings.h"
#include "settings_input.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <string.h>

enum { EVENT_CAP = SF2C_DEVELOPER_N_128_2747 };
static DeveloperEvent events[EVENT_CAP];
static unsigned read_pos, write_pos;
static SDL_SpinLock event_lock;
static atomic_bool console_open;
static bool text_input_started;

static bool push_event(int action, const char *text) {
    if (text && strlen(text) >= sizeof events[0].text) return false;
    SDL_LockSpinlock(&event_lock);
    unsigned next = (write_pos + 1) % EVENT_CAP;
    bool space = next != read_pos;
    if (space) {
        events[write_pos] = (DeveloperEvent){.action = action};
        if (text) strcpy(events[write_pos].text, text);
        write_pos = next;
    }
    SDL_UnlockSpinlock(&event_lock);
    return space;
}

bool developer_poll_event(DeveloperEvent *event) {
    SDL_LockSpinlock(&event_lock);
    bool available = read_pos != write_pos;
    if (available) { *event = events[read_pos]; read_pos = (read_pos + 1) % EVENT_CAP; }
    SDL_UnlockSpinlock(&event_lock);
    return available;
}

bool developer_console_open(void) { return atomic_load(&console_open); }
void developer_set_console_open(bool open) { atomic_store(&console_open, open); }

void developer_input_reset(void) {
    SDL_LockSpinlock(&event_lock);
    read_pos = write_pos = 0;
    SDL_UnlockSpinlock(&event_lock);
    developer_set_console_open(false);
}

bool developer_handle_event(const SDL_Event *event) {
    bool open = developer_console_open();
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        bool down = event->type == SDL_EVENT_KEY_DOWN;
        SDL_Scancode key = event->key.scancode;
        if (g_settings.developer_console && (key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_GRAVE)) {
            if (down && !event->key.repeat && push_event(DEV_CONSOLE_TOGGLE, NULL))
                developer_set_console_open(!open);
            return true;
        }
        if (g_settings.developer_overlays && key == SDL_SCANCODE_F2) {
            if (down && !event->key.repeat) push_event(DEV_OVERLAYS_TOGGLE, NULL);
            return true;
        }
        if (!open) return false;
        if (!down) return true;
        switch (key) {
        case SDL_SCANCODE_ESCAPE:
            if (push_event(DEV_CONSOLE_CLOSE, NULL)) developer_set_console_open(false);
            break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: push_event(DEV_SUBMIT, NULL); break;
        case SDL_SCANCODE_BACKSPACE: push_event(DEV_BACKSPACE, NULL); break;
        case SDL_SCANCODE_UP: push_event(DEV_HISTORY_PREV, NULL); break;
        case SDL_SCANCODE_DOWN: push_event(DEV_HISTORY_NEXT, NULL); break;
        case SDL_SCANCODE_PAGEUP: push_event(DEV_SCROLL_UP, NULL); break;
        case SDL_SCANCODE_PAGEDOWN: push_event(DEV_SCROLL_DOWN, NULL); break;
        case SDL_SCANCODE_V:
            if (event->key.mod & SDL_KMOD_CTRL) {
                char *text = SDL_GetClipboardText();
                if (text) { push_event(DEV_TEXT, text); SDL_free(text); }
            }
            break;
        default: break;
        }
        return true;
    }
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        if (!open) return false;
        push_event(DEV_TEXT, event->text.text);
        return true;
    }
    if (open && event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (event->wheel.y) push_event(event->wheel.y > 0 ? DEV_SCROLL_UP : DEV_SCROLL_DOWN, NULL);
        return true;
    }
    if (open) {
        switch (event->type) {
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL: return true;
        default: break;
        }
    }
    return false;
}

void developer_update_text_input(void *window) {
    bool wanted = developer_console_open() || settings_input_text_active();
    if (wanted == text_input_started) return;
    extern void input_reset(void);
    input_reset();
    if (wanted) SDL_StartTextInput(window);
    else SDL_StopTextInput(window);
    text_input_started = wanted;
}

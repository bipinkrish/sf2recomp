#include "../constants/ui.h"
#include "settings_input.h"
#include "../input_bind.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_SpinLock edit_lock;
static SettingsInputMode mode;
static SettingsInput edit;
static bool pending, replace_text;

void settings_input_begin(SettingsInputMode next, const char *value) {
    SDL_LockSpinlock(&edit_lock);
    mode = next;
    edit = (SettingsInput){0};
    if (value) snprintf(edit.value, sizeof edit.value, SF2C_SETTINGS_INPUT_S_S_0030, value);
    pending = false;
    replace_text = next == SETTINGS_INPUT_TEXT;
    SDL_UnlockSpinlock(&edit_lock);
}

bool settings_input_poll(SettingsInput *input) {
    SDL_LockSpinlock(&edit_lock);
    bool available = pending;
    if (available) *input = edit;
    pending = false;
    SDL_UnlockSpinlock(&edit_lock);
    return available;
}

bool settings_input_text_active(void) {
    SDL_LockSpinlock(&edit_lock);
    bool active = mode == SETTINGS_INPUT_TEXT;
    SDL_UnlockSpinlock(&edit_lock);
    return active;
}

static void complete(SettingsInputResult result, const char *value) {
    if (value) snprintf(edit.value, sizeof edit.value, SF2C_SETTINGS_INPUT_S_S_0030, value);
    edit.result = result;
    pending = true;
}

static void append_text(const char *text) {
    size_t offset = replace_text ? 0 : strlen(edit.value);
    size_t length = strlen(text);
    if (length >= sizeof edit.value - offset || strpbrk(text, SF2C_SETTINGS_INPUT_S_R_N_645E)) return;
    replace_text = false;
    memcpy(edit.value + offset, text, length + 1);
    pending = true;
}

static bool handle_event(const SDL_Event *event) {
    if (mode == SETTINGS_INPUT_NONE) return false;
    if (edit.result != SETTINGS_INPUT_CHANGED) {
        switch (event->type) {
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            return true;
        default: return false;
        }
    }
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: {
        SDL_Scancode key = event->key.scancode;
        if (key == SDL_SCANCODE_ESCAPE && !event->key.repeat) {
            complete(SETTINGS_INPUT_CANCELLED, NULL);
        } else if (mode == SETTINGS_INPUT_KEY && !event->key.repeat) {
            const char *name = bind_config_value(BIND_DEV_KEY, key);
            if (name && *name) complete(SETTINGS_INPUT_ACCEPTED, name);
        } else if (mode == SETTINGS_INPUT_TEXT) {
            if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
                if (!event->key.repeat) complete(SETTINGS_INPUT_ACCEPTED, NULL);
            } else if (key == SDL_SCANCODE_BACKSPACE && *edit.value) {
                size_t length = strlen(edit.value);
                do --length; while (length && ((unsigned char)edit.value[length] & SF2C_SETTINGS_INPUT_N_0XC0_4E7B) == SF2C_SETTINGS_INPUT_N_0X80_D41C);
                edit.value[replace_text ? 0 : length] = 0;
                replace_text = false;
                pending = true;
            } else if (key == SDL_SCANCODE_A && (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                replace_text = true;
            } else if (key == SDL_SCANCODE_V && (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                char *text = SDL_GetClipboardText();
                if (text) { append_text(text); SDL_free(text); }
            }
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (mode == SETTINGS_INPUT_PAD)
            complete(SETTINGS_INPUT_ACCEPTED, bind_config_value(BIND_DEV_PAD, event->gbutton.button));
        else if (event->gbutton.button == g_bind_pad[BIND_B])
            complete(SETTINGS_INPUT_CANCELLED, NULL);
        else if (mode == SETTINGS_INPUT_TEXT && event->gbutton.button == g_bind_pad[BIND_A])
            complete(SETTINGS_INPUT_ACCEPTED, NULL);
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (mode == SETTINGS_INPUT_PAD && event->gaxis.value > SF2C_SETTINGS_INPUT_N_16000_2570 &&
            (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            int host = event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PADB_LTRIGGER : PADB_RTRIGGER;
            complete(SETTINGS_INPUT_ACCEPTED, bind_config_value(BIND_DEV_PAD, host));
        }
        return true;
    case SDL_EVENT_TEXT_INPUT:
        if (mode == SETTINGS_INPUT_TEXT) append_text(event->text.text);
        return true;
    case SDL_EVENT_KEY_UP: case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_TEXT_EDITING:
        return true;
    default:
        return false;
    }
}

bool settings_input_handle_event(const SDL_Event *event) {
    SDL_LockSpinlock(&edit_lock);
    bool consumed = handle_event(event);
    SDL_UnlockSpinlock(&edit_lock);
    return consumed;
}

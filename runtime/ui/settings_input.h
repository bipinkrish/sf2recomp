#pragma once
#include "../settings.h"

typedef union SDL_Event SDL_Event;

typedef enum {
    SETTINGS_INPUT_NONE, SETTINGS_INPUT_TEXT, SETTINGS_INPUT_KEY, SETTINGS_INPUT_PAD
} SettingsInputMode;
typedef enum {
    SETTINGS_INPUT_CHANGED, SETTINGS_INPUT_ACCEPTED, SETTINGS_INPUT_CANCELLED
} SettingsInputResult;
typedef struct {
    SettingsInputResult result;
    char value[SETTINGS_SAVE_DIRECTORY_CAP];
} SettingsInput;

// The guest owns the editor. SDL owns event collection and the system keyboard.
void settings_input_begin(SettingsInputMode mode, const char *value);
bool settings_input_poll(SettingsInput *input);
bool settings_input_text_active(void);
bool settings_input_handle_event(const SDL_Event *event);

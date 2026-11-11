#pragma once
#include "settings.h"
#include "input_bind.h"

// The INI values before environment overrides, plus staged menu changes.
typedef struct {
    Settings settings;
    int16_t key[BIND_KEY_COUNT], pad[BIND_PAD_COUNT];
} SettingsConfig;

typedef enum {
    SETTINGS_GROUP_GENERAL, SETTINGS_GROUP_NETWORK, SETTINGS_GROUP_GAMEPLAY,
    SETTINGS_GROUP_DEVELOPER, SETTINGS_GROUP_AUDIO, SETTINGS_GROUP_VIDEO,
    SETTINGS_GROUP_INPUT, SETTINGS_GROUP_STORAGE, SETTINGS_GROUP_KEYBOARD,
    SETTINGS_GROUP_GAMEPAD, SETTINGS_GROUP_COUNT
} SettingsGroup;

typedef enum {
    SETTINGS_EDIT_CHOICE, SETTINGS_EDIT_VOLUME, SETTINGS_EDIT_FPS,
    SETTINGS_EDIT_PATH, SETTINGS_EDIT_KEY, SETTINGS_EDIT_PAD
} SettingsEditor;

typedef struct {
    unsigned applied, restart_pending, overridden;
    bool request_fullscreen, refresh_fight_settings;
} SettingsSaveResult;

void settings_config_begin(SettingsConfig *draft);
// Save the file first. Apply live fields only after the write succeeds.
bool settings_config_save(const SettingsConfig *draft, SettingsSaveResult *result);
bool settings_config_requires_restart(unsigned field);

unsigned settings_config_count(void);
SettingsGroup settings_config_group(unsigned field);
SettingsEditor settings_config_editor(unsigned field);
const char *settings_config_label(unsigned field);
const char *settings_config_override(unsigned field);
void settings_config_value(const SettingsConfig *draft, unsigned field, char *out, size_t size);
bool settings_config_set(SettingsConfig *draft, unsigned field, const char *value);
void settings_config_step(SettingsConfig *draft, unsigned field, int direction);

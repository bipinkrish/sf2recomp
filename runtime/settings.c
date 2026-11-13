#include "constants/core.h"
#include "constants/nvn.h"
#include "constants/nvn_vulkan.h"
#include "constants/services.h"
// SF2 Recomp settings: defaults, editable INI file, then environment overrides.
#include "settings.h"
#include "settings_config.h"
#include "platform.h"

#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const g_language_codes[N_LANGUAGES] = {
    SF2C_SETTINGS_S_JA_4A86, SF2C_SETTINGS_S_EN_US_C528, SF2C_SETTINGS_S_EN_GB_90A2, SF2C_SETTINGS_S_FR_92E5, SF2C_SETTINGS_S_DE_FA20, SF2C_SETTINGS_S_IT_FF56, SF2C_SETTINGS_S_ES_14BA, SF2C_SETTINGS_S_ZH_CN_CB6A, SF2C_SETTINGS_S_ZH_TW_6F1B, SF2C_SETTINGS_S_RU_A8CA, SF2C_SETTINGS_S_PT_04FA, SF2C_SETTINGS_S_KO_F0B0,
};
const char *const g_language_names[N_LANGUAGES] = {
    SF2C_SETTINGS_S_JAPANESE_A013, SF2C_SETTINGS_S_ENGLISH_US_B197, SF2C_SETTINGS_S_ENGLISH_UK_A2A5, SF2C_SETTINGS_S_FRENCH_0920, SF2C_SETTINGS_S_GERMAN_AE4D, SF2C_SETTINGS_S_ITALIAN_E577, SF2C_SETTINGS_S_SPANISH_F67D,
    SF2C_SETTINGS_S_CHINESE_SIMPLIFIED_0D26, SF2C_SETTINGS_S_CHINESE_TRADITIONAL_A761, SF2C_SETTINGS_S_RUSSIAN_07BB, SF2C_SETTINGS_S_PORTUGUESE_8CB4, SF2C_SETTINGS_S_KOREAN_ACDF,
};

bool g_gpu_integrated = false;

Settings g_settings = {
    .volume = SF2C_SETTINGS_N_1_0F_CA71,
    .reverb = true,
    .language = LANG_AUTO,
    .aspect = ASPECT_ORIGINAL,
    .target_fps = SF2C_SETTINGS_N_60_39FA,
    .res_mode = RES_MATCH,
    .res_auto = true,
    .upscaler = UPSCALE_BILINEAR,
    .upscaler_auto = true,
    .antialias = AA_OFF,
    .dpad_move = true,
    .rumble = true,
    .rumble_strength = SF2C_SETTINGS_N_100_AD57,
    .bg_input = true,
    .prompts = PROMPTS_AUTO,
    .player_setup = PLAYER_SETUP_SOLO,
    .network_enabled = false,
    .unlock_all_sparring_characters = true,
    .enable_shadow_in_sparring = true,
    .enable_titan_in_sparring = true,
    .use_completed_save = false,
    .allow_sparring_lan_multiplayer = true,
    .show_intro = true,
    .ai_enabled = true,
    .developer_console = false,
    .developer_trainer = false,
    .developer_shop_cheats = false,
    .developer_overlays = false,
    .developer_fps = true,
    .developer_memory = true,
    .developer_build_info = true,
    .developer_model_info = true,
    .save_directory = SF2C_SETTINGS_S_SAVE_38AA,
};

// Defaults from environment overrides (SF2RECOMP_VOLUME / SF2RECOMP_REVERB / SF2RECOMP_LANG) — called by boot.
static const char *env_float(const char *name, float *out) {
    const char *e = getenv(name);
    if (e && *e) {
        char *end;
        errno = 0;
        float v = strtof(e, &end);
        if (!errno && end != e && !*end && isfinite(v) && v >= 0 && v <= 1) { *out = v; return e; }
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_INVALID_S_S_EXPECTE_81F1, name, e);
    }
    return NULL;
}

static bool parse_bool(const char *value, bool *out) {
    bool on = !strcasecmp(value, SF2C_SETTINGS_S_TRUE_18D1) || !strcasecmp(value, SF2C_SETTINGS_S_YES_6C76) ||
              !strcasecmp(value, SF2C_SETTINGS_S_ON_990C) || !strcmp(value, SF2C_SETTINGS_S_1_3915);
    bool off = !strcasecmp(value, SF2C_SETTINGS_S_FALSE_076D) || !strcasecmp(value, SF2C_SETTINGS_S_NO_04A0) ||
               !strcasecmp(value, SF2C_SETTINGS_S_OFF_9F3B) || !strcmp(value, SF2C_SETTINGS_S_0_9808);
    if (!on && !off) return false;
    *out = on;
    return true;
}

static bool assign_path(char *out, size_t capacity, const char *value) {
    size_t length = strlen(value);
    if (!length || length >= capacity) return false;
    bool visible = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)value[i];
        if (iscntrl(ch)) return false;
        if (!isspace(ch)) visible = true;
    }
    if (!visible) return false;
    memcpy(out, value, length + 1);
    return true;
}

void settings_init(void) {
    env_float(SF2C_SETTINGS_S_SF2RECOMP_VOLUME_6C32, &g_settings.volume);
    const char *r = getenv(SF2C_SETTINGS_S_SF2RECOMP_REVERB_419C);
    if (r && *r && !parse_bool(r, &g_settings.reverb))
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_INVALID_SF2RECOMP_RE_ED26, r);
    const char *save = getenv(SF2C_SETTINGS_S_SF2RECOMP_SAVE_DIR_4AEE);
    if (save && !assign_path(g_settings.save_directory, sizeof g_settings.save_directory, save))
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_INVALID_SF2RECOMP_SA_EC52);
    const char *lang = getenv(SF2C_SETTINGS_S_SF2RECOMP_LANG_74DD);
    if (lang && *lang) {
        if (!strcasecmp(lang, SF2C_SETTINGS_S_AUTO_6346)) { g_settings.language = LANG_AUTO; return; }
        int i = settings_language_index(lang);
        if (i < 0) fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_UNKNOWN_LANGUAGE_CO_8725, lang, g_language_codes[0]);
        else g_settings.language = i;
    }
}

const char *settings_language(void) {
    if (g_settings.language >= 0 && g_settings.language < N_LANGUAGES)
        return g_language_codes[g_settings.language];
    const char *locales[] = {getenv(SF2C_SETTINGS_S_LC_ALL_C2EB), getenv(SF2C_SETTINGS_S_LC_MESSAGES_49FD), getenv(SF2C_SETTINGS_S_LANG_C3B4)};
    for (size_t i = 0; i < sizeof locales / sizeof locales[0]; ++i) {
        if (!locales[i] || !*locales[i]) continue;
        int language = settings_language_index(locales[i]);
        if (language >= 0) return g_language_codes[language];
    }
    return SF2C_SETTINGS_S_EN_US_C528;
}

const char *settings_language_name(void) {
    int language = settings_language_index(settings_language());
    return g_language_names[language >= 0 ? language : 1];
}

int settings_language_index(const char *code) {
    if (!code || !*code) return LANG_AUTO;
    char normalized[SF2C_SETTINGS_N_64_A68B];
    size_t length = strcspn(code, SF2C_SETTINGS_S_EMPTY_9107);
    if (length >= sizeof normalized) return -1;
    memcpy(normalized, code, length);
    normalized[length] = 0;
    for (size_t i = 0; i < length; ++i) if (normalized[i] == SF2C_SETTINGS_CH_EMPTY_BDC4) normalized[i] = SF2C_SETTINGS_CH_EMPTY_01F8;
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcasecmp(normalized, g_language_codes[i])) return i;
    // Select a supported regional variant for a language-only or host locale
    // (en, en-IN, de-DE, ko-Hangul), returning the canonical Switch identifier.
    size_t base = strcspn(normalized, SF2C_SETTINGS_S_EMPTY_0933);
    for (int i = 0; i < N_LANGUAGES; i++) {
        const char *supported = g_language_codes[i];
        if (strcspn(supported, SF2C_SETTINGS_S_EMPTY_0933) == base && !strncasecmp(normalized, supported, base)) return i;
    }
    return -1;
}

// Named sections and a single option table allow new settings without changing
// the INI parser. Unknown sections/keys are reserved for future versions.
typedef enum {
    CONFIG_LANGUAGE, CONFIG_BOOL, CONFIG_VOLUME, CONFIG_PATH,
    CONFIG_FRAME_RATE, CONFIG_RESOLUTION, CONFIG_UPSCALER, CONFIG_ANTIALIAS, CONFIG_PROMPTS, CONFIG_ASPECT,
    CONFIG_PLAYER_SETUP
} ConfigType;
typedef enum { CONFIG_RESTART, CONFIG_LIVE, CONFIG_WINDOW, CONFIG_FIGHT } ConfigApply;
typedef struct {
    SettingsGroup group;
    const char *key, *label;
    ConfigType type;
    size_t offset;
    ConfigApply apply;
} ConfigOption;
#define OPTION(group, key, label, type, member, apply) \
    {SETTINGS_GROUP_##group, key, label, CONFIG_##type, offsetof(Settings, member), CONFIG_##apply}
static const char *const config_sections[] = {
    [SETTINGS_GROUP_GENERAL] = SF2C_SETTINGS_S_GENERAL_A6F2,
    [SETTINGS_GROUP_NETWORK] = SF2C_SETTINGS_S_NETWORK_57B8,
    [SETTINGS_GROUP_GAMEPLAY] = SF2C_SETTINGS_S_GAMEPLAY_406B,
    [SETTINGS_GROUP_DEVELOPER] = SF2C_SETTINGS_S_DEVELOPER_7E6C,
    [SETTINGS_GROUP_AUDIO] = SF2C_SETTINGS_S_AUDIO_53B7,
    [SETTINGS_GROUP_VIDEO] = SF2C_SETTINGS_S_VIDEO_807F,
    [SETTINGS_GROUP_INPUT] = SF2C_SETTINGS_S_INPUT_48AE,
    [SETTINGS_GROUP_STORAGE] = SF2C_SETTINGS_S_STORAGE_6545,
    [SETTINGS_GROUP_KEYBOARD] = SF2C_SETTINGS_S_CONTROLS_KEYBOARD_4375,
    [SETTINGS_GROUP_GAMEPAD] = SF2C_SETTINGS_S_CONTROLS_GAMEPAD_C5B4,
};
static const ConfigOption config_options[] = {
    OPTION(GENERAL, SF2C_SETTINGS_S_LANGUAGE_1182, SF2C_SETTINGS_S_LANGUAGE_1FA2, LANGUAGE, language, RESTART),
    OPTION(NETWORK, SF2C_SETTINGS_S_ENABLED_6E30, SF2C_SETTINGS_S_GAME_NETWORKING_2326, BOOL, network_enabled, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_UNLOCK_ALL_SPARRING_CHARACTE_8856, SF2C_SETTINGS_S_UNLOCK_SPARRING_ROSTER_AFE6, BOOL, unlock_all_sparring_characters, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_ENABLE_SHADOW_IN_SPARRING_CAFB, SF2C_SETTINGS_S_SHADOW_IN_SPARRING_C18A, BOOL, enable_shadow_in_sparring, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_ENABLE_TITAN_IN_SPARRING_3232, SF2C_SETTINGS_S_TITAN_IN_SPARRING_4D29, BOOL, enable_titan_in_sparring, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_ALLOW_SPARRING_LAN_MULTIPLAY_290D, SF2C_SETTINGS_S_LAN_SPARRING_MENU_79CA, BOOL, allow_sparring_lan_multiplayer, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_UNLOCK_ALL_VIDEOS_A8A0, SF2C_SETTINGS_S_UNLOCK_MOVIES_76A6, BOOL, unlock_all_videos, LIVE),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_SHOW_INTRO_88D4, SF2C_SETTINGS_S_OPENING_VIDEO_9F3B, BOOL, show_intro, RESTART),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_AI_ENABLED_8057, SF2C_SETTINGS_S_FIGHT_AI_ACD7, BOOL, ai_enabled, FIGHT),
    OPTION(GAMEPLAY, SF2C_SETTINGS_S_SHOW_TIME_RESULTS_FC20, SF2C_SETTINGS_S_SHOW_TIME_RESULTS_8415, BOOL, show_time_results, FIGHT),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_CONSOLE_3593, SF2C_SETTINGS_S_DEVELOPER_CONSOLE_6911, BOOL, developer_console, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_TRAINER_EBE0, SF2C_SETTINGS_S_FIGHT_TRAINER_06D5, BOOL, developer_trainer, LIVE),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_SHOP_CHEATS_9653, SF2C_SETTINGS_S_SHOP_CHEATS_4822, BOOL, developer_shop_cheats, LIVE),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_OVERLAYS_178F, SF2C_SETTINGS_S_DEBUG_OVERLAYS_1F44, BOOL, developer_overlays, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_OVERLAYS_ON_START_E5DA, SF2C_SETTINGS_S_SHOW_OVERLAYS_ON_START_E5F3, BOOL, developer_overlays_on_start, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_FPS_A9EF, SF2C_SETTINGS_S_FPS_OVERLAY_6772, BOOL, developer_fps, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_MEMORY_E386, SF2C_SETTINGS_S_MEMORY_OVERLAY_839B, BOOL, developer_memory, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_BUILD_INFO_25FA, SF2C_SETTINGS_S_BUILD_INFO_OVERLAY_60BF, BOOL, developer_build_info, RESTART),
    OPTION(DEVELOPER, SF2C_SETTINGS_S_MODEL_INFO_0D1C, SF2C_SETTINGS_S_MODEL_INFO_OVERLAY_9869, BOOL, developer_model_info, RESTART),
    OPTION(AUDIO, SF2C_SETTINGS_S_VOLUME_3FF4, SF2C_SETTINGS_S_MASTER_VOLUME_93E3, VOLUME, volume, LIVE),
    OPTION(AUDIO, SF2C_SETTINGS_S_REVERB_6A96, SF2C_SETTINGS_S_REVERB_16C6, BOOL, reverb, LIVE),
    OPTION(VIDEO, SF2C_SETTINGS_S_FULLSCREEN_962F, SF2C_SETTINGS_S_FULLSCREEN_7FDF, BOOL, fullscreen, WINDOW),
    OPTION(VIDEO, SF2C_SETTINGS_S_ASPECT_RATIO_46A4, SF2C_SETTINGS_S_ASPECT_RATIO_D248, ASPECT, aspect, RESTART),
    OPTION(VIDEO, SF2C_SETTINGS_S_TARGET_FPS_10CA, SF2C_SETTINGS_S_TARGET_FPS_4DF1, FRAME_RATE, target_fps, RESTART),
    OPTION(VIDEO, SF2C_SETTINGS_S_INTERNAL_RESOLUTION_1625, SF2C_SETTINGS_S_INTERNAL_RESOLUTION_DF76, RESOLUTION, res_mode, LIVE),
    OPTION(VIDEO, SF2C_SETTINGS_S_UPSCALER_3302, SF2C_SETTINGS_S_UPSCALER_EF37, UPSCALER, upscaler, LIVE),
    OPTION(VIDEO, SF2C_SETTINGS_S_ANTIALIASING_FC81, SF2C_SETTINGS_S_ANTI_ALIASING_D042, ANTIALIAS, antialias, LIVE),
    OPTION(INPUT, SF2C_SETTINGS_S_PROMPTS_6DA4, SF2C_SETTINGS_S_BUTTON_PROMPTS_B9C9, PROMPTS, prompts, RESTART),
    OPTION(INPUT, SF2C_SETTINGS_S_PLAYER_SETUP_C139, SF2C_SETTINGS_S_PLAYER_SETUP_3C5A, PLAYER_SETUP, player_setup, RESTART),
    OPTION(STORAGE, SF2C_SETTINGS_S_USE_COMPLETED_SAVE_9A94, SF2C_SETTINGS_S_USE_COMPLETED_SAVE_C4BB, BOOL, use_completed_save, RESTART),
    OPTION(STORAGE, SF2C_SETTINGS_S_SAVE_DIRECTORY_4C87, SF2C_SETTINGS_S_SAVE_DIRECTORY_8AE8, PATH, save_directory, RESTART),
};

#undef OPTION
enum { CONFIG_OPTION_COUNT = sizeof config_options / sizeof config_options[0] };
static SettingsConfig saved_config, launch_config;
static char config_path[PATH_MAX];

static char *config_trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return s;
}

static void config_remove_comment(char *s) {
    char quote = 0;
    for (; *s; ++s) {
        if ((*s == SF2C_SETTINGS_CH_EMPTY_EB70 || *s == SF2C_SETTINGS_S_EMPTY_3251) && (!quote || *s == quote))
            quote = quote ? 0 : *s;
        else if (!quote && (*s == SF2C_SETTINGS_CH_EMPTY_4734 || *s == SF2C_SETTINGS_CH_EMPTY_CF57)) { *s = 0; break; }
    }
}

typedef struct { const char *name; int value; } ConfigChoice;
enum { CONFIG_AUTO = -1 };
static const ConfigChoice aspect_choices[] = {
    {SF2C_SETTINGS_S_ORIGINAL_4D60, ASPECT_ORIGINAL}, {SF2C_SETTINGS_S_NATIVE_4333, ASPECT_NATIVE},
    {SF2C_SETTINGS_S_16_10_7BA3, ASPECT_16_10}, {SF2C_SETTINGS_S_21_9_80DF, ASPECT_21_9},
    {SF2C_SETTINGS_S_32_9_307E, ASPECT_32_9}, {SF2C_SETTINGS_S_16_9_0C60, ASPECT_ORIGINAL},
};
static const ConfigChoice resolution_choices[] = {
    {SF2C_SETTINGS_S_AUTO_6346, CONFIG_AUTO}, {SF2C_SETTINGS_S_MATCH_7C82, RES_MATCH},
    {SF2C_SETTINGS_S_DYNAMIC_68ED, RES_DYNAMIC}, {SF2C_SETTINGS_S_0_25X_63EF, RES_Q25},
    {SF2C_SETTINGS_S_0_50X_1CF2, RES_Q50}, {SF2C_SETTINGS_S_0_75X_50AC, RES_Q75},
    {SF2C_SETTINGS_S_1X_9FF3, 1}, {SF2C_SETTINGS_S_2X_5383, SF2C_SETTINGS_N_2_D473},
    {SF2C_SETTINGS_S_3X_2593, SF2C_SETTINGS_N_3_4E07}, {SF2C_SETTINGS_S_4X_6992, SF2C_SETTINGS_N_4_4B22},
    {SF2C_SETTINGS_S_0_5X_82D2, RES_Q50},
};
static const ConfigChoice upscaler_choices[] = {
    {SF2C_SETTINGS_S_AUTO_6346, CONFIG_AUTO}, {SF2C_SETTINGS_S_BILINEAR_70ED, UPSCALE_BILINEAR},
    {SF2C_SETTINGS_S_FSR1_B074, UPSCALE_FSR1},
};
static const ConfigChoice antialias_choices[] = {
    {SF2C_SETTINGS_S_OFF_9F3B, AA_OFF}, {SF2C_SETTINGS_S_SMAA_4056, AA_SMAA},
};
static const ConfigChoice prompt_choices[] = {
    {SF2C_SETTINGS_S_AUTO_6346, PROMPTS_AUTO}, {SF2C_SETTINGS_S_NINTENDO_AF74, PROMPTS_NINTENDO},
    {SF2C_SETTINGS_S_XBOX_76AE, PROMPTS_XBOX}, {SF2C_SETTINGS_S_PLAYSTATION_15B2, PROMPTS_PLAYSTATION},
};
static const ConfigChoice player_choices[] = {
    {SF2C_SETTINGS_S_SOLO_C940, PLAYER_SETUP_SOLO},
    {SF2C_SETTINGS_S_KEYBOARD_VS_CONTROLLER_18CB, PLAYER_SETUP_KEYBOARD_VS_CONTROLLER},
    {SF2C_SETTINGS_S_TWO_CONTROLLERS_4B78, PLAYER_SETUP_TWO_CONTROLLERS},
};
typedef struct { const ConfigChoice *items; unsigned count, aliases; } ConfigChoices;
#define CHOICES(items, aliases) {items, sizeof items / sizeof items[0], aliases}
static const ConfigChoices config_choices[] = {
    [CONFIG_ASPECT] = CHOICES(aspect_choices, 1),
    [CONFIG_RESOLUTION] = CHOICES(resolution_choices, 1),
    [CONFIG_UPSCALER] = CHOICES(upscaler_choices, 0),
    [CONFIG_ANTIALIAS] = CHOICES(antialias_choices, 0),
    [CONFIG_PROMPTS] = CHOICES(prompt_choices, 0),
    [CONFIG_PLAYER_SETUP] = CHOICES(player_choices, 0),
};
#undef CHOICES

static bool config_assign(Settings *settings, const ConfigOption *option, const char *value) {
    void *field = (char *)settings + option->offset;
    char *end;
    switch (option->type) {
    case CONFIG_LANGUAGE: {
        int language = settings_language_index(value);
        if (!strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346)) language = LANG_AUTO;
        else if (language < 0) return false;
        *(int *)field = language;
        return true;
    }
    case CONFIG_BOOL:
        return parse_bool(value, field);
    case CONFIG_PATH:
        return assign_path(field, SETTINGS_SAVE_DIRECTORY_CAP, value);
    case CONFIG_FRAME_RATE: {
        errno = 0;
        long fps = strtol(value, &end, SF2C_SETTINGS_N_10_4A44);
        if (errno || end == value || *end || fps < SF2C_SETTINGS_N_30_624B || fps > SF2C_SETTINGS_N_360_838F) return false;
        *(int *)field = (int)fps;
        return true;
    }
    case CONFIG_VOLUME: {
        errno = 0;
        float volume = strtof(value, &end);
        if (errno || end == value || *end || !isfinite(volume) || volume < 0 || volume > 1) return false;
        *(float *)field = volume;
        return true;
    }
    default: {
        ConfigChoices choices = config_choices[option->type];
        for (unsigned i = 0; i < choices.count; ++i) {
            ConfigChoice choice = choices.items[i];
            if (strcasecmp(value, choice.name)) continue;
            if (option->type == CONFIG_RESOLUTION) {
                settings->res_auto = choice.value == CONFIG_AUTO;
                *(int *)field = settings->res_auto ? RES_MATCH : choice.value;
            } else if (option->type == CONFIG_UPSCALER) {
                settings->upscaler_auto = choice.value == CONFIG_AUTO;
                if (!settings->upscaler_auto) *(int *)field = choice.value;
            } else *(int *)field = choice.value;
            return true;
        }
        return false;
    }
    }
}

// Parse a copy when saving so whitespace, comments and unknown entries survive.
typedef struct {
    char *key, *value;
    size_t value_start, value_end;
} ConfigLine;
enum { CONFIG_SECTION_CAP = SF2C_SETTINGS_N_64_A68B, CONFIG_LINE_CAP = SF2C_SETTINGS_N_1024_E39E };

static bool config_line(char *line, char section[CONFIG_SECTION_CAP], ConfigLine *entry) {
    *entry = (ConfigLine){0};
    config_remove_comment(line);
    char *text = config_trim(line);
    if (!*text) return true;
    if (*text == SF2C_SETTINGS_CH_EMPTY_854D) {
        char *end = strchr(text + 1, SF2C_SETTINGS_CH_EMPTY_CB80);
        if (!end || *config_trim(end + 1)) goto invalid;
        *end = 0;
        char *name = config_trim(text + 1);
        if (!*name || strlen(name) >= CONFIG_SECTION_CAP) goto invalid;
        strcpy(section, name);
        return true;
    }
    char *equals = strchr(text, SF2C_SETTINGS_CH_EMPTY_C874);
    if (!equals) goto invalid;
    *equals = 0;
    entry->key = config_trim(text);
    entry->value = config_trim(equals + 1);
    size_t length = strlen(entry->value);
    entry->value_start = entry->value - line;
    entry->value_end = entry->value_start + length;
    if (length >= SF2C_SETTINGS_N_2_D473 &&
        (entry->value[0] == SF2C_SETTINGS_CH_EMPTY_EB70 || entry->value[0] == SF2C_SETTINGS_S_EMPTY_3251) &&
        entry->value[length - 1] == entry->value[0]) {
        entry->value[length - 1] = 0;
        ++entry->value;
    }
    return true;
invalid:
    // Isolate malformed headers and their following keys.
    section[0] = 0;
    return false;
}

bool settings_load_file(const char *path) {
    FILE *file = fopen(path, SF2C_SETTINGS_S_R_2659);
    if (!file) return false;
    char line[CONFIG_LINE_CAP], section[CONFIG_SECTION_CAP] = SF2C_SETTINGS_S_GENERAL_A6F2;
    unsigned number = 0;
    while (fgets(line, sizeof line, file)) {
        ++number;
        if (!strchr(line, SF2C_SETTINGS_CH_N_AD65) && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != SF2C_SETTINGS_CH_N_AD65 && ch != EOF) {}
            fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_LINE_TOO_LONG_I_ED4C, path, number);
            continue;
        }
        ConfigLine entry;
        if (!config_line(line, section, &entry)) {
            fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_MALFORMED_LINE__C6E9, path, number);
            continue;
        }
        if (!entry.key) continue;
        const char *key = entry.key, *value = entry.value;
        if (!strcasecmp(section, SF2C_SETTINGS_S_CONTROLS_KEYBOARD_4375) || !strcasecmp(section, SF2C_SETTINGS_S_CONTROLS_GAMEPAD_C5B4)) {
            bool keyboard = !strcasecmp(section, SF2C_SETTINGS_S_CONTROLS_KEYBOARD_4375);
            if (bind_config_set(keyboard ? BIND_DEV_KEY : BIND_DEV_PAD, key, value) == BIND_CONFIG_INVALID)
                fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_INVALID_S_S_S_E_456B,
                        path, number, section, key, value,
                        keyboard ? SF2C_SETTINGS_S_AN_SDL_SCANCODE_NAME_D232 : SF2C_SETTINGS_S_A_CONTROLLER_POSITION_EAST_S_EBEB);
            continue;
        }
        for (size_t i = 0; i < sizeof config_options / sizeof config_options[0]; ++i) {
            const ConfigOption *option = &config_options[i];
            if (strcasecmp(section, config_sections[option->group]) || strcasecmp(key, option->key)) continue;
            if (!config_assign(&g_settings, option, value))
                fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_INVALID_S_S_KEE_B6F4, path, number, section, key);
            break;
        }
    }
    bool ok = !ferror(file);
    fclose(file);
    return ok;
}

void settings_load(void) {
    const char *path = getenv(SF2C_SETTINGS_S_SF2_CONFIG_3707);
    bool explicit_path = path && *path;
    if (!explicit_path) path = SF2C_SETTINGS_S_SF2RECOMP_INI_9F07;
    if (settings_load_file(path)) fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_LOADED_S_N_D8C5, path);
    else if (explicit_path) fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_CANNOT_READ_S_N_7365, path);
    if (snprintf(config_path, sizeof config_path, SF2C_SETTINGS_S_S_0030, path) >= sizeof config_path)
        config_path[0] = 0;
    saved_config.settings = g_settings;
    memcpy(saved_config.key, g_bind_key, sizeof saved_config.key);
    memcpy(saved_config.pad, g_bind_pad, sizeof saved_config.pad);
    launch_config = saved_config;
    settings_init();  // Test/session environment overrides take precedence over the file.
}

void settings_resolve_defaults(bool integrated) {
    g_gpu_integrated = integrated;
    if (g_settings.upscaler_auto) g_settings.upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    if (g_settings.res_auto) g_settings.res_mode = RES_MATCH;
}

enum {
    CONFIG_FIELD_COUNT = CONFIG_OPTION_COUNT + BIND_KEY_COUNT + BIND_PAD_COUNT,
    CONFIG_VALUE_CAP = SETTINGS_SAVE_DIRECTORY_CAP + sizeof SF2C_SETTINGS_S_EMPTY_8BAC
};

void settings_config_begin(SettingsConfig *draft) {
    *draft = saved_config;
}

unsigned settings_config_count(void) {
    return CONFIG_FIELD_COUNT;
}

SettingsGroup settings_config_group(unsigned field) {
    if (field < CONFIG_OPTION_COUNT) return config_options[field].group;
    return field < CONFIG_OPTION_COUNT + BIND_KEY_COUNT ? SETTINGS_GROUP_KEYBOARD : SETTINGS_GROUP_GAMEPAD;
}

static unsigned binding_slot(unsigned field) {
    return field < CONFIG_OPTION_COUNT + BIND_KEY_COUNT
        ? field - CONFIG_OPTION_COUNT : field - CONFIG_OPTION_COUNT - BIND_KEY_COUNT;
}

SettingsEditor settings_config_editor(unsigned field) {
    if (field >= CONFIG_OPTION_COUNT)
        return settings_config_group(field) == SETTINGS_GROUP_KEYBOARD ? SETTINGS_EDIT_KEY : SETTINGS_EDIT_PAD;
    switch (config_options[field].type) {
    case CONFIG_VOLUME: return SETTINGS_EDIT_VOLUME;
    case CONFIG_FRAME_RATE: return SETTINGS_EDIT_FPS;
    case CONFIG_PATH: return SETTINGS_EDIT_PATH;
    default: return SETTINGS_EDIT_CHOICE;
    }
}

const char *settings_config_label(unsigned field) {
    return field < CONFIG_OPTION_COUNT ? config_options[field].label : bind_input_name(binding_slot(field));
}

bool settings_config_requires_restart(unsigned field) {
    return field >= CONFIG_OPTION_COUNT || config_options[field].apply == CONFIG_RESTART;
}

const char *settings_config_override(unsigned field) {
    if (field >= CONFIG_OPTION_COUNT) return NULL;
    const char *name = NULL;
    switch (config_options[field].type) {
    case CONFIG_LANGUAGE: name = SF2C_SETTINGS_S_SF2RECOMP_LANG_74DD; break;
    case CONFIG_VOLUME: name = SF2C_SETTINGS_S_SF2RECOMP_VOLUME_6C32; break;
    case CONFIG_PATH: name = SF2C_SETTINGS_S_SF2RECOMP_SAVE_DIR_4AEE; break;
    default:
        if (config_options[field].offset == offsetof(Settings, reverb)) name = SF2C_SETTINGS_S_SF2RECOMP_REVERB_419C;
        break;
    }
    const char *value = name ? getenv(name) : NULL;
    Settings parsed;
    if (value && *value && config_assign(&parsed, &config_options[field], value)) return name;

    switch (config_options[field].offset) {
    case offsetof(Settings, reverb):
        if (getenv(SF2C_AUDIO_S_SF2RECOMP_NO_REVERB_0417)) return SF2C_AUDIO_S_SF2RECOMP_NO_REVERB_0417;
        break;
    case offsetof(Settings, fullscreen):
        if (getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_7651)) return SF2C_DEVICE_S_SF2RECOMP_WINDOW_7651;
        if (getenv(SF2C_DEVICE_S_SF2RECOMP_HIDDEN_AFF5)) return SF2C_DEVICE_S_SF2RECOMP_HIDDEN_AFF5;
        break;
    case offsetof(Settings, res_mode):
        value = getenv(SF2C_OUTPUT_SCALE_S_SF2RECOMP_SCALE_2675);
        if (value && atof(value) > 0) return SF2C_OUTPUT_SCALE_S_SF2RECOMP_SCALE_2675;
        break;
    default: break;
    }
    return NULL;
}

static const char *config_key(unsigned field) {
    return field < CONFIG_OPTION_COUNT ? config_options[field].key : bind_config_key(binding_slot(field));
}

static int config_choice_value(const Settings *settings, const ConfigOption *option) {
    if (option->type == CONFIG_RESOLUTION && settings->res_auto) return CONFIG_AUTO;
    if (option->type == CONFIG_UPSCALER && settings->upscaler_auto) return CONFIG_AUTO;
    return *(const int *)((const char *)settings + option->offset);
}

static void format_volume(float volume, char *out, size_t size) {
    // Display ordinary decimal input without exposing float rounding noise.
    for (int digits = 1; digits <= FLT_DECIMAL_DIG; ++digits) {
        snprintf(out, size, SF2C_SETTINGS_S_G_324A, digits, (double)volume);
        if (strtof(out, NULL) == volume) break;
    }
}

void settings_config_value(const SettingsConfig *draft, unsigned field, char *out, size_t size) {
    const char *value = SF2C_SETTINGS_S_EMPTY_12AE;
    if (field >= CONFIG_OPTION_COUNT) {
        unsigned slot = binding_slot(field);
        value = settings_config_group(field) == SETTINGS_GROUP_KEYBOARD
            ? bind_config_value(BIND_DEV_KEY, draft->key[slot]) : bind_config_value(BIND_DEV_PAD, draft->pad[slot]);
    } else {
        const ConfigOption *option = &config_options[field];
        const void *data = (const char *)&draft->settings + option->offset;
        switch (option->type) {
        case CONFIG_BOOL: value = *(const bool *)data ? SF2C_SETTINGS_S_TRUE_18D1 : SF2C_SETTINGS_S_FALSE_076D; break;
        case CONFIG_LANGUAGE:
            value = draft->settings.language == LANG_AUTO ? SF2C_SETTINGS_S_AUTO_6346 : g_language_codes[draft->settings.language];
            break;
        case CONFIG_VOLUME: format_volume(*(const float *)data, out, size); return;
        case CONFIG_FRAME_RATE: snprintf(out, size, SF2C_SETTINGS_S_D_42B3, *(const int *)data); return;
        case CONFIG_PATH: value = data; break;
        default: {
            ConfigChoices choices = config_choices[option->type];
            int selected = config_choice_value(&draft->settings, option);
            for (unsigned i = 0; i < choices.count - choices.aliases; ++i)
                if (choices.items[i].value == selected) { value = choices.items[i].name; break; }
            break;
        }
        }
    }
    snprintf(out, size, SF2C_SETTINGS_S_S_0030, value ? value : SF2C_SETTINGS_S_EMPTY_12AE);
}

// Quotes protect leading/trailing whitespace, comment markers and punctuation
// in binding names. Test the encoding with the same lexer the loader uses.
static bool config_encode(const char *value, char out[CONFIG_VALUE_CAP]) {
    const char *const quotes[] = {SF2C_SETTINGS_S_EMPTY_12AE, SF2C_SETTINGS_S_EMPTY_0C92, SF2C_SETTINGS_S_EMPTY_53BD};
    for (unsigned i = 0; i < sizeof quotes / sizeof quotes[0]; ++i) {
        char line[CONFIG_LINE_CAP], section[CONFIG_SECTION_CAP] = {0};
        int len = snprintf(out, CONFIG_VALUE_CAP, SF2C_SETTINGS_S_S_S_S_DA90, quotes[i], value, quotes[i]);
        if (len < 0 || len >= CONFIG_VALUE_CAP) return false;
        snprintf(line, sizeof line, SF2C_SETTINGS_S_X_S_COMMENT_23AA, out);
        ConfigLine entry;
        if (config_line(line, section, &entry) && entry.value && !strcmp(entry.value, value)) return true;
    }
    return false;
}

bool settings_config_set(SettingsConfig *draft, unsigned field, const char *value) {
    char encoded[CONFIG_VALUE_CAP];
    if (field >= CONFIG_FIELD_COUNT || !config_encode(value, encoded)) return false;
    if (field < CONFIG_OPTION_COUNT) return config_assign(&draft->settings, &config_options[field], value);
    unsigned slot = binding_slot(field);
    return settings_config_group(field) == SETTINGS_GROUP_KEYBOARD
        ? bind_config_parse(BIND_DEV_KEY, value, &draft->key[slot])
        : bind_config_parse(BIND_DEV_PAD, value, &draft->pad[slot]);
}

void settings_config_step(SettingsConfig *draft, unsigned field, int direction) {
    if (field >= CONFIG_OPTION_COUNT) return;
    const ConfigOption *option = &config_options[field];
    Settings *settings = &draft->settings;
    void *data = (char *)settings + option->offset;
    switch (option->type) {
    case CONFIG_BOOL: *(bool *)data = !*(bool *)data; break;
    case CONFIG_LANGUAGE: {
        int count = N_LANGUAGES + 1;
        settings->language = (settings->language + 1 + count + direction) % count - 1;
        break;
    }
    case CONFIG_VOLUME: {
        const float step = SF2C_SETTINGS_N_0_05F_D41A;
        float value = roundf(settings->volume / step + direction) * step;
        settings->volume = fminf(1, fmaxf(0, value));
        break;
    }
    case CONFIG_FRAME_RATE: {
        int fps = settings->target_fps + direction;
        if (fps >= SF2C_SETTINGS_N_30_624B && fps <= SF2C_SETTINGS_N_360_838F) settings->target_fps = fps;
        break;
    }
    case CONFIG_PATH: break;
    default: {
        ConfigChoices choices = config_choices[option->type];
        int selected = config_choice_value(settings, option);
        int count = choices.count - choices.aliases;
        int index = 0;
        while (index < count && choices.items[index].value != selected) ++index;
        index = (index + count + direction) % count;
        config_assign(settings, option, choices.items[index].name);
        break;
    }
    }
}

static bool config_write(FILE *in, FILE *out, const bool changed[], const SettingsConfig *draft) {
    bool written[CONFIG_FIELD_COUNT] = {0}, append[CONFIG_FIELD_COUNT] = {0};
    char raw[CONFIG_LINE_CAP], section[CONFIG_SECTION_CAP] = SF2C_SETTINGS_S_GENERAL_A6F2;
    bool continuation = false;
    while (in && fgets(raw, sizeof raw, in)) {
        bool partial = !strchr(raw, SF2C_SETTINGS_CH_N_AD65) && !feof(in);
        bool handled = false;
        if (!partial && !continuation) {
            char line[CONFIG_LINE_CAP];
            strcpy(line, raw);
            ConfigLine entry;
            if (config_line(line, section, &entry) && entry.key) {
                for (unsigned i = 0; i < CONFIG_FIELD_COUNT; ++i) {
                    if (!changed[i] || strcasecmp(section, config_sections[settings_config_group(i)]) ||
                        strcasecmp(entry.key, config_key(i))) continue;
                    char value[CONFIG_VALUE_CAP], encoded[CONFIG_VALUE_CAP];
                    settings_config_value(draft, i, value, sizeof value);
                    if (!config_encode(value, encoded)) return false;
                    size_t length = entry.value_start + strlen(encoded) + strlen(raw + entry.value_end);
                    if (length >= sizeof raw - 1) {
                        append[i] = true;
                        break;
                    }
                    append[i] = false;
                    if (fprintf(out, SF2C_SETTINGS_S_S_S_S_D539, (int)entry.value_start, raw, encoded, raw + entry.value_end) < 0)
                        return false;
                    written[i] = handled = true;
                    break;
                }
            }
        }
        if (!handled && fputs(raw, out) == EOF) return false;
        continuation = partial;
    }
    if (in && ferror(in)) return false;
    for (SettingsGroup group = 0; group < SETTINGS_GROUP_COUNT; ++group) {
        bool header = false;
        for (unsigned i = 0; i < CONFIG_FIELD_COUNT; ++i) {
            if (!changed[i] || (written[i] && !append[i]) || settings_config_group(i) != group) continue;
            if (!header && fprintf(out, SF2C_SETTINGS_S_N_S_N_324E, config_sections[group]) < 0) return false;
            header = true;
            char value[CONFIG_VALUE_CAP], encoded[CONFIG_VALUE_CAP];
            settings_config_value(draft, i, value, sizeof value);
            if (!config_encode(value, encoded) || fprintf(out, SF2C_SETTINGS_S_S_S_N_C2E5, config_key(i), encoded) < 0) return false;
        }
    }
    return !ferror(out);
}

static bool config_different(const SettingsConfig *a, const SettingsConfig *b, unsigned field) {
    char before[CONFIG_VALUE_CAP], after[CONFIG_VALUE_CAP];
    settings_config_value(a, field, before, sizeof before);
    settings_config_value(b, field, after, sizeof after);
    return strcmp(before, after) != 0;
}

static void config_apply_live(const ConfigOption *option, const Settings *settings) {
    switch (option->type) {
    case CONFIG_VOLUME: {
        float volume = settings->volume;
        __atomic_store(&g_settings.volume, &volume, __ATOMIC_RELAXED);
        break;
    }
    case CONFIG_RESOLUTION:
        g_settings.res_auto = settings->res_auto;
        __atomic_store_n(&g_settings.res_mode, settings->res_mode, __ATOMIC_RELAXED);
        break;
    case CONFIG_UPSCALER:
        g_settings.upscaler_auto = settings->upscaler_auto;
        __atomic_store_n(&g_settings.upscaler, settings->upscaler_auto ?
            (g_gpu_integrated ? UPSCALE_BILINEAR : UPSCALE_FSR1) : settings->upscaler, __ATOMIC_RELAXED);
        break;
    case CONFIG_ANTIALIAS:
        __atomic_store_n(&g_settings.antialias, settings->antialias, __ATOMIC_RELAXED);
        break;
    case CONFIG_BOOL: {
        bool value = *(const bool *)((const char *)settings + option->offset);
        if (option->offset == offsetof(Settings, reverb))
            __atomic_store_n(&g_settings.reverb, value, __ATOMIC_RELAXED);
        else
            *(bool *)((char *)&g_settings + option->offset) = value; // guest-owned gameplay settings
        break;
    }
    default: break;
    }
}

static bool config_replace(const SettingsConfig *draft, const bool changed[]) {
    if (!config_path[0]) return false;
    char resolved[PATH_MAX], temp[PATH_MAX];
    const char *path = realpath(config_path, resolved);
    if (!path) {
        if (errno != ENOENT) return false;
        path = config_path;
    }
    FILE *in = fopen(path, SF2C_SETTINGS_S_RB_46CA);
    if (!in && errno != ENOENT) return false;
    int length = snprintf(temp, sizeof temp, SF2C_SETTINGS_S_S_TMP_XXXXXX_B30E, path);
    int fd = -1;
    if (length >= 0 && length < sizeof temp) {
#ifdef _WIN32
        if (!_mktemp_s(temp, sizeof temp)) fd = open(temp, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, _S_IREAD | _S_IWRITE);
#else
        fd = mkstemp(temp);
#endif
    }
    FILE *out = fd < 0 ? NULL : fdopen(fd, SF2C_SETTINGS_S_WB_3D6A);
    bool ok = out && config_write(in, out, changed, draft);
#if !defined(_WIN32) && !defined(__ANDROID__)
    if (ok && in) {
        struct stat st;
        ok = fstat(fileno(in), &st) == 0 && fchmod(fd, st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == 0;
    }
#endif
    if (in) fclose(in);
    if (out) {
        ok = ok && fflush(out) == 0 && fsync(fd) == 0;
        if (fclose(out)) ok = false;
    } else if (fd >= 0) close(fd);
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        ok = rename(temp, path) == 0;
#endif
    }
    if (!ok && fd >= 0) unlink(temp);
    return ok;
}

bool settings_config_save(const SettingsConfig *draft, SettingsSaveResult *result) {
    *result = (SettingsSaveResult){0};
    bool changed[CONFIG_FIELD_COUNT], any = false;
    for (unsigned i = 0; i < CONFIG_FIELD_COUNT; ++i) {
        changed[i] = config_different(&saved_config, draft, i);
        any |= changed[i];
    }
    if (any && !config_replace(draft, changed)) return false;
    saved_config = *draft;

    for (unsigned i = 0; i < CONFIG_FIELD_COUNT; ++i) {
        const char *override = settings_config_override(i);
        if (override) {
            if (changed[i]) ++result->overridden;
        } else if (settings_config_requires_restart(i)) {
            if (config_different(&launch_config, draft, i)) ++result->restart_pending;
        } else {
            const ConfigOption *option = &config_options[i];
            if (option->apply == CONFIG_WINDOW) {
                // Retry after a failed window change even if the INI already holds the new value.
                result->request_fullscreen = draft->settings.fullscreen != g_settings.fullscreen;
            } else if (changed[i]) {
                config_apply_live(option, &draft->settings);
                ++result->applied;
                result->refresh_fight_settings |= option->apply == CONFIG_FIGHT;
            }
        }
    }
    return true;
}

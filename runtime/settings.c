#include "constants/core.h"
// SF2 Recomp settings: defaults, editable INI file, then environment overrides.
#include "settings.h"
#include "input_bind.h"

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

// Defaults from environment overrides (LARECOMP_VOLUME / LARECOMP_REVERB / LARECOMP_LANG) — called by boot.
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
    env_float(SF2C_SETTINGS_S_LARECOMP_VOLUME_6C32, &g_settings.volume);
    const char *r = getenv(SF2C_SETTINGS_S_LARECOMP_REVERB_419C);
    if (r && *r && !parse_bool(r, &g_settings.reverb))
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_INVALID_LARECOMP_RE_ED26, r);
    const char *save = getenv(SF2C_SETTINGS_S_LARECOMP_SAVE_DIR_4AEE);
    if (save && !assign_path(g_settings.save_directory, sizeof g_settings.save_directory, save))
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_INVALID_LARECOMP_SA_EC52);
    const char *lang = getenv(SF2C_SETTINGS_S_LARECOMP_LANG_74DD);
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
typedef struct { const char *section, *key; ConfigType type; void *value; size_t capacity; } ConfigOption;
static const ConfigOption config_options[] = {
    {SF2C_SETTINGS_S_GENERAL_A6F2, SF2C_SETTINGS_S_LANGUAGE_1182, CONFIG_LANGUAGE, &g_settings.language},
    {SF2C_SETTINGS_S_NETWORK_57B8, SF2C_SETTINGS_S_ENABLED_6E30, CONFIG_BOOL, &g_settings.network_enabled},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_UNLOCK_ALL_SPARRING_CHARACTE_8856, CONFIG_BOOL, &g_settings.unlock_all_sparring_characters},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_ENABLE_SHADOW_IN_SPARRING_CAFB, CONFIG_BOOL, &g_settings.enable_shadow_in_sparring},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_ENABLE_TITAN_IN_SPARRING_3232, CONFIG_BOOL, &g_settings.enable_titan_in_sparring},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_ALLOW_SPARRING_LAN_MULTIPLAY_290D, CONFIG_BOOL, &g_settings.allow_sparring_lan_multiplayer},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_UNLOCK_ALL_VIDEOS_A8A0, CONFIG_BOOL, &g_settings.unlock_all_videos},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_SHOW_INTRO_88D4, CONFIG_BOOL, &g_settings.show_intro},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_AI_ENABLED_8057, CONFIG_BOOL, &g_settings.ai_enabled},
    {SF2C_SETTINGS_S_GAMEPLAY_406B, SF2C_SETTINGS_S_SHOW_TIME_RESULTS_FC20, CONFIG_BOOL, &g_settings.show_time_results},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_CONSOLE_3593, CONFIG_BOOL, &g_settings.developer_console},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_TRAINER_EBE0, CONFIG_BOOL, &g_settings.developer_trainer},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_SHOP_CHEATS_9653, CONFIG_BOOL, &g_settings.developer_shop_cheats},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_OVERLAYS_178F, CONFIG_BOOL, &g_settings.developer_overlays},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_OVERLAYS_ON_START_E5DA, CONFIG_BOOL, &g_settings.developer_overlays_on_start},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_FPS_A9EF, CONFIG_BOOL, &g_settings.developer_fps},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_MEMORY_E386, CONFIG_BOOL, &g_settings.developer_memory},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_BUILD_INFO_25FA, CONFIG_BOOL, &g_settings.developer_build_info},
    {SF2C_SETTINGS_S_DEVELOPER_7E6C, SF2C_SETTINGS_S_MODEL_INFO_0D1C, CONFIG_BOOL, &g_settings.developer_model_info},
    {SF2C_SETTINGS_S_AUDIO_53B7, SF2C_SETTINGS_S_VOLUME_3FF4, CONFIG_VOLUME, &g_settings.volume},
    {SF2C_SETTINGS_S_AUDIO_53B7, SF2C_SETTINGS_S_REVERB_6A96, CONFIG_BOOL, &g_settings.reverb},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_FULLSCREEN_962F, CONFIG_BOOL, &g_settings.fullscreen},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_ASPECT_RATIO_46A4, CONFIG_ASPECT, &g_settings.aspect},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_TARGET_FPS_10CA, CONFIG_FRAME_RATE, &g_settings.target_fps},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_INTERNAL_RESOLUTION_1625, CONFIG_RESOLUTION, &g_settings.res_mode},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_UPSCALER_3302, CONFIG_UPSCALER, &g_settings.upscaler},
    {SF2C_SETTINGS_S_VIDEO_807F, SF2C_SETTINGS_S_ANTIALIASING_FC81, CONFIG_ANTIALIAS, &g_settings.antialias},
    {SF2C_SETTINGS_S_INPUT_48AE, SF2C_SETTINGS_S_PROMPTS_6DA4, CONFIG_PROMPTS, &g_settings.prompts},
    {SF2C_SETTINGS_S_INPUT_48AE, SF2C_SETTINGS_S_PLAYER_SETUP_C139, CONFIG_PLAYER_SETUP, &g_settings.player_setup},
    {SF2C_SETTINGS_S_STORAGE_6545, SF2C_SETTINGS_S_USE_COMPLETED_SAVE_9A94, CONFIG_BOOL, &g_settings.use_completed_save},
    {SF2C_SETTINGS_S_STORAGE_6545, SF2C_SETTINGS_S_SAVE_DIRECTORY_4C87, CONFIG_PATH, g_settings.save_directory, sizeof g_settings.save_directory},
};

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

static bool config_assign(const ConfigOption *option, const char *value) {
    if (option->type == CONFIG_PLAYER_SETUP) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_SOLO_C940)) *(int *)option->value = PLAYER_SETUP_SOLO;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_KEYBOARD_VS_CONTROLLER_18CB)) *(int *)option->value = PLAYER_SETUP_KEYBOARD_VS_CONTROLLER;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_TWO_CONTROLLERS_4B78)) *(int *)option->value = PLAYER_SETUP_TWO_CONTROLLERS;
        else return false;
    } else if (option->type == CONFIG_ASPECT) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_ORIGINAL_4D60) || !strcmp(value, SF2C_SETTINGS_S_16_9_0C60)) *(int *)option->value = ASPECT_ORIGINAL;
        else if (!strcmp(value, SF2C_SETTINGS_S_16_10_7BA3)) *(int *)option->value = ASPECT_16_10;
        else if (!strcmp(value, SF2C_SETTINGS_S_21_9_80DF)) *(int *)option->value = ASPECT_21_9;
        else if (!strcmp(value, SF2C_SETTINGS_S_32_9_307E)) *(int *)option->value = ASPECT_32_9;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_NATIVE_4333)) *(int *)option->value = ASPECT_NATIVE;
        else return false;
    } else if (option->type == CONFIG_LANGUAGE) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346)) { *(int *)option->value = LANG_AUTO; return true; }
        int language = settings_language_index(value);
        if (language < 0) return false;
        *(int *)option->value = language;
    } else if (option->type == CONFIG_BOOL) {
        return parse_bool(value, option->value);
    } else if (option->type == CONFIG_PATH) {
        return assign_path(option->value, option->capacity, value);
    } else if (option->type == CONFIG_FRAME_RATE) {
        char *end;
        errno = 0;
        long fps = strtol(value, &end, SF2C_SETTINGS_N_10_4A44);
        if (errno || end == value || *end || fps < SF2C_SETTINGS_N_30_624B || fps > SF2C_SETTINGS_N_360_838F) return false;
        *(int *)option->value = (int)fps;
    } else if (option->type == CONFIG_RESOLUTION) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346) || !strcasecmp(value, SF2C_SETTINGS_S_MATCH_7C82)) {
            g_settings.res_mode = RES_MATCH;
            g_settings.res_auto = !strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346);
            return true;
        }
        if (!strcasecmp(value, SF2C_SETTINGS_S_DYNAMIC_68ED)) g_settings.res_mode = RES_DYNAMIC;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_0_25X_63EF)) g_settings.res_mode = RES_Q25;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_0_50X_1CF2) || !strcasecmp(value, SF2C_SETTINGS_S_0_5X_82D2)) g_settings.res_mode = RES_Q50;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_0_75X_50AC)) g_settings.res_mode = RES_Q75;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_1X_9FF3)) g_settings.res_mode = 1;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_2X_5383)) g_settings.res_mode = SF2C_SETTINGS_N_2_D473;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_3X_2593)) g_settings.res_mode = SF2C_SETTINGS_N_3_4E07;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_4X_6992)) g_settings.res_mode = SF2C_SETTINGS_N_4_4B22;
        else return false;
        g_settings.res_auto = false;
    } else if (option->type == CONFIG_UPSCALER) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346)) {
            g_settings.upscaler_auto = true;
            return true;
        }
        if (!strcasecmp(value, SF2C_SETTINGS_S_BILINEAR_70ED)) g_settings.upscaler = UPSCALE_BILINEAR;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_FSR1_B074)) g_settings.upscaler = UPSCALE_FSR1;
        else return false;  // MetalFX is not available in this Vulkan build.
        g_settings.upscaler_auto = false;
    } else if (option->type == CONFIG_ANTIALIAS) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_OFF_9F3B)) *(int *)option->value = AA_OFF;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_SMAA_4056)) *(int *)option->value = AA_SMAA;
        else return false;
    } else if (option->type == CONFIG_PROMPTS) {
        if (!strcasecmp(value, SF2C_SETTINGS_S_AUTO_6346)) *(int *)option->value = PROMPTS_AUTO;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_NINTENDO_AF74)) *(int *)option->value = PROMPTS_NINTENDO;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_XBOX_76AE)) *(int *)option->value = PROMPTS_XBOX;
        else if (!strcasecmp(value, SF2C_SETTINGS_S_PLAYSTATION_15B2)) *(int *)option->value = PROMPTS_PLAYSTATION;
        else return false;
    } else {
        char *end;
        errno = 0;
        float volume = strtof(value, &end);
        if (errno || end == value || *end || !isfinite(volume) || volume < 0 || volume > 1) return false;
        *(float *)option->value = volume;
    }
    return true;
}

bool settings_load_file(const char *path) {
    FILE *file = fopen(path, SF2C_SETTINGS_S_R_2659);
    if (!file) return false;
    char line[SF2C_SETTINGS_N_1024_E39E], section[SF2C_SETTINGS_N_64_A68B] = SF2C_SETTINGS_S_GENERAL_A6F2;
    unsigned number = 0;
    while (fgets(line, sizeof line, file)) {
        ++number;
        if (!strchr(line, SF2C_SETTINGS_CH_N_AD65) && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != SF2C_SETTINGS_CH_N_AD65 && ch != EOF) {}
            fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_LINE_TOO_LONG_I_ED4C, path, number);
            continue;
        }
        config_remove_comment(line);
        char *text = config_trim(line);
        if (!*text) continue;
        if (*text == SF2C_SETTINGS_CH_EMPTY_854D) {
            char *end = strchr(text + 1, SF2C_SETTINGS_CH_EMPTY_CB80);
            if (!end || *config_trim(end + 1)) goto invalid_line;
            *end = 0;
            char *name = config_trim(text + 1);
            if (!*name || strlen(name) >= sizeof section) goto invalid_line;
            snprintf(section, sizeof section, SF2C_SETTINGS_S_S_0030, name);
            continue;
        }
        char *equals = strchr(text, SF2C_SETTINGS_CH_EMPTY_C874);
        if (!equals) goto invalid_line;
        *equals = 0;
        char *key = config_trim(text), *value = config_trim(equals + 1);
        size_t length = strlen(value);
        if (length >= SF2C_SETTINGS_N_2_D473 && (value[0] == SF2C_SETTINGS_CH_EMPTY_EB70 || value[0] == SF2C_SETTINGS_S_EMPTY_3251) && value[length - 1] == value[0]) {
            value[length - 1] = 0;
            ++value;
        }
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
            if (strcasecmp(section, option->section) || strcasecmp(key, option->key)) continue;
            if (!config_assign(option, value))
                fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_INVALID_S_S_KEE_B6F4, path, number, section, key);
            break;
        }
        continue;
invalid_line:
        // Isolate a malformed section so its following keys cannot alter the
        // previous valid section by accident.
        section[0] = 0;
        fprintf(stderr, SF2C_SETTINGS_S_SETTINGS_S_U_MALFORMED_LINE__C6E9, path, number);
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
    settings_init();  // Test/session environment overrides take precedence over the file.
}

void settings_resolve_defaults(bool integrated) {
    g_gpu_integrated = integrated;
    if (g_settings.upscaler_auto) g_settings.upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    if (g_settings.res_auto) g_settings.res_mode = RES_MATCH;
}

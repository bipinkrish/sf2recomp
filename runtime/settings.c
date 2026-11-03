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
    "ja", "en-US", "en-GB", "fr", "de", "it", "es", "zh-CN", "zh-TW", "ru", "pt", "ko",
};
const char *const g_language_names[N_LANGUAGES] = {
    "Japanese", "English (US)", "English (UK)", "French", "German", "Italian", "Spanish",
    "Chinese (simplified)", "Chinese (traditional)", "Russian", "Portuguese", "Korean",
};

bool g_gpu_integrated = false;

Settings g_settings = {
    .volume = 1.0f,
    .reverb = true,
    .language = LANG_AUTO,
    .aspect = ASPECT_ORIGINAL,
    .target_fps = 60,
    .res_mode = RES_MATCH,
    .res_auto = true,
    .upscaler = UPSCALE_BILINEAR,
    .upscaler_auto = true,
    .antialias = AA_OFF,
    .dpad_move = true,
    .rumble = true,
    .rumble_strength = 100,
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
    .save_directory = "save",
};

// Defaults from environment overrides (LARECOMP_VOLUME / LARECOMP_REVERB / LARECOMP_LANG) — called by boot.
static const char *env_float(const char *name, float *out) {
    const char *e = getenv(name);
    if (e && *e) {
        char *end;
        errno = 0;
        float v = strtof(e, &end);
        if (!errno && end != e && !*end && isfinite(v) && v >= 0 && v <= 1) { *out = v; return e; }
        fprintf(stderr, "[settings] invalid %s='%s', expected 0..1, keeping previous value\n", name, e);
    }
    return NULL;
}

static bool parse_bool(const char *value, bool *out) {
    bool on = !strcasecmp(value, "true") || !strcasecmp(value, "yes") ||
              !strcasecmp(value, "on") || !strcmp(value, "1");
    bool off = !strcasecmp(value, "false") || !strcasecmp(value, "no") ||
               !strcasecmp(value, "off") || !strcmp(value, "0");
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
    env_float("LARECOMP_VOLUME", &g_settings.volume);
    const char *r = getenv("LARECOMP_REVERB");
    if (r && *r && !parse_bool(r, &g_settings.reverb))
        fprintf(stderr, "[settings] invalid LARECOMP_REVERB='%s', expected true/false, keeping previous value\n", r);
    const char *save = getenv("LARECOMP_SAVE_DIR");
    if (save && !assign_path(g_settings.save_directory, sizeof g_settings.save_directory, save))
        fprintf(stderr, "[settings] invalid LARECOMP_SAVE_DIR, keeping previous save directory\n");
    const char *lang = getenv("LARECOMP_LANG");
    if (lang && *lang) {
        if (!strcasecmp(lang, "auto")) { g_settings.language = LANG_AUTO; return; }
        int i = settings_language_index(lang);
        if (i < 0) fprintf(stderr, "[settings] unknown language code %s (use one of %s...)\n", lang, g_language_codes[0]);
        else g_settings.language = i;
    }
}

const char *settings_language(void) {
    if (g_settings.language >= 0 && g_settings.language < N_LANGUAGES)
        return g_language_codes[g_settings.language];
    const char *locales[] = {getenv("LC_ALL"), getenv("LC_MESSAGES"), getenv("LANG")};
    for (size_t i = 0; i < sizeof locales / sizeof locales[0]; ++i) {
        if (!locales[i] || !*locales[i]) continue;
        int language = settings_language_index(locales[i]);
        if (language >= 0) return g_language_codes[language];
    }
    return "en-US";
}

const char *settings_language_name(void) {
    int language = settings_language_index(settings_language());
    return g_language_names[language >= 0 ? language : 1];
}

int settings_language_index(const char *code) {
    if (!code || !*code) return LANG_AUTO;
    char normalized[64];
    size_t length = strcspn(code, ".@");
    if (length >= sizeof normalized) return -1;
    memcpy(normalized, code, length);
    normalized[length] = 0;
    for (size_t i = 0; i < length; ++i) if (normalized[i] == '_') normalized[i] = '-';
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcasecmp(normalized, g_language_codes[i])) return i;
    // Select a supported regional variant for a language-only or host locale
    // (en, en-IN, de-DE, ko-Hangul), returning the canonical Switch identifier.
    size_t base = strcspn(normalized, "-");
    for (int i = 0; i < N_LANGUAGES; i++) {
        const char *supported = g_language_codes[i];
        if (strcspn(supported, "-") == base && !strncasecmp(normalized, supported, base)) return i;
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
    {"general", "language", CONFIG_LANGUAGE, &g_settings.language},
    {"network", "enabled", CONFIG_BOOL, &g_settings.network_enabled},
    {"gameplay", "unlock_all_sparring_characters", CONFIG_BOOL, &g_settings.unlock_all_sparring_characters},
    {"gameplay", "enable_shadow_in_sparring", CONFIG_BOOL, &g_settings.enable_shadow_in_sparring},
    {"gameplay", "enable_titan_in_sparring", CONFIG_BOOL, &g_settings.enable_titan_in_sparring},
    {"gameplay", "allow_sparring_lan_multiplayer", CONFIG_BOOL, &g_settings.allow_sparring_lan_multiplayer},
    {"gameplay", "unlock_all_videos", CONFIG_BOOL, &g_settings.unlock_all_videos},
    {"gameplay", "show_intro", CONFIG_BOOL, &g_settings.show_intro},
    {"gameplay", "ai_enabled", CONFIG_BOOL, &g_settings.ai_enabled},
    {"gameplay", "show_time_results", CONFIG_BOOL, &g_settings.show_time_results},
    {"developer", "console", CONFIG_BOOL, &g_settings.developer_console},
    {"developer", "trainer", CONFIG_BOOL, &g_settings.developer_trainer},
    {"developer", "shop_cheats", CONFIG_BOOL, &g_settings.developer_shop_cheats},
    {"developer", "overlays", CONFIG_BOOL, &g_settings.developer_overlays},
    {"developer", "overlays_on_start", CONFIG_BOOL, &g_settings.developer_overlays_on_start},
    {"developer", "fps", CONFIG_BOOL, &g_settings.developer_fps},
    {"developer", "memory", CONFIG_BOOL, &g_settings.developer_memory},
    {"developer", "build_info", CONFIG_BOOL, &g_settings.developer_build_info},
    {"developer", "model_info", CONFIG_BOOL, &g_settings.developer_model_info},
    {"audio", "volume", CONFIG_VOLUME, &g_settings.volume},
    {"audio", "reverb", CONFIG_BOOL, &g_settings.reverb},
    {"video", "fullscreen", CONFIG_BOOL, &g_settings.fullscreen},
    {"video", "aspect_ratio", CONFIG_ASPECT, &g_settings.aspect},
    {"video", "target_fps", CONFIG_FRAME_RATE, &g_settings.target_fps},
    {"video", "internal_resolution", CONFIG_RESOLUTION, &g_settings.res_mode},
    {"video", "upscaler", CONFIG_UPSCALER, &g_settings.upscaler},
    {"video", "antialiasing", CONFIG_ANTIALIAS, &g_settings.antialias},
    {"input", "prompts", CONFIG_PROMPTS, &g_settings.prompts},
    {"input", "player_setup", CONFIG_PLAYER_SETUP, &g_settings.player_setup},
    {"storage", "use_completed_save", CONFIG_BOOL, &g_settings.use_completed_save},
    {"storage", "save_directory", CONFIG_PATH, g_settings.save_directory, sizeof g_settings.save_directory},
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
        if ((*s == '\'' || *s == '"') && (!quote || *s == quote))
            quote = quote ? 0 : *s;
        else if (!quote && (*s == '#' || *s == ';')) { *s = 0; break; }
    }
}

static bool config_assign(const ConfigOption *option, const char *value) {
    if (option->type == CONFIG_PLAYER_SETUP) {
        if (!strcasecmp(value, "solo")) *(int *)option->value = PLAYER_SETUP_SOLO;
        else if (!strcasecmp(value, "keyboard_vs_controller")) *(int *)option->value = PLAYER_SETUP_KEYBOARD_VS_CONTROLLER;
        else if (!strcasecmp(value, "two_controllers")) *(int *)option->value = PLAYER_SETUP_TWO_CONTROLLERS;
        else return false;
    } else if (option->type == CONFIG_ASPECT) {
        if (!strcasecmp(value, "original") || !strcmp(value, "16:9")) *(int *)option->value = ASPECT_ORIGINAL;
        else if (!strcmp(value, "16:10")) *(int *)option->value = ASPECT_16_10;
        else if (!strcmp(value, "21:9")) *(int *)option->value = ASPECT_21_9;
        else if (!strcmp(value, "32:9")) *(int *)option->value = ASPECT_32_9;
        else if (!strcasecmp(value, "native")) *(int *)option->value = ASPECT_NATIVE;
        else return false;
    } else if (option->type == CONFIG_LANGUAGE) {
        if (!strcasecmp(value, "auto")) { *(int *)option->value = LANG_AUTO; return true; }
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
        long fps = strtol(value, &end, 10);
        if (errno || end == value || *end || fps < 30 || fps > 360) return false;
        *(int *)option->value = (int)fps;
    } else if (option->type == CONFIG_RESOLUTION) {
        if (!strcasecmp(value, "auto") || !strcasecmp(value, "match")) {
            g_settings.res_mode = RES_MATCH;
            g_settings.res_auto = !strcasecmp(value, "auto");
            return true;
        }
        if (!strcasecmp(value, "dynamic")) g_settings.res_mode = RES_DYNAMIC;
        else if (!strcasecmp(value, "0.25x")) g_settings.res_mode = RES_Q25;
        else if (!strcasecmp(value, "0.50x") || !strcasecmp(value, "0.5x")) g_settings.res_mode = RES_Q50;
        else if (!strcasecmp(value, "0.75x")) g_settings.res_mode = RES_Q75;
        else if (!strcasecmp(value, "1x")) g_settings.res_mode = 1;
        else if (!strcasecmp(value, "2x")) g_settings.res_mode = 2;
        else if (!strcasecmp(value, "3x")) g_settings.res_mode = 3;
        else if (!strcasecmp(value, "4x")) g_settings.res_mode = 4;
        else return false;
        g_settings.res_auto = false;
    } else if (option->type == CONFIG_UPSCALER) {
        if (!strcasecmp(value, "auto")) {
            g_settings.upscaler_auto = true;
            return true;
        }
        if (!strcasecmp(value, "bilinear")) g_settings.upscaler = UPSCALE_BILINEAR;
        else if (!strcasecmp(value, "fsr1")) g_settings.upscaler = UPSCALE_FSR1;
        else return false;  // MetalFX is not available in this Vulkan build.
        g_settings.upscaler_auto = false;
    } else if (option->type == CONFIG_ANTIALIAS) {
        if (!strcasecmp(value, "off")) *(int *)option->value = AA_OFF;
        else if (!strcasecmp(value, "smaa")) *(int *)option->value = AA_SMAA;
        else return false;
    } else if (option->type == CONFIG_PROMPTS) {
        if (!strcasecmp(value, "auto")) *(int *)option->value = PROMPTS_AUTO;
        else if (!strcasecmp(value, "nintendo")) *(int *)option->value = PROMPTS_NINTENDO;
        else if (!strcasecmp(value, "xbox")) *(int *)option->value = PROMPTS_XBOX;
        else if (!strcasecmp(value, "playstation")) *(int *)option->value = PROMPTS_PLAYSTATION;
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
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[1024], section[64] = "general";
    unsigned number = 0;
    while (fgets(line, sizeof line, file)) {
        ++number;
        if (!strchr(line, '\n') && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) {}
            fprintf(stderr, "[settings] %s:%u line too long, ignored\n", path, number);
            continue;
        }
        config_remove_comment(line);
        char *text = config_trim(line);
        if (!*text) continue;
        if (*text == '[') {
            char *end = strchr(text + 1, ']');
            if (!end || *config_trim(end + 1)) goto invalid_line;
            *end = 0;
            char *name = config_trim(text + 1);
            if (!*name || strlen(name) >= sizeof section) goto invalid_line;
            snprintf(section, sizeof section, "%s", name);
            continue;
        }
        char *equals = strchr(text, '=');
        if (!equals) goto invalid_line;
        *equals = 0;
        char *key = config_trim(text), *value = config_trim(equals + 1);
        size_t length = strlen(value);
        if (length >= 2 && (value[0] == '\'' || value[0] == '"') && value[length - 1] == value[0]) {
            value[length - 1] = 0;
            ++value;
        }
        if (!strcasecmp(section, "controls.keyboard") || !strcasecmp(section, "controls.gamepad")) {
            bool keyboard = !strcasecmp(section, "controls.keyboard");
            if (bind_config_set(keyboard ? BIND_DEV_KEY : BIND_DEV_PAD, key, value) == BIND_CONFIG_INVALID)
                fprintf(stderr, "[settings] %s:%u invalid [%s] %s='%s'; expected %s or none, keeping previous binding\n",
                        path, number, section, key, value,
                        keyboard ? "an SDL scancode name" : "a controller position (east, south, ltrigger, ...)");
            continue;
        }
        for (size_t i = 0; i < sizeof config_options / sizeof config_options[0]; ++i) {
            const ConfigOption *option = &config_options[i];
            if (strcasecmp(section, option->section) || strcasecmp(key, option->key)) continue;
            if (!config_assign(option, value))
                fprintf(stderr, "[settings] %s:%u invalid [%s] %s, keeping previous value\n", path, number, section, key);
            break;
        }
        continue;
invalid_line:
        // Isolate a malformed section so its following keys cannot alter the
        // previous valid section by accident.
        section[0] = 0;
        fprintf(stderr, "[settings] %s:%u malformed line, ignored\n", path, number);
    }
    bool ok = !ferror(file);
    fclose(file);
    return ok;
}

void settings_load(void) {
    const char *path = getenv("SF2_CONFIG");
    bool explicit_path = path && *path;
    if (!explicit_path) path = "sf2recomp.ini";
    if (settings_load_file(path)) fprintf(stderr, "[settings] loaded %s\n", path);
    else if (explicit_path) fprintf(stderr, "[settings] cannot read %s\n", path);
    settings_init();  // Test/session environment overrides take precedence over the file.
}

void settings_resolve_defaults(bool integrated) {
    g_gpu_integrated = integrated;
    if (g_settings.upscaler_auto) g_settings.upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    if (g_settings.res_auto) g_settings.res_mode = RES_MATCH;
}

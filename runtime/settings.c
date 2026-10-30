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
    .res_mode = RES_MATCH,
    .upscaler = UPSCALE_BILINEAR,
    .antialias = AA_OFF,
    .dpad_move = true,
    .rumble = true,
    .rumble_strength = 100,
    .bg_input = true,
    .prompts = PROMPTS_AUTO,
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
typedef enum { CONFIG_LANGUAGE, CONFIG_BOOL, CONFIG_VOLUME, CONFIG_PATH } ConfigType;
typedef struct { const char *section, *key; ConfigType type; void *value; size_t capacity; } ConfigOption;
static const ConfigOption config_options[] = {
    {"general", "language", CONFIG_LANGUAGE, &g_settings.language},
    {"audio", "volume", CONFIG_VOLUME, &g_settings.volume},
    {"audio", "reverb", CONFIG_BOOL, &g_settings.reverb},
    {"video", "fullscreen", CONFIG_BOOL, &g_settings.fullscreen},
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
    if (option->type == CONFIG_LANGUAGE) {
        if (!strcasecmp(value, "auto")) { *(int *)option->value = LANG_AUTO; return true; }
        int language = settings_language_index(value);
        if (language < 0) return false;
        *(int *)option->value = language;
    } else if (option->type == CONFIG_BOOL) {
        return parse_bool(value, option->value);
    } else if (option->type == CONFIG_PATH) {
        return assign_path(option->value, option->capacity, value);
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
    g_settings.upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    g_settings.res_mode = RES_MATCH;
}

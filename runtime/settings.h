// SF2 Recomp settings, the subset the runtime needs before the UI exists
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ASPECT_UNLOCKED = 0, ASPECT_ORIGINAL = 1, ASPECT_21_9 = 2, ASPECT_16_10 = 3, ASPECT_32_9 = 4, ASPECT_NATIVE = 5 };
enum { RES_MATCH = 0, RES_DYNAMIC = 9, RES_Q75 = 10, RES_Q25 = 11, RES_Q50 = 12 };
enum { UPSCALE_BILINEAR = 0, UPSCALE_METALFX = 1, UPSCALE_FSR1 = 2 };
#define UPSCALE_SHARP UPSCALE_FSR1
enum { AA_OFF = 0, AA_SMAA = 1 };
enum { PLAYER_SETUP_SOLO = 0, PLAYER_SETUP_KEYBOARD_VS_CONTROLLER, PLAYER_SETUP_TWO_CONTROLLERS };
enum {
    PROMPTS_AUTO = 0, PROMPTS_NINTENDO = 1, PROMPTS_XBOX = 2, PROMPTS_PLAYSTATION = 3
};

extern bool g_gpu_integrated;

enum { LANG_AUTO = -1, N_LANGUAGES = 12 };
extern const char *const g_language_codes[N_LANGUAGES];
extern const char *const g_language_names[N_LANGUAGES];

// Leave room for a mount suffix and guest filenames in filesystem path buffers.
#define SETTINGS_SAVE_DIRECTORY_CAP 768

typedef struct Settings {
    float volume;
    bool reverb;
    int language;
    int aspect;
    unsigned native_width, native_height; // window dimensions latched before guest startup
    bool fullscreen;
    int target_fps;
    int res_mode;
    bool res_auto;
    int upscaler;
    bool upscaler_auto;
    int antialias;
    bool dpad_move;
    bool rumble;
    int rumble_strength;
    bool bg_input;
    int prompts;
    int player_setup;
    bool network_enabled;
    bool unlock_all_sparring_characters;
    bool enable_shadow_in_sparring;
    bool enable_titan_in_sparring;
    bool allow_sparring_lan_multiplayer;
    bool unlock_all_videos;
    bool show_intro;
    bool ai_enabled;
    bool show_time_results;
    bool developer_console;
    bool developer_trainer;
    bool developer_shop_cheats;
    bool developer_overlays;
    bool developer_overlays_on_start;
    bool developer_fps;
    bool developer_memory;
    bool developer_build_info;
    bool developer_model_info;
    int screen_filter;
    bool use_completed_save;
    char save_directory[SETTINGS_SAVE_DIRECTORY_CAP];
} Settings;

extern Settings g_settings;

// Native screen dimensions must agree across guest modes, buffers and presentation.
// All supported ratios have exact integer widths at both 720 and 1080 lines.
static inline unsigned settings_screen_width(unsigned height) {
    switch (g_settings.aspect) {
    case ASPECT_NATIVE:
        if (g_settings.native_width && g_settings.native_height)
            return ((unsigned long long)height * g_settings.native_width / g_settings.native_height + 1) & ~1u;
        return height * 16 / 9;
    case ASPECT_16_10: return height * 16 / 10;
    case ASPECT_21_9: return height * 21 / 9;
    case ASPECT_32_9: return height * 32 / 9;
    default: return height * 16 / 9;
    }
}

static inline bool settings_custom_aspect(void) {
    return g_settings.aspect == ASPECT_16_10 || g_settings.aspect == ASPECT_21_9 ||
           g_settings.aspect == ASPECT_32_9 || g_settings.aspect == ASPECT_NATIVE;
}

const char *settings_language(void);
const char *settings_language_name(void);
int settings_language_index(const char *code);
void settings_init(void);
void settings_load(void);
bool settings_load_file(const char *path);
void settings_resolve_defaults(bool integrated);

#ifdef __cplusplus
}
#endif

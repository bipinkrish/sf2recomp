#include "constants/core.h"
// SF2 Recomp settings, the subset the runtime needs before the UI exists
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern SF2C_SETTINGS_S_C_C2E8 {
#endif

enum { ASPECT_UNLOCKED = 0, ASPECT_ORIGINAL = 1, ASPECT_21_9 = SF2C_SETTINGS_N_2_D473, ASPECT_16_10 = SF2C_SETTINGS_N_3_4E07, ASPECT_32_9 = SF2C_SETTINGS_N_4_4B22, ASPECT_NATIVE = SF2C_SETTINGS_N_5_EF2D };
enum { RES_MATCH = 0, RES_DYNAMIC = SF2C_SETTINGS_N_9_1958, RES_Q75 = SF2C_SETTINGS_N_10_4A44, RES_Q25 = SF2C_SETTINGS_N_11_4FC8, RES_Q50 = SF2C_SETTINGS_N_12_6B51 };
enum { UPSCALE_BILINEAR = 0, UPSCALE_METALFX = 1, UPSCALE_FSR1 = SF2C_SETTINGS_N_2_D473 };
#define UPSCALE_SHARP UPSCALE_FSR1
enum { AA_OFF = 0, AA_SMAA = 1 };
enum { PLAYER_SETUP_SOLO = 0, PLAYER_SETUP_KEYBOARD_VS_CONTROLLER, PLAYER_SETUP_TWO_CONTROLLERS };
enum {
    PROMPTS_AUTO = 0, PROMPTS_NINTENDO = 1, PROMPTS_XBOX = SF2C_SETTINGS_N_2_D473, PROMPTS_PLAYSTATION = SF2C_SETTINGS_N_3_4E07
};

extern bool g_gpu_integrated;

enum { LANG_AUTO = -1, N_LANGUAGES = SF2C_SETTINGS_N_12_6B51 };
extern const char *const g_language_codes[N_LANGUAGES];
extern const char *const g_language_names[N_LANGUAGES];

// Leave room for a mount suffix and guest filenames in filesystem path buffers.
#define SETTINGS_SAVE_DIRECTORY_CAP SF2C_SETTINGS_SETTINGS_SAVE_DIRECTORY_CAP
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
            return ((unsigned long long)height * g_settings.native_width / g_settings.native_height + 1) & ~SF2C_SETTINGS_N_1U_B8E6;
        return height * SF2C_SETTINGS_N_16_B17E / SF2C_SETTINGS_N_9_1958;
    case ASPECT_16_10: return height * SF2C_SETTINGS_N_16_B17E / SF2C_SETTINGS_N_10_4A44;
    case ASPECT_21_9: return height * SF2C_SETTINGS_N_21_6F4B / SF2C_SETTINGS_N_9_1958;
    case ASPECT_32_9: return height * SF2C_SETTINGS_N_32_E29C / SF2C_SETTINGS_N_9_1958;
    default: return height * SF2C_SETTINGS_N_16_B17E / SF2C_SETTINGS_N_9_1958;
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

// SF2 Recomp settings, the subset the runtime needs before the UI exists
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ASPECT_UNLOCKED = 0, ASPECT_ORIGINAL = 1 };
enum { RES_MATCH = 0, RES_DYNAMIC = 9, RES_Q75 = 10, RES_Q25 = 11, RES_Q50 = 12 };
enum { UPSCALE_BILINEAR = 0, UPSCALE_METALFX = 1, UPSCALE_FSR1 = 2 };
#define UPSCALE_SHARP UPSCALE_FSR1
enum { AA_OFF = 0, AA_SMAA = 1 };
enum {
    PROMPTS_AUTO = 0, PROMPTS_NINTENDO = 1, PROMPTS_XBOX = 2, PROMPTS_PLAYSTATION = 3,
    PROMPTS_KEYBOARD = 4
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
    bool experimental_keyboard_prompts;
    int screen_filter;
    char save_directory[SETTINGS_SAVE_DIRECTORY_CAP];
} Settings;

extern Settings g_settings;

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

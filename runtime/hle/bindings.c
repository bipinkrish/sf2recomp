#include "../constants/hle.h"
// Control remapping: input.c reads these tables on each event. sf2recomp.ini
// supplies runtime mappings; capture helpers also support development input tests.
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../input_bind.h"

_Static_assert(PADB_LTRIGGER == SDL_GAMEPAD_BUTTON_COUNT, SF2C_BINDINGS_S_TRIGGER_PSEUDO_BUTTONS_FOLLO_ADF1);

// Defaults: Nintendo layout by position (east = A, south = B, north = X, west = Y), triggers = ZL / ZR;
// keyboard WASD left stick, arrows d-pad, K/J/L/I = A/B/X/Y, Q/E = L/R, 1/3 = ZL/ZR, Tab / Backspace = Plus /
// Minus, F / numpad 0 = stick clicks, numpad 8/5/4/6 right stick.
#define DEF_PAD { \
    SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST, \
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, \
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PADB_LTRIGGER, PADB_RTRIGGER, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK, \
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN}
#define DEF_KEY { \
    SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_I, SDL_SCANCODE_F, SDL_SCANCODE_KP_0, \
    SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_1, SDL_SCANCODE_3, SDL_SCANCODE_TAB, SDL_SCANCODE_BACKSPACE, \
    SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, \
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5, \
    SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6}
static const int16_t k_def_pad[BIND_PAD_COUNT] = DEF_PAD, k_def_key[BIND_KEY_COUNT] = DEF_KEY;
int16_t g_bind_pad[BIND_PAD_COUNT] = DEF_PAD, g_bind_key[BIND_KEY_COUNT] = DEF_KEY;

static const char *const k_ini[BIND_KEY_COUNT] = {
    SF2C_BINDINGS_S_A_AC8D, SF2C_BINDINGS_S_B_C100, SF2C_BINDINGS_S_X_BA2D, SF2C_BINDINGS_S_Y_2BC9, SF2C_BINDINGS_S_LSTICK_A71F, SF2C_BINDINGS_S_RSTICK_4923, SF2C_BINDINGS_S_L_FBDC, SF2C_BINDINGS_S_R_2659, SF2C_BINDINGS_S_ZL_9A51, SF2C_BINDINGS_S_ZR_7019, SF2C_BINDINGS_S_PLUS_B917, SF2C_BINDINGS_S_MINUS_865A, SF2C_BINDINGS_S_LEFT_6157, SF2C_BINDINGS_S_UP_5CC8, SF2C_BINDINGS_S_RIGHT_187E, SF2C_BINDINGS_S_DOWN_DE3A,
    SF2C_BINDINGS_S_LS_UP_8A3E, SF2C_BINDINGS_S_LS_DOWN_F43B, SF2C_BINDINGS_S_LS_LEFT_B24F, SF2C_BINDINGS_S_LS_RIGHT_44E9, SF2C_BINDINGS_S_RS_UP_8DF0, SF2C_BINDINGS_S_RS_DOWN_FE12, SF2C_BINDINGS_S_RS_LEFT_C308, SF2C_BINDINGS_S_RS_RIGHT_77BB,
};
static const char *const k_names[BIND_KEY_COUNT] = {
    SF2C_BINDINGS_S_A_7986, SF2C_BINDINGS_S_B_955C, SF2C_BINDINGS_S_X_DE9E, SF2C_BINDINGS_S_Y_DB5B, SF2C_BINDINGS_S_LEFT_STICK_PRESS_C8C0, SF2C_BINDINGS_S_RIGHT_STICK_PRESS_3B03, SF2C_BINDINGS_S_L_8463, SF2C_BINDINGS_S_R_D742, SF2C_BINDINGS_S_ZL_38D1, SF2C_BINDINGS_S_ZR_5317, SF2C_BINDINGS_S_PLUS_8DFC, SF2C_BINDINGS_S_MINUS_CBF2,
    SF2C_BINDINGS_S_D_PAD_LEFT_D0BE, SF2C_BINDINGS_S_D_PAD_UP_798B, SF2C_BINDINGS_S_D_PAD_RIGHT_A5E9, SF2C_BINDINGS_S_D_PAD_DOWN_5A0F, SF2C_BINDINGS_S_LEFT_STICK_UP_BA8C, SF2C_BINDINGS_S_LEFT_STICK_DOWN_1E80, SF2C_BINDINGS_S_LEFT_STICK_LEFT_32E1,
    SF2C_BINDINGS_S_LEFT_STICK_RIGHT_196F, SF2C_BINDINGS_S_RIGHT_STICK_UP_EDE6, SF2C_BINDINGS_S_RIGHT_STICK_DOWN_6080, SF2C_BINDINGS_S_RIGHT_STICK_LEFT_CD66, SF2C_BINDINGS_S_RIGHT_STICK_RIGHT_DB11,
};
// Controller buttons by position, independent of printed Xbox/PS/Nintendo labels.
static const char *const k_pad_ini[PADB_HOST_COUNT] = {
    SF2C_BINDINGS_S_SOUTH_C449, SF2C_BINDINGS_S_EAST_46E1, SF2C_BINDINGS_S_WEST_8814, SF2C_BINDINGS_S_NORTH_3717, SF2C_BINDINGS_S_BACK_D249, SF2C_BINDINGS_S_GUIDE_35B2, SF2C_BINDINGS_S_START_A92A, SF2C_BINDINGS_S_LSTICK_A71F, SF2C_BINDINGS_S_RSTICK_4923, SF2C_BINDINGS_S_LSHOULDER_F9C9, SF2C_BINDINGS_S_RSHOULDER_2453,
    SF2C_BINDINGS_S_DPAD_UP_D3AA, SF2C_BINDINGS_S_DPAD_DOWN_1910, SF2C_BINDINGS_S_DPAD_LEFT_2631, SF2C_BINDINGS_S_DPAD_RIGHT_2EB8, SF2C_BINDINGS_S_MISC1_5037, SF2C_BINDINGS_S_RPADDLE1_5375, SF2C_BINDINGS_S_LPADDLE1_C21F, SF2C_BINDINGS_S_RPADDLE2_DAB5, SF2C_BINDINGS_S_LPADDLE2_F932,
    SF2C_BINDINGS_S_TOUCHPAD_7CAC, SF2C_BINDINGS_S_MISC2_986F, SF2C_BINDINGS_S_MISC3_BD50, SF2C_BINDINGS_S_MISC4_0059, SF2C_BINDINGS_S_MISC5_D565, SF2C_BINDINGS_S_MISC6_E745, SF2C_BINDINGS_S_LTRIGGER_11BD, SF2C_BINDINGS_S_RTRIGGER_CD5B,
};

bool bind_key_unassigned(int scancode) {
    if(scancode<=0 || scancode>=SDL_SCANCODE_COUNT)return false;
    for(int i=0;i<BIND_KEY_COUNT;++i)if(g_bind_key[i]==scancode)return false;
    return true;
}
bool bind_menu_key(int scancode,int action) {
    if(action<0 || action>=BIND_KEY_COUNT || scancode<=0)return false;
    if(g_bind_key[action]==scancode)return true;
    return bind_key_unassigned(scancode) &&
        ((action==BIND_B && scancode==SDL_SCANCODE_ESCAPE) ||
         (action==BIND_A && (scancode==SDL_SCANCODE_RETURN || scancode==SDL_SCANCODE_KP_ENTER)));
}

int bind_slot_from_name(const char *name) {
    for (int i = 0; i < BIND_KEY_COUNT; i++) if (!strcasecmp(name, k_ini[i])) return i;
    return -1;
}
int bind_pad_from_name(const char *name) {
    for (int i = 0; i < PADB_HOST_COUNT; i++) if (!strcasecmp(name, k_pad_ini[i])) return i;
    return -1;
}

const char *bind_input_name(int slot) { return slot >= 0 && slot < BIND_KEY_COUNT ? k_names[slot] : SF2C_BINDINGS_S_EMPTY_15C5; }

// fam: 0 Nintendo, 1 Xbox (and pads SDL labels like one), 2 PlayStation
const char *bind_pad_label(int host, int fam) {
    static const char *const nin[PADB_HOST_COUNT] = {
        SF2C_BINDINGS_S_B_955C, SF2C_BINDINGS_S_A_7986, SF2C_BINDINGS_S_Y_DB5B, SF2C_BINDINGS_S_X_DE9E, SF2C_BINDINGS_S_MINUS_CBF2, SF2C_BINDINGS_S_HOME_D218, SF2C_BINDINGS_S_PLUS_8DFC, SF2C_BINDINGS_S_LEFT_STICK_PRESS_C8C0, SF2C_BINDINGS_S_RIGHT_STICK_PRESS_3B03, SF2C_BINDINGS_S_L_8463, SF2C_BINDINGS_S_R_D742,
        SF2C_BINDINGS_S_D_PAD_UP_798B, SF2C_BINDINGS_S_D_PAD_DOWN_5A0F, SF2C_BINDINGS_S_D_PAD_LEFT_D0BE, SF2C_BINDINGS_S_D_PAD_RIGHT_A5E9, SF2C_BINDINGS_S_CAPTURE_3A07, SF2C_BINDINGS_S_RIGHT_PADDLE_1_B703, SF2C_BINDINGS_S_LEFT_PADDLE_1_91D3,
        SF2C_BINDINGS_S_RIGHT_PADDLE_2_CAD1, SF2C_BINDINGS_S_LEFT_PADDLE_2_9C62, SF2C_BINDINGS_S_TOUCHPAD_9884, SF2C_BINDINGS_S_MISC_2_D09B, SF2C_BINDINGS_S_MISC_3_AC6B, SF2C_BINDINGS_S_MISC_4_844D, SF2C_BINDINGS_S_MISC_5_65E8, SF2C_BINDINGS_S_MISC_6_57DB, SF2C_BINDINGS_S_ZL_38D1, SF2C_BINDINGS_S_ZR_5317,
    };
    static const char *const xb[PADB_HOST_COUNT] = {
        SF2C_BINDINGS_S_A_7986, SF2C_BINDINGS_S_B_955C, SF2C_BINDINGS_S_X_DE9E, SF2C_BINDINGS_S_Y_DB5B, SF2C_BINDINGS_S_VIEW_A5A2, SF2C_BINDINGS_S_XBOX_B4A3, SF2C_BINDINGS_S_MENU_1340, SF2C_BINDINGS_S_LS_PRESS_75C1, SF2C_BINDINGS_S_RS_PRESS_1400, SF2C_BINDINGS_S_LB_804C, SF2C_BINDINGS_S_RB_08C0,
        SF2C_BINDINGS_S_D_PAD_UP_798B, SF2C_BINDINGS_S_D_PAD_DOWN_5A0F, SF2C_BINDINGS_S_D_PAD_LEFT_D0BE, SF2C_BINDINGS_S_D_PAD_RIGHT_A5E9, SF2C_BINDINGS_S_SHARE_331E, SF2C_BINDINGS_S_PADDLE_P1_BB3C, SF2C_BINDINGS_S_PADDLE_P3_F7BF,
        SF2C_BINDINGS_S_PADDLE_P2_28EF, SF2C_BINDINGS_S_PADDLE_P4_03A2, SF2C_BINDINGS_S_TOUCHPAD_9884, SF2C_BINDINGS_S_MISC_2_D09B, SF2C_BINDINGS_S_MISC_3_AC6B, SF2C_BINDINGS_S_MISC_4_844D, SF2C_BINDINGS_S_MISC_5_65E8, SF2C_BINDINGS_S_MISC_6_57DB, SF2C_BINDINGS_S_LT_FB61, SF2C_BINDINGS_S_RT_9749,
    };
    static const char *const ps[PADB_HOST_COUNT] = {
        SF2C_BINDINGS_S_CROSS_D500, SF2C_BINDINGS_S_CIRCLE_207A, SF2C_BINDINGS_S_SQUARE_3928, SF2C_BINDINGS_S_TRIANGLE_2662, SF2C_BINDINGS_S_CREATE_88F0, SF2C_BINDINGS_S_PS_AA12, SF2C_BINDINGS_S_OPTIONS_0AC7, SF2C_BINDINGS_S_L3_465D, SF2C_BINDINGS_S_R3_A86F, SF2C_BINDINGS_S_L1_56A4, SF2C_BINDINGS_S_R1_5521,
        SF2C_BINDINGS_S_D_PAD_UP_798B, SF2C_BINDINGS_S_D_PAD_DOWN_5A0F, SF2C_BINDINGS_S_D_PAD_LEFT_D0BE, SF2C_BINDINGS_S_D_PAD_RIGHT_A5E9, SF2C_BINDINGS_S_MUTE_88DB, SF2C_BINDINGS_S_RIGHT_BACK_BUTTON_78B4, SF2C_BINDINGS_S_LEFT_BACK_BUTTON_8B49,
        SF2C_BINDINGS_S_RIGHT_FN_532C, SF2C_BINDINGS_S_LEFT_FN_00AF, SF2C_BINDINGS_S_TOUCHPAD_9884, SF2C_BINDINGS_S_MISC_2_D09B, SF2C_BINDINGS_S_MISC_3_AC6B, SF2C_BINDINGS_S_MISC_4_844D, SF2C_BINDINGS_S_MISC_5_65E8, SF2C_BINDINGS_S_MISC_6_57DB, SF2C_BINDINGS_S_L2_C79E, SF2C_BINDINGS_S_R2_85BD,
    };
    if (host < 0 || host >= PADB_HOST_COUNT) return SF2C_BINDINGS_S_NONE_A64B;
    return fam == 1 ? xb[host] : fam == SF2C_BINDINGS_N_2_D473 ? ps[host] : nin[host];
}

const char *bind_key_label(int sc) {
    if (sc <= 0 || sc >= SDL_SCANCODE_COUNT) return SF2C_BINDINGS_S_NONE_A64B;
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    return n && *n ? n : SF2C_BINDINGS_S_UNNAMED_KEY_4EB5;
}

int bind_label_family(void) {
    extern int input_pad_family(void);  // runtime/hle/input.c (the game's open pad, or LARECOMP_PAD_TYPE)
    int f = input_pad_family();
    if (f) return f;
    int n = 0, t = 0;  // start screen: input.c hasn't opened a pad yet
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) t = SDL_GetGamepadTypeForID(ids[0]);
    bool any = ids && n > 0;
    SDL_free(ids);
    return any ? input_family_of_type(t) : 0;
}

static void result(int kind, int dev, int slot, int other);

void bind_reset(int dev) {
    if (dev == BIND_DEV_PAD) memcpy(g_bind_pad, k_def_pad, sizeof g_bind_pad);
    else memcpy(g_bind_key, k_def_key, sizeof g_bind_key);
    LOG(SF2C_BINDINGS_S_INPUT_S_BINDINGS_RESET_TO_TH_1D7A, dev == BIND_DEV_PAD ? SF2C_BINDINGS_S_CONTROLLER_E1A8 : SF2C_BINDINGS_S_KEYBOARD_FBA3);
    result(SF2C_BINDINGS_N_5_EF2D, dev, -1, -1);
}

bool bind_is_default(int dev) {
    return dev == BIND_DEV_PAD ? !memcmp(g_bind_pad, k_def_pad, sizeof g_bind_pad) : !memcmp(g_bind_key, k_def_key, sizeof g_bind_key);
}

bool bind_is_default_slot(int dev, int slot) {
    return dev == BIND_DEV_PAD ? g_bind_pad[slot] == k_def_pad[slot] : g_bind_key[slot] == k_def_key[slot];
}

int bind_prompt_host(int slot) { return slot >= 0 && slot < BIND_PAD_COUNT ? g_bind_pad[slot] : -1; }

// ------------------------------------------------------------------ capture
static _Atomic int cap_dev = -1, cap_slot = -1;
static _Atomic uint64_t cap_deadline;
static _Atomic int res_kind, res_dev, res_slot, res_other, res_serial;

static void result(int kind, int dev, int slot, int other) {
    res_kind = kind, res_dev = dev, res_slot = slot, res_other = other;
    res_serial++;
}

void bind_capture_begin(int dev, int slot) {
    if (slot < 0 || slot >= (dev == BIND_DEV_PAD ? BIND_PAD_COUNT : BIND_KEY_COUNT)) return;
    cap_deadline = SDL_GetTicks() + BIND_CAPTURE_MS;
    cap_slot = slot;
    cap_dev = dev;
}

void bind_capture_cancel(void) {
    if (cap_dev < 0) return;
    result(SF2C_BINDINGS_N_3_4E07, cap_dev, cap_slot, -1);
    cap_dev = -1;
}

bool bind_capture_active(int *dev, int *slot, int *ms_left) {
    int d = cap_dev;
    if (d >= 0 && SDL_GetTicks() >= cap_deadline) { result(SF2C_BINDINGS_N_4_4B22, d, cap_slot, -1); cap_dev = d = -1; }
    if (dev) *dev = d;
    if (slot) *slot = d >= 0 ? cap_slot : -1;
    if (ms_left) *ms_left = d >= 0 ? (int)(cap_deadline - SDL_GetTicks()) : 0;
    return d >= 0;
}

int bind_last_result(int *dev, int *slot, int *other, int *serial) {
    if (dev) *dev = res_dev;
    if (slot) *slot = res_slot;
    if (other) *other = res_other;
    if (serial) *serial = res_serial;
    return res_kind;
}

// Bind `host` to `slot`; an input already on `host` takes the slot's old binding (swap).
static void assign(int dev, int slot, int host) {
    int16_t *t = dev == BIND_DEV_PAD ? g_bind_pad : g_bind_key;
    int n = dev == BIND_DEV_PAD ? BIND_PAD_COUNT : BIND_KEY_COUNT, other = -1;
    for (int i = 0; i < n; i++)
        if (i != slot && t[i] == host) { other = i; t[i] = t[slot]; }
    t[slot] = (int16_t)host;
    LOG(SF2C_BINDINGS_S_INPUT_S_S_S_S_S_S_N_DB89, dev == BIND_DEV_PAD ? SF2C_BINDINGS_S_CONTROLLER_E1A8 : SF2C_BINDINGS_S_KEYBOARD_FBA3, k_names[slot],
        dev == BIND_DEV_PAD ? k_pad_ini[host] : bind_key_label(host), other >= 0 ? SF2C_BINDINGS_S_SWAPPED_WITH_0960 : SF2C_BINDINGS_S_EMPTY_12AE,
        other >= 0 ? k_names[other] : SF2C_BINDINGS_S_EMPTY_12AE, other >= 0 ? SF2C_BINDINGS_S_EMPTY_C921 : SF2C_BINDINGS_S_EMPTY_12AE);
    if (other >= 0) LOG(SF2C_BINDINGS_S_INPUT_S_S_N_F4E3, k_names[other], dev == BIND_DEV_PAD ? k_pad_ini[t[other]] : bind_key_label(t[other]));
    result(other >= 0 ? SF2C_BINDINGS_N_2_D473 : 1, dev, slot, other);
}

bool bind_capture_event(const SDL_Event *e) {
    int dev = cap_dev;
    if (dev < 0) return false;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (e->key.repeat) return true;
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) { bind_capture_cancel(); return true; }
        // F1 (menu), F9 (frame dump), F11 (fullscreen) stay the overlay's; a controller capture ignores keys
        if (dev != BIND_DEV_KEY || e->key.scancode == SDL_SCANCODE_F1 || e->key.scancode == SDL_SCANCODE_F9 ||
            e->key.scancode == SDL_SCANCODE_F11 || e->key.scancode >= SDL_SCANCODE_COUNT) return true;
        cap_dev = -1;
        assign(dev, cap_slot, e->key.scancode);
        return true;
    case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        // Home / Guide opens the menu; a keyboard capture ignores the controller (it times out, or Esc)
        if (dev != BIND_DEV_PAD || e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE || e->gbutton.button >= SDL_GAMEPAD_BUTTON_COUNT) return true;
        cap_dev = -1;
        assign(dev, cap_slot, e->gbutton.button);
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (dev == BIND_DEV_PAD && e->gaxis.value > SF2C_BINDINGS_N_16000_2570 &&
            (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            cap_dev = -1;
            assign(dev, cap_slot, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PADB_LTRIGGER : PADB_RTRIGGER);
        }
        return true;  // (sticks: no menu movement while waiting)
    default:
        return false;
    }
}

// ------------------------------------------------------------------ runtime INI
const char *bind_config_key(int slot) { return k_ini[slot]; }

const char *bind_config_value(int dev, int host) {
    if (dev == BIND_DEV_PAD)
        return host >= 0 && host < PADB_HOST_COUNT ? k_pad_ini[host] : SF2C_BINDINGS_S_NONE_30A0;
    return host == SDL_SCANCODE_UNKNOWN ? SF2C_BINDINGS_S_NONE_30A0 : SDL_GetScancodeName((SDL_Scancode)host);
}

bool bind_config_parse(int dev, const char *value, int16_t *host) {
    bool pad = dev == BIND_DEV_PAD;
    if (!strcasecmp(value, SF2C_BINDINGS_S_NONE_30A0)) {
        *host = pad ? -1 : SDL_SCANCODE_UNKNOWN;
        return true;
    }
    int parsed = pad ? bind_pad_from_name(value) : (int)SDL_GetScancodeFromName(value);
    if (parsed < 0 || (!pad && parsed == SDL_SCANCODE_UNKNOWN)) return false;
    *host = (int16_t)parsed;
    return true;
}

BindConfigResult bind_config_set(int dev, const char *action, const char *value) {
    if (dev != BIND_DEV_KEY && dev != BIND_DEV_PAD) return BIND_CONFIG_UNKNOWN;
    int slot = bind_slot_from_name(action);
    bool pad = dev == BIND_DEV_PAD;
    if (slot < 0 || slot >= (pad ? BIND_PAD_COUNT : BIND_KEY_COUNT)) return BIND_CONFIG_UNKNOWN;
    // INI bindings are independent. One host input can drive several actions.
    int16_t *binding = pad ? &g_bind_pad[slot] : &g_bind_key[slot];
    return bind_config_parse(dev, value, binding) ? BIND_CONFIG_APPLIED : BIND_CONFIG_INVALID;
}

void bind_config_save(FILE *f) {
    fprintf(f, SF2C_BINDINGS_S_N_CONTROLS_GAMEPAD_N_B389);
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (g_bind_pad[i] != k_def_pad[i])
            fprintf(f, SF2C_BINDINGS_S_S_S_N_C2E5, k_ini[i], bind_config_value(BIND_DEV_PAD, g_bind_pad[i]));
    fprintf(f, SF2C_BINDINGS_S_N_CONTROLS_KEYBOARD_N_5ABB);
    for (int i = 0; i < BIND_KEY_COUNT; i++)
        if (g_bind_key[i] != k_def_key[i])
            fprintf(f, SF2C_BINDINGS_S_S_S_N_A1C0, k_ini[i], bind_config_value(BIND_DEV_KEY, g_bind_key[i]));
}

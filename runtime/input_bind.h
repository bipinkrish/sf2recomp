#include "constants/core.h"
// Control remapping (runtime/hle/bindings.c): runtime sf2recomp.ini maps keyboard
// keys and controller positions to Switch inputs. The original game reads these
// mappings through the HID service; analog controller sticks remain unchanged.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern SF2C_INPUT_BIND_S_C_C2E8 {
#endif

typedef union SDL_Event SDL_Event;

// Switch inputs, in nn::hid button-bit order (runtime/hle/input.c BTN_*), then the keyboard's stick directions.
enum {
    BIND_A, BIND_B, BIND_X, BIND_Y, BIND_LSTICK, BIND_RSTICK, BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_PLUS, BIND_MINUS,
    BIND_LEFT, BIND_UP, BIND_RIGHT, BIND_DOWN,
    BIND_PAD_COUNT,                                      // controller: the 16 buttons (its sticks stay the sticks)
    BIND_LS_UP = BIND_PAD_COUNT, BIND_LS_DOWN, BIND_LS_LEFT, BIND_LS_RIGHT,
    BIND_RS_UP, BIND_RS_DOWN, BIND_RS_LEFT, BIND_RS_RIGHT,
    BIND_KEY_COUNT                                       // keyboard: buttons + both sticks
};
enum { BIND_DEV_PAD, BIND_DEV_KEY };
// Controller binding values: an SDL_GamepadButton, or the analog triggers as buttons
enum { PADB_LTRIGGER = SF2C_INPUT_BIND_N_26_5F9C /* SDL_GAMEPAD_BUTTON_COUNT */, PADB_RTRIGGER, PADB_HOST_COUNT };

extern int16_t g_bind_pad[BIND_PAD_COUNT];  // PADB value per Switch button (-1 = none)
extern int16_t g_bind_key[BIND_KEY_COUNT];  // SDL_Scancode per input (0 = none)

const char *bind_input_name(int slot);         // "A", "D-pad Up", "Left stick Up"
const char *bind_pad_label(int host, int fam); // controller button in the pad family's own words (fam: input_pad_family)
const char *bind_key_label(int scancode);
// Desktop shortcuts are fallbacks: every explicit gameplay binding wins.
bool bind_key_unassigned(int scancode);
bool bind_menu_key(int scancode, int action);
int bind_label_family(void);                   // the connected controller's family (also on the start screen)
int input_family_of_type(int sdl_gamepad_type); // runtime/hle/input.c: 0 Nintendo, 1 Xbox (or labelled like one), 2 PS
int input_menu_family(uint32_t sdl_joystick_id); // runtime/hle/input.c: our menus' layout for that pad (0: any)
bool input_any_gamepad_connected(void);
typedef enum { INPUT_NAV_COMBAT, INPUT_NAV_MENU, INPUT_NAV_SPARRING, INPUT_NAV_CAMPAIGN } InputNavigation;
bool input_menu_navigation_active(void);
int input_menu_back_binding(void);
void input_menu_navigation(InputNavigation context); // guest publishes context; SDL reads atomically
void bind_reset(int dev);
bool bind_is_default(int dev);
bool bind_is_default_slot(int dev, int slot);

// "Click a binding, then press": begun from the UI; the next press (any thread's SDL event) is taken. Escape cancels,
// as does BIND_CAPTURE_MS without a press. On a conflict the two inputs swap.
#define BIND_CAPTURE_MS SF2C_INPUT_BIND_BIND_CAPTURE_MS
void bind_capture_begin(int dev, int slot);
void bind_capture_cancel(void);
// capturing: dev / slot (else -1), ms left
bool bind_capture_active(int *dev, int *slot, int *ms_left);
// Main thread, before anything else sees the event: true = consumed by a capture
bool bind_capture_event(const SDL_Event *e);
// Last change for the UI: 0 none, 1 set, 2 swapped (with *other), 3 cancelled, 4 timed out, 5 reset; *serial bumps
int bind_last_result(int *dev, int *slot, int *other, int *serial);

// sf2recomp.ini [controls.keyboard] / [controls.gamepad]. Unknown actions are
// ignored for forward compatibility; an invalid value leaves its binding intact.
typedef enum { BIND_CONFIG_UNKNOWN, BIND_CONFIG_APPLIED, BIND_CONFIG_INVALID } BindConfigResult;
BindConfigResult bind_config_set(int dev, const char *action, const char *value);
const char *bind_config_key(int slot);
const char *bind_config_value(int dev, int host);
bool bind_config_parse(int dev, const char *value, int16_t *host);
void bind_config_save(FILE *f);  // changed bindings, in the same named sections

// Switch input "a", "zl", "ls_up"... -> slot; controller position "south"... -> PADB value
int bind_slot_from_name(const char *name);
int bind_pad_from_name(const char *name);

// The controller button now driving Switch input `slot` (BIND_A..).
int bind_prompt_host(int slot);

#ifdef __cplusplus
}
#endif

#include "../constants/hle.h"
// Controller input: SDL keyboard/gamepad -> nn::hid Npad state.
//
// Both go through the player's bindings (runtime/hle/bindings.c, defaults there: keyboard WASD left stick, arrows
// d-pad, K/J/L/I = A/B/X/Y, Q/E = L/R, 1/3 = ZL/ZR, Enter / Backspace = Plus / Minus, numpad 8/4/5/6 right stick;
// controller by position, Switch A = east).
#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../nvn/nvn.h"
#include "../nvn/renderer.h"
#include "../settings.h"
#include "../input_bind.h"
#include "../rumble.h"
#include "../ui/menu.h"
#include "../ui/mouse.h"
#include "../ui/touch.h"
#include "movie.h"

enum {
    BTN_A = 0, BTN_B, BTN_X, BTN_Y, BTN_LSTICK, BTN_RSTICK, BTN_L, BTN_R, BTN_ZL, BTN_ZR, BTN_PLUS, BTN_MINUS,
    BTN_LEFT, BTN_UP, BTN_RIGHT, BTN_DOWN
};
enum { LOCAL_PLAYER_COUNT = SF2C_INPUT_N_2_D473 };

_Static_assert(BTN_DOWN == BIND_DOWN && BTN_ZL == BIND_ZL, SF2C_INPUT_S_BINDINGS_FOLLOW_THE_BUTTON_B_BD36);
_Static_assert(LOCAL_PLAYER_COUNT == RUMBLE_PLAYER_COUNT, SF2C_INPUT_S_INPUT_AND_RUMBLE_PLAYER_SLOT_54D3);

typedef struct {
    SDL_Gamepad *handle;       // UI thread only
    SDL_JoystickID id;         // stable while connected; UI thread only
    _Atomic bool connected;    // published to guest input threads
    _Atomic uint64_t buttons;
    _Atomic int32_t lx, ly, rx, ry;
    bool trigger_down[SF2C_INPUT_N_2_D473];      // UI thread only
} LocalPad;

static _Atomic uint64_t key_buttons;
static _Atomic int32_t key_lx, key_ly, key_rx, key_ry;
static uint8_t keys_down[SDL_SCANCODE_COUNT];
static LocalPad pads[LOCAL_PLAYER_COUNT];
static _Atomic int64_t sampling;
static bool host_test;  // scripted runs must not vibrate the user's controller

// Loaded before SDL starts; changing the INI requires a restart. Physical pads
// occupy their guest player slot so buttons, short taps and rumble share routing.
static bool keyboard_enabled(void) { return g_settings.player_setup != PLAYER_SETUP_TWO_CONTROLLERS; }
static int first_pad_player(void) { return g_settings.player_setup == PLAYER_SETUP_KEYBOARD_VS_CONTROLLER ? 1 : 0; }
static bool player_connected(int player) {
    return (player == 0 && keyboard_enabled()) || atomic_load(&pads[player].connected);
}

// Guest IPC threads publish values; only the UI thread touches SDL's pad.
// The lock also makes paired value snapshots consistent with permission/reset.
static SDL_SpinLock rumble_lock;
static RumbleValue rumble_values[RUMBLE_VALUE_COUNT];
static bool rumble_allowed = true;
static _Atomic uint64_t rumble_frame_ms;
static uint16_t rumble_last_lo[LOCAL_PLAYER_COUNT], rumble_last_hi[LOCAL_PLAYER_COUNT];
static uint64_t rumble_last_ms[LOCAL_PLAYER_COUNT];
static bool rumble_warned[LOCAL_PLAYER_COUNT];

static void rumble_clear(void) {
    SDL_LockSpinlock(&rumble_lock);
    memset(rumble_values, 0, sizeof rumble_values);
    SDL_UnlockSpinlock(&rumble_lock);
}
void input_rumble_send(uint32_t h, const RumbleValue *v) {
    int index = rumble_index(h);
    SDL_LockSpinlock(&rumble_lock);
    if (index < 0) { SDL_UnlockSpinlock(&rumble_lock); return; }
    if (rumble_allowed) rumble_values[index] = *v;
    SDL_UnlockSpinlock(&rumble_lock);
}
RumbleValue input_rumble_get(uint32_t h) {
    RumbleValue v = {0, SF2C_INPUT_N_160_A512, 0, SF2C_INPUT_N_320_8882};
    int index = rumble_index(h);
    SDL_LockSpinlock(&rumble_lock);
    if (index >= 0 && rumble_allowed) v = rumble_values[index];
    SDL_UnlockSpinlock(&rumble_lock);
    return v;  // requested value, not physical motor feedback
}
void input_rumble_permit(bool allowed) {
    SDL_LockSpinlock(&rumble_lock);
    rumble_allowed = allowed;
    if (!allowed) memset(rumble_values, 0, sizeof rumble_values);
    SDL_UnlockSpinlock(&rumble_lock);
}
bool input_rumble_permitted(void) {
    SDL_LockSpinlock(&rumble_lock);
    bool allowed = rumble_allowed;
    SDL_UnlockSpinlock(&rumble_lock);
    return allowed;
}
void input_rumble_poll(void) {
    uint64_t now = SDL_GetTicks();
    uint64_t last_frame = atomic_load(&rumble_frame_ms);
    // Switch values are latched until changed: they contain no duration.
    // Renew SDL's 250 ms effect every 100 ms while frames are progressing.
    // A stalled render/UI thread therefore cannot leave an infinite effect.
    // This watchdog is a host policy, not a duration supplied by the game.
    bool stalled = now > last_frame && now - last_frame > SF2C_INPUT_N_250_1E47;
    for (int player = 0; player < LOCAL_PLAYER_COUNT; player++) {
        SDL_Gamepad *pad = pads[player].handle;
        uint16_t lo = 0, hi = 0;
        bool suppress = stalled || !pad || host_test || !g_settings.rumble || menu_is_open() || !SDL_GetKeyboardFocus();
        SDL_LockSpinlock(&rumble_lock);
        if (suppress) memset(&rumble_values[player * RUMBLE_SIDES_PER_PLAYER], 0,
                             RUMBLE_SIDES_PER_PLAYER * sizeof rumble_values[0]);
        else if (rumble_allowed) {
            rumble_mix(rumble_values, player, &lo, &hi);
            int pct = g_settings.rumble_strength;  // (Controls tab: Intensity)
            if (pct > 0 && pct < SF2C_INPUT_N_100_AD57) lo = (uint16_t)(lo * pct / SF2C_INPUT_N_100_AD57), hi = (uint16_t)(hi * pct / SF2C_INPUT_N_100_AD57);
        }
        SDL_UnlockSpinlock(&rumble_lock);
        if (pad && (lo != rumble_last_lo[player] || hi != rumble_last_hi[player] ||
                    ((lo || hi) && now - rumble_last_ms[player] >= SF2C_INPUT_N_100_AD57))) {
            if (!SDL_RumbleGamepad(pad, lo, hi, (lo || hi) ? SF2C_INPUT_N_250_1E47 : 0) && !rumble_warned[player]) {
                LOG(SF2C_INPUT_S_RUMBLE_PLAYER_D_UNAVAILABLE__2B46, player + 1, SDL_GetError());
                rumble_warned[player] = true;  // keyboard/non-rumble pads are still usable
            }
            rumble_last_ms[player] = now;
        }
        rumble_last_lo[player] = lo;
        rumble_last_hi[player] = hi;
    }
}
void input_rumble_stop(void) {  // UI thread: also called before normal app exit
    rumble_clear();
    atomic_store(&rumble_frame_ms, 0);
    for (int player = 0; player < LOCAL_PLAYER_COUNT; player++) {
        if (pads[player].handle) SDL_RumbleGamepad(pads[player].handle, 0, 0, 0);
        rumble_last_lo[player] = rumble_last_hi[player] = 0;
    }
}
RT_ON_RESET(input_rumble_reset) {
    input_rumble_stop();
    input_rumble_permit(true);
}

// Host keys / buttons held by a test script (SF2RECOMP_HOST_INPUT, input_frame): they go through the bindings
static uint8_t test_keys[SDL_SCANCODE_COUNT];
static _Atomic uint32_t test_pad;
static _Atomic int32_t test_lx, test_ly;  // (a resting stick's drift: "lstick" in SF2RECOMP_HOST_INPUT)

static int held_key(int sc) { return sc > 0 && sc < SDL_SCANCODE_COUNT && (keys_down[sc] || test_keys[sc]); }

// Through the bindings (runtime/hle/bindings.c: g_bind_key), read on every key event, so a rebind applies at once.
static void recompute_keyboard(void) {
    uint64_t b = 0;
    if (!keyboard_enabled()) {
        key_buttons = 0;
        key_lx = key_ly = key_rx = key_ry = 0;
        return;
    }
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (held_key(g_bind_key[i])) b |= SF2C_INPUT_N_1ULL_622E << i;
    if(input_menu_navigation_active()) {
        if(held_key(SDL_SCANCODE_ESCAPE) && bind_key_unassigned(SDL_SCANCODE_ESCAPE))b|=SF2C_INPUT_N_1ULL_622E<<input_menu_back_binding();
        if((held_key(SDL_SCANCODE_RETURN) && bind_key_unassigned(SDL_SCANCODE_RETURN)) ||
           (held_key(SDL_SCANCODE_KP_ENTER) && bind_key_unassigned(SDL_SCANCODE_KP_ENTER)))b|=SF2C_INPUT_N_1ULL_622E<<BIND_A;
    } else if(held_key(SDL_SCANCODE_ESCAPE) && bind_key_unassigned(SDL_SCANCODE_ESCAPE))b|=SF2C_INPUT_N_1ULL_622E<<BIND_PLUS;
    key_buttons = b;
    key_lx = (held_key(g_bind_key[BIND_LS_RIGHT]) - held_key(g_bind_key[BIND_LS_LEFT])) * SF2C_INPUT_N_32767_D844;
    key_ly = (held_key(g_bind_key[BIND_LS_UP]) - held_key(g_bind_key[BIND_LS_DOWN])) * SF2C_INPUT_N_32767_D844;
    key_rx = (held_key(g_bind_key[BIND_RS_RIGHT]) - held_key(g_bind_key[BIND_RS_LEFT])) * SF2C_INPUT_N_32767_D844;
    key_ry = (held_key(g_bind_key[BIND_RS_UP]) - held_key(g_bind_key[BIND_RS_DOWN])) * SF2C_INPUT_N_32767_D844;
}

// Controller buttons (and the triggers as buttons) -> Switch buttons through g_bind_pad; the sticks stay the sticks.
static void recompute_pad(int player) {
    if (player < 0 || player >= LOCAL_PLAYER_COUNT) return;
    LocalPad *p = &pads[player];
    uint32_t host = player == 0 ? test_pad : 0;
    if (p->handle && !host_test) {
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
            if (SDL_GetGamepadButton(p->handle, (SDL_GamepadButton)i)) host |= SF2C_INPUT_N_1U_B8E6 << i;
        if (SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > SF2C_INPUT_N_16000_2570) host |= SF2C_INPUT_N_1U_B8E6 << PADB_LTRIGGER;
        if (SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > SF2C_INPUT_N_16000_2570) host |= SF2C_INPUT_N_1U_B8E6 << PADB_RTRIGGER;
    }
    uint64_t b = 0;
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (g_bind_pad[i] >= 0 && g_bind_pad[i] < PADB_HOST_COUNT && (host >> g_bind_pad[i] & 1)) b |= SF2C_INPUT_N_1ULL_622E << i;
    atomic_store(&p->buttons, b);
    if (!p->handle || host_test) {
        atomic_store(&p->lx, player == 0 ? test_lx : 0);
        atomic_store(&p->ly, player == 0 ? test_ly : 0);
        atomic_store(&p->rx, 0);
        atomic_store(&p->ry, 0);
        return;
    }
    atomic_store(&p->lx, SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_LEFTX));
    atomic_store(&p->ly, -SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_LEFTY));
    atomic_store(&p->rx, SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_RIGHTX));
    atomic_store(&p->ry, -SDL_GetGamepadAxis(p->handle, SDL_GAMEPAD_AXIS_RIGHTY));
}

// Every press reaches the game, however short. recompute_pad reads the pad as it is now, so a quick tap whose press
// and release SDL hands over together (a Bluetooth pad's reports often come batched), or that fell between two of the
// game's polls, never showed: the game's menus took a second press now and then. A press the game hasn't polled yet
// that is released is still given to the game's next polls (fill_state). UI thread: note_press; game thread: the rest.
static _Atomic uint64_t movie_consumed[LOCAL_PLAYER_COUNT];
static _Atomic uint64_t press_unseen[LOCAL_PLAYER_COUNT];  // pressed (events), not yet in a state the game polled
static _Atomic uint64_t tap_buttons[LOCAL_PLAYER_COUNT];   // released while unseen: that player's next poll gets them
static _Atomic uint32_t input_generation[LOCAL_PLAYER_COUNT];
static uint64_t pad_bits(int host) {   // the game buttons bound to host button `host` (SDL button / PADB_*TRIGGER)
    uint64_t b = 0;
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (g_bind_pad[i] == host) b |= SF2C_INPUT_N_1ULL_622E << i;
    return b;
}
static void note_press(int player, uint64_t down, uint64_t up) {
    if (player < 0 || player >= LOCAL_PLAYER_COUNT) return;
    // The original VideoPlayerController accepts HintID A or B to skip.
    if (movie_overlay_active() && (down & SF2C_INPUT_N_3_4E07)) {
        movie_request_skip();
        atomic_fetch_or(&movie_consumed[player], down & SF2C_INPUT_N_3_4E07);
        down &= ~SF2C_INPUT_N_3ULL_BD3E;
    }
    if (up) atomic_fetch_and(&movie_consumed[player], ~up);
    if (down) atomic_fetch_or(&press_unseen[player], down);
    if (up) {
        uint64_t u = atomic_fetch_and(&press_unseen[player], ~up) & up;
        if (u) atomic_fetch_or(&tap_buttons[player], u);
    }
}
static void reset_player_edges(int player) {
    if (player < 0 || player >= LOCAL_PLAYER_COUNT) return;
    atomic_store(&press_unseen[player], 0);
    atomic_store(&tap_buttons[player], 0);
    atomic_fetch_add(&input_generation[player], 1);
}

// Forget held keys (the menu took focus); the gamepad is re-read on its next event.
void input_reset(void) {
    menu_mouse_reset();
    for (int player = 0; player < LOCAL_PLAYER_COUNT; player++) reset_player_edges(player);
    rumble_clear();
    memset(keys_down, 0, sizeof keys_down);
    recompute_keyboard();
}

volatile int g_debug_frame_request;  // F9: dump the next frame's passes (build/pass_*.png) + draw log

// Events a test pushes (SF2RECOMP_HOST_INPUT type: / sdlkey: / sdlpad: / mouse:) carry this device id: they go the whole way, menu first.
#define TEST_DEVICE SF2C_INPUT_TEST_DEVICE
static bool test_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e->key.which == TEST_DEVICE;
    case SDL_EVENT_TEXT_INPUT: return e->text.windowID == TEST_DEVICE;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: return e->gbutton.which == TEST_DEVICE;
    case SDL_EVENT_MOUSE_MOTION: return e->motion.which == TEST_DEVICE;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return e->button.which == TEST_DEVICE;
    default: return false;
    }
}

// Scripted test runs ignore the user's own input (a key or stick touched while a test drives the game spoils it):
// SF2RECOMP_NO_USER_INPUT=1, on by default whenever SF2RECOMP_INPUT / SF2RECOMP_DOORJUMP_TEST / SF2RECOMP_PLACE is set
// (or SF2RECOMP_HOST_INPUT / SF2RECOMP_BIND_TEST; SF2RECOMP_NO_USER_INPUT=0 lets it through again).
bool input_user_blocked(const SDL_Event *e) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv(SF2C_INPUT_S_SF2RECOMP_NO_USER_INPUT_EB20);
        on = v ? atoi(v) != 0 : (getenv(SF2C_INPUT_S_SF2RECOMP_INPUT_D0A3) || getenv(SF2C_INPUT_S_SF2RECOMP_DOORJUMP_TEST_AF02) || getenv(SF2C_INPUT_S_SF2RECOMP_PLACE_61F0) ||
                                 getenv(SF2C_INPUT_S_SF2RECOMP_HOST_INPUT_8D91) || getenv(SF2C_INPUT_S_SF2RECOMP_BIND_TEST_C2F2));
        if (on) LOG(SF2C_INPUT_S_INPUT_SCRIPTED_RUN_KEYBOARD__420D);
    }
    if (!on) return false;
    if (test_event(e)) return false;  // (SF2RECOMP_HOST_INPUT type: / sdlkey: / sdlpad: - the test's own presses)
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_AXIS_MOTION: case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_JOYSTICK_ADDED: case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP:
        return true;
    default:
        return false;
    }
}

// The mouse cursor during play: hidden as soon as a controller (a button, a stick or trigger past a small deadzone) or
// a key is used, shown again only when the mouse moves (a few points, so a bumped desk doesn't count), clicks or
// scrolls. Every window mode and platform: SDL hides it over our window only. Main thread (the event loops: the
// game's ui_run and the start screen).
static bool cursor_hidden;  // (the start screen keeps ImGui's backend from showing it again meanwhile)
bool input_cursor_hidden(void) { return cursor_hidden; }
void input_cursor_event(const SDL_Event *e) {
    static float moved;  // (points the mouse moved since the cursor was hidden)
    bool hide = false, show = false;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:  // (a modifier alone - Cmd / Alt for switching apps - isn't play)
        hide = !e->key.repeat && !(e->key.scancode >= SDL_SCANCODE_LCTRL && e->key.scancode <= SDL_SCANCODE_RGUI);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: hide = true; break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: hide = abs(e->gaxis.value) > SF2C_INPUT_N_12000_C5D1; break;  // (stick drift stays below)
    case SDL_EVENT_MOUSE_MOTION:
        moved += fabsf(e->motion.xrel) + fabsf(e->motion.yrel);
        show = moved > SF2C_INPUT_N_6_E7F6;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_WHEEL: show = true; break;
    default: break;
    }
    if (hide && !cursor_hidden) {
        SDL_HideCursor();
        cursor_hidden = true, moved = 0;
        LOG(SF2C_INPUT_S_CURSOR_HIDDEN_S_SDL_CURSOR_V_F786, e->type == SDL_EVENT_KEY_DOWN ? SF2C_INPUT_S_KEY_C0C1 : SF2C_INPUT_S_CONTROLLER_E1A8,
            SDL_CursorVisible());
    } else if (show && cursor_hidden) {
        SDL_ShowCursor();
        cursor_hidden = false;
        LOG(SF2C_INPUT_S_CURSOR_SHOWN_MOUSE_S_SDL_CUR_0846,
            e->type == SDL_EVENT_MOUSE_MOTION ? SF2C_INPUT_S_MOVED_485A : e->type == SDL_EVENT_MOUSE_WHEEL ? SF2C_INPUT_S_WHEEL_721D : SF2C_INPUT_S_BUTTON_A7B7,
            SDL_CursorVisible());
    }
}

// "Allow background inputs" (Controls tab, off by default): controllers keep working while the window isn't in front.
// SDL drops controller events without keyboard focus unless this hint is set; it reads the hint live. (On macOS the
// hint makes the system ask for Input Monitoring - reading HID input for a background app - so it was taken out as a
// default there; the player opts in here.)
SDL_JoystickID input_active_pad(void) { return pads[0].handle ? pads[0].id : 0; }
bool input_any_gamepad_connected(void) {
    return atomic_load(&pads[0].connected) || atomic_load(&pads[1].connected);
}
void input_apply_background(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, g_settings.bg_input ? SF2C_INPUT_S_1_3915 : SF2C_INPUT_S_0_9808);
}

// A controller SDL has no mapping for (on macOS e.g. a generic Bluetooth HID pad, such as a phone app posing as one)
// is only a joystick to SDL, and the game reads gamepads only. gamecontrollerdb.txt in the data root holds the
// player's own mappings, in SDL's format (lines for other platforms are skipped, so SDL_GameControllerDB's file works
// as is). Called right after SDL_Init: a pad already connected becomes a gamepad then (SDL sends GAMEPAD_ADDED).
void input_load_user_mappings(void) {
    if (!SDL_GetPathInfo(SF2C_INPUT_S_GAMECONTROLLERDB_TXT_BBE9, NULL)) return;
    int n = SDL_AddGamepadMappingsFromFile(SF2C_INPUT_S_GAMECONTROLLERDB_TXT_BBE9);
    if (n < 0) LOG(SF2C_INPUT_S_INPUT_GAMECONTROLLERDB_TXT_S_4E98, SDL_GetError());
    else LOG(SF2C_INPUT_S_INPUT_GAMECONTROLLERDB_TXT_D_CAB3, n);
}

// Controller family for input labels and menus: 0 Nintendo / none, 1 Xbox
// (and every other pad SDL labels like one: generic / third-party pads with A on the bottom), 2 PlayStation.
// SF2RECOMP_PAD_TYPE=xbox|playstation|nintendo pretends one is attached (tests).
// By SDL's own face-button labels for the type: the bottom button reads B on Nintendo pads, A on Xbox-style ones,
// Cross on PlayStation ones.
int input_family_of_type(int type) {
    switch (SDL_GetGamepadButtonLabelForType((SDL_GamepadType)type, SDL_GAMEPAD_BUTTON_SOUTH)) {
    case SDL_GAMEPAD_BUTTON_LABEL_B: return 0;
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return SF2C_INPUT_N_2_D473;
    default: return 1;
    }
}
static _Atomic int pad_family;
static void note_family(void) {
    SDL_Gamepad *pad = pads[0].handle ? pads[0].handle : pads[1].handle;
    int f = pad ? input_family_of_type(SDL_GetGamepadType(pad)) : 0;
    if (f != pad_family) LOG(SF2C_INPUT_S_INPUT_BUTTON_PROMPTS_S_CONTR_75BA, f == 1 ? SF2C_INPUT_S_XBOX_B4A3 : f == SF2C_INPUT_N_2_D473 ? SF2C_INPUT_S_PLAYSTATION_824F : SF2C_INPUT_S_NINTENDO_853F);
    pad_family = f;
}
int input_pad_family(void) {
    static int env = -SF2C_INPUT_N_2_D473;
    if (env == -SF2C_INPUT_N_2_D473) {
        const char *e = getenv(SF2C_INPUT_S_SF2RECOMP_PAD_TYPE_2AEC);
        env = !e ? -1 : !strcmp(e, SF2C_INPUT_S_XBOX_76AE) ? 1 : !strcmp(e, SF2C_INPUT_S_PLAYSTATION_15B2) || !strcmp(e, SF2C_INPUT_S_PS_5831) ? SF2C_INPUT_N_2_D473 : 0;
    }
    return env >= 0 ? env : pad_family;
}

// Our menus' face buttons: the family whose button names they show, and (Confirm button = Controller convention, #172)
// whose layout they use - Nintendo (0): the right button confirms, the bottom one goes back; Xbox / PlayStation
// (1 / 2): the bottom one confirms, the right one goes back (runtime/ui/pad_nav.mm). "Button icons"
// forced to a style wins (icons and menus agree); Auto: the controller `which` that pressed (0, or not a connected
// gamepad - a test's pushed press: the game's controller / the first connected one, else Nintendo).
int input_menu_family(SDL_JoystickID which) {
    switch (g_settings.prompts) {
    case PROMPTS_NINTENDO: return 0;
    case PROMPTS_XBOX: return 1;
    case PROMPTS_PLAYSTATION: return SF2C_INPUT_N_2_D473;
    default: break;
    }
    if (getenv(SF2C_INPUT_S_SF2RECOMP_PAD_TYPE_2AEC)) return input_pad_family();
    if (which && SDL_IsGamepad(which)) return input_family_of_type(SDL_GetGamepadTypeForID(which));
    return bind_label_family();
}

static int pad_slot_for_id(SDL_JoystickID id) {
    for (int player = 0; player < LOCAL_PLAYER_COUNT; player++)
        if (pads[player].handle && pads[player].id == id) return player;
    return -1;
}

static void attach_pad(SDL_JoystickID id) {
    if (pad_slot_for_id(id) >= 0) return;
    int player;
    for (player = first_pad_player(); player < LOCAL_PLAYER_COUNT && pads[player].handle; player++) {}
    if (player == LOCAL_PLAYER_COUNT) {
        LOG(SF2C_INPUT_S_INPUT_IGNORING_GAMEPAD_D_NO__D194, id);
        return;
    }
    SDL_Gamepad *pad = SDL_OpenGamepad(id);
    if (!pad) {
        LOG(SF2C_INPUT_S_INPUT_CANNOT_OPEN_GAMEPAD_D__FA80, id, SDL_GetError());
        return;
    }
    pads[player].handle = pad;
    pads[player].id = id;
    pads[player].trigger_down[0] = pads[player].trigger_down[1] = false;
    reset_player_edges(player);
    recompute_pad(player);
    atomic_store(&pads[player].connected, true);
    LOG(SF2C_INPUT_S_INPUT_PLAYER_D_GAMEPAD_S_N_EF37, player + 1, SDL_GetGamepadName(pad));
    note_family();
}

static void detach_pad(SDL_JoystickID id) {
    int player = pad_slot_for_id(id);
    if (player < 0) return;
    SDL_Gamepad *pad = pads[player].handle;
    if (pad) {
        SDL_RumbleGamepad(pad, 0, 0, 0);
        SDL_CloseGamepad(pad);
    }
    pads[player].handle = NULL;
    pads[player].id = 0;
    pads[player].trigger_down[0] = pads[player].trigger_down[1] = false;
    atomic_store(&pads[player].connected, false);
    reset_player_edges(player);
    recompute_pad(player);
    SDL_LockSpinlock(&rumble_lock);
    memset(&rumble_values[player * RUMBLE_SIDES_PER_PLAYER], 0,
           RUMBLE_SIDES_PER_PLAYER * sizeof rumble_values[0]);
    SDL_UnlockSpinlock(&rumble_lock);
    rumble_last_lo[player] = rumble_last_hi[player] = 0;
    rumble_warned[player] = false;
    note_family();
    LOG(SF2C_INPUT_S_INPUT_PLAYER_D_GAMEPAD_DISCO_3B0C, player + 1);
}

static void scan_connected_pads(void) {
    static bool scanned;
    if (scanned) return;
    scanned = true;  // a late-added device arrives as GAMEPAD_ADDED
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) {
        bool free_slot = false;
        for (int player = first_pad_player(); player < LOCAL_PLAYER_COUNT; player++)
            if (!pads[player].handle) free_slot = true;
        if (free_slot) attach_pad(ids[i]);
    }
    SDL_free(ids);
}

void input_handle_event(const SDL_Event *e) {
#ifdef __ANDROID__
    bool movie_visible=movie_overlay_active();
    if (menu_touch_movie_event(e,movie_visible)) {
        if (movie_visible && e->type==SDL_EVENT_FINGER_DOWN) movie_request_skip();
        return;
    }
    menu_touch_event(e);
#endif
    // Consume the whole skip click, including its release after the overlay goes
    // away, so it cannot activate a menu item while the next scene is loading.
    static uint32_t movie_mouse_consumed;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        uint32_t bit = e->button.button > 0 && e->button.button <= SF2C_INPUT_N_32_E29C ? SDL_BUTTON_MASK(e->button.button) : 0;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && movie_overlay_active()) {
            movie_mouse_consumed |= bit;
            menu_mouse_reset();
            movie_request_skip();
            return;
        }
        if (movie_mouse_consumed & bit) {
            if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) movie_mouse_consumed &= ~bit;
            return;
        }
    } else if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        movie_mouse_consumed = 0;
    }
    menu_mouse_event(e);
    scan_connected_pads();  // pads present at startup may have emitted ADDED before this event loop
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
        if(movie_overlay_active() && e->key.scancode==SDL_SCANCODE_SPACE && bind_key_unassigned(SDL_SCANCODE_SPACE)) {
            if(e->type==SDL_EVENT_KEY_DOWN && !e->key.repeat)movie_request_skip();
            break; // skip must never become a held combat/menu button
        }
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F9 && !e->key.repeat) g_debug_frame_request = 1;
        if (e->key.scancode < SDL_SCANCODE_COUNT) keys_down[e->key.scancode] = e->type == SDL_EVENT_KEY_DOWN;
        uint64_t before = key_buttons;
        recompute_keyboard();
        uint64_t now = key_buttons;
        note_press(0, now & ~before, before & ~now);
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        attach_pad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        detach_pad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        int player = e->gbutton.which == TEST_DEVICE ? 0 : pad_slot_for_id(e->gbutton.which);
        if (player >= 0 && (!host_test || e->gbutton.which == TEST_DEVICE)) {  // event state is available before re-reading the device
            uint64_t b = pad_bits(e->gbutton.button);
            note_press(player, e->gbutton.down ? b : 0, e->gbutton.down ? 0 : b);
        }
        if (player >= 0) recompute_pad(player);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        int player = pad_slot_for_id(e->gaxis.which);
        if ((e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
            player >= 0 && !host_test) {  // triggers are buttons past halfway
            int t = e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            bool on = e->gaxis.value > SF2C_INPUT_N_16000_2570;
            if (on != pads[player].trigger_down[t]) {
                uint64_t b = pad_bits(PADB_LTRIGGER + t);
                note_press(player, on ? b : 0, on ? 0 : b);
                pads[player].trigger_down[t] = on;
            }
        }
        if (player >= 0) recompute_pad(player);
        break;
    }
    }
}

// Scripted input for testing: SF2RECOMP_INPUT="frame:BUTTON:frames,..." (BUTTON = A,B,X,Y,L,R,ZL,ZR,PLUS,
// MINUS,LEFT,UP,RIGHT,DOWN, or STICK_UP/DOWN/LEFT/RIGHT, RSTICK_UP/DOWN/LEFT/RIGHT, STICK_<degrees>: full tilt at
// that angle, 0 = right, 90 = up).
static _Atomic uint64_t script_buttons;
static _Atomic int32_t script_lx, script_ly, script_rx, script_ry;

// Tests of the bindings, through them like a real player:
// SF2RECOMP_HOST_INPUT="frame:key:<SDL key name>:frames,frame:pad:<button>:frames,..." holds a host key / controller
//   button (names as in sf2recomp.ini control sections: "Z", "Keypad 0", "south", "ltrigger"...);
//   "frame:lstick:<x>/<y>:frames" holds the controller's left stick there (raw SDL units, y up: a drifting stick).
// "frame:type:<text>" types <text> as real key events (down, text, up per character), through the overlay menu / name
//   entry first like a player's typing; "{Escape}" (any SDL key name) presses that key. No ',' or ':' in <text>.
// "frame:sdlpad:<button>:frames" / "frame:sdlkey:<key>:frames" hold a controller button / key as real events (down
//   at frame, up `frames` later): they reach menus and the on-screen keyboard first, as a player's would.
// "frame:mouse:<dx>/<dy>" moves by points; "at/<x>/<y>" positions in window points.
// "frame:mouse:click" taps; "down" / "up" hold/release left (real SDL events).
// "frame:shot:<name>" saves the window (game + menus) to build/<name>.png.
// "frame:resize:<W>x<H>" resizes the window to W x H points (menu_apply_window_requests: the main thread).
// SF2RECOMP_BIND_TEST="frame:pad|key:<input>:<host>,..." starts a rebind capture of <input> (bind_* name: a, zl,
//   ls_up...) at that frame (the Controls tab shows it waiting) and presses <host> 90 frames later ("esc" cancels,
//   "wait" lets it time out); "frame:reset:pad|key" resets to the defaults.
static void push_key(SDL_Scancode sc, SDL_Keymod mod, bool down) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.which = TEST_DEVICE, e.key.scancode = sc, e.key.mod = mod, e.key.down = down;
    e.key.key = SDL_GetKeyFromScancode(sc, mod, false);
    SDL_PushEvent(&e);
}
uint32_t g_window_resize_request;  // (W << 16 | H: menu_apply_window_requests applies it)
static void test_push(uint64_t frame, unsigned long start, unsigned long len, const char *dev, const char *arg) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    if (!strcmp(dev, SF2C_INPUT_S_QUIT_2156)) {
        if(frame==start) { e.type=SDL_EVENT_QUIT;SDL_PushEvent(&e); }
        return;
    }
    if (!strcmp(dev, SF2C_INPUT_S_RESIZE_2ABB)) {
        unsigned w = 0, h = 0;
        if (frame == start && sscanf(arg, SF2C_INPUT_S_UX_U_5273, &w, &h) == SF2C_INPUT_N_2_D473 && w && h && w < SF2C_INPUT_N_65536_0F25 && h < SF2C_INPUT_N_65536_0F25) {
            LOG(SF2C_INPUT_S_INPUT_TEST_WINDOW_TO_UX_U_N_B2C5, w, h);
            __atomic_store_n(&g_window_resize_request, w << SF2C_INPUT_N_16_B17E | h, __ATOMIC_RELAXED);
        }
        return;
    }
    if (!strcmp(dev, SF2C_INPUT_S_MOUSE_9C7C)) {
        if (frame != start) return;
        static bool positioned;
        static float mx, my;
        if (!positioned) SDL_GetMouseState(&mx, &my), positioned = true;
        if (!strcmp(arg, SF2C_INPUT_S_CLICK_FE4C) || !strcmp(arg, SF2C_INPUT_S_DOWN_DE3A) || !strcmp(arg, SF2C_INPUT_S_UP_5CC8)) {
            bool down = strcmp(arg, SF2C_INPUT_S_UP_5CC8) != 0;
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.which = TEST_DEVICE, e.button.button = SDL_BUTTON_LEFT, e.button.down = down, e.button.clicks = 1;
            e.button.x = mx, e.button.y = my;
            if (!strcmp(arg, SF2C_INPUT_S_CLICK_FE4C)) {
                SDL_PushEvent(&e);
                e.type = SDL_EVENT_MOUSE_BUTTON_UP, e.button.down = false;
            }
        } else {
            float x, y;
            if (sscanf(arg, SF2C_INPUT_S_AT_F_F_4191, &x, &y) != SF2C_INPUT_N_2_D473) {
                float dx, dy;
                if (sscanf(arg, SF2C_INPUT_S_F_F_4E0C, &dx, &dy) != SF2C_INPUT_N_2_D473) return;
                x = mx + dx, y = my + dy;
            }
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.which = TEST_DEVICE, e.motion.x = x, e.motion.y = y, e.motion.xrel = x - mx, e.motion.yrel = y - my;
            mx = x, my = y;
        }
        SDL_PushEvent(&e);
        return;
    }
    if (!strcmp(dev, SF2C_INPUT_S_SDLKEY_0436)) {
        if (frame == start || frame == start + len) push_key(SDL_GetScancodeFromName(arg), SDL_KMOD_NONE, frame == start);
        return;
    }
    if (!strcmp(dev, SF2C_INPUT_S_SDLPAD_091E)) {
        int b = bind_pad_from_name(arg);
        if (b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT || (frame != start && frame != start + len)) return;
        e.type = frame == start ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.which = TEST_DEVICE, e.gbutton.button = (Uint8)b, e.gbutton.down = frame == start;
        SDL_PushEvent(&e);
        return;
    }
    if (frame != start) return;
    LOG(SF2C_INPUT_S_INPUT_TEST_TYPING_S_N_FA03, arg);
    static char ascii[SF2C_INPUT_N_128_2747][SF2C_INPUT_N_2_D473];  // (a text event keeps a pointer to its text: these live on)
    for (const char *p = arg; *p; p++) {
        if (*p == SF2C_INPUT_CH_EMPTY_E88E && strchr(p, SF2C_INPUT_CH_EMPTY_9AC4)) {  // a named key
            char nm[SF2C_INPUT_N_32_E29C];
            size_t n = (size_t)(strchr(p, SF2C_INPUT_CH_EMPTY_9AC4) - p - 1);
            snprintf(nm, sizeof nm, SF2C_INPUT_S_S_F03C, (int)(n < sizeof nm ? n : sizeof nm - 1), p + 1);
            SDL_Scancode sc = SDL_GetScancodeFromName(nm);
            push_key(sc, SDL_KMOD_NONE, true), push_key(sc, SDL_KMOD_NONE, false);
            p += n + 1;
            continue;
        }
        SDL_Keymod mod = SDL_KMOD_NONE;
        SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)tolower((unsigned char)*p), &mod);
        if (isupper((unsigned char)*p)) mod |= SDL_KMOD_LSHIFT;
        push_key(sc, mod, true);
        int c = *p & SF2C_INPUT_N_0X7F_0AFB;
        ascii[c][0] = (char)c;
        memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_TEXT_INPUT, e.text.windowID = TEST_DEVICE, e.text.text = ascii[c];
        SDL_PushEvent(&e);
        push_key(sc, mod, false);
    }
}

static void host_test_frame(uint64_t frame) {
    static const char *hspec, *bspec;
    static int init;
    if (!init) { init = 1; hspec = getenv(SF2C_INPUT_S_SF2RECOMP_HOST_INPUT_8D91); bspec = getenv(SF2C_INPUT_S_SF2RECOMP_BIND_TEST_C2F2); host_test = hspec; }
    // Developer UI automation can append future SDL events while a real match
    // is running. The converted game never needs this file or an interpreter.
    static const char *script_file;
    static bool file_checked;
    static char *file_script;
    if(!file_checked) { file_checked=true;script_file=getenv(SF2C_INPUT_S_SF2RECOMP_HOST_INPUT_FILE_D436); }
    if(script_file) {
        if(!file_script)file_script=malloc(SF2C_INPUT_N_65537_D572);
        FILE *file=fopen(script_file,SF2C_INPUT_S_RB_46CA);
        if(file && file_script) {
            size_t n=fread(file_script,1,SF2C_INPUT_N_65536_0F25,file);file_script[n]=0;
            if(!ferror(file))hspec=file_script;
        }
        if(file)fclose(file);
        host_test=hspec;
    }
    static char *buf;  // (a copy to cut up; as long as the longest spec: a long scripted run is thousands of steps)
    static size_t cap;
    size_t need = SF2C_INPUT_N_2048_BFA0;
    if (hspec && strlen(hspec) + 1 > need) need = strlen(hspec) + 1;
    if (bspec && strlen(bspec) + 1 > need) need = strlen(bspec) + 1;
    if (need > cap) { free(buf); buf = malloc(need); cap = need; }
    if (hspec) {
        uint8_t keys[SDL_SCANCODE_COUNT] = {0};
        uint32_t padm = 0;
        int32_t sx = 0, sy = 0;
        snprintf(buf, cap, SF2C_INPUT_S_S_0030, hspec);
        char *save = NULL;
        for (char *tok = strtok_r(buf, SF2C_INPUT_S_EMPTY_08B5, &save); tok; tok = strtok_r(NULL, SF2C_INPUT_S_EMPTY_08B5, &save)) {
            unsigned long start = 0, len = SF2C_INPUT_N_10_4A44;
            char dev[SF2C_INPUT_N_8_2C62] = SF2C_INPUT_S_EMPTY_12AE, name[SF2C_INPUT_N_400_26D2] = SF2C_INPUT_S_EMPTY_12AE;
            if (sscanf(tok, SF2C_INPUT_S_LU_7_A_Z_399_LU_3672, &start, dev, name, &len) < SF2C_INPUT_N_3_4E07) continue;
            if (!strcmp(dev, SF2C_INPUT_S_QUIT_2156) || !strcmp(dev, SF2C_INPUT_S_TYPE_D007) || !strcmp(dev, SF2C_INPUT_S_SDLPAD_091E) || !strcmp(dev, SF2C_INPUT_S_SDLKEY_0436) || !strcmp(dev, SF2C_INPUT_S_SHOT_E3FA) ||
                !strcmp(dev, SF2C_INPUT_S_RESIZE_2ABB) || !strcmp(dev, SF2C_INPUT_S_MOUSE_9C7C)) {
                test_push(frame, start, len, dev, name);
                continue;
            }
            if (frame < start || frame >= start + len) continue;
            if (!strcmp(dev, SF2C_INPUT_S_KEY_C0C1)) { SDL_Scancode sc = SDL_GetScancodeFromName(name); if (sc) keys[sc] = 1; }
            else if (!strcmp(dev, SF2C_INPUT_S_LSTICK_A71F)) sscanf(name, SF2C_INPUT_S_D_D_9432, &sx, &sy);
            else { int h = bind_pad_from_name(name); if (h >= 0) padm |= SF2C_INPUT_N_1U_B8E6 << h; }
        }
        bool kch = memcmp(keys, test_keys, sizeof keys) != 0, pch = padm != test_pad || sx != test_lx || sy != test_ly;
        memcpy(test_keys, keys, sizeof keys);
        test_pad = padm;
        test_lx = sx, test_ly = sy;
        if (kch) recompute_keyboard();
        if (pch) recompute_pad(0);
    }
    if (bspec) {
        snprintf(buf, cap, SF2C_INPUT_S_S_0030, bspec);
        char *save = NULL;
        for (char *tok = strtok_r(buf, SF2C_INPUT_S_EMPTY_08B5, &save); tok; tok = strtok_r(NULL, SF2C_INPUT_S_EMPTY_08B5, &save)) {
            unsigned long start = 0;
            char dev[SF2C_INPUT_N_8_2C62] = SF2C_INPUT_S_EMPTY_12AE, in[SF2C_INPUT_N_16_B17E] = SF2C_INPUT_S_EMPTY_12AE, host[SF2C_INPUT_N_40_D59E] = SF2C_INPUT_S_EMPTY_12AE;
            if (sscanf(tok, SF2C_INPUT_S_LU_7_A_Z_15_A_Z_39_0E69, &start, dev, in, host) < SF2C_INPUT_N_3_4E07) continue;
            if (!strcmp(dev, SF2C_INPUT_S_RESET_1C7A)) { if (frame == start) bind_reset(!strcmp(in, SF2C_INPUT_S_KEY_C0C1) ? BIND_DEV_KEY : BIND_DEV_PAD); continue; }
            int d = !strcmp(dev, SF2C_INPUT_S_KEY_C0C1) ? BIND_DEV_KEY : BIND_DEV_PAD;
            if (frame == start) {
                LOG(SF2C_INPUT_S_INPUT_TEST_CAPTURE_S_S_N_5AD4, dev, in);
                bind_capture_begin(d, bind_slot_from_name(in));
            }
            if (frame == start + SF2C_INPUT_N_90_69F5 && strcmp(host, SF2C_INPUT_S_WAIT_F301)) {
                SDL_Event e;
                memset(&e, 0, sizeof e);
                if (!strcmp(host, SF2C_INPUT_S_ESC_5BAB) || d == BIND_DEV_KEY) {
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.scancode = !strcmp(host, SF2C_INPUT_S_ESC_5BAB) ? SDL_SCANCODE_ESCAPE : SDL_GetScancodeFromName(host);
                } else {
                    int h = bind_pad_from_name(host);
                    if (h >= PADB_LTRIGGER) {
                        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                        e.gaxis.axis = h == PADB_LTRIGGER ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                        e.gaxis.value = SF2C_INPUT_N_32767_D844;
                    } else {
                        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                        e.gbutton.button = (Uint8)h;
                    }
                }
                bind_capture_event(&e);
            }
        }
    }
}

static uint64_t g_pad_obj;
volatile int g_inject_plus;  // frames to hold Plus for the game (developer input injection)
RT_ON_RESET(input_reboot_reset) { g_pad_obj = 0; g_inject_plus = 0; }  // (reboot: the controller object was guest memory)
void input_frame(uint64_t frame) {
    atomic_store(&rumble_frame_ms, SDL_GetTicks());
    if (g_pad_obj && getenv(SF2C_INPUT_S_SF2RECOMP_DUMP_PAD_10A5) && frame % SF2C_INPUT_N_600_284B == 0) {
        const uint8_t *m = (const uint8_t *)(uintptr_t)(g_pad_obj - SF2C_INPUT_N_0X98_14A1);
        LOG(SF2C_INPUT_S_PAD_MGR_LLX_4E0_U_4E4_U_4E8__7E44, g_pad_obj - SF2C_INPUT_N_0X98_14A1,
            *(uint32_t *)(m + SF2C_INPUT_N_0X4E0_5731), *(uint32_t *)(m + SF2C_INPUT_N_0X4E4_8500), *(uint32_t *)(m + SF2C_INPUT_N_0X4E8_E512), *(uint32_t *)(m + SF2C_INPUT_N_0X548_9D5D),
            m[SF2C_INPUT_N_0X54C_B95A], m[SF2C_INPUT_N_0X54D_F4B6], m[SF2C_INPUT_N_0X54E_1B62], m[SF2C_INPUT_N_0X56C_5C91]);
        for (int i = 0; i < SF2C_INPUT_N_9_1958; i++) {
            const uint8_t *o = m + SF2C_INPUT_N_0X98_14A1 + SF2C_INPUT_N_0X48_0EF0 * i;
            uint64_t player = *(uint64_t *)(m + SF2C_INPUT_N_0X10_BF9C * i);
            LOG(SF2C_INPUT_S_PAD_CTL_D_ID_X_STYLE_X_BTN_X_55AF, i, *(uint32_t *)(o + SF2C_INPUT_N_8_2C62),
                *(uint32_t *)(o + SF2C_INPUT_N_0XC_DBAA), *(uint32_t *)(o + SF2C_INPUT_N_0X14_F942), o[SF2C_INPUT_N_0X44_E723], o[SF2C_INPUT_N_0X45_17A0], player,
                player ? *(uint32_t *)(uintptr_t)(player + SF2C_INPUT_N_8_2C62) : 0);
        }
    }
    extern void watch_arm(void);
    watch_arm();
    host_test_frame(frame);
    const char *spec = getenv(SF2C_INPUT_S_SF2RECOMP_INPUT_D0A3);
    if (!spec) return;
    static const char *const names[] = {SF2C_INPUT_S_A_7986, SF2C_INPUT_S_B_955C, SF2C_INPUT_S_X_DE9E, SF2C_INPUT_S_Y_DB5B, SF2C_INPUT_S_LSTICK_69E3, SF2C_INPUT_S_RSTICK_E134, SF2C_INPUT_S_L_8463, SF2C_INPUT_S_R_D742, SF2C_INPUT_S_ZL_38D1, SF2C_INPUT_S_ZR_5317, SF2C_INPUT_S_PLUS_7BAB,
                                        SF2C_INPUT_S_MINUS_0698, SF2C_INPUT_S_LEFT_251C, SF2C_INPUT_S_UP_DC56, SF2C_INPUT_S_RIGHT_1DD8, SF2C_INPUT_S_DOWN_31A3};
    uint64_t b = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    static char buf[SF2C_INPUT_N_16384_CA90];  // (long scripted sessions: the perf walk is ~4 KB)
    snprintf(buf, sizeof buf, SF2C_INPUT_S_S_0030, spec);
    for (char *tok = strtok(buf, SF2C_INPUT_S_EMPTY_08B5); tok; tok = strtok(NULL, SF2C_INPUT_S_EMPTY_08B5)) {
        unsigned long start = 0, len = SF2C_INPUT_N_10_4A44;
        char name[SF2C_INPUT_N_32_E29C] = SF2C_INPUT_S_EMPTY_12AE;
        if (sscanf(tok, SF2C_INPUT_S_LU_31_A_Z_0_9_LU_8035, &start, name, &len) < SF2C_INPUT_N_2_D473) continue;
        if (frame < start || frame >= start + len) continue;
        if (!strcmp(name, SF2C_INPUT_S_STICK_UP_D3F0)) ly = SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_STICK_DOWN_CF04)) ly = -SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_STICK_LEFT_0CB6)) lx = -SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_STICK_RIGHT_8316)) lx = SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_RSTICK_UP_B08A)) ry = SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_RSTICK_DOWN_11CD)) ry = -SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_RSTICK_LEFT_DE68)) rx = -SF2C_INPUT_N_32767_D844;
        else if (!strcmp(name, SF2C_INPUT_S_RSTICK_RIGHT_D371)) rx = SF2C_INPUT_N_32767_D844;
        else if (!strncmp(name, SF2C_INPUT_S_STICK_E164, SF2C_INPUT_N_6_E7F6) && name[SF2C_INPUT_N_6_E7F6] >= SF2C_INPUT_CH_0_59A9 && name[SF2C_INPUT_N_6_E7F6] <= SF2C_INPUT_CH_9_E453) {
            double r = atoi(name + SF2C_INPUT_N_6_E7F6) * SF2C_INPUT_N_3_14159265358979_B9A2 / SF2C_INPUT_N_180_7B69;
            lx = (int32_t)lround(cos(r) * SF2C_INPUT_N_32767_D844), ly = (int32_t)lround(sin(r) * SF2C_INPUT_N_32767_D844);
        }
        for (int i = 0; i < SF2C_INPUT_N_16_B17E; i++) if (!strcmp(name, names[i])) b |= SF2C_INPUT_N_1ULL_622E << i;
    }
    if (movie_overlay_active() && (b & ~script_buttons & SF2C_INPUT_N_3_4E07)) {
        movie_request_skip();
        atomic_fetch_or(&movie_consumed[0], b & SF2C_INPUT_N_3_4E07);
    }
    script_buttons = b;
    script_lx = lx;
    script_ly = ly;
    script_rx = rx;
    script_ry = ry;
}

extern bool menu_input_captured(void);  // runtime/ui/menu.mm: overlay menu or name entry up (it has the keys / pad)

// Buttons held right now (keyboard, gamepad, test script), independent of whether the game polls (movies).
uint64_t input_buttons_now(void) {
    return (menu_input_captured() ? 0 : key_buttons | atomic_load(&pads[0].buttons)) | script_buttons;
}

static int32_t clamp_axis(int32_t v) { return v > SF2C_INPUT_N_32767_D844 ? SF2C_INPUT_N_32767_D844 : v < -SF2C_INPUT_N_32767_D844 ? -SF2C_INPUT_N_32767_D844 : v; }

// NpadFullKeyState / NpadHandheldState / NpadJoy*State share this layout (0x28 bytes):
// {int64 sampling, u64 buttons, AnalogStick L {x,y}, AnalogStick R {x,y}, u32 attributes}
static void fill_state(Cpu *c) {
    uint8_t *s = (uint8_t *)(uintptr_t)c->x[0];
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[1];
    memset(s, 0, SF2C_INPUT_N_0X28_EAB9);
    *(int64_t *)s = ++sampling;
    static int nlog;
    if (nlog < SF2C_INPUT_N_6_E7F6 && (sampling % SF2C_INPUT_N_600_284B) == 1) { nlog++; LOG(SF2C_INPUT_S_INPUT_GETNPADSTATE_ID_X_LR_L_5FFB, id, c->x[SF2C_INPUT_N_30_624B]); }
    if (id >= LOCAL_PLAYER_COUNT) return;  // handheld and player IDs above the two local controllers are disconnected

    int player = (int)id;
    static uint64_t tap[LOCAL_PLAYER_COUNT], tap_frame[LOCAL_PLAYER_COUNT];
    static uint64_t last_b[LOCAL_PLAYER_COUNT], inject_until[LOCAL_PLAYER_COUNT];
    static uint32_t seen_generation[LOCAL_PLAYER_COUNT];
    uint32_t generation = atomic_load(&input_generation[player]);
    if (seen_generation[player] != generation) {
        tap[player] = tap_frame[player] = last_b[player] = 0;
        inject_until[player] = 0;
        seen_generation[player] = generation;
    }
    if (!player_connected(player)) return;

    // Host video covers the guest's menu: discard presses instead of navigating it unseen.
    if (movie_overlay_active()) {
        tap[player] = 0;
        atomic_store(&tap_buttons[player], 0);
        atomic_store(&press_unseen[player], 0);
        *(uint32_t *)(s + SF2C_INPUT_N_32_E29C) = 1;
        return;
    }

    bool menu = menu_input_captured();
    uint64_t pad = atomic_load(&pads[player].buttons);
    uint64_t key = player == 0 ? key_buttons : 0;
    uint64_t held = key | pad;
    uint64_t consumed = atomic_fetch_and(&movie_consumed[player], held | (player == 0 ? script_buttons : 0));
    held &= ~consumed;
    atomic_fetch_and(&press_unseen[player], ~held);
    {
        extern uint64_t renderer_present_count(void);
        uint64_t t = atomic_exchange(&tap_buttons[player], 0), f = renderer_present_count();
        if (t) {
            tap[player] |= t, tap_frame[player] = f;
            if (getenv(SF2C_INPUT_S_SF2RECOMP_INPUT_D0A3)) LOG(SF2C_INPUT_S_INPUT_SHORT_PRESS_LLX_N_25D6, (unsigned long long)t);
        } else if (f > tap_frame[player]) tap[player] = 0;
    }
    uint64_t scripted_buttons = player == 0 ? script_buttons & ~consumed : 0;
    int32_t scripted_lx = player == 0 ? script_lx : 0, scripted_ly = player == 0 ? script_ly : 0;
    int32_t scripted_rx = player == 0 ? script_rx : 0, scripted_ry = player == 0 ? script_ry : 0;
    uint64_t b = menu ? scripted_buttons : held | tap[player] | scripted_buttons;
    {   // injected Plus: held for ~10 presented frames
        extern uint64_t renderer_present_count(void);
        if (player == 0 && g_inject_plus > 0) {
            inject_until[player] = renderer_present_count() + SF2C_INPUT_N_10_4A44;
            g_inject_plus = 0;
        }
        if (renderer_present_count() < inject_until[player]) b |= SF2C_INPUT_N_1ULL_622E << BTN_PLUS;
    }
    int32_t lx = clamp_axis((menu ? 0 : (player == 0 ? key_lx : 0) + atomic_load(&pads[player].lx)) + scripted_lx);
    int32_t ly = clamp_axis((menu ? 0 : (player == 0 ? key_ly : 0) + atomic_load(&pads[player].ly)) + scripted_ly);
    int32_t rx = clamp_axis((menu ? 0 : (player == 0 ? key_rx : 0) + atomic_load(&pads[player].rx)) + scripted_rx);
    int32_t ry = clamp_axis((menu ? 0 : (player == 0 ? key_ry : 0) + atomic_load(&pads[player].ry)) + scripted_ry);

    if (!menu && g_settings.dpad_move && (int64_t)lx * lx + (int64_t)ly * ly < SF2C_INPUT_N_8000LL_9BC4 * SF2C_INPUT_N_8000_AD5F) {
        uint64_t d = held | scripted_buttons;
        int dx = (int)((d >> BTN_RIGHT) & 1) - (int)((d >> BTN_LEFT) & 1);
        int dy = (int)((d >> BTN_UP) & 1) - (int)((d >> BTN_DOWN) & 1);
        int32_t m = dx && dy ? SF2C_INPUT_N_23170_DE19 : SF2C_INPUT_N_32767_D844;
        if (dx || dy) lx = dx * m, ly = dy * m;
    }

    if (lx < -SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_16_B17E;
    if (ly > SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_17_4523;
    if (lx > SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_18_4EC9;
    if (ly < -SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_19_9400;
    if (rx < -SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_20_F5CA;
    if (ry > SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_21_6F4B;
    if (rx > SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_22_785F;
    if (ry < -SF2C_INPUT_N_16000_2570) b |= SF2C_INPUT_N_1ULL_622E << SF2C_INPUT_N_23_535F;

    *(uint64_t *)(s + SF2C_INPUT_N_8_2C62) = b;
    if (b != last_b[player] && getenv(SF2C_INPUT_S_SF2RECOMP_INPUT_D0A3))
        LOG(SF2C_INPUT_S_INPUT_ID_X_BUTTONS_LLX_SAMPL_47C5, id, (unsigned long long)b,
            (long long)sampling, c->x[SF2C_INPUT_N_30_624B]);
    last_b[player] = b;
    *(int32_t *)(s + SF2C_INPUT_N_16_B17E) = lx;
    *(int32_t *)(s + SF2C_INPUT_N_20_F5CA) = ly;
    *(int32_t *)(s + SF2C_INPUT_N_24_C235) = rx;
    *(int32_t *)(s + SF2C_INPUT_N_28_59E1) = ry;
    *(uint32_t *)(s + SF2C_INPUT_N_32_E29C) = 1;  // IsConnected
}

HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID12GETNPADSTATEEPNS0_FB34, hid_state_fullkey) { fill_state(c); }
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID12GETNPADSTATEEPNS0_6103, hid_state_dual) { fill_state(c); }
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID12GETNPADSTATEEPNS0_545E, hid_state_left) { fill_state(c); }
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID12GETNPADSTATEEPNS0_C279, hid_state_handheld) { fill_state(c); }
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID12GETNPADSTATEEPNS0_9B5C, hid_state_right) { fill_state(c); }

// FullKey for connected local players, including the virtual keyboard player.
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID15GETNPADSTYLESETER_546C, hid_style_set) {
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[0];
    static int nlog;
    if (nlog++ < SF2C_INPUT_N_4_4B22) LOG(SF2C_INPUT_S_INPUT_GETNPADSTYLESET_ID_X_N_9012, id);
    c->x[0] = id < LOCAL_PLAYER_COUNT && player_connected((int)id) ? 1 : 0;
}

// BindNpadStyleSetUpdateEvent(id, SystemEventType*, clear mode): signaled once so the game sees a
// controller connected.
#include "../kernel.h"
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID27BINDNPADSTYLESETU_76D5, hid_bind_style_event) {
    uint32_t w, r;
    KEvent *ev = kernel_create_event_pair(&w, &r);
    pthread_mutex_lock(&g_klock);
    kernel_signal(ev);
    pthread_mutex_unlock(&g_klock);
    uint64_t clear_mode = c->x[SF2C_INPUT_N_2_D473];
    c->x[0] = c->x[1];
    c->x[1] = r;
    c->x[SF2C_INPUT_N_2_D473] = 0;
    c->x[SF2C_INPUT_N_3_4E07] = clear_mode;
    guest_call(c, guest_sym(SF2C_INPUT_S_ZN2NN2OS33ATTACHREADABLEHAND_FA24));
    LOG(SF2C_INPUT_S_INPUT_STYLE_SET_UPDATE_EVENT_BFC3);
}

// The SDK vibration node graph stays recompiled; HID IPC 201/206 sends its
// output to rumble.h / SDL using the native-layout handles below.
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID25INITIALIZEVIBRATI_11B1, hid_init_vibration) { (void)c; }
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID25GETVIBRATIONDEVIC_8173, hid_vibration_handles) {
    uint32_t *out = (uint32_t *)(uintptr_t)c->x[0];
    int n = (int)c->x[1] < SF2C_INPUT_N_2_D473 ? (int)c->x[1] : SF2C_INPUT_N_2_D473;
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[SF2C_INPUT_N_2_D473];
    // Native wire layout is style, player, device index, padding (one byte each).
    // The old synthetic handles placed the style byte in padding.
    for (int i = 0; i < n; i++) out[i] = SF2C_INPUT_N_3U_3B80 | ((id & SF2C_INPUT_N_0XFF_7312) << SF2C_INPUT_N_8_2C62) | ((uint32_t)i << SF2C_INPUT_N_16_B17E);
    c->x[0] = (uint64_t)n;
}
HLE_FUNC(SF2C_INPUT_S_ZN2NN3HID22GETVIBRATIONDEVIC_F18A, hid_vibration_info) {
    uint32_t *info = (uint32_t *)(uintptr_t)c->x[0];
    uint32_t h = *(uint32_t *)(uintptr_t)c->x[1];
    info[0] = 1;        // VibrationDeviceType_LinearResonantActuator
    int side = rumble_side(h);
    info[1] = side < 0 ? 0 : (uint32_t)side + 1;  // None=0, Left=1, Right=2
}

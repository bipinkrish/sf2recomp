#include "../constants/hle.h"
// Instruction hooks needed by the native runtime. Game initialization, scene
// loading, object construction and exception behavior remain in the original code.
#include "hle.h"
#include "../settings.h"
#include "../network.h"
#include "../replay.h"
#include "../input_bind.h"
#include "../ui/menu.h"
#include "../ui/mouse.h"
#include "../ui/touch.h"
#include "movie.h"
#include <math.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    GUEST_STRING_ALLOC = SF2C_HOOKS_N_0X70003F6870ULL_5D22,
    GUEST_BEHAVIOUR_SET_ENABLED = SF2C_HOOKS_N_0X70012199E0ULL_E3B6
};

uint32_t rt_hook_features;

// Invoke one recompiled guest method while preserving the caller's CPU state.
static uint64_t guest_invoke4(Cpu *caller, uint64_t pc, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3) {
    Cpu saved = *caller;
    caller->x[0] = x0; caller->x[1] = x1; caller->x[SF2C_HOOKS_N_2_D473] = x2; caller->x[SF2C_HOOKS_N_3_4E07] = x3;
    caller->x[SF2C_HOOKS_N_4_4B22] = 0;
    guest_call(caller, pc);
    uint64_t result = caller->x[0];
    *caller = saved;
    return result;
}

static uint64_t guest_invoke2(Cpu *caller, uint64_t pc, uint64_t x0, uint64_t x1) {
    return guest_invoke4(caller, pc, x0, x1, 0, 0);
}

static void guest_set_vec2(Cpu *caller, uint64_t pc, uint64_t object, float x, float y) {
    Cpu saved = *caller;
    caller->x[0] = object; caller->x[1] = 0;
    memcpy(&caller->v[0], &x, sizeof x);
    memcpy(&caller->v[1], &y, sizeof y);
    guest_call(caller, pc);
    *caller = saved;
}

static float guest_get_float(Cpu *caller, uint64_t pc, uint64_t object) {
    Cpu saved = *caller;
    caller->x[0] = object; caller->x[1] = 0;
    guest_call(caller, pc);
    float result;
    memcpy(&result, &caller->v[0], sizeof result);
    *caller = saved;
    return result;
}

static uint32_t utf8_next(const unsigned char **cursor) {
    const unsigned char *s = *cursor;
    uint32_t cp = s[0];
    if (cp < SF2C_HOOKS_N_0X80_D41C) {
        if (cp) *cursor = s + 1;
        return cp;
    }
    unsigned need = cp >= SF2C_HOOKS_N_0XC2_0982 && cp <= SF2C_HOOKS_N_0XDF_F3B9 ? SF2C_HOOKS_N_2_D473 : cp >= SF2C_HOOKS_N_0XE0_F5C1 && cp <= SF2C_HOOKS_N_0XEF_8D17 ? SF2C_HOOKS_N_3_4E07 : cp >= SF2C_HOOKS_N_0XF0_2A69 && cp <= SF2C_HOOKS_N_0XF4_A61B ? SF2C_HOOKS_N_4_4B22 : 0;
    if (!need) { *cursor = s + 1; return SF2C_HOOKS_N_0XFFFD_1B30; }
    for (unsigned i = 1; i < need; ++i)
        if ((s[i] & SF2C_HOOKS_N_0XC0_4E7B) != SF2C_HOOKS_N_0X80_D41C) { *cursor = s + 1; return SF2C_HOOKS_N_0XFFFD_1B30; }
    cp = need == SF2C_HOOKS_N_2_D473 ? (s[0] & SF2C_HOOKS_N_0X1F_9227) : need == SF2C_HOOKS_N_3_4E07 ? (s[0] & SF2C_HOOKS_N_0X0F_33C5) : (s[0] & SF2C_HOOKS_N_0X07_A022);
    for (unsigned i = 1; i < need; ++i) cp = (cp << SF2C_HOOKS_N_6_E7F6) | (s[i] & SF2C_HOOKS_N_0X3F_AEBF);
    if ((need == SF2C_HOOKS_N_3_4E07 && cp < SF2C_HOOKS_N_0X800_D398) || (need == SF2C_HOOKS_N_4_4B22 && cp < SF2C_HOOKS_N_0X10000_4939) || cp > SF2C_HOOKS_N_0X10FFFF_3DC9 || (cp >= SF2C_HOOKS_N_0XD800_3321 && cp <= SF2C_HOOKS_N_0XDFFF_4FD5)) {
        *cursor = s + 1;
        return SF2C_HOOKS_N_0XFFFD_1B30;
    }
    *cursor = s + need;
    return cp;
}

static uint64_t guest_string(Cpu *caller, const char *utf8) {
    size_t units = 0;
    const unsigned char *cursor = (const unsigned char *)utf8;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        units += cp > SF2C_HOOKS_N_0XFFFF_B111 ? SF2C_HOOKS_N_2_D473 : 1;
        if (units > SF2C_HOOKS_N_256_51E8 || units > INT_MAX) return 0;
    }

    uint64_t string = guest_invoke2(caller, GUEST_STRING_ALLOC, 0, units);
    if (!string) return 0;
    ST32(string + SF2C_HOOKS_N_0X10_BF9C, (uint32_t)units);
    uint64_t chars = string + SF2C_HOOKS_N_0X14_F942;
    cursor = (const unsigned char *)utf8;
    size_t index = 0;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        if (cp <= SF2C_HOOKS_N_0XFFFF_B111) ST16(chars + index++ * SF2C_HOOKS_N_2_D473, (uint16_t)cp);
        else {
            cp -= SF2C_HOOKS_N_0X10000_4939;
            ST16(chars + index++ * SF2C_HOOKS_N_2_D473, (uint16_t)(SF2C_HOOKS_N_0XD800_3321 + (cp >> SF2C_HOOKS_N_10_4A44)));
            ST16(chars + index++ * SF2C_HOOKS_N_2_D473, (uint16_t)(SF2C_HOOKS_N_0XDC00_D759 + (cp & SF2C_HOOKS_N_0X3FF_6900)));
        }
    }
    ST16(chars + index * SF2C_HOOKS_N_2_D473, 0);
    return string;
}

// Call only before guest threads start (or after they have stopped). INI
// experiments require a restart; guest UI objects are never hot-unhooked.
static void reset_developer_state(void);
static void reset_lan_ui(void);
static void reset_campaign_replay(void);
void rt_hooks_configure(void) {
    LOG(SF2C_HOOKS_S_NETWORK_S_N_E20C, g_settings.network_enabled ? SF2C_HOOKS_S_ORIGINAL_NETWORK_PATHS_ENABL_BF61 : SF2C_HOOKS_S_DISABLED_OFFLINE_POLICY_6663);
    reset_developer_state();
    reset_lan_ui();
    reset_campaign_replay();
    replay_init();
    rt_hook_features =
#ifdef __ANDROID__
                       RT_HOOK_MOBILE_UI |
#endif
                       (replay_enabled() ? RT_HOOK_REPLAY : 0) |
                       (g_settings.allow_sparring_lan_multiplayer ? SF2C_HOOKS_N_4U_0B4E : 0);
}

// The Switch standard-stream getters return null. Corlib FileStream expects a
// live position/length handle even when the host has no guest console stream.
static int console_handle(Cpu *c) {
    static uint64_t handle;
    if (!handle) handle = (uint64_t)(uintptr_t)hle_alloc(SF2C_HOOKS_N_0X40_1AF3);
    c->x[0] = handle;
    return 1;
}

// PvpSettings.UpdateCharactersLock operates on the sparring roster, including
// inlined save checks. Override only its transient PvpCharacter.isOpened flags;
// leave MainSettings and the saved UnlockedCharacters list untouched.
static int unlock_sparring_characters(Cpu *c) {
    if (!g_settings.unlock_all_sparring_characters || !c->x[0]) return 0;
    uint64_t characters = LD64(c->x[0] + SF2C_HOOKS_N_0X18_D47C);
    if (!characters) return 0;
    uint64_t count = LD64(characters + SF2C_HOOKS_N_0X18_D47C);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t character = LD64(characters + SF2C_HOOKS_N_0X20_A766 + i * SF2C_HOOKS_N_8_2C62);
        if (character) ST8(character + SF2C_HOOKS_N_0X58_08FB, 1);
    }
    return 1;
}

static bool guest_object_named(Cpu *c, uint64_t object, const char *expected) {
    if (!object) return false;
    uint64_t name = guest_invoke2(c, SF2C_HOOKS_N_0X700122E460ULL_C805, object, 0);
    size_t length = strlen(expected);
    if (!name || LD32(name + SF2C_HOOKS_N_0X10_BF9C) != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (LD16(name + SF2C_HOOKS_N_0X14_F942 + i * SF2C_HOOKS_N_2_D473) != (unsigned char)expected[i]) return false;
    return true;
}

#include "developer_hooks.inc"
#include "sparring_roster_hooks.inc"
#include "lan_ui_hooks.inc"
#include "platform_ui_hooks.inc"
#include "campaign_replay_hooks.inc"

// The shipped MainSection has a fully wired, localized Exit item. Reveal it
// before GeneralMenuList.Awake collects active buttons and installs navigation.
static void enable_desktop_exit(Cpu *c, uint64_t list) {
    uint64_t root = guest_invoke2(c, SF2C_HOOKS_N_0X700121E6B0ULL_59E6, list, 0);
    if (!guest_object_named(c, root, SF2C_HOOKS_S_MAINSECTION_7138)) return;
    int children = guest_invoke2(c, SF2C_HOOKS_N_0X70015D9E00ULL_574A, root, 0);
    for (int i = 0; i < children; ++i) {
        uint64_t content = guest_invoke2(c, SF2C_HOOKS_N_0X70015DA1F0ULL_3A2D, root, i);
        if (!guest_object_named(c, content, SF2C_HOOKS_S_LISTCONTENT_1E7F)) continue;
        int items = guest_invoke2(c, SF2C_HOOKS_N_0X70015D9E00ULL_574A, content, 0);
        for (int j = 0; j < items; ++j) {
            uint64_t item = guest_invoke2(c, SF2C_HOOKS_N_0X70015DA1F0ULL_3A2D, content, j);
            if (!guest_object_named(c, item, SF2C_HOOKS_S_MENUITEMBUTTON_EXIT_6EC3)) continue;
            uint64_t object = guest_invoke2(c, SF2C_HOOKS_N_0X700121E720ULL_0F0E, item, 0);
            if (object) {
                lan_capture_menu_item(c,item);
                guest_invoke2(c, SF2C_HOOKS_N_0X7001226480ULL_8D5A, object, 1);
                LOG(SF2C_HOOKS_S_MENU_ENABLED_DESKTOP_EXIT_IT_DF45);
            }
            return;
        }
    }
}

#include "widescreen_hooks.inc"

static MenuMouse menu_mouse;
RT_ON_RESET(reset_menu_mouse_frame) { memset(&menu_mouse, 0, sizeof menu_mouse); }

// Hook BaseInput rather than global UnityEngine.Input: this enables the existing
// menu raycasters, pointer enter/exit, click and drag handlers without changing
// the Switch controller input used by fights.
static int menu_mouse_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case SF2C_HOOKS_N_0X70000FD740ULL_A3F5: {  // current EventSystem.Update, before TickModules
        lan_ui_pump(c);
        unsigned width = guest_invoke2(c, SF2C_HOOKS_N_0X70015CCB90ULL_4971, 0, 0);
        unsigned height = guest_invoke2(c, SF2C_HOOKS_N_0X70015CCC00ULL_3E02, 0, 0);
        MenuMouse previous = menu_mouse;
        menu_mouse = menu_mouse_frame(width, height, developer_console_open());
#ifdef __ANDROID__
        android_touch_count = menu_touch_frame(width, height, developer_console_open(), android_touches);
        // The Sparring roster uses a custom cell hit-test rather than Unity
        // Buttons. Feed only that helper the first real contact; the actual
        // EventSystem receives all contacts through UnityEngine.Input below.
        MenuMouse touch_mouse = {.x = -SF2C_HOOKS_N_10000_39E5, .y = -SF2C_HOOKS_N_10000_39E5};
        for (unsigned i = 0; i < android_touch_count; ++i) {
            MenuTouch *t = &android_touches[i];
            if (t->finger != 0) continue;
            touch_mouse.x = t->x; touch_mouse.y = t->y;
            touch_mouse.pressed = t->phase == TOUCH_BEGAN;
            touch_mouse.released = t->phase == TOUCH_ENDED;
            touch_mouse.held = t->phase < TOUCH_ENDED;
        }
        android_primary_touch=android_touch_count?touch_mouse:menu_mouse;
        sparring_mouse_frame(c, android_touch_count ? &touch_mouse : &menu_mouse);
#else
        sparring_mouse_frame(c,&menu_mouse);
#endif
        if (ENV(SF2C_HOOKS_S_LARECOMP_MOUSE_TRACE_86DC) &&
            (previous.x != menu_mouse.x || previous.y != menu_mouse.y || menu_mouse.pressed || menu_mouse.released))
            LOG(SF2C_HOOKS_S_MOUSE_SCREEN_UX_U_POINTER_1F_26D7, width, height,
                menu_mouse.x, menu_mouse.y, menu_mouse.held, menu_mouse.pressed, menu_mouse.released);
        return 0;
    }
    case SF2C_HOOKS_N_0X70000FA8F0ULL_340B: c->x[0] = 1; return 1;  // mousePresent: enable StandaloneInputModule
    // The Switch backend reports 16 touch slots even without contact. Unity's
    // ProcessTouchEvents then suppresses ProcessMouseEvent on every update.
    // Override only UI BaseInput; raw game/touch/controller APIs stay original.
    case SF2C_HOOKS_N_0X70000FABE0ULL_269E: case SF2C_HOOKS_N_0X70000FAC50ULL_467D:
#ifdef __ANDROID__
        c->x[0] = site == SF2C_HOOKS_N_0X70000FABE0ULL_269E ? 1 : android_touch_count;
#else
        c->x[0] = 0;
#endif
        return 1;
    case SF2C_HOOKS_N_0X70000FA960ULL_DAB1: case SF2C_HOOKS_N_0X70000FA9D0ULL_6B96: case SF2C_HOOKS_N_0X70000FAA40ULL_1AC9: {
        unsigned button = (uint32_t)c->x[1];
        unsigned bits = site == SF2C_HOOKS_N_0X70000FA960ULL_DAB1 ? menu_mouse.pressed :
                        site == SF2C_HOOKS_N_0X70000FA9D0ULL_6B96 ? menu_mouse.released : menu_mouse.held;
        c->x[0] = button < SF2C_HOOKS_N_3_4E07 && (bits & (SF2C_HOOKS_N_1U_B8E6 << button)) != 0;
        return 1;
    }
    case SF2C_HOOKS_N_0X70000FAAB0ULL_0E4D: case SF2C_HOOKS_N_0X70000FAB70ULL_F3C1: {
        float x = site == SF2C_HOOKS_N_0X70000FAAB0ULL_0E4D ? menu_mouse.x : menu_mouse.scroll_x;
        float y = site == SF2C_HOOKS_N_0X70000FAAB0ULL_0E4D ? menu_mouse.y : menu_mouse.scroll_y;
        c->v[0] = c->v[1] = 0;
        memcpy(&c->v[0], &x, sizeof x);
        memcpy(&c->v[1], &y, sizeof y);
        return 1;
    }
    default: return -1;
    }
}

int rt_hook(Cpu *c, uint64_t site) {
    int sparring_result = sparring_roster_hook(c, site);
    if (sparring_result >= 0) return sparring_result;
    int campaign_result = campaign_replay_hook(c, site);
    if (campaign_result >= 0) return campaign_result;
    int platform_result = platform_ui_hook(c, site);
    if (platform_result >= 0) return platform_result;
    int lan_result = lan_ui_hook(c,site);
    if(lan_result>=0)return lan_result;
    int mouse_result = menu_mouse_hook(c, site);
    if (mouse_result >= 0) return mouse_result;
    int wide_result = widescreen_hook(c, site);
    if (wide_result >= 0) return wide_result;
    if (network_hook(c, site)) return 1;
    switch (site) {
    case SF2C_HOOKS_N_0X7000DA027CULL_6732:
    case SF2C_HOOKS_N_0X7000C99E30ULL_08A4:
    case SF2C_HOOKS_N_0X7000C9DD50ULL_1622:
    case SF2C_HOOKS_N_0X7000CA7C00ULL_BAC6:
    case SF2C_HOOKS_N_0X7001177D10ULL_B365:
    case SF2C_HOOKS_N_0X7001177D20ULL_6E12:
    case SF2C_HOOKS_N_0X7001237B50ULL_DDBF:
    case SF2C_HOOKS_N_0X7001237BC0ULL_48FE:
    case SF2C_HOOKS_N_0X70010ED6A0ULL_3FF9:
    case SF2C_HOOKS_N_0X70010F1450ULL_9A8B:
    case SF2C_HOOKS_N_0X70010F1550ULL_0311:
    case SF2C_HOOKS_N_0X70010F1600ULL_3D3A:
    case SF2C_HOOKS_N_0X70010F1710ULL_F613:
    case SF2C_HOOKS_N_0X70010F17D0ULL_E5B0:
    case SF2C_HOOKS_N_0X70010F18E0ULL_6752:
    case SF2C_HOOKS_N_0X70010F19A0ULL_539C:
    case SF2C_HOOKS_N_0X70010F1AB0ULL_AF17:
    case SF2C_HOOKS_N_0X70010F1B80ULL_CC82:
    case SF2C_HOOKS_N_0X70010F1C50ULL_CA02:
    case SF2C_HOOKS_N_0X70010F1D60ULL_CBC9:
    case SF2C_HOOKS_N_0X70010F1E80ULL_AA67:
    case SF2C_HOOKS_N_0X70010F1F90ULL_C00E:
    case SF2C_HOOKS_N_0X70010F2DE0ULL_E9D4:
    case SF2C_HOOKS_N_0X7000CA7E80ULL_C4A9:
    case SF2C_HOOKS_N_0X7000CA6DF0ULL_A60A:
    case SF2C_HOOKS_N_0X7000D961F0ULL_34E0:
    case SF2C_HOOKS_N_0X7000C9DE18ULL_49F6:
    case SF2C_HOOKS_N_0X7000C9DE40ULL_6267:
    case SF2C_HOOKS_N_0X7000FF7AF0ULL_9A66:
    case SF2C_HOOKS_N_0X7000FF7C40ULL_F462:
        return replay_hook(c, site);
    case SF2C_HOOKS_N_0X7000FC9230ULL_1EA3:  // PvpSettings.UpdateCharactersLock
        return unlock_sparring_characters(c);
    case SF2C_HOOKS_N_0X7000E6B820ULL_6DB6:
        enable_desktop_exit(c, c->x[0]);
        dev_ensure_ui(c);
        return 0;
    case SF2C_HOOKS_N_0X7000E6EF20ULL_AD65:  // GeneralMenuScene.ShowExit
        menu_request_quit();
        return 1;
    case SF2C_HOOKS_N_0X7001217700ULL_7167: {  // UnityEngine.Application.set_targetFrameRate(int)
        int requested = (int32_t)c->x[1];
        int configured = g_settings.target_fps;
        if (requested != configured) {
            static int logged;
            if (logged++ < SF2C_HOOKS_N_8_2C62)
                LOG(SF2C_HOOKS_S_FPS_UNITY_TARGETFRAMERATE_D__E993, requested, configured);
            c->x[1] = (uint32_t)configured;
        }
        return 0;  // keep the game's original setter; replace its requested rate
    }
    case SF2C_HOOKS_N_0X7000844C30ULL_0148:
    case SF2C_HOOKS_N_0X7000844C40ULL_C8A5:
    case SF2C_HOOKS_N_0X7000844C50ULL_A088:
    case SF2C_HOOKS_N_0X7000386660ULL_880B:
    case SF2C_HOOKS_N_0X7000386650ULL_1DE2:
    case SF2C_HOOKS_N_0X7000386670ULL_414B:
        return console_handle(c);
    default:
        { int result = developer_hook(c, site); if (result >= 0) return result; }
        return 0;
    }
}

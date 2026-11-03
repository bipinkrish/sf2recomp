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
    GUEST_STRING_ALLOC = 0x70003f6870ull,
    GUEST_BEHAVIOUR_SET_ENABLED = 0x70012199e0ull
};

uint32_t rt_hook_features;

// Invoke one recompiled guest method while preserving the caller's CPU state.
static uint64_t guest_invoke4(Cpu *caller, uint64_t pc, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3) {
    Cpu saved = *caller;
    caller->x[0] = x0; caller->x[1] = x1; caller->x[2] = x2; caller->x[3] = x3;
    caller->x[4] = 0;
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
    if (cp < 0x80) {
        if (cp) *cursor = s + 1;
        return cp;
    }
    unsigned need = cp >= 0xc2 && cp <= 0xdf ? 2 : cp >= 0xe0 && cp <= 0xef ? 3 : cp >= 0xf0 && cp <= 0xf4 ? 4 : 0;
    if (!need) { *cursor = s + 1; return 0xfffd; }
    for (unsigned i = 1; i < need; ++i)
        if ((s[i] & 0xc0) != 0x80) { *cursor = s + 1; return 0xfffd; }
    cp = need == 2 ? (s[0] & 0x1f) : need == 3 ? (s[0] & 0x0f) : (s[0] & 0x07);
    for (unsigned i = 1; i < need; ++i) cp = (cp << 6) | (s[i] & 0x3f);
    if ((need == 3 && cp < 0x800) || (need == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        *cursor = s + 1;
        return 0xfffd;
    }
    *cursor = s + need;
    return cp;
}

static uint64_t guest_string(Cpu *caller, const char *utf8) {
    size_t units = 0;
    const unsigned char *cursor = (const unsigned char *)utf8;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        units += cp > 0xffff ? 2 : 1;
        if (units > 256 || units > INT_MAX) return 0;
    }

    uint64_t string = guest_invoke2(caller, GUEST_STRING_ALLOC, 0, units);
    if (!string) return 0;
    ST32(string + 0x10, (uint32_t)units);
    uint64_t chars = string + 0x14;
    cursor = (const unsigned char *)utf8;
    size_t index = 0;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        if (cp <= 0xffff) ST16(chars + index++ * 2, (uint16_t)cp);
        else {
            cp -= 0x10000;
            ST16(chars + index++ * 2, (uint16_t)(0xd800 + (cp >> 10)));
            ST16(chars + index++ * 2, (uint16_t)(0xdc00 + (cp & 0x3ff)));
        }
    }
    ST16(chars + index * 2, 0);
    return string;
}

// Call only before guest threads start (or after they have stopped). INI
// experiments require a restart; guest UI objects are never hot-unhooked.
static void reset_developer_state(void);
static void reset_lan_ui(void);
static void reset_campaign_replay(void);
void rt_hooks_configure(void) {
    LOG("[network] %s\n", g_settings.network_enabled ? "original network paths enabled" : "disabled (offline policy)");
    reset_developer_state();
    reset_lan_ui();
    reset_campaign_replay();
    replay_init();
    rt_hook_features =
#ifdef __ANDROID__
                       RT_HOOK_MOBILE_UI |
#endif
                       (replay_enabled() ? RT_HOOK_REPLAY : 0) |
                       (g_settings.allow_sparring_lan_multiplayer ? 4u : 0);
}

// The Switch standard-stream getters return null. Corlib FileStream expects a
// live position/length handle even when the host has no guest console stream.
static int console_handle(Cpu *c) {
    static uint64_t handle;
    if (!handle) handle = (uint64_t)(uintptr_t)hle_alloc(0x40);
    c->x[0] = handle;
    return 1;
}

// PvpSettings.UpdateCharactersLock operates on the sparring roster, including
// inlined save checks. Override only its transient PvpCharacter.isOpened flags;
// leave MainSettings and the saved UnlockedCharacters list untouched.
static int unlock_sparring_characters(Cpu *c) {
    if (!g_settings.unlock_all_sparring_characters || !c->x[0]) return 0;
    uint64_t characters = LD64(c->x[0] + 0x18);
    if (!characters) return 0;
    uint64_t count = LD64(characters + 0x18);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t character = LD64(characters + 0x20 + i * 8);
        if (character) ST8(character + 0x58, 1);
    }
    return 1;
}

static bool guest_object_named(Cpu *c, uint64_t object, const char *expected) {
    if (!object) return false;
    uint64_t name = guest_invoke2(c, 0x700122e460ull, object, 0);
    size_t length = strlen(expected);
    if (!name || LD32(name + 0x10) != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (LD16(name + 0x14 + i * 2) != (unsigned char)expected[i]) return false;
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
    uint64_t root = guest_invoke2(c, 0x700121e6b0ull, list, 0);
    if (!guest_object_named(c, root, "MainSection")) return;
    int children = guest_invoke2(c, 0x70015d9e00ull, root, 0);
    for (int i = 0; i < children; ++i) {
        uint64_t content = guest_invoke2(c, 0x70015da1f0ull, root, i);
        if (!guest_object_named(c, content, "ListContent")) continue;
        int items = guest_invoke2(c, 0x70015d9e00ull, content, 0);
        for (int j = 0; j < items; ++j) {
            uint64_t item = guest_invoke2(c, 0x70015da1f0ull, content, j);
            if (!guest_object_named(c, item, "MenuItemButton_exit")) continue;
            uint64_t object = guest_invoke2(c, 0x700121e720ull, item, 0);
            if (object) {
                lan_capture_menu_item(c,item);
                guest_invoke2(c, 0x7001226480ull, object, 1);
                LOG("[menu] enabled desktop Exit item\n");
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
    case 0x70000fd740ull: {  // current EventSystem.Update, before TickModules
        lan_ui_pump(c);
        unsigned width = guest_invoke2(c, 0x70015ccb90ull, 0, 0);
        unsigned height = guest_invoke2(c, 0x70015ccc00ull, 0, 0);
        MenuMouse previous = menu_mouse;
        menu_mouse = menu_mouse_frame(width, height, developer_console_open());
#ifdef __ANDROID__
        android_touch_count = menu_touch_frame(width, height, developer_console_open(), android_touches);
        // The Sparring roster uses a custom cell hit-test rather than Unity
        // Buttons. Feed only that helper the first real contact; the actual
        // EventSystem receives all contacts through UnityEngine.Input below.
        MenuMouse touch_mouse = {.x = -10000, .y = -10000};
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
        if (ENV("LARECOMP_MOUSE_TRACE") &&
            (previous.x != menu_mouse.x || previous.y != menu_mouse.y || menu_mouse.pressed || menu_mouse.released))
            LOG("[mouse] screen %ux%u pointer %.1f/%.1f held=%x down=%x up=%x\n", width, height,
                menu_mouse.x, menu_mouse.y, menu_mouse.held, menu_mouse.pressed, menu_mouse.released);
        return 0;
    }
    case 0x70000fa8f0ull: c->x[0] = 1; return 1;  // mousePresent: enable StandaloneInputModule
    // The Switch backend reports 16 touch slots even without contact. Unity's
    // ProcessTouchEvents then suppresses ProcessMouseEvent on every update.
    // Override only UI BaseInput; raw game/touch/controller APIs stay original.
    case 0x70000fabe0ull: case 0x70000fac50ull:
#ifdef __ANDROID__
        c->x[0] = site == 0x70000fabe0ull ? 1 : android_touch_count;
#else
        c->x[0] = 0;
#endif
        return 1;
    case 0x70000fa960ull: case 0x70000fa9d0ull: case 0x70000faa40ull: {
        unsigned button = (uint32_t)c->x[1];
        unsigned bits = site == 0x70000fa960ull ? menu_mouse.pressed :
                        site == 0x70000fa9d0ull ? menu_mouse.released : menu_mouse.held;
        c->x[0] = button < 3 && (bits & (1u << button)) != 0;
        return 1;
    }
    case 0x70000faab0ull: case 0x70000fab70ull: {
        float x = site == 0x70000faab0ull ? menu_mouse.x : menu_mouse.scroll_x;
        float y = site == 0x70000faab0ull ? menu_mouse.y : menu_mouse.scroll_y;
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
    case 0x7000da027cull:
    case 0x7000c99e30ull:
    case 0x7000c9dd50ull:
    case 0x7000ca7c00ull:
    case 0x7001177d10ull:
    case 0x7001177d20ull:
    case 0x7001237b50ull:
    case 0x7001237bc0ull:
    case 0x70010ed6a0ull:
    case 0x70010f1450ull:
    case 0x70010f1550ull:
    case 0x70010f1600ull:
    case 0x70010f1710ull:
    case 0x70010f17d0ull:
    case 0x70010f18e0ull:
    case 0x70010f19a0ull:
    case 0x70010f1ab0ull:
    case 0x70010f1b80ull:
    case 0x70010f1c50ull:
    case 0x70010f1d60ull:
    case 0x70010f1e80ull:
    case 0x70010f1f90ull:
    case 0x70010f2de0ull:
    case 0x7000ca7e80ull:
    case 0x7000ca6df0ull:
    case 0x7000d961f0ull:
    case 0x7000c9de18ull:
    case 0x7000c9de40ull:
    case 0x7000ff7af0ull:
    case 0x7000ff7c40ull:
        return replay_hook(c, site);
    case 0x7000fc9230ull:  // PvpSettings.UpdateCharactersLock
        return unlock_sparring_characters(c);
    case 0x7000e6b820ull:
        enable_desktop_exit(c, c->x[0]);
        dev_ensure_ui(c);
        return 0;
    case 0x7000e6ef20ull:  // GeneralMenuScene.ShowExit
        menu_request_quit();
        return 1;
    case 0x7001217700ull: {  // UnityEngine.Application.set_targetFrameRate(int)
        int requested = (int32_t)c->x[1];
        int configured = g_settings.target_fps;
        if (requested != configured) {
            static int logged;
            if (logged++ < 8)
                LOG("[fps] Unity targetFrameRate %d -> configured %d\n", requested, configured);
            c->x[1] = (uint32_t)configured;
        }
        return 0;  // keep the game's original setter; replace its requested rate
    }
    case 0x7000844c30ull:
    case 0x7000844c40ull:
    case 0x7000844c50ull:
    case 0x7000386660ull:
    case 0x7000386650ull:
    case 0x7000386670ull:
        return console_handle(c);
    default:
        { int result = developer_hook(c, site); if (result >= 0) return result; }
        return 0;
    }
}

// Included after the guest invocation/string helpers in hooks.c. Every Unity
// call below runs on the guest update thread, preserving its complete CPU state.
#include "../developer.h"

static bool dev_overlays_visible, dev_creating, dev_pumping;
static bool dev_welcomed;
static uint64_t dev_console_input;
static unsigned dev_scroll_bottom_frames;
static char dev_command[256];

static void reset_developer_state(void) {
    dev_overlays_visible = g_settings.developer_overlays_on_start;
    dev_creating = dev_pumping = dev_welcomed = false;
    dev_console_input = 0;
    dev_scroll_bottom_frames = 0;
    dev_command[0] = 0;
    developer_input_reset();
}

static bool dev_enabled(void) {
    return g_settings.developer_console || g_settings.developer_overlays;
}

static bool dev_read_string(uint64_t string, char *out, size_t cap) {
    if (!string || !cap) return false;
    uint32_t length = LD32(string + 0x10);
    if (length > 256) return false;
    size_t at = 0;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t cp = LD16(string + 0x14 + i * 2);
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < length) {
            uint32_t low = LD16(string + 0x14 + (i + 1) * 2);
            if (low >= 0xdc00 && low <= 0xdfff) { ++i; cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00; }
        }
        unsigned bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (at + bytes >= cap) return false;
        if (bytes == 1) out[at++] = cp;
        else {
            out[at++] = (bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0) | (cp >> (6 * (bytes - 1)));
            for (int k = bytes - 2; k >= 0; --k) out[at++] = 0x80 | ((cp >> (6 * k)) & 0x3f);
        }
    }
    out[at] = 0;
    return true;
}

static void dev_log(Cpu *c, const char *text) {
    uint64_t value = guest_string(c, text);
    if (value) guest_invoke2(c, 0x7000ceb940ull, 0, value); // ConsoleUI.Log
    LOG("[developer] %s\n", text);
}

static void dev_ensure_ui(Cpu *c) {
    if (!dev_enabled() || dev_creating) return;
    if (guest_invoke2(c, 0x7000ced0f0ull, 0, 0)) return;
    dev_creating = true;
    guest_invoke2(c, 0x7000c7b050ull, 0, 0); // Register the original console command database (idempotent)
    // ModuleHolder.Awake changes a shared construction context. A nested debug
    // canvas must not become the parent scene's current holder.
    uint64_t previous_holder = guest_invoke2(c, 0x7000e2f610ull, 0, 0);
    uint64_t holder_slot = 0, holder_class = 0, holder_fields = 0;
    if (rt_probe_u64(0x70039e9b98ull, &holder_slot) && holder_slot &&
        rt_probe_u64(holder_slot, &holder_class) && holder_class)
        rt_probe_u64(holder_class + 0xb8, &holder_fields);
    uint64_t prefab = guest_invoke2(c, 0x700123de70ull, 0,
        guest_string(c, "Prefabs/Debug/DebugCanvas"));
    if (prefab) {
        uint64_t object = guest_invoke2(c, 0x70012331f0ull, 0, prefab);
        if (object) {
            guest_invoke2(c, 0x70012339d0ull, 0, object); // original DontDestroyOnLoad
            LOG("[developer] mounted original DebugCanvas\n");
        }
    } else LOG("[developer] DebugCanvas resource not found\n");
    if (holder_fields && LD64(holder_fields) != previous_holder) {
        ST64(holder_fields, previous_holder);
        LOG("[developer] restored scene module-holder context\n");
    }
    dev_creating = false;
}

static void dev_refresh_overlays(Cpu *c) {
    uint64_t ui = guest_invoke2(c, 0x7000ced0f0ull, 0, 0);
    if (ui) guest_invoke2(c, 0x7000ced1d0ull, ui, dev_overlays_visible);
}

static void dev_set_input(Cpu *c, uint64_t console) {
    uint64_t input = console ? LD64(console + 0x30) : 0;
    if (input) guest_invoke2(c, 0x70005b5e80ull, input, guest_string(c, dev_command));
}

static void dev_get_input(Cpu *c, uint64_t console) {
    uint64_t input = console ? LD64(console + 0x30) : 0;
    if (input) dev_read_string(guest_invoke2(c, 0x70005b5e70ull, input, 0), dev_command, sizeof dev_command);
}

static void dev_scroll(Cpu *c, uint64_t console, float delta, bool bottom) {
    uint64_t text = console ? LD64(console + 0x28) : 0;
    if (!text) return;
    uint64_t type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.ScrollRect, UnityEngine.UI"));
    uint64_t scroll = type ? guest_invoke2(c, 0x700121ead0ull, text, type) : 0;
    if (!scroll) return;
    float position = bottom ? 0 : guest_get_float(c, 0x70005d6420ull, scroll);
    position = fminf(1, fmaxf(0, position + delta));
    Cpu saved = *c;
    c->x[0] = scroll; c->x[1] = 0;
    memcpy(&c->v[0], &position, sizeof position);
    guest_call(c, 0x70005d6560ull);
    *c = saved;
}

static const char *dev_trainer(Cpu *c, const char *action) {
    static const struct { const char *name; int id; } commands[] = {
        {"pause", 1}, {"perks", 2}, {"step", 3}, {"win_round", 4}, {"win", 5},
        {"lose_round", 6}, {"lose", 7}, {"reset_round", 9}, {"reset", 10},
        {"slow", 13}, {"magic", 18}, {"combo", 19}, {"style", 20},
        {"critical", 21}, {"god", 22}, {"bot_god", 23}, {"hud", 24}
    };
    if (!g_settings.developer_trainer) return "Trainer disabled in [developer] trainer.";
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        if (strcmp(action, commands[i].name)) continue;
        if (!guest_invoke2(c, 0x7000c91c70ull, 0, 0)) return "Start a fight before using trainer commands.";
        guest_invoke2(c, 0x700102ecd0ull, 0, 0); // FightCheatHandler.Init
        return guest_invoke2(c, 0x700102f540ull, 0, commands[i].id) ?
            "Trainer action applied." : "This trainer action is unavailable in the current fight state.";
    }
    return "trainer: god, bot_god, critical, magic, slow, pause, step, win, win_round, lose, lose_round, reset, reset_round, combo, style, perks, hud";
}

static const char *dev_shop(Cpu *c, const char *action) {
    if (!g_settings.developer_shop_cheats) return "Shop cheats disabled in [developer] shop_cheats.";
    uint64_t type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "Nekki.SF2.GUI.Shop.CheatsPanel, Assembly-CSharp"));
    uint64_t panel = type ? guest_invoke2(c, 0x7001233b80ull, 0, type) : 0;
    if (!panel) return "Open the Story shop before using shop commands.";
    uint64_t method = !strcmp(action, "show") ? 0x7000da6e80ull :
        !strcmp(action, "hide") ? 0x7000da6f50ull :
        !strcmp(action, "money") ? 0x7000da6ff0ull :
        !strcmp(action, "gems") ? 0x7000da7100ull :
        !strcmp(action, "level") ? 0x7000da7210ull :
        !strcmp(action, "items") ? 0x7000da7350ull : 0;
    if (!method) return "shop: show, hide, money, gems, level, items. Progression commands are saved by the game.";
    guest_invoke2(c, method, panel, 0);
    return "Shop action applied.";
}

// Return NULL for the game's original command database to handle the command.
static const char *dev_execute(Cpu *c, const char *command) {
    if (!strcmp(command, "devhelp"))
        return "F1 / `: console; F2: overlays. Commands: trainer <action>, shop <action>, overlay [on|off|toggle], config [ai|intro|timings] [on|off]. Use help for original game commands.";
    if (!strcmp(command, "trainer") || !strncmp(command, "trainer ", 8)) return dev_trainer(c, command[7] ? command + 8 : "");
    if (!strcmp(command, "shop") || !strncmp(command, "shop ", 5)) return dev_shop(c, command[4] ? command + 5 : "");
    if (!strcmp(command, "overlay") || !strncmp(command, "overlay ", 8)) {
        if (!g_settings.developer_overlays) return "Overlays disabled in [developer] overlays.";
        const char *arg = command[7] ? command + 8 : "toggle";
        if (!strcmp(arg, "on")) dev_overlays_visible = true;
        else if (!strcmp(arg, "off")) dev_overlays_visible = false;
        else if (!strcmp(arg, "toggle")) dev_overlays_visible = !dev_overlays_visible;
        else return "overlay: on, off, toggle. Select FPS/memory/build/model overlays in [developer].";
        dev_refresh_overlays(c);
        return dev_overlays_visible ? "Debug overlays on." : "Debug overlays off.";
    }
    if (!strcmp(command, "config") || !strncmp(command, "config ", 7)) {
        char name[24], value[12], extra;
        if (sscanf(command + 6, "%23s %11s %c", name, value, &extra) != 2 ||
            (strcmp(value, "on") && strcmp(value, "off")))
            return "config: ai on/off, intro on/off, timings on/off. Session changes; edit [gameplay] to persist across launches.";
        bool on = !strcmp(value, "on");
        uint64_t setter = 0;
        if (!strcmp(name, "ai")) { g_settings.ai_enabled = on; setter = 0x7000e19d30ull; }
        else if (!strcmp(name, "intro")) { g_settings.show_intro = on; setter = 0x7000e19c10ull; }
        else if (!strcmp(name, "timings")) { g_settings.show_time_results = on; setter = 0x7000e1a450ull; }
        else return "Unknown setting. Use config ai/intro/timings on/off.";
        guest_invoke2(c, setter, 0, on);
        return "Session setting updated.";
    }
    return NULL;
}

static void dev_pump(Cpu *c) {
    if (!dev_enabled() || dev_pumping) return;
    dev_pumping = true;
    DeveloperEvent event;
    while (developer_poll_event(&event)) {
        uint64_t console = guest_invoke2(c, 0x7000ceb1a0ull, 0, 0);
        if (event.action == DEV_OVERLAYS_TOGGLE) {
            dev_overlays_visible = !dev_overlays_visible;
            dev_refresh_overlays(c);
            continue;
        }
        if (!console || !g_settings.developer_console) { developer_set_console_open(false); continue; }
        dev_console_input = LD64(console + 0x30);
        bool open = guest_invoke2(c, 0x7000ceb210ull, console, 0);
        if (event.action == DEV_CONSOLE_TOGGLE || event.action == DEV_CONSOLE_CLOSE) {
            if (event.action == DEV_CONSOLE_TOGGLE || open) guest_invoke2(c, 0x7000ceb520ull, console, 0);
            open = guest_invoke2(c, 0x7000ceb210ull, console, 0);
            developer_set_console_open(open);
            if (open && !dev_welcomed) {
                dev_welcomed = true;
                dev_log(c, "SF2 developer console. F1 / ` closes; F2 toggles overlays. Type devhelp for trainer/config controls, help for game commands.");
            }
            continue;
        }
        if (!open) continue;
        if (event.action == DEV_TEXT) {
            if (!strcmp(event.text, "`") || !strcmp(event.text, "~")) continue;
            size_t have = strlen(dev_command), add = strlen(event.text);
            if (have + add < sizeof dev_command) memcpy(dev_command + have, event.text, add + 1);
            dev_set_input(c, console);
        } else if (event.action == DEV_BACKSPACE) {
            size_t n = strlen(dev_command);
            if (n) { --n; while (n && ((unsigned char)dev_command[n] & 0xc0) == 0x80) --n; dev_command[n] = 0; }
            dev_set_input(c, console);
        } else if (event.action == DEV_SUBMIT) {
            if (*dev_command) guest_invoke2(c, 0x7000cebcc0ull, console, 0);
            dev_scroll_bottom_frames = 3; // let Unity finish updating the scroll content's layout
            dev_command[0] = 0;
            dev_set_input(c, console);
        } else if (event.action == DEV_HISTORY_PREV || event.action == DEV_HISTORY_NEXT) {
            guest_invoke2(c, event.action == DEV_HISTORY_PREV ? 0x7000ceb710ull : 0x7000ceb830ull, console, 0);
            dev_get_input(c, console);
        } else if (event.action == DEV_SCROLL_UP || event.action == DEV_SCROLL_DOWN) {
            dev_scroll_bottom_frames = 0;
            dev_scroll(c, console, event.action == DEV_SCROLL_UP ? 0.2f : -0.2f, false);
        }
    }
    dev_pumping = false;
}

static int dev_return_bool(Cpu *c, bool value) { c->x[0] = value; return 1; }

static int developer_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000f41cd0ull: // KeyboardController.HandleKeyboard: stable guest update thread
        dev_pump(c);
        return 0;
    case 0x7000ceb3c0ull: { // ConsoleUI.Update, including before KeyboardController starts
        uint64_t console = c->x[0];
        if (g_settings.developer_console) dev_console_input = LD64(console + 0x30);
        dev_pump(c);
        if (g_settings.developer_console) {
            developer_set_console_open(guest_invoke2(c, 0x7000ceb210ull, console, 0));
            if (dev_scroll_bottom_frames) { --dev_scroll_bottom_frames; dev_scroll(c, console, 0, true); }
        }
        return 0;
    }
    case 0x7000ceb330ull: // ConsoleUI.Free
        developer_set_console_open(false);
        dev_console_input = 0;
        dev_command[0] = 0;
        return 0;
    case 0x70005c07c0ull: // Console text comes from SDL, without opening Switch's modal software keyboard
        if (g_settings.developer_console && !dev_console_input) {
            uint64_t console = guest_invoke2(c, 0x7000ceb1a0ull, 0, 0);
            if (console) dev_console_input = LD64(console + 0x30);
        }
        return g_settings.developer_console && dev_console_input && c->x[0] == dev_console_input ? 1 : 0;
    case 0x7000ceb230ull: return dev_return_bool(c, g_settings.developer_console);
    case 0x7000ce9d90ull: // DebugUIModule.IsAvailable; keep original scene restrictions
        if (!g_settings.developer_overlays || !dev_overlays_visible) return dev_return_bool(c, false);
        return 0;
    case 0x7000cec500ull: if (!g_settings.developer_fps) return dev_return_bool(c, false); return 0;
    case 0x7000cec850ull: if (!g_settings.developer_memory) return dev_return_bool(c, false); return 0;
    case 0x7000ce9c10ull: if (!g_settings.developer_build_info) return dev_return_bool(c, false); return 0;
    case 0x7000cecaa0ull: if (!g_settings.developer_model_info) return dev_return_bool(c, false); return 0;
    case 0x7000da20a0ull: // Keep the desktop console's keyboard input and history controls
        if (g_settings.developer_console && guest_object_named(c, c->x[0], "ConsoleUI")) ST8(c->x[0] + 0x1a, 1);
        return 0;
    case 0x70000ad0e0ull: // Enable only the local shop panel's debug checks, never global/online debug mode
        if (g_settings.developer_shop_cheats && (c->x[30] == 0x7000ec7330ull ||
            (c->x[30] >= 0x7000da6e80ull && c->x[30] < 0x7000da7340ull)))
            return dev_return_bool(c, true);
        return 0;
    case 0x700102ec00ull: return dev_return_bool(c, g_settings.developer_trainer);
    case 0x7000e1a5b0ull:
        // The original game shares its intro switch with manual gallery
        // playback. Disabling the boot intro must not disable selected movies.
        return dev_return_bool(c,c->x[30]==0x7000e6e49cull || g_settings.show_intro);
    case 0x7000e1a760ull: return dev_return_bool(c, g_settings.ai_enabled);
    case 0x7000e19adcull: // AssemblyController.Parse completed; apply INI after the original config
        guest_invoke2(c, 0x7000e19c10ull, 0, g_settings.show_intro);
        guest_invoke2(c, 0x7000e19d30ull, 0, g_settings.ai_enabled);
        guest_invoke2(c, 0x7000e1a450ull, 0, g_settings.show_time_results);
        return 0;
    case 0x7000fc5060ull: // Gallery only; retain normal watched-video save behavior elsewhere
        if (g_settings.unlock_all_videos && c->x[30] >= 0x7000e71070ull && c->x[30] < 0x7000e71390ull)
            return dev_return_bool(c, true);
        return 0;
    case 0x7000c89ea0ull: { // ConsoleDatabase.ExecuteCommand
        if (!g_settings.developer_console) return 0;
        char text[256];
        if (!dev_read_string(c->x[1], text, sizeof text)) return 0;
        const char *result = dev_execute(c, text);
        if (!result) return 0;
        c->x[0] = guest_string(c, result);
        return 1;
    }
    default: return -1;
    }
}

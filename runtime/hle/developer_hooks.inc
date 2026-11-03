#include "../constants/hle.h"
// Included after the guest invocation/string helpers in hooks.c. Every Unity
// call below runs on the guest update thread, preserving its complete CPU state.
#include "../developer.h"

static bool dev_overlays_visible, dev_creating, dev_pumping;
static bool dev_welcomed;
static uint64_t dev_console_input;
static unsigned dev_scroll_bottom_frames;
static char dev_command[SF2C_DEVELOPER_HOOKS_N_256_51E8];

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
    uint32_t length = LD32(string + SF2C_DEVELOPER_HOOKS_N_0X10_BF9C);
    if (length > SF2C_DEVELOPER_HOOKS_N_256_51E8) return false;
    size_t at = 0;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t cp = LD16(string + SF2C_DEVELOPER_HOOKS_N_0X14_F942 + i * SF2C_DEVELOPER_HOOKS_N_2_D473);
        if (cp >= SF2C_DEVELOPER_HOOKS_N_0XD800_3321 && cp <= SF2C_DEVELOPER_HOOKS_N_0XDBFF_235E && i + 1 < length) {
            uint32_t low = LD16(string + SF2C_DEVELOPER_HOOKS_N_0X14_F942 + (i + 1) * SF2C_DEVELOPER_HOOKS_N_2_D473);
            if (low >= SF2C_DEVELOPER_HOOKS_N_0XDC00_D759 && low <= SF2C_DEVELOPER_HOOKS_N_0XDFFF_4FD5) { ++i; cp = SF2C_DEVELOPER_HOOKS_N_0X10000_4939 + ((cp - SF2C_DEVELOPER_HOOKS_N_0XD800_3321) << SF2C_DEVELOPER_HOOKS_N_10_4A44) + low - SF2C_DEVELOPER_HOOKS_N_0XDC00_D759; }
        }
        unsigned bytes = cp < SF2C_DEVELOPER_HOOKS_N_0X80_D41C ? 1 : cp < SF2C_DEVELOPER_HOOKS_N_0X800_D398 ? SF2C_DEVELOPER_HOOKS_N_2_D473 : cp < SF2C_DEVELOPER_HOOKS_N_0X10000_4939 ? SF2C_DEVELOPER_HOOKS_N_3_4E07 : SF2C_DEVELOPER_HOOKS_N_4_4B22;
        if (at + bytes >= cap) return false;
        if (bytes == 1) out[at++] = cp;
        else {
            out[at++] = (bytes == SF2C_DEVELOPER_HOOKS_N_2_D473 ? SF2C_DEVELOPER_HOOKS_N_0XC0_4E7B : bytes == SF2C_DEVELOPER_HOOKS_N_3_4E07 ? SF2C_DEVELOPER_HOOKS_N_0XE0_F5C1 : SF2C_DEVELOPER_HOOKS_N_0XF0_2A69) | (cp >> (SF2C_DEVELOPER_HOOKS_N_6_E7F6 * (bytes - 1)));
            for (int k = bytes - SF2C_DEVELOPER_HOOKS_N_2_D473; k >= 0; --k) out[at++] = SF2C_DEVELOPER_HOOKS_N_0X80_D41C | ((cp >> (SF2C_DEVELOPER_HOOKS_N_6_E7F6 * k)) & SF2C_DEVELOPER_HOOKS_N_0X3F_AEBF);
        }
    }
    out[at] = 0;
    return true;
}

static void dev_log(Cpu *c, const char *text) {
    uint64_t value = guest_string(c, text);
    if (value) guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB940ULL_876C, 0, value); // ConsoleUI.Log
    LOG(SF2C_DEVELOPER_HOOKS_S_DEVELOPER_S_N_A6A0, text);
}

static void dev_ensure_ui(Cpu *c) {
    if (!dev_enabled() || dev_creating) return;
    if (guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CED0F0ULL_F599, 0, 0)) return;
    dev_creating = true;
    guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000C7B050ULL_B12B, 0, 0); // Register the original console command database (idempotent)
    // ModuleHolder.Awake changes a shared construction context. A nested debug
    // canvas must not become the parent scene's current holder.
    uint64_t previous_holder = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000E2F610ULL_4FB3, 0, 0);
    uint64_t holder_slot = 0, holder_class = 0, holder_fields = 0;
    if (rt_probe_u64(SF2C_DEVELOPER_HOOKS_N_0X70039E9B98ULL_22D6, &holder_slot) && holder_slot &&
        rt_probe_u64(holder_slot, &holder_class) && holder_class)
        rt_probe_u64(holder_class + SF2C_DEVELOPER_HOOKS_N_0XB8_8F73, &holder_fields);
    uint64_t prefab = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X700123DE70ULL_36E0, 0,
        guest_string(c, SF2C_DEVELOPER_HOOKS_S_PREFABS_DEBUG_DEBUGCANVAS_C6B7));
    if (prefab) {
        uint64_t object = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70012331F0ULL_1E3D, 0, prefab);
        if (object) {
            guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70012339D0ULL_280C, 0, object); // original DontDestroyOnLoad
            LOG(SF2C_DEVELOPER_HOOKS_S_DEVELOPER_MOUNTED_ORIGINAL_D_97D7);
        }
    } else LOG(SF2C_DEVELOPER_HOOKS_S_DEVELOPER_DEBUGCANVAS_RESOUR_F285);
    if (holder_fields && LD64(holder_fields) != previous_holder) {
        ST64(holder_fields, previous_holder);
        LOG(SF2C_DEVELOPER_HOOKS_S_DEVELOPER_RESTORED_SCENE_MOD_2898);
    }
    dev_creating = false;
}

static void dev_refresh_overlays(Cpu *c) {
    uint64_t ui = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CED0F0ULL_F599, 0, 0);
    if (ui) guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CED1D0ULL_12CF, ui, dev_overlays_visible);
}

static void dev_set_input(Cpu *c, uint64_t console) {
    uint64_t input = console ? LD64(console + SF2C_DEVELOPER_HOOKS_N_0X30_8BAD) : 0;
    if (input) guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70005B5E80ULL_1F84, input, guest_string(c, dev_command));
}

static void dev_get_input(Cpu *c, uint64_t console) {
    uint64_t input = console ? LD64(console + SF2C_DEVELOPER_HOOKS_N_0X30_8BAD) : 0;
    if (input) dev_read_string(guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70005B5E70ULL_5763, input, 0), dev_command, sizeof dev_command);
}

static void dev_scroll(Cpu *c, uint64_t console, float delta, bool bottom) {
    uint64_t text = console ? LD64(console + SF2C_DEVELOPER_HOOKS_N_0X28_EAB9) : 0;
    if (!text) return;
    uint64_t type = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_DEVELOPER_HOOKS_S_UNITYENGINE_UI_SCROLLRECT_UN_FBA2));
    uint64_t scroll = type ? guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X700121EAD0ULL_885E, text, type) : 0;
    if (!scroll) return;
    float position = bottom ? 0 : guest_get_float(c, SF2C_DEVELOPER_HOOKS_N_0X70005D6420ULL_01FB, scroll);
    position = fminf(1, fmaxf(0, position + delta));
    Cpu saved = *c;
    c->x[0] = scroll; c->x[1] = 0;
    memcpy(&c->v[0], &position, sizeof position);
    guest_call(c, SF2C_DEVELOPER_HOOKS_N_0X70005D6560ULL_E893);
    *c = saved;
}

static const char *dev_trainer(Cpu *c, const char *action) {
    static const struct { const char *name; int id; } commands[] = {
        {SF2C_DEVELOPER_HOOKS_S_PAUSE_1A27, 1}, {SF2C_DEVELOPER_HOOKS_S_PERKS_3233, SF2C_DEVELOPER_HOOKS_N_2_D473}, {SF2C_DEVELOPER_HOOKS_S_STEP_0D9F, SF2C_DEVELOPER_HOOKS_N_3_4E07}, {SF2C_DEVELOPER_HOOKS_S_WIN_ROUND_D8FC, SF2C_DEVELOPER_HOOKS_N_4_4B22}, {SF2C_DEVELOPER_HOOKS_S_WIN_0FA0, SF2C_DEVELOPER_HOOKS_N_5_EF2D},
        {SF2C_DEVELOPER_HOOKS_S_LOSE_ROUND_3DFF, SF2C_DEVELOPER_HOOKS_N_6_E7F6}, {SF2C_DEVELOPER_HOOKS_S_LOSE_293D, SF2C_DEVELOPER_HOOKS_N_7_7902}, {SF2C_DEVELOPER_HOOKS_S_RESET_ROUND_41BA, SF2C_DEVELOPER_HOOKS_N_9_1958}, {SF2C_DEVELOPER_HOOKS_S_RESET_1C7A, SF2C_DEVELOPER_HOOKS_N_10_4A44},
        {SF2C_DEVELOPER_HOOKS_S_SLOW_FB18, SF2C_DEVELOPER_HOOKS_N_13_3FDB}, {SF2C_DEVELOPER_HOOKS_S_MAGIC_C5A7, SF2C_DEVELOPER_HOOKS_N_18_4EC9}, {SF2C_DEVELOPER_HOOKS_S_COMBO_3DAA, SF2C_DEVELOPER_HOOKS_N_19_9400}, {SF2C_DEVELOPER_HOOKS_S_STYLE_BB07, SF2C_DEVELOPER_HOOKS_N_20_F5CA},
        {SF2C_DEVELOPER_HOOKS_S_CRITICAL_D354, SF2C_DEVELOPER_HOOKS_N_21_6F4B}, {SF2C_DEVELOPER_HOOKS_S_GOD_196C, SF2C_DEVELOPER_HOOKS_N_22_785F}, {SF2C_DEVELOPER_HOOKS_S_BOT_GOD_F1A5, SF2C_DEVELOPER_HOOKS_N_23_535F}, {SF2C_DEVELOPER_HOOKS_S_HUD_C065, SF2C_DEVELOPER_HOOKS_N_24_C235}
    };
    if (!g_settings.developer_trainer) return SF2C_DEVELOPER_HOOKS_S_TRAINER_DISABLED_IN_DEVELOPE_E392;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        if (strcmp(action, commands[i].name)) continue;
        if (!guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000C91C70ULL_A0A1, 0, 0)) return SF2C_DEVELOPER_HOOKS_S_START_A_FIGHT_BEFORE_USING_T_CDEB;
        guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X700102ECD0ULL_209A, 0, 0); // FightCheatHandler.Init
        return guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X700102F540ULL_BD1C, 0, commands[i].id) ?
            SF2C_DEVELOPER_HOOKS_S_TRAINER_ACTION_APPLIED_2519 : SF2C_DEVELOPER_HOOKS_S_THIS_TRAINER_ACTION_IS_UNAVA_1604;
    }
    return SF2C_DEVELOPER_HOOKS_S_TRAINER_GOD_BOT_GOD_CRITICAL_BDFC;
}

static const char *dev_shop(Cpu *c, const char *action) {
    if (!g_settings.developer_shop_cheats) return SF2C_DEVELOPER_HOOKS_S_SHOP_CHEATS_DISABLED_IN_DEVE_A188;
    uint64_t type = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_DEVELOPER_HOOKS_S_NEKKI_SF2_GUI_SHOP_CHEATSPAN_20BD));
    uint64_t panel = type ? guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7001233B80ULL_1953, 0, type) : 0;
    if (!panel) return SF2C_DEVELOPER_HOOKS_S_OPEN_THE_STORY_SHOP_BEFORE_U_BD33;
    uint64_t method = !strcmp(action, SF2C_DEVELOPER_HOOKS_S_SHOW_8F06) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA6E80ULL_0027 :
        !strcmp(action, SF2C_DEVELOPER_HOOKS_S_HIDE_E7E1) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA6F50ULL_6F89 :
        !strcmp(action, SF2C_DEVELOPER_HOOKS_S_MONEY_7028) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA6FF0ULL_A693 :
        !strcmp(action, SF2C_DEVELOPER_HOOKS_S_GEMS_D370) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA7100ULL_100A :
        !strcmp(action, SF2C_DEVELOPER_HOOKS_S_LEVEL_0643) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA7210ULL_76AB :
        !strcmp(action, SF2C_DEVELOPER_HOOKS_S_ITEMS_3C22) ? SF2C_DEVELOPER_HOOKS_N_0X7000DA7350ULL_73D3 : 0;
    if (!method) return SF2C_DEVELOPER_HOOKS_S_SHOP_SHOW_HIDE_MONEY_GEMS_LE_F4E8;
    guest_invoke2(c, method, panel, 0);
    return SF2C_DEVELOPER_HOOKS_S_SHOP_ACTION_APPLIED_643E;
}

// Return NULL for the game's original command database to handle the command.
static const char *dev_execute(Cpu *c, const char *command) {
    if (!strcmp(command, SF2C_DEVELOPER_HOOKS_S_DEVHELP_75E6))
        return SF2C_DEVELOPER_HOOKS_S_F1_CONSOLE_F2_OVERLAYS_COMMA_4FF6;
    if (!strcmp(command, SF2C_DEVELOPER_HOOKS_S_TRAINER_EBE0) || !strncmp(command, SF2C_DEVELOPER_HOOKS_S_TRAINER_EB19, SF2C_DEVELOPER_HOOKS_N_8_2C62)) return dev_trainer(c, command[SF2C_DEVELOPER_HOOKS_N_7_7902] ? command + SF2C_DEVELOPER_HOOKS_N_8_2C62 : SF2C_DEVELOPER_HOOKS_S_EMPTY_12AE);
    if (!strcmp(command, SF2C_DEVELOPER_HOOKS_S_SHOP_6908) || !strncmp(command, SF2C_DEVELOPER_HOOKS_S_SHOP_5842, SF2C_DEVELOPER_HOOKS_N_5_EF2D)) return dev_shop(c, command[SF2C_DEVELOPER_HOOKS_N_4_4B22] ? command + SF2C_DEVELOPER_HOOKS_N_5_EF2D : SF2C_DEVELOPER_HOOKS_S_EMPTY_12AE);
    if (!strcmp(command, SF2C_DEVELOPER_HOOKS_S_OVERLAY_65DD) || !strncmp(command, SF2C_DEVELOPER_HOOKS_S_OVERLAY_114F, SF2C_DEVELOPER_HOOKS_N_8_2C62)) {
        if (!g_settings.developer_overlays) return SF2C_DEVELOPER_HOOKS_S_OVERLAYS_DISABLED_IN_DEVELOP_1035;
        const char *arg = command[SF2C_DEVELOPER_HOOKS_N_7_7902] ? command + SF2C_DEVELOPER_HOOKS_N_8_2C62 : SF2C_DEVELOPER_HOOKS_S_TOGGLE_1257;
        if (!strcmp(arg, SF2C_DEVELOPER_HOOKS_S_ON_990C)) dev_overlays_visible = true;
        else if (!strcmp(arg, SF2C_DEVELOPER_HOOKS_S_OFF_9F3B)) dev_overlays_visible = false;
        else if (!strcmp(arg, SF2C_DEVELOPER_HOOKS_S_TOGGLE_1257)) dev_overlays_visible = !dev_overlays_visible;
        else return SF2C_DEVELOPER_HOOKS_S_OVERLAY_ON_OFF_TOGGLE_SELECT_BAC1;
        dev_refresh_overlays(c);
        return dev_overlays_visible ? SF2C_DEVELOPER_HOOKS_S_DEBUG_OVERLAYS_ON_DABA : SF2C_DEVELOPER_HOOKS_S_DEBUG_OVERLAYS_OFF_A49F;
    }
    if (!strcmp(command, SF2C_DEVELOPER_HOOKS_S_CONFIG_4888) || !strncmp(command, SF2C_DEVELOPER_HOOKS_S_CONFIG_9581, SF2C_DEVELOPER_HOOKS_N_7_7902)) {
        char name[SF2C_DEVELOPER_HOOKS_N_24_C235], value[SF2C_DEVELOPER_HOOKS_N_12_6B51], extra;
        if (sscanf(command + SF2C_DEVELOPER_HOOKS_N_6_E7F6, SF2C_DEVELOPER_HOOKS_S_23S_11S_C_55E2, name, value, &extra) != SF2C_DEVELOPER_HOOKS_N_2_D473 ||
            (strcmp(value, SF2C_DEVELOPER_HOOKS_S_ON_990C) && strcmp(value, SF2C_DEVELOPER_HOOKS_S_OFF_9F3B)))
            return SF2C_DEVELOPER_HOOKS_S_CONFIG_AI_ON_OFF_INTRO_ON_OF_28AA;
        bool on = !strcmp(value, SF2C_DEVELOPER_HOOKS_S_ON_990C);
        uint64_t setter = 0;
        if (!strcmp(name, SF2C_DEVELOPER_HOOKS_S_AI_1F0B)) { g_settings.ai_enabled = on; setter = SF2C_DEVELOPER_HOOKS_N_0X7000E19D30ULL_36DB; }
        else if (!strcmp(name, SF2C_DEVELOPER_HOOKS_S_INTRO_0DA0)) { g_settings.show_intro = on; setter = SF2C_DEVELOPER_HOOKS_N_0X7000E19C10ULL_45F0; }
        else if (!strcmp(name, SF2C_DEVELOPER_HOOKS_S_TIMINGS_5C84)) { g_settings.show_time_results = on; setter = SF2C_DEVELOPER_HOOKS_N_0X7000E1A450ULL_EB9A; }
        else return SF2C_DEVELOPER_HOOKS_S_UNKNOWN_SETTING_USE_CONFIG_A_E43F;
        guest_invoke2(c, setter, 0, on);
        return SF2C_DEVELOPER_HOOKS_S_SESSION_SETTING_UPDATED_3B29;
    }
    return NULL;
}

static void dev_pump(Cpu *c) {
    if (!dev_enabled() || dev_pumping) return;
    dev_pumping = true;
    DeveloperEvent event;
    while (developer_poll_event(&event)) {
        uint64_t console = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB1A0ULL_BF55, 0, 0);
        if (event.action == DEV_OVERLAYS_TOGGLE) {
            dev_overlays_visible = !dev_overlays_visible;
            dev_refresh_overlays(c);
            continue;
        }
        if (!console || !g_settings.developer_console) { developer_set_console_open(false); continue; }
        dev_console_input = LD64(console + SF2C_DEVELOPER_HOOKS_N_0X30_8BAD);
        bool open = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB210ULL_C733, console, 0);
        if (event.action == DEV_CONSOLE_TOGGLE || event.action == DEV_CONSOLE_CLOSE) {
            if (event.action == DEV_CONSOLE_TOGGLE || open) guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB520ULL_21CA, console, 0);
            open = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB210ULL_C733, console, 0);
            developer_set_console_open(open);
            if (open && !dev_welcomed) {
                dev_welcomed = true;
                dev_log(c, SF2C_DEVELOPER_HOOKS_S_SF2_DEVELOPER_CONSOLE_F1_CLO_3530);
            }
            continue;
        }
        if (!open) continue;
        if (event.action == DEV_TEXT) {
            if (!strcmp(event.text, SF2C_DEVELOPER_HOOKS_S_EMPTY_0ED6) || !strcmp(event.text, SF2C_DEVELOPER_HOOKS_S_EMPTY_16EB)) continue;
            size_t have = strlen(dev_command), add = strlen(event.text);
            if (have + add < sizeof dev_command) memcpy(dev_command + have, event.text, add + 1);
            dev_set_input(c, console);
        } else if (event.action == DEV_BACKSPACE) {
            size_t n = strlen(dev_command);
            if (n) { --n; while (n && ((unsigned char)dev_command[n] & SF2C_DEVELOPER_HOOKS_N_0XC0_4E7B) == SF2C_DEVELOPER_HOOKS_N_0X80_D41C) --n; dev_command[n] = 0; }
            dev_set_input(c, console);
        } else if (event.action == DEV_SUBMIT) {
            if (*dev_command) guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEBCC0ULL_0362, console, 0);
            dev_scroll_bottom_frames = SF2C_DEVELOPER_HOOKS_N_3_4E07; // let Unity finish updating the scroll content's layout
            dev_command[0] = 0;
            dev_set_input(c, console);
        } else if (event.action == DEV_HISTORY_PREV || event.action == DEV_HISTORY_NEXT) {
            guest_invoke2(c, event.action == DEV_HISTORY_PREV ? SF2C_DEVELOPER_HOOKS_N_0X7000CEB710ULL_183E : SF2C_DEVELOPER_HOOKS_N_0X7000CEB830ULL_1F6C, console, 0);
            dev_get_input(c, console);
        } else if (event.action == DEV_SCROLL_UP || event.action == DEV_SCROLL_DOWN) {
            dev_scroll_bottom_frames = 0;
            dev_scroll(c, console, event.action == DEV_SCROLL_UP ? SF2C_DEVELOPER_HOOKS_N_0_2F_ED8E : -SF2C_DEVELOPER_HOOKS_N_0_2F_ED8E, false);
        }
    }
    dev_pumping = false;
}

static int dev_return_bool(Cpu *c, bool value) { c->x[0] = value; return 1; }

static int developer_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case SF2C_DEVELOPER_HOOKS_N_0X7000F41CD0ULL_EA08: // KeyboardController.HandleKeyboard: stable guest update thread
        dev_pump(c);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CEB3C0ULL_1729: { // ConsoleUI.Update, including before KeyboardController starts
        uint64_t console = c->x[0];
        if (g_settings.developer_console) dev_console_input = LD64(console + SF2C_DEVELOPER_HOOKS_N_0X30_8BAD);
        dev_pump(c);
        if (g_settings.developer_console) {
            developer_set_console_open(guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB210ULL_C733, console, 0));
            if (dev_scroll_bottom_frames) { --dev_scroll_bottom_frames; dev_scroll(c, console, 0, true); }
        }
        return 0;
    }
    case SF2C_DEVELOPER_HOOKS_N_0X7000CEB330ULL_0E1D: // ConsoleUI.Free
        developer_set_console_open(false);
        dev_console_input = 0;
        dev_command[0] = 0;
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X70005C07C0ULL_94FB: // Console text comes from SDL, without opening Switch's modal software keyboard
        if (g_settings.developer_console && !dev_console_input) {
            uint64_t console = guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000CEB1A0ULL_BF55, 0, 0);
            if (console) dev_console_input = LD64(console + SF2C_DEVELOPER_HOOKS_N_0X30_8BAD);
        }
        return g_settings.developer_console && dev_console_input && c->x[0] == dev_console_input ? 1 : 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CEB230ULL_1354: return dev_return_bool(c, g_settings.developer_console);
    case SF2C_DEVELOPER_HOOKS_N_0X7000CE9D90ULL_82F9: // DebugUIModule.IsAvailable; keep original scene restrictions
        if (!g_settings.developer_overlays || !dev_overlays_visible) return dev_return_bool(c, false);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CEC500ULL_CE8F: if (!g_settings.developer_fps) return dev_return_bool(c, false); return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CEC850ULL_C334: if (!g_settings.developer_memory) return dev_return_bool(c, false); return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CE9C10ULL_89F9: if (!g_settings.developer_build_info) return dev_return_bool(c, false); return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000CECAA0ULL_F163: if (!g_settings.developer_model_info) return dev_return_bool(c, false); return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000DA20A0ULL_B20B: // Keep the desktop console's keyboard input and history controls
        if (g_settings.developer_console && guest_object_named(c, c->x[0], SF2C_DEVELOPER_HOOKS_S_CONSOLEUI_7D4B)) ST8(c->x[0] + SF2C_DEVELOPER_HOOKS_N_0X1A_D752, 1);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X70000AD0E0ULL_D12D: // Enable only the local shop panel's debug checks, never global/online debug mode
        if (g_settings.developer_shop_cheats && (c->x[SF2C_DEVELOPER_HOOKS_N_30_624B] == SF2C_DEVELOPER_HOOKS_N_0X7000EC7330ULL_7860 ||
            (c->x[SF2C_DEVELOPER_HOOKS_N_30_624B] >= SF2C_DEVELOPER_HOOKS_N_0X7000DA6E80ULL_0027 && c->x[SF2C_DEVELOPER_HOOKS_N_30_624B] < SF2C_DEVELOPER_HOOKS_N_0X7000DA7340ULL_187D)))
            return dev_return_bool(c, true);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X700102EC00ULL_21E5: return dev_return_bool(c, g_settings.developer_trainer);
    case SF2C_DEVELOPER_HOOKS_N_0X7000E1A5B0ULL_0575:
        // The original game shares its intro switch with manual gallery
        // playback. Disabling the boot intro must not disable selected movies.
        return dev_return_bool(c,c->x[SF2C_DEVELOPER_HOOKS_N_30_624B]==SF2C_DEVELOPER_HOOKS_N_0X7000E6E49CULL_122B || g_settings.show_intro);
    case SF2C_DEVELOPER_HOOKS_N_0X7000E1A760ULL_ECDD: return dev_return_bool(c, g_settings.ai_enabled);
    case SF2C_DEVELOPER_HOOKS_N_0X7000E19ADCULL_5BB4: // AssemblyController.Parse completed; apply INI after the original config
        guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000E19C10ULL_45F0, 0, g_settings.show_intro);
        guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000E19D30ULL_36DB, 0, g_settings.ai_enabled);
        guest_invoke2(c, SF2C_DEVELOPER_HOOKS_N_0X7000E1A450ULL_EB9A, 0, g_settings.show_time_results);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000FC5060ULL_B103: // Gallery only; retain normal watched-video save behavior elsewhere
        if (g_settings.unlock_all_videos && c->x[SF2C_DEVELOPER_HOOKS_N_30_624B] >= SF2C_DEVELOPER_HOOKS_N_0X7000E71070ULL_A591 && c->x[SF2C_DEVELOPER_HOOKS_N_30_624B] < SF2C_DEVELOPER_HOOKS_N_0X7000E71390ULL_06A2)
            return dev_return_bool(c, true);
        return 0;
    case SF2C_DEVELOPER_HOOKS_N_0X7000C89EA0ULL_A88D: { // ConsoleDatabase.ExecuteCommand
        if (!g_settings.developer_console) return 0;
        char text[SF2C_DEVELOPER_HOOKS_N_256_51E8];
        if (!dev_read_string(c->x[1], text, sizeof text)) return 0;
        const char *result = dev_execute(c, text);
        if (!result) return 0;
        c->x[0] = guest_string(c, result);
        return 1;
    }
    default: return -1;
    }
}

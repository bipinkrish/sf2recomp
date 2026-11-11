#include "../constants/hle.h"
// Recomp Settings owns a draft on Unity's update thread. The dialog and
// keyboard/controller navigation use the same native UI as Sparring's menus.
#include "../settings_config.h"
#include "../ui/settings_input.h"

enum {
    SETTINGS_ROOT, SETTINGS_GROUP, SETTINGS_FIELD, SETTINGS_ENTRY,
    SETTINGS_DISCARD, SETTINGS_SAVED
};
enum {
    SETTINGS_SELECT_GROUP = SF2C_SETTINGS_MENU_HOOKS_N_200_27BA, SETTINGS_SELECT_FIELD = SF2C_SETTINGS_MENU_HOOKS_N_220_3679,
    SETTINGS_NEXT = SF2C_SETTINGS_MENU_HOOKS_N_240_6AF1, SETTINGS_PREVIOUS, SETTINGS_DISCARD_EDITS,
    SETTINGS_KEEP_EDITING, SETTINGS_EDIT, SETTINGS_UNBIND, SETTINGS_ACCEPT, SETTINGS_EXIT
};
enum { SETTINGS_GROUPS_PER_PAGE = SF2C_SETTINGS_MENU_HOOKS_N_5_EF2D, SETTINGS_FIELDS_PER_PAGE = SF2C_SETTINGS_MENU_HOOKS_N_4_4B22 };

static const char *const settings_group_labels[SETTINGS_GROUP_COUNT] = {
    [SETTINGS_GROUP_GENERAL] = SF2C_SETTINGS_MENU_HOOKS_S_GENERAL_468D,
    [SETTINGS_GROUP_NETWORK] = SF2C_SETTINGS_MENU_HOOKS_S_NETWORK_23E1,
    [SETTINGS_GROUP_GAMEPLAY] = SF2C_SETTINGS_MENU_HOOKS_S_GAMEPLAY_73FF,
    [SETTINGS_GROUP_DEVELOPER] = SF2C_SETTINGS_MENU_HOOKS_S_DEVELOPER_581A,
    [SETTINGS_GROUP_AUDIO] = SF2C_SETTINGS_MENU_HOOKS_S_AUDIO_7454,
    [SETTINGS_GROUP_VIDEO] = SF2C_SETTINGS_MENU_HOOKS_S_VIDEO_72BC,
    [SETTINGS_GROUP_INPUT] = SF2C_SETTINGS_MENU_HOOKS_S_INPUT_16F5,
    [SETTINGS_GROUP_STORAGE] = SF2C_SETTINGS_MENU_HOOKS_S_STORAGE_D159,
    [SETTINGS_GROUP_KEYBOARD] = SF2C_SETTINGS_MENU_HOOKS_S_KEYBOARD_BINDINGS_63A1,
    [SETTINGS_GROUP_GAMEPAD] = SF2C_SETTINGS_MENU_HOOKS_S_CONTROLLER_BINDINGS_3485,
};

static struct {
    SettingsConfig draft;
    unsigned screen, root_page, field_page, field;
    unsigned visible_fields[SETTINGS_FIELDS_PER_PAGE];
    SettingsGroup group;
    bool pending_open, dirty, fullscreen_failed;
    SettingsSaveResult result;
    uint64_t main_list, main_item;
    char input[SETTINGS_SAVE_DIRECTORY_CAP];
    const char *error;
} settings_menu;

static void reset_settings_menu(void) {
    memset(&settings_menu, 0, sizeof settings_menu);
    settings_input_begin(SETTINGS_INPUT_NONE, NULL);
}

static void settings_add_main_item(Cpu *c, uint64_t list, uint64_t content, uint64_t exit_item, int exit_index) {
    if (list == settings_menu.main_list) return;
    uint64_t type = lan_type(c, SF2C_LAN_UI_HOOKS_S_NEKKI_SF2_GUI_GENERALMENU_ME_6EB3);
    uint64_t source = type ? guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X700121E790ULL_8667, exit_item, type) : 0;
    if (!source) return;
    uint64_t item = guest_invoke4(c, SF2C_LAN_UI_HOOKS_N_0X70012333A0ULL_826D, 0, source, content, 0);
    if (!item) return;
    settings_menu.main_list = list;
    settings_menu.main_item = item;
    uint64_t transform = guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X700121E6B0ULL_59E6, item, 0);
    uint64_t object = guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E, item, 0);
    guest_invoke2(c, SF2C_SETTINGS_MENU_HOOKS_N_0X70015D9F50ULL_5450, transform, exit_index); // Transform.SetSiblingIndex
    guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A, object, 1);
    uint64_t label = LD64(item + SF2C_LAN_UI_HOOKS_N_0X28_EAB9);
    guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X7000E73B90ULL_3B3B, label, 1); // literal text
    guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X7000E71230ULL_67AB, label, guest_string(c, SETTINGS_MAIN_MENU_LABEL));
}

static int settings_menu_hook(Cpu *c, uint64_t site) {
    // The stock scroll dialog requires scrolling to the end before confirming.
    if (site == SF2C_SETTINGS_MENU_HOOKS_N_0X7000CB31D0ULL_8E7B && lan_primary_button && c->x[0] == lan_dialog) return 1;
    if (site == SF2C_SETTINGS_MENU_HOOKS_N_0X70011CC660ULL_0510 && lan_primary_button && c->x[0] == lan_primary_button) {
        lan_action = LAN_CHOICE_PRIMARY;
        return 1;
    }
    bool control = site == SF2C_SETTINGS_MENU_HOOKS_N_0X7000E6FDD0ULL_702F; // TryCallEvent
    if ((!control && site != SF2C_LAN_UI_HOOKS_N_0X7000E6FCA0ULL_29CC) ||
        !settings_menu.main_item || c->x[0] != settings_menu.main_item) return -1;
    bool confirm = control ? c->x[1] == 0 : // KeyControlEvent.A
        c->x[1] && LD32(c->x[1] + SF2C_LAN_UI_HOOKS_N_0X12C_7DB2) == 0;
    if (confirm && !lan_menu_capture() && !developer_console_open()) settings_menu.pending_open = true;
    return 1;
}

static void settings_menu_close(Cpu *c) {
    settings_input_begin(SETTINGS_INPUT_NONE, NULL);
    lan_close_dialog(c, false);
    lan_phase = LAN_UI_NONE;
}

static void settings_menu_render(Cpu *c, unsigned focus) {
    const char *title = SF2C_SETTINGS_MENU_HOOKS_S_RECOMP_SETTINGS_3138;
    const char *rows[SETTINGS_GROUPS_PER_PAGE + SF2C_SETTINGS_MENU_HOOKS_N_2_D473];
    unsigned actions[SETTINGS_GROUPS_PER_PAGE + SF2C_SETTINGS_MENU_HOOKS_N_2_D473];
    char values[SETTINGS_FIELDS_PER_PAGE][SF2C_SETTINGS_MENU_HOOKS_N_128_2747], value[SETTINGS_SAVE_DIRECTORY_CAP];
    unsigned count = 0;
    bool show_save = settings_menu.screen != SETTINGS_ENTRY && settings_menu.screen != SETTINGS_SAVED;
#define ROW(text, action) do { rows[count] = (text); actions[count] = (action); ++count; } while (0)
    switch (settings_menu.screen) {
    case SETTINGS_ROOT: {
        unsigned first = settings_menu.root_page * SETTINGS_GROUPS_PER_PAGE;
        for (unsigned i = first; i < first + SETTINGS_GROUPS_PER_PAGE && i < SETTINGS_GROUP_COUNT; ++i)
            ROW(settings_group_labels[i], SETTINGS_SELECT_GROUP + i);
        ROW(settings_menu.root_page ? SF2C_SETTINGS_MENU_HOOKS_S_PREVIOUS_CATEGORIES_5BC7 : SF2C_SETTINGS_MENU_HOOKS_S_MORE_CATEGORIES_4C5A, SETTINGS_NEXT);
        if (settings_menu.error) title = settings_menu.error;
        break;
    }
    case SETTINGS_GROUP: {
        title = settings_group_labels[settings_menu.group];
        unsigned matching = 0, first = settings_menu.field_page * SETTINGS_FIELDS_PER_PAGE;
        for (unsigned i = 0; i < settings_config_count(); ++i) {
            if (settings_config_group(i) != settings_menu.group) continue;
            if (matching++ < first || count == SETTINGS_FIELDS_PER_PAGE) continue;
            settings_config_value(&settings_menu.draft, i, value, sizeof value);
            const char *label = settings_config_label(i);
            if (settings_config_override(i))
                snprintf(values[count], sizeof values[count], SF2C_SETTINGS_MENU_HOOKS_S_S_24S_OVERRIDE_7030, label, value);
            else if (settings_config_requires_restart(i))
                snprintf(values[count], sizeof values[count], SF2C_SETTINGS_MENU_HOOKS_S_S_24S_RESTART_D09E, label, value);
            else
                snprintf(values[count], sizeof values[count], SF2C_SETTINGS_MENU_HOOKS_S_S_50S_85FD, label, value);
            settings_menu.visible_fields[count] = i;
            ROW(values[count], SETTINGS_SELECT_FIELD + count);
        }
        if (matching > first + SETTINGS_FIELDS_PER_PAGE) ROW(SF2C_SETTINGS_MENU_HOOKS_S_NEXT_PAGE_360B, SETTINGS_NEXT);
        if (first) ROW(SF2C_SETTINGS_MENU_HOOKS_S_PREVIOUS_PAGE_AC1D, SETTINGS_PREVIOUS);
        break;
    }
    case SETTINGS_FIELD: {
        unsigned field = settings_menu.field;
        title = settings_menu.error ? settings_menu.error : settings_config_label(field);
        settings_config_value(&settings_menu.draft, field, value, sizeof value);
        snprintf(values[0], sizeof values[0], SETTINGS_MENU_VALUE_FORMAT, value);
        ROW(values[0], 0);
        const char *override = settings_config_override(field);
        if (override) {
            snprintf(values[1], sizeof values[1], SF2C_SETTINGS_MENU_HOOKS_S_ENVIRONMENT_OVERRIDE_S_5BD8, override);
            ROW(values[1], 0);
        } else {
            ROW(settings_config_requires_restart(field) ? SF2C_SETTINGS_MENU_HOOKS_S_RESTART_REQUIRED_9AFD : SF2C_SETTINGS_MENU_HOOKS_S_APPLIES_ON_SAVE_DF60, 0);
        }
        SettingsEditor editor = settings_config_editor(field);
        if (editor == SETTINGS_EDIT_CHOICE || editor == SETTINGS_EDIT_VOLUME || editor == SETTINGS_EDIT_FPS) {
            ROW(editor == SETTINGS_EDIT_CHOICE ? SF2C_SETTINGS_MENU_HOOKS_S_NEXT_VALUE_C67A : SF2C_SETTINGS_MENU_HOOKS_S_INCREASE_9F03, SETTINGS_NEXT);
            ROW(editor == SETTINGS_EDIT_CHOICE ? SF2C_SETTINGS_MENU_HOOKS_S_PREVIOUS_VALUE_099D : SF2C_SETTINGS_MENU_HOOKS_S_DECREASE_8532, SETTINGS_PREVIOUS);
        }
        if (editor == SETTINGS_EDIT_KEY || editor == SETTINGS_EDIT_PAD) {
            ROW(editor == SETTINGS_EDIT_KEY ? SF2C_SETTINGS_MENU_HOOKS_S_PRESS_A_KEY_D5FA : SF2C_SETTINGS_MENU_HOOKS_S_PRESS_A_CONTROLLER_BUTTON_D6BE, SETTINGS_EDIT);
            ROW(SF2C_SETTINGS_MENU_HOOKS_S_UNBIND_F766, SETTINGS_UNBIND);
        } else if (editor != SETTINGS_EDIT_CHOICE) {
            ROW(SF2C_SETTINGS_MENU_HOOKS_S_ENTER_VALUE_E3A6, SETTINGS_EDIT);
        }
        break;
    }
    case SETTINGS_ENTRY: {
        SettingsEditor editor = settings_config_editor(settings_menu.field);
        if (editor == SETTINGS_EDIT_KEY || editor == SETTINGS_EDIT_PAD) {
            title = editor == SETTINGS_EDIT_KEY ? SF2C_SETTINGS_MENU_HOOKS_S_PRESS_A_KEY_D5FA : SF2C_SETTINGS_MENU_HOOKS_S_PRESS_A_CONTROLLER_BUTTON_D6BE;
            ROW(SETTINGS_CAPTURE_CANCEL_LABEL, 0);
        } else {
            title = settings_config_label(settings_menu.field);
            const char *input = settings_menu.input;
            snprintf(values[0], sizeof values[0], SF2C_SETTINGS_MENU_HOOKS_S_96S_D145, *input ? input : SF2C_SETTINGS_MENU_HOOKS_S_TYPE_A_VALUE_6D97);
            ROW(values[0], 0);
            ROW(SF2C_SETTINGS_MENU_HOOKS_S_APPLY_VALUE_A90E, SETTINGS_ACCEPT);
        }
        break;
    }
    case SETTINGS_DISCARD:
        title = SF2C_SETTINGS_MENU_HOOKS_S_SAVE_CHANGES_2CB0;
        ROW(SF2C_SETTINGS_MENU_HOOKS_S_DISCARD_CHANGES_07F6, SETTINGS_DISCARD_EDITS);
        ROW(SF2C_SETTINGS_MENU_HOOKS_S_KEEP_EDITING_DF57, SETTINGS_KEEP_EDITING);
        break;
    case SETTINGS_SAVED:
        title = SF2C_SETTINGS_MENU_HOOKS_S_SETTINGS_SAVED_1FBE;
        if (settings_menu.result.request_fullscreen) {
            ROW(SF2C_SETTINGS_MENU_HOOKS_S_APPLYING_FULLSCREEN_SETTING_E10E, 0);
            break;
        }
        if (settings_menu.result.applied) ROW(SF2C_SETTINGS_MENU_HOOKS_S_LIVE_CHANGES_APPLIED_1096, 0);
        if (settings_menu.result.restart_pending) ROW(SF2C_SETTINGS_MENU_HOOKS_S_RESTART_REQUIRED_FOR_MARKED__E6BE, 0);
        if (settings_menu.result.overridden) ROW(SF2C_SETTINGS_MENU_HOOKS_S_ENVIRONMENT_OVERRIDES_STILL__83DB, 0);
        if (settings_menu.fullscreen_failed) ROW(SF2C_SETTINGS_MENU_HOOKS_S_COULD_NOT_SWITCH_FULLSCREEN_7D53, 0);
        ROW(SF2C_SETTINGS_MENU_HOOKS_S_DONE_1655, SETTINGS_EXIT);
        break;
    }
#undef ROW
    lan_menu_requests();
    if (!lan_show(c, LAN_UI_SETTINGS, title, rows, actions, count,
                  show_save ? SF2C_SETTINGS_MENU_HOOKS_S_SAVE_TO_INI_597C : NULL)) {
        settings_menu_close(c);
        return;
    }
    for (unsigned i = 0; focus && i < count; ++i) {
        if (actions[i] != focus) continue;
        lan_choice = i;
        lan_focus_choice(c);
        break;
    }
}

static void settings_menu_finish_input(Cpu *c, const char *value) {
    settings_input_begin(SETTINGS_INPUT_NONE, NULL);
    settings_menu.error = NULL;
    if (value) {
        if (settings_config_set(&settings_menu.draft, settings_menu.field, value)) settings_menu.dirty = true;
        else settings_menu.error = SF2C_SETTINGS_MENU_HOOKS_S_INVALID_VALUE_13D1;
    }
    settings_menu.screen = SETTINGS_FIELD;
    settings_menu_render(c, 0);
}

static void settings_menu_action(Cpu *c, unsigned action) {
    if (settings_menu.result.request_fullscreen) return;
    if (action == LAN_CHOICE_BACK && settings_menu.screen == SETTINGS_ENTRY) {
        settings_menu_finish_input(c, NULL);
        return;
    }
    unsigned focus = 0;
    settings_menu.error = NULL;
    if (action == LAN_CHOICE_BACK) {
        switch (settings_menu.screen) {
        case SETTINGS_ROOT:
            if (!settings_menu.dirty) { settings_menu_close(c); return; }
            settings_menu.screen = SETTINGS_DISCARD;
            break;
        case SETTINGS_GROUP: case SETTINGS_DISCARD: settings_menu.screen = SETTINGS_ROOT; break;
        case SETTINGS_FIELD: settings_menu.screen = SETTINGS_GROUP; break;
        default: settings_menu_close(c); return;
        }
    } else if (action == SETTINGS_EXIT || action == SETTINGS_DISCARD_EDITS) {
        settings_menu_close(c);
        return;
    } else if (action == SETTINGS_KEEP_EDITING) {
        settings_menu.screen = SETTINGS_ROOT;
    } else if (action == LAN_CHOICE_PRIMARY) {
        if (settings_config_save(&settings_menu.draft, &settings_menu.result)) {
            settings_menu.dirty = settings_menu.fullscreen_failed = false;
            settings_menu.screen = SETTINGS_SAVED;
            if (settings_menu.result.refresh_fight_settings) dev_apply_fight_settings(c);
            if (settings_menu.result.request_fullscreen) menu_request_fullscreen(settings_menu.draft.settings.fullscreen);
        } else {
            settings_menu.screen = SETTINGS_ROOT;
            settings_menu.error = SF2C_SETTINGS_MENU_HOOKS_S_COULD_NOT_SAVE_INI_55AA;
        }
    } else if (action >= SETTINGS_SELECT_GROUP && action < SETTINGS_SELECT_GROUP + SETTINGS_GROUP_COUNT) {
        settings_menu.group = action - SETTINGS_SELECT_GROUP;
        settings_menu.field_page = 0;
        settings_menu.screen = SETTINGS_GROUP;
    } else if (action >= SETTINGS_SELECT_FIELD && action < SETTINGS_SELECT_FIELD + SETTINGS_FIELDS_PER_PAGE) {
        settings_menu.field = settings_menu.visible_fields[action - SETTINGS_SELECT_FIELD];
        settings_menu.screen = SETTINGS_FIELD;
    } else if (action == SETTINGS_NEXT || action == SETTINGS_PREVIOUS) {
        int direction = action == SETTINGS_NEXT ? 1 : -1;
        if (settings_menu.screen == SETTINGS_ROOT) settings_menu.root_page = !settings_menu.root_page;
        else if (settings_menu.screen == SETTINGS_GROUP) settings_menu.field_page += direction;
        else {
            settings_config_step(&settings_menu.draft, settings_menu.field, direction);
            focus = action;
            settings_menu.dirty = true;
        }
    } else if (action == SETTINGS_EDIT) {
        SettingsEditor editor = settings_config_editor(settings_menu.field);
        SettingsInputMode mode = SETTINGS_INPUT_TEXT;
        if (editor == SETTINGS_EDIT_KEY) mode = SETTINGS_INPUT_KEY;
        if (editor == SETTINGS_EDIT_PAD) mode = SETTINGS_INPUT_PAD;
        settings_config_value(&settings_menu.draft, settings_menu.field, settings_menu.input, sizeof settings_menu.input);
        settings_input_begin(mode, settings_menu.input);
        settings_menu.screen = SETTINGS_ENTRY;
    } else if (action == SETTINGS_ACCEPT || action == SETTINGS_UNBIND) {
        settings_menu_finish_input(c, action == SETTINGS_ACCEPT ? settings_menu.input : SF2C_SETTINGS_MENU_HOOKS_S_NONE_30A0);
        return;
    } else return;
    settings_menu_render(c, focus);
}

static void settings_menu_pump(Cpu *c) {
    bool applied;
    if (settings_menu.result.request_fullscreen && menu_fullscreen_result(&applied)) {
        settings_menu.result.request_fullscreen = false;
        settings_menu.fullscreen_failed = !applied;
        if (applied) {
            g_settings.fullscreen = settings_menu.draft.settings.fullscreen;
            ++settings_menu.result.applied;
        }
        if (lan_phase == LAN_UI_SETTINGS) {
            lan_action = 0;
            settings_menu_render(c, 0);
        }
    }
    if (settings_menu.pending_open && !settings_menu.result.request_fullscreen) {
        settings_menu.pending_open = false;
        if (lan_menu_capture()) return;
        settings_config_begin(&settings_menu.draft);
        settings_menu.screen = SETTINGS_ROOT;
        settings_menu.root_page = 0;
        settings_menu.error = NULL;
        settings_menu.dirty = false;
        lan_keep_menu_template(c);
        settings_menu_render(c, 0);
    }
    if (lan_phase != LAN_UI_SETTINGS || settings_menu.screen != SETTINGS_ENTRY) return;
    SettingsInput input;
    if (!settings_input_poll(&input)) return;
    if (input.result == SETTINGS_INPUT_ACCEPTED || input.result == SETTINGS_INPUT_CANCELLED) {
        lan_action = 0;
        settings_menu_finish_input(c, input.result == SETTINGS_INPUT_ACCEPTED ? input.value : NULL);
    } else if (lan_dialog) {
        strcpy(settings_menu.input, input.value);
        uint64_t rows = LD64(lan_dialog + SF2C_LAN_UI_HOOKS_N_0XE8_7494);
        uint64_t items = rows ? LD64(rows + SF2C_LAN_UI_HOOKS_N_0X10_BF9C) : 0;
        uint64_t row = items ? LD64(items + SF2C_LAN_UI_HOOKS_N_0X20_A766) : 0;
        uint64_t label = row ? LD64(row + SF2C_LAN_UI_HOOKS_N_0X18_D47C) : 0;
        if (label) {
            char visible[SF2C_SETTINGS_MENU_HOOKS_N_128_2747];
            snprintf(visible, sizeof visible, SF2C_SETTINGS_MENU_HOOKS_S_96S_D145, input.value);
            guest_invoke2(c, SF2C_LAN_UI_HOOKS_N_0X7000E71230ULL_67AB, label, guest_string(c, visible));
        }
    }
}

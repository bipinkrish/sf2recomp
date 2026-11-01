// Hide only the authored menu footer panels. Dialog HintUI instances
// and FightLeft/FightRightHintList retain their original rendering and behavior.
// A CanvasGroup leaves module initialization, input subscriptions and native
// selection state intact while hiding the whole footer (including its BG).
static uint64_t footer_canvas_group_type;
#ifdef __ANDROID__
static MenuTouch android_touches[MENU_TOUCH_MAX];
static unsigned android_touch_count;
#endif
RT_ON_RESET(reset_platform_ui) {
    footer_canvas_group_type = 0;
#ifdef __ANDROID__
    memset(android_touches, 0, sizeof android_touches);
    android_touch_count = 0;
#endif
}

static void hide_ui_module(Cpu *c, uint64_t list) {
    if (!footer_canvas_group_type)
        footer_canvas_group_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
            guest_string(c, "UnityEngine.CanvasGroup, UnityEngine"));
    if (!footer_canvas_group_type) return;
    uint64_t object = guest_invoke2(c, 0x700121e720ull, list, 0);
    uint64_t group = guest_invoke2(c, 0x700121e790ull, list, footer_canvas_group_type);
    if (!group) group = guest_invoke2(c, 0x70012260e0ull, object, footer_canvas_group_type);
    if (!group) return;
    guest_set_vec2(c, 0x7000af7fb0ull, group, 0, 0); // CanvasGroup.alpha
    guest_invoke2(c, 0x7000af8110ull, group, 0);     // no invisible pointer targets
}

static void hide_menu_footer(Cpu *c, uint64_t list) {
    static const char *const names[] = {
        "GeneralMenuHintList", "MapHintList", "ProfileHintList", "ShopHintList",
        "DojoHintList", "PvPLeftHintList", "PvPRightHintList"
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        if (!guest_object_named(c, list, names[i])) continue;
        hide_ui_module(c, list);
        LOG("[ui] hidden menu control footer\n");
        break;
    }
}

static int platform_ui_hook(Cpu *c, uint64_t site) {
    if (site == 0x7000cdc110ull) { // HintList.Init
        hide_menu_footer(c, c->x[0]);
        return 0;
    }
#ifdef __ANDROID__
    switch (site) {
    case 0x7000c95bf0ull: // GameControllerSwitch.Init
        // The mobile ActionButtons already display these cooldowns on the
        // right. Keep the Switch module's state/callbacks, hide its duplicate HUD.
        hide_ui_module(c, c->x[0]);
        return 0;
    case 0x7000e1a610ull: // AssemblyController.ShowController
        c->x[0] = 1; return 1;
    case 0x7000c8f80cull: // InitTouchControl: LeftContainer.SetActive
    case 0x7000c8f830ull: // InitTouchControl: ActionButtons.SetVisible
        c->x[1] = 1; return 0;
    case 0x7000c970b0ull: // TouchControlData.InputType
        // Touch controls drive the same local player as guest gamepad index 0.
        // Keep InputSelector's existing solo/Sparring/LAN team assignment.
        c->x[0] = 2; return 1;
    case 0x70012283e0ull: c->x[0] = android_touch_count; return 1;
    case 0x70012285c0ull: c->x[0] = 1; return 1;
    case 0x7001228450ull: { // Input.GetTouch returns a struct through x8
        unsigned index = (uint32_t)c->x[1];
        MenuTouch value = {.finger = -1, .phase = TOUCH_CANCELED};
        if (index < android_touch_count) value = android_touches[index];
        memcpy((void *)(uintptr_t)c->x[8], &value, sizeof value);
        return 1;
    }
    default: break;
    }
#endif
    return -1;
}

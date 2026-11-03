#include "../constants/hle.h"
// Extend only the in-memory PvP configuration. Original assets and campaign
// equipment remain owned by the game; combat receives native cloned parameters.
static const char sparring_shadow[] = SF2C_SPARRING_ROSTER_HOOKS_S_PVP_SHADOW_6B9B;
static const char sparring_titan[] = SF2C_SPARRING_ROSTER_HOOKS_S_PVP_TITAN_1F1F;
static bool sparring_building_model;
static bool sparring_changing_phase;
static bool sparring_initializing_table, sparring_selecting_cell;
static bool sparring_setting_portrait;
static uint64_t sparring_models[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473];
static struct {
    uint64_t table, content;
    uint64_t portraits[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473];
    unsigned first_row;
    float press_y;
    bool pressed, dragged;
} sparring_view;

RT_ON_RESET(reset_sparring_roster) {
    memset(&sparring_view, 0, sizeof sparring_view);
    sparring_building_model = sparring_changing_phase = false;
    sparring_initializing_table = sparring_selecting_cell = false;
    sparring_setting_portrait = false;
    memset(sparring_models, 0, sizeof sparring_models);
}

static bool sparring_named(uint64_t string, const char *name) {
    char value[SF2C_SPARRING_ROSTER_HOOKS_N_64_A68B];
    return dev_read_string(string, value, sizeof value) && !strcmp(value, name);
}

static bool sparring_fixed_loadout(uint64_t character) {
    return character && (sparring_named(LD64(character + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C), sparring_shadow) ||
                         sparring_named(LD64(character + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C), sparring_titan));
}

static uint64_t sparring_xml(Cpu *c, const char *xml) {
    // These literals are ASCII. Keep the UI label allocator's 256-unit limit.
    size_t length = strlen(xml);
    uint64_t string = guest_invoke2(c, GUEST_STRING_ALLOC, 0, length);
    if (!string) return 0;
    ST32(string + SF2C_SPARRING_ROSTER_HOOKS_N_0X10_BF9C, length);
    for (size_t i = 0; i <= length; ++i) ST16(string + SF2C_SPARRING_ROSTER_HOOKS_N_0X14_F942 + i * SF2C_SPARRING_ROSTER_HOOKS_N_2_D473, xml[i]);
    uint64_t doc = guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CB980ULL_9431, 0, string, 1, 0);
    return doc ? guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70002D9050ULL_8AFC, doc, 0) : 0;
}

static uint64_t sparring_child(Cpu *c, uint64_t node, const char *name) {
    return node ? guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CA0D0ULL_534B, 0, node, guest_string(c, name), 0) : 0;
}

static void sparring_append_xml(Cpu *c, uint64_t parent, const char *xml) {
    uint64_t node = sparring_xml(c, xml);
    if (node) guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CA830ULL_7C01, 0, parent, node, 0);
}

static void sparring_extend_roster(Cpu *c, uint64_t document) {
    if (!g_settings.enable_shadow_in_sparring && !g_settings.enable_titan_in_sparring) return;
    uint64_t root = sparring_child(c, document, SF2C_SPARRING_ROSTER_HOOKS_S_PVPSETTINGS_0326);
    if (!root || sparring_child(c, root, SF2C_SPARRING_ROSTER_HOOKS_S_RECOMPSPARRING_41AB)) return;
    uint64_t area = sparring_child(c, root, SF2C_SPARRING_ROSTER_HOOKS_S_CHARACTERAREA_3A80);
    uint64_t gui = sparring_child(c, area, SF2C_SPARRING_ROSTER_HOOKS_S_GUI_D6FC);
    uint64_t characters = sparring_child(c, area, SF2C_SPARRING_ROSTER_HOOKS_S_CHARACTERS_6931);
    uint64_t templates = sparring_child(c, sparring_child(c, root, SF2C_SPARRING_ROSTER_HOOKS_S_WARRIORS_CE58), SF2C_SPARRING_ROSTER_HOOKS_S_TEMPLATES_BD8D);
    if (!gui || !characters || !templates) return;

    // The allocation hook below uses the real count, allowing a partial last
    // row when only one fighter is enabled. Disabled entries have no object.
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70002E30A0ULL_9A0C, gui, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_HORIZONTALCOUNT_2358), guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_11_F326), 0);
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70002E30A0ULL_9A0C, gui, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_VERTICALCOUNT_7042), guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_4_2BF1), 0);
    if (g_settings.enable_titan_in_sparring) sparring_append_xml(c, characters,
        SF2C_SPARRING_ROSTER_HOOKS_S_CHAR_NAME_BOSS_TITAN_TEMPLAT_0777
        SF2C_SPARRING_ROSTER_HOOKS_S_BIGPORTRAIT_BOSS_TITAN_DEFAU_FE25);
    if (g_settings.enable_shadow_in_sparring) sparring_append_xml(c, characters,
        SF2C_SPARRING_ROSTER_HOOKS_S_CHAR_NAME_NAME_SHADOW_TEMPLA_794E
        SF2C_SPARRING_ROSTER_HOOKS_S_BIGPORTRAIT_AVATAR_HUMAN_DEF_885B);
    if (g_settings.enable_shadow_in_sparring) sparring_append_xml(c, templates,
        SF2C_SPARRING_ROSTER_HOOKS_S_TEMPLATE_NAME_PVP_SHADOW_TEM_B560);
    // Sparring stays first to two wins. Combine the best campaign values into
    // one fixed loadout for EVERY round; do not advance Titan's campaign phases.
    // Effect                       Campaign phases 1 / 2 / 3       Sparring
    // Sword lifesteal chance        .30 / .25 / .22                  .30
    // Harpoon lifesteal chance      .20 / absent / .22               .22
    // Enfeeble magic chance         .50 / absent / absent            .50
    // Weakness magic chance         absent / .50 / .20               .50
    // Head-hit magic recharge       absent / .70 / absent            .70
    // Regeneration ChanceFactor     absent / absent / 1.375          1.375
    // Shield cooldown, 60 Hz ticks  absent / 420 / 180                180
    // Enlightenment chance/health   .15/.15 / .10/.10 / .05/.10       .15/.15
    // Ricochet chance               .50 / .50 / .50                  .50
    // Enchantment resistance        100 / 100 / 100                  100
    // Aspects: sword/regeneration 1900, harpoon/magic/helmet 1920.
    // Keep the native shield automatic. Campaign has no active Titan boss
    // power, so BossAbilityAlias stays empty and Super is not a fake action.
    if (g_settings.enable_titan_in_sparring) sparring_append_xml(c, templates,
        SF2C_SPARRING_ROSTER_HOOKS_S_TEMPLATE_NAME_PVP_TITAN_TEMP_9AF8
        SF2C_SPARRING_ROSTER_HOOKS_S_ENCHANTMENTRESISTANCE_100_MA_FC20
        SF2C_SPARRING_ROSTER_HOOKS_S_ITEMS_ITEM_NAME_WEAPON_TITAN_3F5A
        SF2C_SPARRING_ROSTER_HOOKS_S_ITEM_NAME_HEAD_TITAN_ITEM_NA_AFE9
        SF2C_SPARRING_ROSTER_HOOKS_S_PERKS_PERK_NAME_PERK_TITAN_P_E418
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_TITANS_SHIELD_3F27
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__57C6
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__A986
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__BDA4
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__CB7C
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__ED11
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ITEM_SPECIAL__6D1B
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_ENLIGHTENMENT_F54E
        SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NAME_PERK_RICOCHET_SET__3D9A
        SF2C_SPARRING_ROSTER_HOOKS_S_PERKS_TEMPLATE_3869);
    sparring_append_xml(c, root, SF2C_SPARRING_ROSTER_HOOKS_S_RECOMPSPARRING_FE50);
    LOG(SF2C_SPARRING_ROSTER_HOOKS_S_SPARRING_ROSTER_SHADOW_U_TIT_F4D7, g_settings.enable_shadow_in_sparring, g_settings.enable_titan_in_sparring);
}

static uint64_t sparring_loadout_weapon(Cpu *c, uint64_t name) {
    if (sparring_named(name, sparring_shadow)) {
        uint64_t campaign = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D17E80ULL_FFFD, 0, 0);
        return campaign ? LD64(campaign + SF2C_SPARRING_ROSTER_HOOKS_N_0XA8_D409) : 0;
    }
    if (sparring_named(name, sparring_titan)) {
        uint64_t list = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000CF9830ULL_7401, 0, 0);
        uint64_t entry = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D22680ULL_F1A3, list, guest_string(c, sparring_titan));
        uint64_t model = entry ? LD64(entry + SF2C_SPARRING_ROSTER_HOOKS_N_0X10_BF9C) : 0;
        return model ? LD64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0XA8_D409) : 0;
    }
    return 0;
}

static void sparring_shadow_equipment(Cpu *c, uint64_t model) {
    uint64_t campaign = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D17E80ULL_FFFD, 0, 0);
    if (!campaign) return; // Pvp_Default already has the unequipped starter body/fists.
    uint64_t copy = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000EA31D0ULL_F85F, campaign, 0);
    if (!copy) return;
    // The native clone owns its lists. Take only equipment, keeping PvP's
    // attributes, level, health and controls independent of campaign progress.
    ST64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0X38_AE4F, guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9DA70ULL_68D6, copy, 0));
    for (unsigned offset = SF2C_SPARRING_ROSTER_HOOKS_N_0XA0_3862; offset <= SF2C_SPARRING_ROSTER_HOOKS_N_0XC8_62CB; offset += SF2C_SPARRING_ROSTER_HOOKS_N_8_2C62)
        ST64(model + offset, LD64(copy + offset));
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9E580ULL_EB5F, model, 0);
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9ECD0ULL_089B, model, 0);
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9F7D0ULL_532D, model, 0);
    char weapon[SF2C_SPARRING_ROSTER_HOOKS_N_96_7B1A] = SF2C_SPARRING_ROSTER_HOOKS_S_FISTS_4F71;
    uint64_t item = LD64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0XA8_D409);
    if (item) dev_read_string(LD64(item + SF2C_SPARRING_ROSTER_HOOKS_N_0X10_BF9C), weapon, sizeof weapon);
    LOG(SF2C_SPARRING_ROSTER_HOOKS_S_SPARRING_SHADOW_USES_CAMPAIG_C146, weapon);
}

static void sparring_scroll_row(Cpu *c, unsigned first) {
    uint64_t table = sparring_view.table;
    if (!table || first == sparring_view.first_row) return;
    sparring_view.first_row = first;
    float height;
    uint32_t bits = LD32(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X4C_C1BA);
    memcpy(&height, &bits, sizeof height);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A550ULL_8625, sparring_view.content, 0, first * height);
}

static void sparring_attach_roster(Cpu *c, uint64_t table, uint64_t content) {
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015D8EC0ULL_472E, LD64(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X28_EAB9), content, 1, 0);
    for (unsigned player = 0; player < SF2C_SPARRING_ROSTER_HOOKS_N_2_D473; ++player) {
        uint64_t selector = LD64(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C + player * SF2C_SPARRING_ROSTER_HOOKS_N_8_2C62);
        uint64_t transforms = LD64(selector + SF2C_SPARRING_ROSTER_HOOKS_N_0X48_0EF0);
        // PlayerSelector.Awake collects its mask AND the two independent
        // border containers. They are siblings, not children of the mask.
        for (unsigned i = 0; i < LD64(transforms + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C); ++i)
            guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015D8EC0ULL_472E, LD64(transforms + SF2C_SPARRING_ROSTER_HOOKS_N_0X20_A766 + i * SF2C_SPARRING_ROSTER_HOOKS_N_8_2C62), content, 1, 0);
    }
}

static uint64_t sparring_scroll_content(Cpu *c, uint64_t table) {
    // The native selectors animate their own transforms. Moving those same
    // transforms for scrolling races their coroutines and leaves the highlight
    // below the portrait it selects. Scroll their shared parent instead.
    uint64_t parent = LD64(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X30_8BAD);
    uint64_t content = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015DA040ULL_291E, parent, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_SPARRINGSCROLLCONTENT_784A));
    if (content) return content;
    uint64_t rect_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_UNITYENGINE_RECTTRANSFORM_UN_68EA));
    uint64_t type_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_SYSTEM_TYPE_7033));
    uint64_t source = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E720ULL_0F0E, table, 0);
    uint64_t go = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000B5B110ULL_CA79, LD64(source), 0);
    uint64_t types = guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70006E6740ULL_63EE, 0, type_type, 1, 0);
    ST64(types + SF2C_SPARRING_ROSTER_HOOKS_N_0X20_A766, rect_type);
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7001225F80ULL_EE42, go, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_SPARRINGSCROLLCONTENT_784A), types, 0);
    content = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7001226320ULL_D386, go, 0);
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015D8EC0ULL_472E, content, parent, 0, 0);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A050ULL_D8FB, content, 0, 0);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A250ULL_F630, content, 1, 1);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A7D0ULL_D64C, content, 0, 0);
    // Native movement targets are absolute in the original parent's space.
    // Preserve its origin as well as its bounds when inserting the wrapper.
    Cpu saved = *c;
    c->x[0] = parent; c->x[1] = 0;
    guest_call(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A8D0ULL_1997);
    float pivot[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473];
    memcpy(pivot, &c->v[0], SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22); memcpy(pivot + 1, &c->v[1], SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22);
    *c = saved;
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A9D0ULL_564C, content, pivot[0], pivot[1]);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A550ULL_8625, content, 0, 0);
    sparring_attach_roster(c, table, content);
    return content;
}

static void sparring_scroll_to_player(Cpu *c, unsigned player) {
    uint64_t table = sparring_view.table;
    uint64_t selector = table && player < SF2C_SPARRING_ROSTER_HOOKS_N_2_D473 ? LD64(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C + player * SF2C_SPARRING_ROSTER_HOOKS_N_8_2C62) : 0;
    if (!selector) return;
    unsigned row = LD32(selector + SF2C_SPARRING_ROSTER_HOOKS_N_0X3C_4150), first = sparring_view.first_row;
    if (row < first) first = row;
    else if (row >= first + SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07) first = row - SF2C_SPARRING_ROSTER_HOOKS_N_2_D473;
    sparring_scroll_row(c, first);
}

static bool sparring_roster_scroll_input(Cpu *c, uint64_t table, const MenuMouse *mouse, bool inside) {
    if (!table || table != sparring_view.table) return false;
    if (inside && mouse->scroll_y) {
        sparring_scroll_row(c, mouse->scroll_y < 0 ? 1 : 0);
        return true;
    }
    if (mouse->pressed & 1) {
        sparring_view.pressed = inside;
        sparring_view.dragged = false;
        sparring_view.press_y = mouse->y;
    }
    if (sparring_view.pressed && (mouse->held & 1) && fabsf(mouse->y - sparring_view.press_y) > SF2C_SPARRING_ROSTER_HOOKS_N_24_C235) {
        // Unity mouse/touch coordinates increase upward. A swipe up reveals
        // the later row. Consume the release so swiping cannot confirm a fighter.
        sparring_scroll_row(c, mouse->y > sparring_view.press_y ? 1 : 0);
        sparring_view.dragged = true;
    }
    bool consumed = sparring_view.dragged;
    if (mouse->released & 1) sparring_view.pressed = sparring_view.dragged = false;
    return consumed;
}

static bool sparring_roster_cell_visible(uint64_t table, unsigned index) {
    if (table != sparring_view.table) return true;
    unsigned row = index / SF2C_SPARRING_ROSTER_HOOKS_N_11_4FC8;
    return row >= sparring_view.first_row && row < sparring_view.first_row + SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07;
}

static void sparring_prevent_throws(Cpu *c, uint64_t model) {
    // Resolve the campaign perk through GameUtils.PerkItems, as PerkRule does.
    // Sparse ParseUser XML skips perk loading; add it to this match's owned
    // PerksUser list and rebuild combat Perks on the already-returned model.
    uint64_t klass = LD64(SF2C_SPARRING_ROSTER_HOOKS_N_0X7003B37410ULL_CAE6); // GameUtils_TypeInfo
    uint64_t fields = klass ? LD64(klass + SF2C_SPARRING_ROSTER_HOOKS_N_0XB8_8F73) : 0;
    uint64_t items = fields ? LD64(fields + SF2C_SPARRING_ROSTER_HOOKS_N_0X48_0EF0) : 0;
    if (!items) return;
    uint64_t perk = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000F72C30ULL_91A5, items, guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_PERK_NO_THROWS_EC00));
    if (!perk) return;
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9DA00ULL_9DE9, model, perk);
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E9F7D0ULL_532D, model, 0);
}

static void sparring_release_portraits(Cpu *c) {
    for (unsigned i = 0; i < SF2C_SPARRING_ROSTER_HOOKS_N_2_D473; ++i) if (sparring_view.portraits[i]) {
        guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7001233760ULL_8B27, 0, sparring_view.portraits[i]);
        sparring_view.portraits[i] = 0;
    }
}

static uint64_t sparring_portrait(Cpu *c, uint64_t source, unsigned fighter) {
    if (sparring_view.portraits[fighter]) return sparring_view.portraits[fighter];
    uint64_t texture = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CF640ULL_6BEE, source, 0);
    Cpu saved = *c;
    c->x[0] = source; c->x[1] = 0;
    guest_call(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CF720ULL_0B4D);
    float rect[SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22];
    for (unsigned i = 0; i < SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22; ++i) memcpy(rect + i, &c->v[i], SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22);
    *c = saved;
    // Use every pixel of the original badge, without the virtual padding in
    // its campaign sprite metadata. Image.preserveAspect fits the full artwork.
    float fitted[] = {rect[0], rect[1], rect[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473], rect[SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07], SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0};
    c->x[0] = 0; c->x[1] = texture; c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473] = 0;
    for (unsigned i = 0; i < SF2C_SPARRING_ROSTER_HOOKS_N_6_E7F6; ++i) { c->v[i] = 0; memcpy(&c->v[i], fitted + i, SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22); }
    guest_call(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015CF3F0ULL_5E7C);
    uint64_t sprite = c->x[0];
    *c = saved;
    sparring_view.portraits[fighter] = sprite;
    return sprite;
}

// Create a plain UI child without cloning localization scripts or scaling.
static uint64_t sparring_create_graphic(Cpu *c, uint64_t source, uint64_t parent, uint64_t type, const char *name) {
    uint64_t rect_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_UNITYENGINE_RECTTRANSFORM_UN_68EA));
    uint64_t type_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_SYSTEM_TYPE_7033));
    if (!type || !rect_type || !type_type) return 0;
    uint64_t source_go = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E720ULL_0F0E, source, 0);
    uint64_t go = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000B5B110ULL_CA79, LD64(source_go), 0);
    uint64_t types = guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70006E6740ULL_63EE, 0, type_type, 1, 0);
    if (!go || !types) return 0;
    ST64(types + SF2C_SPARRING_ROSTER_HOOKS_N_0X20_A766, rect_type);
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7001225F80ULL_EE42, go, guest_string(c, name), types, 0);
    uint64_t graphic = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70012260E0ULL_58B9, go, type);
    if (!graphic) return 0;
    uint64_t rect = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E6B0ULL_59E6, graphic, 0);
    guest_invoke4(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015D8EC0ULL_472E, rect, parent, 0, 0);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A050ULL_D8FB, rect, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0); // anchorMin
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A250ULL_F630, rect, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0); // anchorMax
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A550ULL_8625, rect, 0, 0);       // anchoredPosition
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A9D0ULL_564C, rect, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0, SF2C_SPARRING_ROSTER_HOOKS_N_0_5F_2DB0); // pivot
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000114FF0ULL_9972, graphic, 0); // no raycast target
    return graphic;
}

static void sparring_fit_portrait(Cpu *c, uint64_t image, uint64_t sprite, unsigned fighter) {
    uint64_t rect = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E6B0ULL_59E6, image, 0);
    uint64_t image_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
        guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_UNITYENGINE_UI_IMAGE_UNITYEN_7373));
    const char *name = SF2C_SPARRING_ROSTER_HOOKS_S_FITTEDSPARRINGPORTRAIT_3136;
    uint64_t child = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70015DA040ULL_291E, rect, guest_string(c, name));
    uint64_t portrait = child ? guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E790ULL_8667, child, image_type) :
                               sparring_create_graphic(c, image, rect, image_type, name);
    if (!portrait) return;
    uint64_t portrait_rect = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E6B0ULL_59E6, portrait, 0);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A050ULL_D8FB, portrait_rect, 0, 0);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A250ULL_F630, portrait_rect, 1, 1);
    guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A7D0ULL_D64C, portrait_rect, 0, 0);
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700010D790ULL_F093, portrait, sprite);
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700011D690ULL_2049, portrait, 1);
    // The original image becomes the rectangular backing. Its child contains
    // the complete badge, so the roster's GridLayoutGroup gains no extra cells.
    guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700010D790ULL_F093, image, 0);
    const float colors[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473][SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22] = {{SF2C_SPARRING_ROSTER_HOOKS_N_0_56F_7F90, SF2C_SPARRING_ROSTER_HOOKS_N_0_46F_C158, SF2C_SPARRING_ROSTER_HOOKS_N_0_33F_3A25, 1}, {SF2C_SPARRING_ROSTER_HOOKS_N_0_40F_E8BB, SF2C_SPARRING_ROSTER_HOOKS_N_0_54F_17FA, SF2C_SPARRING_ROSTER_HOOKS_N_0_64F_C041, 1}};
    Cpu saved = *c;
    c->x[0] = image; c->x[1] = 0;
    for (unsigned i = 0; i < SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22; ++i) { c->v[i] = 0; memcpy(&c->v[i], &colors[fighter][i], SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22); }
    guest_call(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000114F90ULL_9628);
    *c = saved;
}

static int sparring_roster_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000FC8430ULL_B08D: // PvpSettings..ctor, before roster parsing.
        sparring_extend_roster(c, c->x[1]);
        return 0;
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D11714ULL_ECCD: // ParseUsersModels reads its own PvP XML document.
        sparring_extend_roster(c, c->x[0]);
        return 0;
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000FC8690ULL_2B7B: // ParseCharacterNode: allocate actual entries, not h*v.
        c->x[SF2C_SPARRING_ROSTER_HOOKS_N_22_785F] = SF2C_SPARRING_ROSTER_HOOKS_N_33_C6F3 + g_settings.enable_shadow_in_sparring + g_settings.enable_titan_in_sparring;
        return 0;
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D93FD0ULL_95C0: { // Preserve the authored 11x3 cell size; navigate four rows.
        if (sparring_initializing_table || c->x[1] != SF2C_SPARRING_ROSTER_HOOKS_N_11_4FC8 || c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473] != SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22) return 0;
        uint64_t table = c->x[0];
        sparring_initializing_table = true;
        guest_invoke4(c, site, table, SF2C_SPARRING_ROSTER_HOOKS_N_11_4FC8, SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07, c->x[SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07]);
        sparring_initializing_table = false;
        ST32(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X40_1AF3, SF2C_SPARRING_ROSTER_HOOKS_N_4_4B22);
        uint64_t portraits[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473] = {0};
        if (!sparring_view.table || sparring_view.table == table) memcpy(portraits, sparring_view.portraits, sizeof portraits);
        else sparring_release_portraits(c);
        memset(&sparring_view, 0, sizeof sparring_view);
        memcpy(sparring_view.portraits, portraits, sizeof portraits);
        sparring_view.table = table;
        sparring_view.content = sparring_scroll_content(c, table);
        guest_set_vec2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700123A550ULL_8625, sparring_view.content, 0, 0);
        uint64_t mask_type = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
            guest_string(c, SF2C_SPARRING_ROSTER_HOOKS_S_UNITYENGINE_UI_RECTMASK2D_UN_DADD));
        uint64_t go = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E720ULL_0F0E, table, 0);
        uint64_t mask = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E860ULL_4FAD, go, mask_type);
        if (!mask) guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X70012260E0ULL_58B9, go, mask_type);
        return 1;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D94310ULL_2F01: { // Selection moves the viewport, without changing cell proportions.
        if (sparring_selecting_cell || c->x[0] != sparring_view.table) return 0;
        unsigned player = c->x[1];
        sparring_selecting_cell = true;
        Cpu saved = *c;
        guest_call(c, site);
        *c = saved;
        sparring_selecting_cell = false;
        sparring_scroll_to_player(c, player);
        return 1;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E43DA0ULL_93F4: { // Keep controller navigation inside a partial final row.
        uint64_t module = c->x[0], table = LD64(module + SF2C_SPARRING_ROSTER_HOOKS_N_0X20_A766), items = LD64(module + SF2C_SPARRING_ROSTER_HOOKS_N_0X98_14A1);
        unsigned player = c->x[1], columns = LD32(module + SF2C_SPARRING_ROSTER_HOOKS_N_0X5C_F9CC), rows = LD32(module + SF2C_SPARRING_ROSTER_HOOKS_N_0X60_3D6E);
        unsigned count = items ? LD64(items + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C) : 0;
        if (!table || player > 1 || !columns || !rows || !count || count == columns * rows) return -1;
        uint64_t selector = LD64(table + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C + player * SF2C_SPARRING_ROSTER_HOOKS_N_8_2C62);
        if (!selector) return -1;
        int old_x = LD32(selector + SF2C_SPARRING_ROSTER_HOOKS_N_0X38_AE4F), old_y = LD32(selector + SF2C_SPARRING_ROSTER_HOOKS_N_0X3C_4150);
        int x = (old_x + (int32_t)c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473]) % (int)columns;
        int y = (old_y + (int32_t)c->x[SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07]) % (int)rows;
        if (x < 0) x += columns;
        if (y < 0) y += rows;
        if ((unsigned)(y * columns + x) >= count) {
            x = c->x[SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07] || (int32_t)c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473] < 0 ? (count - 1) % columns : 0;
            c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473] = (uint32_t)(x - old_x);
            c->x[SF2C_SPARRING_ROSTER_HOOKS_N_3_4E07] = (uint32_t)(y - old_y);
        }
        return -1; // LAN still needs to observe and synchronize the menu action.
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D985B0ULL_BCB0:
        sparring_release_portraits(c);
        memset(&sparring_view, 0, sizeof sparring_view);
        return -1;
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D979E0ULL_E905: { // Fit complete circular artwork inside rectangular roster cells.
        if (sparring_setting_portrait) return 0;
        unsigned fighter;
        if (sparring_named(c->x[1], SF2C_SPARRING_ROSTER_HOOKS_S_UI_USERS_AVATAR_HUMAN_0D79)) fighter = 0;
        else if (sparring_named(c->x[1], SF2C_SPARRING_ROSTER_HOOKS_S_UI_USERS_BOSS_TITAN_6D42)) fighter = 1;
        else return 0;
        uint64_t component = c->x[0];
        sparring_setting_portrait = true;
        guest_invoke2(c, site, component, c->x[1]);
        sparring_setting_portrait = false;
        uint64_t source = guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7000111A30ULL_F6E5, component, 0);
        uint64_t sprite = source ? sparring_portrait(c, source, fighter) : 0;
        if (sprite) {
            sparring_fit_portrait(c, component, sprite, fighter);
        }
        return 1;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E432A0ULL_8C23: { // PhaseBox.ChangeCharacter, refresh after campaign equipment changes.
        uint64_t character = c->x[1];
        if (character && sparring_named(LD64(character + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C), sparring_shadow)) {
            uint64_t weapon = sparring_loadout_weapon(c, LD64(character + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C));
            if (weapon) ST64(character + SF2C_SPARRING_ROSTER_HOOKS_N_0X30_8BAD, LD64(weapon + SF2C_SPARRING_ROSTER_HOOKS_N_0X10_BF9C));
        }
        return 0;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E46A90ULL_5E1E: // Fixed equipment has no weapon carousel.
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E46DE0ULL_CE06: {
        uint64_t box = c->x[0], phases = LD64(box + SF2C_SPARRING_ROSTER_HOOKS_N_0X40_1AF3);
        uint64_t phase = phases && LD64(phases + SF2C_SPARRING_ROSTER_HOOKS_N_0X18_D47C) > SF2C_SPARRING_ROSTER_HOOKS_N_2_D473 ? LD64(phases + SF2C_SPARRING_ROSTER_HOOKS_N_0X30_8BAD) : 0;
        if (!phase || !sparring_fixed_loadout(LD64(phase + SF2C_SPARRING_ROSTER_HOOKS_N_0X38_AE4F))) return 0;
        if (site == SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E46DE0ULL_CE06) {
            if (!c->x[1]) c->x[1] = 1;
            return 0;
        }
        if (sparring_changing_phase) return 0;
        sparring_changing_phase = true;
        uint64_t next = guest_invoke2(c, site, box, 0);
        if (!next) next = guest_invoke2(c, site, box, 0);
        sparring_changing_phase = false;
        c->x[0] = next;
        return 1;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E44080ULL_5B3E: // CharacterSelectModule.GetPlayerWeapon.
        if (sparring_named(c->x[1], sparring_shadow) || sparring_named(c->x[1], sparring_titan)) {
            c->x[0] = sparring_loadout_weapon(c, c->x[1]);
            return 1;
        }
        return 0;
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000D96FB0ULL_ABF7: { // PvpScene.GetModelParameters. Keep normal random resolution and construction.
        if (sparring_building_model) return 0;
        uint64_t scene = c->x[0], name = c->x[1];
        unsigned player = c->x[SF2C_SPARRING_ROSTER_HOOKS_N_2_D473];
        // PvpScene.StartFight constructs Second (1) before First (0).
        // Clear the previous match before the first native construction.
        if (player == 1) memset(sparring_models, 0, sizeof sparring_models);
        sparring_building_model = true;
        uint64_t model = guest_invoke4(c, site, scene, name, player, 0);
        sparring_building_model = false;
        if (!model) { c->x[0] = 0; return 1; }
        // Random can select either new fighter; Avatar identifies its resolved template.
        if (sparring_named(LD64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0X118_23BF), SF2C_SPARRING_ROSTER_HOOKS_S_NAME_SHADOW_186D)) sparring_shadow_equipment(c, model);
        if (player < SF2C_SPARRING_ROSTER_HOOKS_N_2_D473) {
            sparring_models[player] = model;
            uint64_t opponent = sparring_models[1 - player];
            if (opponent) {
                if (sparring_named(LD64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0X118_23BF), SF2C_SPARRING_ROSTER_HOOKS_S_BOSS_TITAN_E334)) sparring_prevent_throws(c, opponent);
                if (sparring_named(LD64(opponent + SF2C_SPARRING_ROSTER_HOOKS_N_0X118_23BF), SF2C_SPARRING_ROSTER_HOOKS_S_BOSS_TITAN_E334)) sparring_prevent_throws(c, model);
            }
        }
        c->x[0] = model;
        return 1;
    }
    case SF2C_SPARRING_ROSTER_HOOKS_N_0X7000E60BA0ULL_C95B: { // Versus-screen ability panel, reused for each match.
        uint64_t panel = c->x[0], model = c->x[1];
        if (!sparring_named(LD64(model + SF2C_SPARRING_ROSTER_HOOKS_N_0X118_23BF), SF2C_SPARRING_ROSTER_HOOKS_S_BOSS_TITAN_E334)) return 0;
        // Titan has no active boss-power button or versus-screen ability panel.
        guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X7001226480ULL_8D5A,
            guest_invoke2(c, SF2C_SPARRING_ROSTER_HOOKS_N_0X700121E720ULL_0F0E, panel, 0), 0);
        return 1;
    }
    default: return -1;
    }
}

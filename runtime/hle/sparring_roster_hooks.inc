// Extend only the in-memory PvP configuration. Original assets and campaign
// equipment remain owned by the game; combat receives native cloned parameters.
static const char sparring_shadow[] = "Pvp_Shadow";
static const char sparring_titan[] = "Pvp_Titan";
static bool sparring_building_model;
static bool sparring_changing_phase;
static bool sparring_initializing_table, sparring_selecting_cell;
static bool sparring_setting_portrait;
static uint64_t sparring_models[2];
static struct {
    uint64_t table, content;
    uint64_t portraits[2];
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
    char value[64];
    return dev_read_string(string, value, sizeof value) && !strcmp(value, name);
}

static bool sparring_fixed_loadout(uint64_t character) {
    return character && (sparring_named(LD64(character + 0x18), sparring_shadow) ||
                         sparring_named(LD64(character + 0x18), sparring_titan));
}

static uint64_t sparring_xml(Cpu *c, const char *xml) {
    // These literals are ASCII. Keep the UI label allocator's 256-unit limit.
    size_t length = strlen(xml);
    uint64_t string = guest_invoke2(c, GUEST_STRING_ALLOC, 0, length);
    if (!string) return 0;
    ST32(string + 0x10, length);
    for (size_t i = 0; i <= length; ++i) ST16(string + 0x14 + i * 2, xml[i]);
    uint64_t doc = guest_invoke4(c, 0x70015cb980ull, 0, string, 1, 0);
    return doc ? guest_invoke2(c, 0x70002d9050ull, doc, 0) : 0;
}

static uint64_t sparring_child(Cpu *c, uint64_t node, const char *name) {
    return node ? guest_invoke4(c, 0x70015ca0d0ull, 0, node, guest_string(c, name), 0) : 0;
}

static void sparring_append_xml(Cpu *c, uint64_t parent, const char *xml) {
    uint64_t node = sparring_xml(c, xml);
    if (node) guest_invoke4(c, 0x70015ca830ull, 0, parent, node, 0);
}

static void sparring_extend_roster(Cpu *c, uint64_t document) {
    if (!g_settings.enable_shadow_in_sparring && !g_settings.enable_titan_in_sparring) return;
    uint64_t root = sparring_child(c, document, "PvpSettings");
    if (!root || sparring_child(c, root, "RecompSparring")) return;
    uint64_t area = sparring_child(c, root, "CharacterArea");
    uint64_t gui = sparring_child(c, area, "GUI");
    uint64_t characters = sparring_child(c, area, "Characters");
    uint64_t templates = sparring_child(c, sparring_child(c, root, "Warriors"), "Templates");
    if (!gui || !characters || !templates) return;

    // The allocation hook below uses the real count, allowing a partial last
    // row when only one fighter is enabled. Disabled entries have no object.
    guest_invoke4(c, 0x70002e30a0ull, gui, guest_string(c, "HorizontalCount"), guest_string(c, "11"), 0);
    guest_invoke4(c, 0x70002e30a0ull, gui, guest_string(c, "VerticalCount"), guest_string(c, "4"), 0);
    if (g_settings.enable_titan_in_sparring) sparring_append_xml(c, characters,
        "<Char Name=\"BOSS_TITAN\" Template=\"Pvp_Titan\" MiniPortrait=\"ui/users/boss_titan\" "
        "BigPortrait=\"boss_titan\" DefaultWeapon=\"WEAPON_TITAN_GIANT_SWORD\" IsLocked=\"0\"/>");
    if (g_settings.enable_shadow_in_sparring) sparring_append_xml(c, characters,
        "<Char Name=\"NAME_SHADOW\" Template=\"Pvp_Shadow\" MiniPortrait=\"ui/users/avatar_human\" "
        "BigPortrait=\"avatar_human\" DefaultWeapon=\"Fists\" IsLocked=\"0\"/>");
    if (g_settings.enable_shadow_in_sparring) sparring_append_xml(c, templates,
        "<Template Name=\"Pvp_Shadow\" Template=\"Pvp_Default\" FirstName=\"NAME_SHADOW\" Avatar=\"avatar_human\"/>");
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
        "<Template Name=\"Pvp_Titan\" Template=\"Pvp_Default\" FirstName=\"BOSS_TITAN\" Avatar=\"boss_titan\" "
        "EnchantmentResistance=\"100\" MagicInitialCharge=\"4500\" MagicPainRecharge=\"-4000\" MagicDamageRecharge=\"5300\">"
        "<Items><Item Name=\"WEAPON_TITAN_GIANT_SWORD\"/><Item Name=\"BODY_TITAN\"/>"
        "<Item Name=\"HEAD_TITAN\"/><Item Name=\"MAGIC_MIND_THROW\"/><Item Name=\"RANGED_TITANS_HARPOON\"/></Items>"
        "<Perks><Perk Name=\"PERK_TITAN\"/><Perk Name=\"PERK_ANTI_SHOCK\"/>"
        "<Perk Name=\"PERK_TITANS_SHIELD\"><Set Cooldown=\"180\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_LIFESTEAL_WEAPON\"><Set Aspect=\"1900\" Chance=\"0.30\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_LIFESTEAL_RANGED\"><Set Aspect=\"1920\" Chance=\"0.22\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_ENFEEBLE_MAGIC\"><Set Aspect=\"1920\" Chance=\"0.50\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_WEAKNESS_MAGIC\"><Set Aspect=\"1920\" Chance=\"0.50\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_MAGIC_PAIN_RECHARGE_HELM\"><Set Aspect=\"1920\" Chance=\"0.70\"/></Perk>"
        "<Perk Name=\"PERK_ITEM_SPECIAL_REGENERATION_HELM\"><Set Aspect=\"1900\" ChanceFactor=\"1.375\"/></Perk>"
        "<Perk Name=\"PERK_ENLIGHTENMENT\"><Set Chance=\"0.15\" Health=\"0.15\"/></Perk>"
        "<Perk Name=\"PERK_RICOCHET\"><Set Chance=\"0.50\"/></Perk>"
        "</Perks></Template>");
    sparring_append_xml(c, root, "<RecompSparring/>");
    LOG("[sparring] roster Shadow=%u Titan=%u\n", g_settings.enable_shadow_in_sparring, g_settings.enable_titan_in_sparring);
}

static uint64_t sparring_loadout_weapon(Cpu *c, uint64_t name) {
    if (sparring_named(name, sparring_shadow)) {
        uint64_t campaign = guest_invoke2(c, 0x7000d17e80ull, 0, 0);
        return campaign ? LD64(campaign + 0xa8) : 0;
    }
    if (sparring_named(name, sparring_titan)) {
        uint64_t list = guest_invoke2(c, 0x7000cf9830ull, 0, 0);
        uint64_t entry = guest_invoke2(c, 0x7000d22680ull, list, guest_string(c, sparring_titan));
        uint64_t model = entry ? LD64(entry + 0x10) : 0;
        return model ? LD64(model + 0xa8) : 0;
    }
    return 0;
}

static void sparring_shadow_equipment(Cpu *c, uint64_t model) {
    uint64_t campaign = guest_invoke2(c, 0x7000d17e80ull, 0, 0);
    if (!campaign) return; // Pvp_Default already has the unequipped starter body/fists.
    uint64_t copy = guest_invoke2(c, 0x7000ea31d0ull, campaign, 0);
    if (!copy) return;
    // The native clone owns its lists. Take only equipment, keeping PvP's
    // attributes, level, health and controls independent of campaign progress.
    ST64(model + 0x38, guest_invoke2(c, 0x7000e9da70ull, copy, 0));
    for (unsigned offset = 0xa0; offset <= 0xc8; offset += 8)
        ST64(model + offset, LD64(copy + offset));
    guest_invoke2(c, 0x7000e9e580ull, model, 0);
    guest_invoke2(c, 0x7000e9ecd0ull, model, 0);
    guest_invoke2(c, 0x7000e9f7d0ull, model, 0);
    char weapon[96] = "Fists";
    uint64_t item = LD64(model + 0xa8);
    if (item) dev_read_string(LD64(item + 0x10), weapon, sizeof weapon);
    LOG("[sparring] Shadow uses campaign equipment; weapon=%s\n", weapon);
}

static void sparring_scroll_row(Cpu *c, unsigned first) {
    uint64_t table = sparring_view.table;
    if (!table || first == sparring_view.first_row) return;
    sparring_view.first_row = first;
    float height;
    uint32_t bits = LD32(table + 0x4c);
    memcpy(&height, &bits, sizeof height);
    guest_set_vec2(c, 0x700123a550ull, sparring_view.content, 0, first * height);
}

static void sparring_attach_roster(Cpu *c, uint64_t table, uint64_t content) {
    guest_invoke4(c, 0x70015d8ec0ull, LD64(table + 0x28), content, 1, 0);
    for (unsigned player = 0; player < 2; ++player) {
        uint64_t selector = LD64(table + 0x18 + player * 8);
        uint64_t transforms = LD64(selector + 0x48);
        // PlayerSelector.Awake collects its mask AND the two independent
        // border containers. They are siblings, not children of the mask.
        for (unsigned i = 0; i < LD64(transforms + 0x18); ++i)
            guest_invoke4(c, 0x70015d8ec0ull, LD64(transforms + 0x20 + i * 8), content, 1, 0);
    }
}

static uint64_t sparring_scroll_content(Cpu *c, uint64_t table) {
    // The native selectors animate their own transforms. Moving those same
    // transforms for scrolling races their coroutines and leaves the highlight
    // below the portrait it selects. Scroll their shared parent instead.
    uint64_t parent = LD64(table + 0x30);
    uint64_t content = guest_invoke2(c, 0x70015da040ull, parent, guest_string(c, "SparringScrollContent"));
    if (content) return content;
    uint64_t rect_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.RectTransform, UnityEngine"));
    uint64_t type_type = guest_invoke2(c, 0x70008ea5c0ull, 0, guest_string(c, "System.Type"));
    uint64_t source = guest_invoke2(c, 0x700121e720ull, table, 0);
    uint64_t go = guest_invoke2(c, 0x7000b5b110ull, LD64(source), 0);
    uint64_t types = guest_invoke4(c, 0x70006e6740ull, 0, type_type, 1, 0);
    ST64(types + 0x20, rect_type);
    guest_invoke4(c, 0x7001225f80ull, go, guest_string(c, "SparringScrollContent"), types, 0);
    content = guest_invoke2(c, 0x7001226320ull, go, 0);
    guest_invoke4(c, 0x70015d8ec0ull, content, parent, 0, 0);
    guest_set_vec2(c, 0x700123a050ull, content, 0, 0);
    guest_set_vec2(c, 0x700123a250ull, content, 1, 1);
    guest_set_vec2(c, 0x700123a7d0ull, content, 0, 0);
    // Native movement targets are absolute in the original parent's space.
    // Preserve its origin as well as its bounds when inserting the wrapper.
    Cpu saved = *c;
    c->x[0] = parent; c->x[1] = 0;
    guest_call(c, 0x700123a8d0ull);
    float pivot[2];
    memcpy(pivot, &c->v[0], 4); memcpy(pivot + 1, &c->v[1], 4);
    *c = saved;
    guest_set_vec2(c, 0x700123a9d0ull, content, pivot[0], pivot[1]);
    guest_set_vec2(c, 0x700123a550ull, content, 0, 0);
    sparring_attach_roster(c, table, content);
    return content;
}

static void sparring_scroll_to_player(Cpu *c, unsigned player) {
    uint64_t table = sparring_view.table;
    uint64_t selector = table && player < 2 ? LD64(table + 0x18 + player * 8) : 0;
    if (!selector) return;
    unsigned row = LD32(selector + 0x3c), first = sparring_view.first_row;
    if (row < first) first = row;
    else if (row >= first + 3) first = row - 2;
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
    if (sparring_view.pressed && (mouse->held & 1) && fabsf(mouse->y - sparring_view.press_y) > 24) {
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
    unsigned row = index / 11;
    return row >= sparring_view.first_row && row < sparring_view.first_row + 3;
}

static void sparring_prevent_throws(Cpu *c, uint64_t model) {
    // Resolve the campaign perk through GameUtils.PerkItems, as PerkRule does.
    // Sparse ParseUser XML skips perk loading; add it to this match's owned
    // PerksUser list and rebuild combat Perks on the already-returned model.
    uint64_t klass = LD64(0x7003b37410ull); // GameUtils_TypeInfo
    uint64_t fields = klass ? LD64(klass + 0xb8) : 0;
    uint64_t items = fields ? LD64(fields + 0x48) : 0;
    if (!items) return;
    uint64_t perk = guest_invoke2(c, 0x7000f72c30ull, items, guest_string(c, "PERK_NO_THROWS"));
    if (!perk) return;
    guest_invoke2(c, 0x7000e9da00ull, model, perk);
    guest_invoke2(c, 0x7000e9f7d0ull, model, 0);
}

static void sparring_release_portraits(Cpu *c) {
    for (unsigned i = 0; i < 2; ++i) if (sparring_view.portraits[i]) {
        guest_invoke2(c, 0x7001233760ull, 0, sparring_view.portraits[i]);
        sparring_view.portraits[i] = 0;
    }
}

static uint64_t sparring_portrait(Cpu *c, uint64_t source, unsigned fighter) {
    if (sparring_view.portraits[fighter]) return sparring_view.portraits[fighter];
    uint64_t texture = guest_invoke2(c, 0x70015cf640ull, source, 0);
    Cpu saved = *c;
    c->x[0] = source; c->x[1] = 0;
    guest_call(c, 0x70015cf720ull);
    float rect[4];
    for (unsigned i = 0; i < 4; ++i) memcpy(rect + i, &c->v[i], 4);
    *c = saved;
    // Use every pixel of the original badge, without the virtual padding in
    // its campaign sprite metadata. Image.preserveAspect fits the full artwork.
    float fitted[] = {rect[0], rect[1], rect[2], rect[3], 0.5f, 0.5f};
    c->x[0] = 0; c->x[1] = texture; c->x[2] = 0;
    for (unsigned i = 0; i < 6; ++i) { c->v[i] = 0; memcpy(&c->v[i], fitted + i, 4); }
    guest_call(c, 0x70015cf3f0ull);
    uint64_t sprite = c->x[0];
    *c = saved;
    sparring_view.portraits[fighter] = sprite;
    return sprite;
}

// Create a plain UI child without cloning localization scripts or scaling.
static uint64_t sparring_create_graphic(Cpu *c, uint64_t source, uint64_t parent, uint64_t type, const char *name) {
    uint64_t rect_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.RectTransform, UnityEngine"));
    uint64_t type_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "System.Type"));
    if (!type || !rect_type || !type_type) return 0;
    uint64_t source_go = guest_invoke2(c, 0x700121e720ull, source, 0);
    uint64_t go = guest_invoke2(c, 0x7000b5b110ull, LD64(source_go), 0);
    uint64_t types = guest_invoke4(c, 0x70006e6740ull, 0, type_type, 1, 0);
    if (!go || !types) return 0;
    ST64(types + 0x20, rect_type);
    guest_invoke4(c, 0x7001225f80ull, go, guest_string(c, name), types, 0);
    uint64_t graphic = guest_invoke2(c, 0x70012260e0ull, go, type);
    if (!graphic) return 0;
    uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, graphic, 0);
    guest_invoke4(c, 0x70015d8ec0ull, rect, parent, 0, 0);
    guest_set_vec2(c, 0x700123a050ull, rect, 0.5f, 0.5f); // anchorMin
    guest_set_vec2(c, 0x700123a250ull, rect, 0.5f, 0.5f); // anchorMax
    guest_set_vec2(c, 0x700123a550ull, rect, 0, 0);       // anchoredPosition
    guest_set_vec2(c, 0x700123a9d0ull, rect, 0.5f, 0.5f); // pivot
    guest_invoke2(c, 0x7000114ff0ull, graphic, 0); // no raycast target
    return graphic;
}

static void sparring_fit_portrait(Cpu *c, uint64_t image, uint64_t sprite, unsigned fighter) {
    uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, image, 0);
    uint64_t image_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.Image, UnityEngine.UI"));
    const char *name = "FittedSparringPortrait";
    uint64_t child = guest_invoke2(c, 0x70015da040ull, rect, guest_string(c, name));
    uint64_t portrait = child ? guest_invoke2(c, 0x700121e790ull, child, image_type) :
                               sparring_create_graphic(c, image, rect, image_type, name);
    if (!portrait) return;
    uint64_t portrait_rect = guest_invoke2(c, 0x700121e6b0ull, portrait, 0);
    guest_set_vec2(c, 0x700123a050ull, portrait_rect, 0, 0);
    guest_set_vec2(c, 0x700123a250ull, portrait_rect, 1, 1);
    guest_set_vec2(c, 0x700123a7d0ull, portrait_rect, 0, 0);
    guest_invoke2(c, 0x700010d790ull, portrait, sprite);
    guest_invoke2(c, 0x700011d690ull, portrait, 1);
    // The original image becomes the rectangular backing. Its child contains
    // the complete badge, so the roster's GridLayoutGroup gains no extra cells.
    guest_invoke2(c, 0x700010d790ull, image, 0);
    const float colors[2][4] = {{0.56f, 0.46f, 0.33f, 1}, {0.40f, 0.54f, 0.64f, 1}};
    Cpu saved = *c;
    c->x[0] = image; c->x[1] = 0;
    for (unsigned i = 0; i < 4; ++i) { c->v[i] = 0; memcpy(&c->v[i], &colors[fighter][i], 4); }
    guest_call(c, 0x7000114f90ull);
    *c = saved;
}

static int sparring_roster_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000fc8430ull: // PvpSettings..ctor, before roster parsing.
        sparring_extend_roster(c, c->x[1]);
        return 0;
    case 0x7000d11714ull: // ParseUsersModels reads its own PvP XML document.
        sparring_extend_roster(c, c->x[0]);
        return 0;
    case 0x7000fc8690ull: // ParseCharacterNode: allocate actual entries, not h*v.
        c->x[22] = 33 + g_settings.enable_shadow_in_sparring + g_settings.enable_titan_in_sparring;
        return 0;
    case 0x7000d93fd0ull: { // Preserve the authored 11x3 cell size; navigate four rows.
        if (sparring_initializing_table || c->x[1] != 11 || c->x[2] != 4) return 0;
        uint64_t table = c->x[0];
        sparring_initializing_table = true;
        guest_invoke4(c, site, table, 11, 3, c->x[3]);
        sparring_initializing_table = false;
        ST32(table + 0x40, 4);
        uint64_t portraits[2] = {0};
        if (!sparring_view.table || sparring_view.table == table) memcpy(portraits, sparring_view.portraits, sizeof portraits);
        else sparring_release_portraits(c);
        memset(&sparring_view, 0, sizeof sparring_view);
        memcpy(sparring_view.portraits, portraits, sizeof portraits);
        sparring_view.table = table;
        sparring_view.content = sparring_scroll_content(c, table);
        guest_set_vec2(c, 0x700123a550ull, sparring_view.content, 0, 0);
        uint64_t mask_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
            guest_string(c, "UnityEngine.UI.RectMask2D, UnityEngine.UI"));
        uint64_t go = guest_invoke2(c, 0x700121e720ull, table, 0);
        uint64_t mask = guest_invoke2(c, 0x700121e860ull, go, mask_type);
        if (!mask) guest_invoke2(c, 0x70012260e0ull, go, mask_type);
        return 1;
    }
    case 0x7000d94310ull: { // Selection moves the viewport, without changing cell proportions.
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
    case 0x7000e43da0ull: { // Keep controller navigation inside a partial final row.
        uint64_t module = c->x[0], table = LD64(module + 0x20), items = LD64(module + 0x98);
        unsigned player = c->x[1], columns = LD32(module + 0x5c), rows = LD32(module + 0x60);
        unsigned count = items ? LD64(items + 0x18) : 0;
        if (!table || player > 1 || !columns || !rows || !count || count == columns * rows) return -1;
        uint64_t selector = LD64(table + 0x18 + player * 8);
        if (!selector) return -1;
        int old_x = LD32(selector + 0x38), old_y = LD32(selector + 0x3c);
        int x = (old_x + (int32_t)c->x[2]) % (int)columns;
        int y = (old_y + (int32_t)c->x[3]) % (int)rows;
        if (x < 0) x += columns;
        if (y < 0) y += rows;
        if ((unsigned)(y * columns + x) >= count) {
            x = c->x[3] || (int32_t)c->x[2] < 0 ? (count - 1) % columns : 0;
            c->x[2] = (uint32_t)(x - old_x);
            c->x[3] = (uint32_t)(y - old_y);
        }
        return -1; // LAN still needs to observe and synchronize the menu action.
    }
    case 0x7000d985b0ull:
        sparring_release_portraits(c);
        memset(&sparring_view, 0, sizeof sparring_view);
        return -1;
    case 0x7000d979e0ull: { // Fit complete circular artwork inside rectangular roster cells.
        if (sparring_setting_portrait) return 0;
        unsigned fighter;
        if (sparring_named(c->x[1], "ui/users/avatar_human")) fighter = 0;
        else if (sparring_named(c->x[1], "ui/users/boss_titan")) fighter = 1;
        else return 0;
        uint64_t component = c->x[0];
        sparring_setting_portrait = true;
        guest_invoke2(c, site, component, c->x[1]);
        sparring_setting_portrait = false;
        uint64_t source = guest_invoke2(c, 0x7000111a30ull, component, 0);
        uint64_t sprite = source ? sparring_portrait(c, source, fighter) : 0;
        if (sprite) {
            sparring_fit_portrait(c, component, sprite, fighter);
        }
        return 1;
    }
    case 0x7000e432a0ull: { // PhaseBox.ChangeCharacter, refresh after campaign equipment changes.
        uint64_t character = c->x[1];
        if (character && sparring_named(LD64(character + 0x18), sparring_shadow)) {
            uint64_t weapon = sparring_loadout_weapon(c, LD64(character + 0x18));
            if (weapon) ST64(character + 0x30, LD64(weapon + 0x10));
        }
        return 0;
    }
    case 0x7000e46a90ull: // Fixed equipment has no weapon carousel.
    case 0x7000e46de0ull: {
        uint64_t box = c->x[0], phases = LD64(box + 0x40);
        uint64_t phase = phases && LD64(phases + 0x18) > 2 ? LD64(phases + 0x30) : 0;
        if (!phase || !sparring_fixed_loadout(LD64(phase + 0x38))) return 0;
        if (site == 0x7000e46de0ull) {
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
    case 0x7000e44080ull: // CharacterSelectModule.GetPlayerWeapon.
        if (sparring_named(c->x[1], sparring_shadow) || sparring_named(c->x[1], sparring_titan)) {
            c->x[0] = sparring_loadout_weapon(c, c->x[1]);
            return 1;
        }
        return 0;
    case 0x7000d96fb0ull: { // PvpScene.GetModelParameters. Keep normal random resolution and construction.
        if (sparring_building_model) return 0;
        uint64_t scene = c->x[0], name = c->x[1];
        unsigned player = c->x[2];
        // PvpScene.StartFight constructs Second (1) before First (0).
        // Clear the previous match before the first native construction.
        if (player == 1) memset(sparring_models, 0, sizeof sparring_models);
        sparring_building_model = true;
        uint64_t model = guest_invoke4(c, site, scene, name, player, 0);
        sparring_building_model = false;
        if (!model) { c->x[0] = 0; return 1; }
        // Random can select either new fighter; Avatar identifies its resolved template.
        if (sparring_named(LD64(model + 0x118), "NAME_SHADOW")) sparring_shadow_equipment(c, model);
        if (player < 2) {
            sparring_models[player] = model;
            uint64_t opponent = sparring_models[1 - player];
            if (opponent) {
                if (sparring_named(LD64(model + 0x118), "BOSS_TITAN")) sparring_prevent_throws(c, opponent);
                if (sparring_named(LD64(opponent + 0x118), "BOSS_TITAN")) sparring_prevent_throws(c, model);
            }
        }
        c->x[0] = model;
        return 1;
    }
    case 0x7000e60ba0ull: { // Versus-screen ability panel, reused for each match.
        uint64_t panel = c->x[0], model = c->x[1];
        if (!sparring_named(LD64(model + 0x118), "BOSS_TITAN")) return 0;
        // Titan has no active boss-power button or versus-screen ability panel.
        guest_invoke2(c, 0x7001226480ull,
            guest_invoke2(c, 0x700121e720ull, panel, 0), 0);
        return 1;
    }
    default: return -1;
    }
}

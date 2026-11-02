// Campaign practice uses the authored FightList, without reopening its roster
// entry. Hooks skip the native start/result transactions, not just disk writes:
// otherwise practice rewards and counters would leak into the next normal save.
static uint64_t campaign_replay_fight;
static bool campaign_info_updating;
static uint64_t campaign_layout_info, campaign_video_rect;
static float campaign_video_position[2], campaign_video_height;
static uint64_t campaign_refight_label;
static unsigned campaign_label_font_size;

RT_ON_RESET(reset_campaign_replay) {
    campaign_replay_fight = 0;
    campaign_info_updating = false;
    campaign_layout_info = campaign_video_rect = 0;
    campaign_refight_label = 0;
}

static void campaign_vec2(Cpu *c, uint64_t method, uint64_t object, float out[2]) {
    Cpu saved = *c;
    c->x[0] = object; c->x[1] = 0;
    guest_call(c, method);
    memcpy(out, &c->v[0], 4); memcpy(out + 1, &c->v[1], 4);
    *c = saved;
}

static void campaign_movie_layout(Cpu *c, uint64_t info, bool replay) {
    uint64_t content = LD64(info + 0x88), video = content ? LD64(content + 0x30) : 0;
    if (!video) return;
    uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, video, 0);
    if (info != campaign_layout_info || rect != campaign_video_rect) {
        campaign_layout_info = info; campaign_video_rect = rect;
        campaign_vec2(c, 0x700123a410ull, rect, campaign_video_position);
        float size[2]; campaign_vec2(c, 0x700123a6d0ull, rect, size);
        campaign_video_height = size[1];
    }
    // Move the movie up by a third of its authored height, leaving equal visual
    // spacing between the completed label, movie and Refight controls.
    guest_set_vec2(c, 0x700123a550ull, rect, campaign_video_position[0],
                  campaign_video_position[1] + (replay ? campaign_video_height / 3.f : 0));
}

static bool campaign_can_replay(Cpu *c, uint64_t battle) {
    if (!battle || !LD16(battle + 0xa0)) return false;
    unsigned type = LD32(battle + 0x20);
    // Once the Gates are cleared, the first six map bosses use their
    // intermission battles (8), rather than the original bodyguard battles (2).
    // The Gates themselves use FightFinal (19) or FightFinalReplayable (20).
    switch (type) {
    case 2: case 8: case 11: case 19: case 20: case 22:
        return guest_invoke2(c, 0x7000efe380ull, battle, 0) == 1;
    default:
        return false;
    }
}

static void campaign_show_replay(Cpu *c, uint64_t info, uint64_t battle) {
    bool replay = campaign_can_replay(c, battle);
    campaign_movie_layout(c, info, replay);
    uint64_t button = LD64(info + 0x40), label = button ? LD64(button + 0x120) : 0;
    if (!button || !label) return;
    guest_invoke2(c, 0x70011cb2f0ull, button, replay ? 2 : 0); // X for Refight; normal fights keep A.
    if (!replay) {
        if (label == campaign_refight_label)
            guest_invoke2(c, 0x7000e72180ull, label, campaign_label_font_size);
        return;
    }
    if (label != campaign_refight_label) {
        campaign_refight_label = label;
        campaign_label_font_size = guest_invoke2(c, 0x7000e73cd0ull, label, 0);
    }
    guest_invoke2(c, 0x7000e73b90ull, label, 1); // literal text, no localization alias
    guest_invoke2(c, 0x7000e72180ull, label, 90); // leave vertical padding inside the authored button
    guest_invoke2(c, 0x70011cbb80ull, button, guest_string(c, "REFIGHT"));
    guest_invoke2(c, 0x70005d8310ull, button, 1);
    guest_invoke2(c, 0x7001226480ull,
                  guest_invoke2(c, 0x700121e720ull, button, 0), 1);
    // Completed content is normally the last sibling and overlaps the hidden
    // fight button's hit area. Refight must receive its own pointer clicks.
    guest_invoke2(c, 0x70015d9ee0ull,
                  guest_invoke2(c, 0x700121e6b0ull, button, 0), 0);
}

static void campaign_practice_statistics(uint64_t result, uint64_t stack) {
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t combo = LD64(stack + (i ? 0x28 : 0x18));
        ST64(result + 0x38 + i * 8, combo);
        uint64_t prize = combo ? LD64(combo + 0x40) : 0;
        // EndFightContent reads the combat statistics directly. Keep its
        // required objects, but display zero money for every combo category.
        if (prize) memset((void *)(uintptr_t)(prize + 0x10), 0, 0x60);
    }
}

static int campaign_replay_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000e7d4e0ull: { // InfoBattle.UpdateBattleInfo, including completed content
        if (campaign_info_updating) return 0;
        uint64_t info = c->x[0], battle = c->x[1];
        campaign_info_updating = true;
        guest_invoke2(c, site, info, battle);
        campaign_info_updating = false;
        campaign_show_replay(c, info, battle);
        return 1;
    }
    case 0x7000e7e7c0ull: { // The existing button handles mouse, touch and mapped X.
        uint64_t info = c->x[0];
        uint64_t battle = guest_invoke2(c, 0x7000e7e000ull, info, 0);
        if (!campaign_can_replay(c, battle)) return 0;
        uint64_t fight = guest_invoke2(c, 0x7000d448f0ull, 0, battle);
        if (!fight) return 1;
        campaign_replay_fight = fight;
        LOG("[campaign] starting practice type=%u fight=%u; save unchanged\n",
            LD32(battle + 0x20), LD32(fight + 0x50));
        // Keep the normal gong, loading and fight construction. The hook below
        // jumps over roster history, energy, time stamps, quests and Save.
        Cpu saved = *c;
        c->x[0] = 0; c->x[1] = fight; c->x[2] = 0; c->x[3] = battle;
        c->x[4] = 1; c->x[5] = 0; c->x[6] = 0;
        guest_call(c, 0x7000d48820ull);
        bool started = c->x[0] != 0;
        *c = saved;
        if (!started) campaign_replay_fight = 0;
        return 1;
    }
    case 0x7000d48820ull: // Retry must keep using the selected practice fight.
        if (campaign_replay_fight) c->x[1] = campaign_replay_fight;
        return 0;
    case 0x7000d48870ull:
        return campaign_replay_fight && c->x[19] == campaign_replay_fight ? 2 : 0;
    case 0x7000d47398ull:
        if (!campaign_replay_fight || c->x[20] != campaign_replay_fight) return 0;
        // FightResult is initialized, with winner/loser and an empty prize.
        // Go straight to the real results UI, skipping reward calculation,
        // CompleteFight, quest actions, statistics and save transactions.
        campaign_practice_statistics(c->x[19], c->sp);
        LOG("[campaign] practice result=%u; no rewards or progression\n", LD32(c->x[19] + 0x10));
        return 2;
    case 0x7000ca8a78ull:
        return campaign_replay_fight ? 2 : 0; // skip end-of-match achievements
    case 0x7000c977a0ull: // counters during combat must not survive in memory
    case 0x7000c99430ull:
    case 0x7000c995f0ull:
        return campaign_replay_fight ? 1 : 0;
    case 0x7000fc3fd0ull: // include periodic/backup saves while practicing
    case 0x7000fc32b0ull:
    case 0x7000fc55c0ull: // virtual SaveToFile can also be called directly
    case 0x7000fdf620ull:
    case 0x7000f5d8c0ull:
        return campaign_replay_fight ? 1 : 0;
    case 0x7000e8e750ull: // clear after the fight has been freed, on destination init
    case 0x7000d9c7c0ull:
    case 0x7000e6c600ull:
        campaign_replay_fight = 0;
        campaign_layout_info = campaign_video_rect = 0;
        campaign_refight_label = 0;
        return -1; // other UI hooks still need these scene callbacks
    default: return -1;
    }
}

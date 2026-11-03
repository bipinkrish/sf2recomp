#include "../constants/hle.h"
// Campaign practice uses the authored FightList, without reopening its roster
// entry. Hooks skip the native start/result transactions, not just disk writes:
// otherwise practice rewards and counters would leak into the next normal save.
static uint64_t campaign_replay_fight;
static bool campaign_info_updating;
static uint64_t campaign_layout_info, campaign_video_rect;
static float campaign_video_position[SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473], campaign_video_height;
static uint64_t campaign_refight_label;
static unsigned campaign_label_font_size;

RT_ON_RESET(reset_campaign_replay) {
    campaign_replay_fight = 0;
    campaign_info_updating = false;
    campaign_layout_info = campaign_video_rect = 0;
    campaign_refight_label = 0;
}

static void campaign_vec2(Cpu *c, uint64_t method, uint64_t object, float out[SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473]) {
    Cpu saved = *c;
    c->x[0] = object; c->x[1] = 0;
    guest_call(c, method);
    memcpy(out, &c->v[0], SF2C_CAMPAIGN_REPLAY_HOOKS_N_4_4B22); memcpy(out + 1, &c->v[1], SF2C_CAMPAIGN_REPLAY_HOOKS_N_4_4B22);
    *c = saved;
}

static void campaign_movie_layout(Cpu *c, uint64_t info, bool replay) {
    uint64_t content = LD64(info + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X88_AC70), video = content ? LD64(content + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X30_8BAD) : 0;
    if (!video) return;
    uint64_t rect = guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700121E6B0ULL_59E6, video, 0);
    if (info != campaign_layout_info || rect != campaign_video_rect) {
        campaign_layout_info = info; campaign_video_rect = rect;
        campaign_vec2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700123A410ULL_52BF, rect, campaign_video_position);
        float size[SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473]; campaign_vec2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700123A6D0ULL_19AD, rect, size);
        campaign_video_height = size[1];
    }
    // Move the movie up by a third of its authored height, leaving equal visual
    // spacing between the completed label, movie and Refight controls.
    guest_set_vec2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700123A550ULL_8625, rect, campaign_video_position[0],
                  campaign_video_position[1] + (replay ? campaign_video_height / SF2C_CAMPAIGN_REPLAY_HOOKS_N_3_F_002F : 0));
}

static bool campaign_can_replay(Cpu *c, uint64_t battle) {
    if (!battle || !LD16(battle + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0XA0_3862)) return false;
    unsigned type = LD32(battle + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X20_A766);
    // Once the Gates are cleared, the first six map bosses use their
    // intermission battles (8), rather than the original bodyguard battles (2).
    // The Gates themselves use FightFinal (19) or FightFinalReplayable (20).
    switch (type) {
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473: case SF2C_CAMPAIGN_REPLAY_HOOKS_N_8_2C62: case SF2C_CAMPAIGN_REPLAY_HOOKS_N_11_4FC8: case SF2C_CAMPAIGN_REPLAY_HOOKS_N_19_9400: case SF2C_CAMPAIGN_REPLAY_HOOKS_N_20_F5CA: case SF2C_CAMPAIGN_REPLAY_HOOKS_N_22_785F:
        return guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000EFE380ULL_94F6, battle, 0) == 1;
    default:
        return false;
    }
}

static void campaign_show_replay(Cpu *c, uint64_t info, uint64_t battle) {
    bool replay = campaign_can_replay(c, battle);
    campaign_movie_layout(c, info, replay);
    uint64_t button = LD64(info + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X40_1AF3), label = button ? LD64(button + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X120_50E1) : 0;
    if (!button || !label) return;
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X70011CB2F0ULL_466B, button, replay ? SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473 : 0); // X for Refight; normal fights keep A.
    if (!replay) {
        if (label == campaign_refight_label)
            guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E72180ULL_A6E0, label, campaign_label_font_size);
        return;
    }
    if (label != campaign_refight_label) {
        campaign_refight_label = label;
        campaign_label_font_size = guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E73CD0ULL_BC87, label, 0);
    }
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E73B90ULL_3B3B, label, 1); // literal text, no localization alias
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E72180ULL_A6E0, label, SF2C_CAMPAIGN_REPLAY_HOOKS_N_90_69F5); // leave vertical padding inside the authored button
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X70011CBB80ULL_C4C0, button, guest_string(c, SF2C_CAMPAIGN_REPLAY_HOOKS_S_REFIGHT_32F8));
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X70005D8310ULL_7E78, button, 1);
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7001226480ULL_8D5A,
                  guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700121E720ULL_0F0E, button, 0), 1);
    // Completed content is normally the last sibling and overlaps the hidden
    // fight button's hit area. Refight must receive its own pointer clicks.
    guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X70015D9EE0ULL_E969,
                  guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X700121E6B0ULL_59E6, button, 0), 0);
}

static void campaign_practice_statistics(uint64_t result, uint64_t stack) {
    for (unsigned i = 0; i < SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473; ++i) {
        uint64_t combo = LD64(stack + (i ? SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X28_EAB9 : SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X18_D47C));
        ST64(result + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X38_AE4F + i * SF2C_CAMPAIGN_REPLAY_HOOKS_N_8_2C62, combo);
        uint64_t prize = combo ? LD64(combo + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X40_1AF3) : 0;
        // EndFightContent reads the combat statistics directly. Keep its
        // required objects, but display zero money for every combo category.
        if (prize) memset((void *)(uintptr_t)(prize + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X10_BF9C), 0, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X60_3D6E);
    }
}

static int campaign_replay_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E7D4E0ULL_75D7: { // InfoBattle.UpdateBattleInfo, including completed content
        if (campaign_info_updating) return 0;
        uint64_t info = c->x[0], battle = c->x[1];
        campaign_info_updating = true;
        guest_invoke2(c, site, info, battle);
        campaign_info_updating = false;
        campaign_show_replay(c, info, battle);
        return 1;
    }
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E7E7C0ULL_F052: { // The existing button handles mouse, touch and mapped X.
        uint64_t info = c->x[0];
        uint64_t battle = guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E7E000ULL_8EEB, info, 0);
        if (!campaign_can_replay(c, battle)) return 0;
        uint64_t fight = guest_invoke2(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D448F0ULL_D516, 0, battle);
        if (!fight) return 1;
        campaign_replay_fight = fight;
        LOG(SF2C_CAMPAIGN_REPLAY_HOOKS_S_CAMPAIGN_STARTING_PRACTICE_T_524E,
            LD32(battle + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X20_A766), LD32(fight + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X50_4171));
        // Keep the normal gong, loading and fight construction. The hook below
        // jumps over roster history, energy, time stamps, quests and Save.
        Cpu saved = *c;
        c->x[0] = 0; c->x[1] = fight; c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473] = 0; c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_3_4E07] = battle;
        c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_4_4B22] = 1; c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_5_EF2D] = 0; c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_6_E7F6] = 0;
        guest_call(c, SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D48820ULL_A9BD);
        bool started = c->x[0] != 0;
        *c = saved;
        if (!started) campaign_replay_fight = 0;
        return 1;
    }
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D48820ULL_A9BD: // Retry must keep using the selected practice fight.
        if (campaign_replay_fight) c->x[1] = campaign_replay_fight;
        return 0;
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D48870ULL_2263:
        return campaign_replay_fight && c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_19_9400] == campaign_replay_fight ? SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473 : 0;
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D47398ULL_74D0:
        if (!campaign_replay_fight || c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_20_F5CA] != campaign_replay_fight) return 0;
        // FightResult is initialized, with winner/loser and an empty prize.
        // Go straight to the real results UI, skipping reward calculation,
        // CompleteFight, quest actions, statistics and save transactions.
        campaign_practice_statistics(c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_19_9400], c->sp);
        LOG(SF2C_CAMPAIGN_REPLAY_HOOKS_S_CAMPAIGN_PRACTICE_RESULT_U_N_AA0A, LD32(c->x[SF2C_CAMPAIGN_REPLAY_HOOKS_N_19_9400] + SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X10_BF9C));
        return SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473;
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000CA8A78ULL_EEE2:
        return campaign_replay_fight ? SF2C_CAMPAIGN_REPLAY_HOOKS_N_2_D473 : 0; // skip end-of-match achievements
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000C977A0ULL_1616: // counters during combat must not survive in memory
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000C99430ULL_2E48:
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000C995F0ULL_AE12:
        return campaign_replay_fight ? 1 : 0;
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000FC3FD0ULL_187A: // include periodic/backup saves while practicing
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000FC32B0ULL_B67F:
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000FC55C0ULL_1444: // virtual SaveToFile can also be called directly
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000FDF620ULL_42B7:
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000F5D8C0ULL_7AF6:
        return campaign_replay_fight ? 1 : 0;
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E8E750ULL_BDE1: // clear after the fight has been freed, on destination init
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000D9C7C0ULL_C157:
    case SF2C_CAMPAIGN_REPLAY_HOOKS_N_0X7000E6C600ULL_7128:
        campaign_replay_fight = 0;
        campaign_layout_info = campaign_video_rect = 0;
        campaign_refight_label = 0;
        return -1; // other UI hooks still need these scene callbacks
    default: return -1;
    }
}

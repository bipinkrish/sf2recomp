#include "constants/core.h"
// SF2 update v131072: tick-indexed logical input and pointer-free state traces.
// These offsets are from the user's IL2CPP metadata. This is an observation
// harness, not a complete state serializer or proof of engine determinism.
#include "replay.h"
#include "lan.h"
#include "lan_random.h"
#include "hle.h"
#include "settings.h"
#include "ui/menu.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

enum { MAX_EVENTS = SF2C_REPLAY_N_65536_0F25, MAX_LIST = SF2C_REPLAY_N_4096_8B92 };
typedef struct { uint32_t tick, player, code, down; } ReplayEvent;
static ReplayEvent *events;
static size_t event_count, event_next, applied_count;
static FILE *trace, *record;
static bool playback, injecting, completed, in_step;
static FILE *hash_file;
static uint64_t combat_hash, identity_hash;
static LanEvent local_inputs[LAN_MAX_EVENTS];
static unsigned local_count;
static uint32_t lan_round, lan_stance_steps=UINT32_MAX;
static bool lan_started, lan_initializing;
static bool bind_fight(Cpu *c,uint64_t current);
static void snapshot(Cpu *c);
static uint64_t fight, players[SF2C_REPLAY_N_2_D473], start_ns;
static uint32_t tick, limit, frame_origin;
static uint16_t selection[SF2C_REPLAY_N_3_4E07][SF2C_REPLAY_N_1024_E39E];
static unsigned selection_length[SF2C_REPLAY_N_3_4E07];
static bool selected;
static bool fixed_match;
static uint32_t match_indices[SF2C_REPLAY_N_3_4E07];
static bool controlled;
static uint32_t seed;
// Guest combat runs on the update thread. Scopes must not leak into other
// guest threads (loading/audio), even while one wrapped guest call is active.
static _Thread_local unsigned simulation_depth;
static _Thread_local uint64_t wrapped, random_scope;
typedef struct { uint64_t key; uint32_t state, calls; } RandomStream;
static RandomStream streams[SF2C_REPLAY_N_1024_E39E];
static unsigned stream_count;
static _Thread_local bool draw_wrapped, constructor_wrapped, play_wrapped;
static uint64_t preparing;
static uint32_t warmup;
static _Thread_local bool transition_authorized;
extern uint64_t renderer_present_count(void);

static void fail(const char *why) {
    fprintf(stderr, SF2C_REPLAY_S_REPLAY_S_N_03CA, why);
    if(lan_enabled())lan_close();
    fflush(stderr);_Exit(SF2C_REPLAY_N_2_D473);
}
static uint32_t number(const char *s, uint32_t maximum) {
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, SF2C_REPLAY_N_10_4A44);
    if (!*s || *s == SF2C_REPLAY_CH_EMPTY_01F8 || *s == SF2C_REPLAY_CH_EMPTY_3604 || *end || errno || value > maximum) fail(SF2C_REPLAY_S_INVALID_NUMERIC_OPTION_9134);
    return (uint32_t)value;
}

static void replay_header(void);
void replay_init(void) {
    lan_init();
    const char *out = getenv(SF2C_REPLAY_S_SF2_REPLAY_TRACE_2CB0), *input = getenv(SF2C_REPLAY_S_SF2_REPLAY_INPUT_88E5);
    const char *rec = getenv(SF2C_REPLAY_S_SF2_REPLAY_RECORD_FBB2), *ticks = getenv(SF2C_REPLAY_S_SF2_REPLAY_TICKS_1950);
    const char *match = getenv(SF2C_REPLAY_S_SF2_REPLAY_MATCH_ABA9);
    const char *seed_option = getenv(SF2C_REPLAY_S_SF2_REPLAY_SEED_30B5);
    if (!out && !lan_enabled()) {
        if (input || rec || ticks || match || seed_option) fail(SF2C_REPLAY_S_SF2_REPLAY_TRACE_IS_REQUIRED_BA76);
        return;
    }
    if (input && rec) fail(SF2C_REPLAY_S_INPUT_AND_RECORD_ARE_MUTUALL_49CD);
    if (out && ((input && !strcmp(input, out)) || (rec && !strcmp(rec, out)))) fail(SF2C_REPLAY_S_TRACE_AND_INPUT_RECORD_PATHS_B715);
    limit = ticks ? number(ticks, SF2C_REPLAY_N_1000000_6CCE) : (lan_enabled() ? UINT32_MAX : SF2C_REPLAY_N_600_284B);
    if (!limit) fail(SF2C_REPLAY_S_SF2_REPLAY_TICKS_MUST_BE_POS_230F);
    controlled = seed_option != NULL;
    seed = seed_option ? number(seed_option, UINT32_MAX) : 0;
    if (match) {
        char copy[SF2C_REPLAY_N_64_A68B];
        if (strlen(match) >= sizeof copy) fail(SF2C_REPLAY_S_INVALID_FIXED_MATCH_INDICES_59B0);
        strcpy(copy,match);
        char *part=copy;
        for(unsigned i=0;i<SF2C_REPLAY_N_3_4E07;++i) {
            char *comma=strchr(part,SF2C_REPLAY_CH_EMPTY_EE1B);
            if ((i<SF2C_REPLAY_N_2_D473 && !comma) || (i==SF2C_REPLAY_N_2_D473 && comma)) fail(SF2C_REPLAY_S_MATCH_REQUIRES_CHARACTER_CHA_9327);
            if(comma)*comma=0;
            match_indices[i]=number(part,SF2C_REPLAY_N_255_9556);
            if(comma)part=comma+1;
        }
        fixed_match=true;
    }
    if(lan_enabled()) {
        if(seed_option || match || rec)fail(SF2C_REPLAY_S_LAN_CHOOSES_INITIALIZATION_R_B1F1);
        const LanConfig *cfg=lan_config();controlled=true;fixed_match=true;seed=cfg->seed;limit=cfg->limit;
        memcpy(match_indices,cfg->match,sizeof match_indices);
    }
    if (input) {
        FILE *f = fopen(input, SF2C_REPLAY_S_R_2659);
        if (!f) fail(SF2C_REPLAY_S_CANNOT_OPEN_INPUT_TAPE_0566);
        char line[SF2C_REPLAY_N_256_51E8];
        if (!fgets(line, sizeof line, f) || strcmp(line, SF2C_REPLAY_S_SF2_REPLAY_1_N_2DC7)) fail(SF2C_REPLAY_S_INVALID_INPUT_TAPE_HEADER_2E89);
        events = calloc(MAX_EVENTS, sizeof *events);
        if (!events) fail(SF2C_REPLAY_S_CANNOT_ALLOCATE_INPUT_TAPE_B5F9);
        unsigned lineno = 1;
        while (fgets(line, sizeof line, f)) {
            ++lineno;
            if (line[0] == SF2C_REPLAY_CH_EMPTY_4734 || line[0] == SF2C_REPLAY_CH_N_AD65) continue;
            char *fields[SF2C_REPLAY_N_5_EF2D] = {0};
            unsigned count = 0;
            for (char *token = strtok(line, SF2C_REPLAY_S_T_R_N_6308); token && count < SF2C_REPLAY_N_5_EF2D; token = strtok(NULL, SF2C_REPLAY_S_T_R_N_6308)) fields[count++] = token;
            if (count != SF2C_REPLAY_N_4_4B22) fail(SF2C_REPLAY_S_INPUT_TAPE_REQUIRES_FOUR_UNS_40A1);
            unsigned t=number(fields[0],SF2C_REPLAY_N_1000000_6CCE), p=number(fields[1],SF2C_REPLAY_N_2_D473);
            unsigned code=number(fields[SF2C_REPLAY_N_2_D473],SF2C_REPLAY_N_14_8527), down=number(fields[SF2C_REPLAY_N_3_4E07],1);
            if (
                t >= limit || p < 1 || p > SF2C_REPLAY_N_2_D473 || code > SF2C_REPLAY_N_14_8527 || down > 1 ||
                event_count == MAX_EVENTS || (event_count && t < events[event_count - 1].tick)) {
                fprintf(stderr, SF2C_REPLAY_S_REPLAY_INVALID_INPUT_TAPE_LI_326F, lineno);
                exit(SF2C_REPLAY_N_2_D473);
            }
            events[event_count++] = (ReplayEvent){t, p - 1, code, down};
        }
        if (ferror(f)) fail(SF2C_REPLAY_S_CANNOT_READ_INPUT_TAPE_D48C);
        fclose(f);
        playback = true;
    }
    if(lan_enabled()) {
        hash_file=tmpfile();if(!hash_file)fail(SF2C_REPLAY_S_CANNOT_CREATE_BOUNDED_STATE__7BA0);
    }
    trace = out ? fopen(out, SF2C_REPLAY_S_WX_6B8F) : hash_file;  // never truncate previous evidence or an input tape
    if (!trace) fail(SF2C_REPLAY_S_CANNOT_CREATE_TRACE_OUTPUT_M_F6E9);
    if (rec) {
        record = fopen(rec, SF2C_REPLAY_S_WX_6B8F);
        if (!record) fail(SF2C_REPLAY_S_CANNOT_CREATE_RECORD_TAPE_OU_7DAC);
        fputs(SF2C_REPLAY_S_SF2_REPLAY_1_N_2DC7, record);
        fflush(record);
    }
    replay_header();
    LOG(SF2C_REPLAY_S_REPLAY_S_U_COMBAT_STEPS_S_N_243C, playback ? SF2C_REPLAY_S_PLAYBACK_67D7 : SF2C_REPLAY_S_OBSERVE_RECORD_8C4E, limit,
        controlled ? SF2C_REPLAY_S_CONTROLLED_INITIALIZATION_AN_2FFD : SF2C_REPLAY_S_ORIGINAL_RANDOM_TIMING_BEHAV_87F9);
}
static void replay_header(void) {
    fprintf(trace, SF2C_REPLAY_S_KIND_HEADER_SCHEMA_1_GAME_UP_5A81
            SF2C_REPLAY_S_LIMIT_U_PLAYBACK_S_EVENTS_ZU_1BDB
            SF2C_REPLAY_S_RESOLUTION_D_UPSCALER_D_AA_D_7D52
            SF2C_REPLAY_S_RNG_POLICY_S_SEED_U_INITIALI_55BA
            SF2C_REPLAY_S_COMBAT_GEOMETRY_PVP_FRAMING__D175,
            limit, playback ? SF2C_REPLAY_S_TRUE_18D1 : SF2C_REPLAY_S_FALSE_076D, event_count, g_settings.target_fps, g_settings.aspect,
            settings_resolution(), settings_upscaler(), settings_antialias(), g_settings.ai_enabled ? SF2C_REPLAY_S_TRUE_18D1 : SF2C_REPLAY_S_FALSE_076D,
            controlled ? SF2C_REPLAY_S_SCOPED_CALLSITE_LCG_V2_14CF : SF2C_REPLAY_S_ORIGINAL_UNMODIFIED_952E, seed,
            lan_enabled()?SF2C_REPLAY_S_LAN_TWO_HUMAN_NATIVE_STANCE__5D80:controlled ? SF2C_REPLAY_S_NATIVE_ROUND_STANCE_240_STEP_C958 : SF2C_REPLAY_S_ORIGINAL_4D60, fixed_match ? SF2C_REPLAY_S_TRUE_18D1 : SF2C_REPLAY_S_FALSE_076D,lan_enabled()?SF2C_REPLAY_S_TRUE_18D1:SF2C_REPLAY_S_FALSE_076D);
    fflush(trace);
}
bool replay_playback_enabled(void) { return playback; }
bool replay_enabled(void) { return trace != NULL; }

static uint64_t ptr(uint64_t object, unsigned offset) { return object ? LD64(object + offset) : 0; }
static uint32_t word(uint64_t object, unsigned offset) { return object ? LD32(object + offset) : 0; }
static uint32_t byte(uint64_t object, unsigned offset) { return object ? LD8(object + offset) : 0; }
static uint32_t random_value(uint64_t caller, unsigned family) {
    // Address identifies code, never a process-specific allocation. Ordered
    // insertion makes traces independent of the order streams are first used.
    uint64_t key = ((uint64_t)family << SF2C_REPLAY_N_56_7688) | (caller & SF2C_REPLAY_N_0XFFFFFFFFU_7BAF);
    unsigned i=0;
    while(i<stream_count && streams[i].key<key) ++i;
    if(i==stream_count || streams[i].key!=key) {
        if(stream_count==SF2C_REPLAY_N_1024_E39E) fail(SF2C_REPLAY_S_TOO_MANY_SCOPED_RANDOM_STREA_6AA7);
        memmove(streams+i+1,streams+i,(stream_count-i)*sizeof *streams);
        // Nearby call sites must not start linearly related LCG streams.
        // Mix the shared seed and the full scope key; retain the native LCG.
        uint32_t mixed=(uint32_t)lan_random_mix(((uint64_t)seed<<SF2C_REPLAY_N_32_E29C)^key^SF2C_REPLAY_N_UINT64_C_0X534632524E475632_418C)&SF2C_REPLAY_N_0X7FFFFFFFU_7A69;
        streams[i]=(RandomStream){key,mixed,0};++stream_count;
    }
    RandomStream *s=&streams[i];
    s->state=(s->state*SF2C_REPLAY_N_0X41C64E6DU_FE1E+SF2C_REPLAY_N_0X3039U_2AC9)&SF2C_REPLAY_N_0X7FFFFFFFU_7A69;
    ++s->calls;
    return s->state;
}
static void transition(Cpu *c, uint64_t object, uint64_t method) {
    Cpu saved=*c;
    transition_authorized=true;
    ++simulation_depth;
    c->x[0]=object;c->x[1]=0;guest_call(c,method);
    --simulation_depth;
    transition_authorized=false;
    *c=saved;
}
static void lan_human_fighters(uint64_t current) {
    // Native PvP normally makes the solo opponent a non-user dummy when AI is
    // disabled. Both LAN roles are humans, with their own logical controller.
    for(unsigned i=0;i<SF2C_REPLAY_N_2_D473;++i) {
        uint64_t model=ptr(current,i?SF2C_REPLAY_N_0XC0_4E7B:SF2C_REPLAY_N_0XB8_8F73),parameters=ptr(model,SF2C_REPLAY_N_0X140_A49E);
        if(!parameters)fail(SF2C_REPLAY_S_LAN_FIGHTER_PARAMETERS_ARE_M_EDA9);
        ST8(parameters+SF2C_REPLAY_N_0X62_88BC,0);ST8(parameters+SF2C_REPLAY_N_0X63_A6C6,1);
    }
}
static int controlled_hook(Cpu *c, uint64_t site) {
    if(!controlled || !selected) return -1;
    if(site==SF2C_REPLAY_N_0X7000CA7E80ULL_C4A9 && word(ptr(c->x[0],SF2C_REPLAY_N_0X30_8BAD),SF2C_REPLAY_N_0X18_D47C)==SF2C_REPLAY_N_18_4EC9) return 1;
    if((site==SF2C_REPLAY_N_0X7000CA6DF0ULL_A60A || site==SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6) && !transition_authorized &&
       word(ptr(c->x[0],SF2C_REPLAY_N_0X30_8BAD),SF2C_REPLAY_N_0X18_D47C)==SF2C_REPLAY_N_18_4EC9) return 1;
    if(site==SF2C_REPLAY_N_0X7000C9DD50ULL_1622 && !draw_wrapped && word(ptr(c->x[0],SF2C_REPLAY_N_0X30_8BAD),SF2C_REPLAY_N_0X18_D47C)==SF2C_REPLAY_N_18_4EC9) {
        if(!preparing) {
            // FightHolder can draw before PreFight.InitPreFight initializes
            // ViewerFight.Round. NextRound/ViewerFight.Reset require it.
            // UI loading proceeds on Unity Update while combat stays at birth.
            uint64_t viewer=ptr(ptr(c->x[0],SF2C_REPLAY_N_0X1A0_98D8),SF2C_REPLAY_N_0X80_D41C);
            if(!ptr(viewer,SF2C_REPLAY_N_0X70_B0F6)) return 1;
            preparing=c->x[0];
            if(word(preparing,SF2C_REPLAY_N_0X24_EB21) || word(ptr(preparing,SF2C_REPLAY_N_0X60_3D6E),SF2C_REPLAY_N_0X18_D47C))
                fail(SF2C_REPLAY_S_CONTROLLED_INITIALIZATION_MI_01A7);
            if(lan_enabled())lan_human_fighters(preparing);
            transition(c,preparing,SF2C_REPLAY_N_0X7000CA7100ULL_DA44); // original NextRound
            if(lan_enabled())lan_human_fighters(preparing);
            transition(c,preparing,SF2C_REPLAY_N_0X7000CA6DF0ULL_A60A); // original StartStance
            LOG(SF2C_REPLAY_S_REPLAY_INITIALIZED_NATIVE_RO_40D0);
        }
        if(lan_enabled() && warmup==SF2C_REPLAY_N_240_6AF1) {
            if(!fight) {
                transition(c,c->x[0],SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6);
                if(!bind_fight(c,c->x[0]))fail(SF2C_REPLAY_S_LAN_COMBAT_COULD_NOT_INITIAL_7E4B);
            }
            if(!lan_ready_poll(combat_hash))return 1;
            if(!lan_started) {
                lan_started=true;start_ns=plat_now_ns();snapshot(c);
                LOG(SF2C_REPLAY_S_LAN_READY_BARRIER_PASSED_AT__60E6 PRIu64 SF2C_REPLAY_S_N_D7A5,renderer_present_count());
            }
        }
        draw_wrapped=true;++simulation_depth;
        guest_call(c,site);
        --simulation_depth;draw_wrapped=false;
        return 1;
    }
    if((site==SF2C_REPLAY_N_0X7000C99E30ULL_08A4 && !constructor_wrapped) || (site==SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6 && !play_wrapped)) {
        bool *guard=site==SF2C_REPLAY_N_0X7000C99E30ULL_08A4 ? &constructor_wrapped : &play_wrapped;
        *guard=true;++simulation_depth;guest_call(c,site);--simulation_depth;*guard=false;
        return 1;
    }
    if(!simulation_depth) return -1;
    if(site==SF2C_REPLAY_N_0X7001177D10ULL_B365) {
        ST32(c->x[0]+SF2C_REPLAY_N_0X10_BF9C,seed);ST32(c->x[0]+SF2C_REPLAY_N_0X14_F942,seed);
        return 1;
    }
    if(site==SF2C_REPLAY_N_0X7001177D20ULL_6E12) {
        uint32_t value=random_value(random_scope ? random_scope : c->x[SF2C_REPLAY_N_30_624B],1);
        ST32(c->x[0]+SF2C_REPLAY_N_0X10_BF9C,value);c->x[0]=value;
        return 1;
    }
    if(site==SF2C_REPLAY_N_0X7001237BC0ULL_48FE) {
        int64_t lo=(int32_t)c->x[1],hi=(int32_t)c->x[SF2C_REPLAY_N_2_D473];
        if(lo>hi) { int64_t tmp=lo;lo=hi;hi=tmp; }
        uint32_t r=random_value(c->x[SF2C_REPLAY_N_30_624B],SF2C_REPLAY_N_2_D473);
        c->x[0]=(uint32_t)(lo+((uint64_t)r*(uint64_t)(hi-lo)>>SF2C_REPLAY_N_31_EB1E));
        return 1;
    }
    if(site==SF2C_REPLAY_N_0X7001237B50ULL_DDBF) {
        float lo,hi;memcpy(&lo,&c->v[0],SF2C_REPLAY_N_4_4B22);memcpy(&hi,&c->v[1],SF2C_REPLAY_N_4_4B22);
        if(!isfinite(lo)||!isfinite(hi)) fail(SF2C_REPLAY_S_NONFINITE_CONTROLLED_UNITY_R_0847);
        float fraction=(random_value(c->x[SF2C_REPLAY_N_30_624B],SF2C_REPLAY_N_3_4E07)&SF2C_REPLAY_N_0XFFFFFFU_0F78)/SF2C_REPLAY_N_16777215_0F_EA68;
        volatile float delta=hi-lo, scaled=delta*fraction;
        float value=lo+scaled;uint32_t bits;memcpy(&bits,&value,SF2C_REPLAY_N_4_4B22);c->v[0]=bits;
        return 1;
    }
    // Preserve Nekki's native float/int/chance distribution, including its
    // two-draw precision. Only replace the LCG source inside the original call.
    if((site==SF2C_REPLAY_N_0X70010ED6A0ULL_3FF9 || (site>=SF2C_REPLAY_N_0X70010F1450ULL_9A8B && site<=SF2C_REPLAY_N_0X70010F1F90ULL_C00E) || site==SF2C_REPLAY_N_0X70010F2DE0ULL_E9D4)
       && site!=wrapped && !random_scope) {
        uint64_t saved_wrapped=wrapped;
        wrapped=site;random_scope=c->x[SF2C_REPLAY_N_30_624B];
        guest_call(c,site);
        random_scope=0;wrapped=saved_wrapped;
        return 1;
    }
    return -1;
}
static unsigned list_size(uint64_t list) {
    unsigned n = word(list, SF2C_REPLAY_N_0X18_D47C);
    if (n > MAX_LIST || (n && !ptr(list, SF2C_REPLAY_N_0X10_BF9C))) fail(SF2C_REPLAY_S_INVALID_OVERSIZED_GUEST_LIST_21B6);
    return n;
}
static uint64_t item(uint64_t list, unsigned i) { return LD64(ptr(list, SF2C_REPLAY_N_0X10_BF9C) + SF2C_REPLAY_N_0X20_A766 + i * SF2C_REPLAY_N_8_2C62); }
static void string(uint64_t s) {
    fputc(SF2C_REPLAY_S_EMPTY_3251, trace);
    unsigned n = word(s, SF2C_REPLAY_N_0X10_BF9C);
    if (n > SF2C_REPLAY_N_1024_E39E) fail(SF2C_REPLAY_S_INVALID_GUEST_STRING_IN_COMB_0E00);
    for (unsigned i = 0; i < n; ++i) {
        unsigned ch = LD16(s + SF2C_REPLAY_N_0X14_F942 + i * SF2C_REPLAY_N_2_D473);
        if (ch >= SF2C_REPLAY_N_32_E29C && ch < SF2C_REPLAY_N_127_922C && ch != SF2C_REPLAY_S_EMPTY_3251 && ch != SF2C_REPLAY_CH_EMPTY_6672) fputc((int)ch, trace);
        else fprintf(trace, SF2C_REPLAY_S_U_04X_B175, ch);
    }
    fputc(SF2C_REPLAY_S_EMPTY_3251, trace);
}
static void selection_string(unsigned slot) {
    fputc(SF2C_REPLAY_S_EMPTY_3251,trace);
    for(unsigned i=0;i<selection_length[slot];++i) {
        unsigned ch=selection[slot][i];
        if(ch>=SF2C_REPLAY_N_32_E29C && ch<SF2C_REPLAY_N_127_922C && ch!=SF2C_REPLAY_S_EMPTY_3251 && ch!=SF2C_REPLAY_CH_EMPTY_6672) fputc((int)ch,trace);
        else fprintf(trace,SF2C_REPLAY_S_U_04X_B175,ch);
    }
    fputc(SF2C_REPLAY_S_EMPTY_3251,trace);
}
static uint32_t invoke_float_bits(Cpu *c, uint64_t fn, uint64_t object) {
    Cpu saved = *c;
    c->x[0] = object; c->x[1] = 0;
    guest_call(c, fn);
    uint32_t bits;
    memcpy(&bits, &c->v[0], sizeof bits);
    *c = saved;
    return bits;
}
static void words(uint64_t object, const unsigned *offsets, unsigned count) {
    fputc(SF2C_REPLAY_CH_EMPTY_854D, trace);
    for (unsigned i = 0; i < count; ++i) fprintf(trace, SF2C_REPLAY_S_S_U_35E0, i ? SF2C_REPLAY_S_EMPTY_08B5 : SF2C_REPLAY_S_EMPTY_12AE, word(object, offsets[i]));
    fputc(SF2C_REPLAY_CH_EMPTY_CB80, trace);
}
static void int_list(uint64_t list) {
    unsigned n = list_size(list);
    fputc(SF2C_REPLAY_CH_EMPTY_854D, trace);
    for (unsigned i = 0; i < n; ++i) fprintf(trace, SF2C_REPLAY_S_S_U_35E0, i ? SF2C_REPLAY_S_EMPTY_08B5 : SF2C_REPLAY_S_EMPTY_12AE, LD32(ptr(list, SF2C_REPLAY_N_0X10_BF9C) + SF2C_REPLAY_N_0X20_A766 + i * SF2C_REPLAY_N_4_4B22));
    fputc(SF2C_REPLAY_CH_EMPTY_CB80, trace);
}
static void vector(uint64_t v) {
    const unsigned offsets[] = {SF2C_REPLAY_N_0X10_BF9C, SF2C_REPLAY_N_0X14_F942, SF2C_REPLAY_N_0X18_D47C};
    words(v, offsets, SF2C_REPLAY_N_3_4E07);  // float bit patterns: exact comparison, including NaNs
}
static void equipment(uint64_t params) {
    fputc(SF2C_REPLAY_CH_EMPTY_854D,trace);
    for(unsigned i=0;i<SF2C_REPLAY_N_7_7902;++i) {
        if(i)fputc(SF2C_REPLAY_CH_EMPTY_EE1B,trace);
        string(ptr(ptr(params,SF2C_REPLAY_N_0XA0_3862+i*SF2C_REPLAY_N_8_2C62),SF2C_REPLAY_N_0X10_BF9C));
    }
    fputc(SF2C_REPLAY_CH_EMPTY_CB80,trace);
}
static void model_state(Cpu *c, uint64_t m) {
    uint64_t params = ptr(m, SF2C_REPLAY_N_0X140_A49E), animation = ptr(m, SF2C_REPLAY_N_0X210_7579), controller = ptr(m, SF2C_REPLAY_N_0X1D0_216A);
    fprintf(trace, SF2C_REPLAY_S_NAME_0B4D); string(ptr(m, SF2C_REPLAY_N_0X1B8_642F));
    fputs(SF2C_REPLAY_S_EQUIPMENT_F74C,trace);equipment(params);
    fprintf(trace, SF2C_REPLAY_S_HEALTH_BITS_U_USER_U_AI_U_MO_654E,
            invoke_float_bits(c, SF2C_REPLAY_N_0X7000FFBF90ULL_F3D5, m), byte(params, SF2C_REPLAY_N_0X63_A6C6), byte(params, SF2C_REPLAY_N_0X62_88BC));
    const unsigned model_words[] = {SF2C_REPLAY_N_0X118_23BF,SF2C_REPLAY_N_0X130_CCDF,SF2C_REPLAY_N_0X148_30C9,SF2C_REPLAY_N_0X15C_13EF,SF2C_REPLAY_N_0X164_974D,SF2C_REPLAY_N_0X168_35C9,SF2C_REPLAY_N_0X170_9B54,SF2C_REPLAY_N_0X174_7CDD,SF2C_REPLAY_N_0X190_6770,SF2C_REPLAY_N_0X194_CE4B,SF2C_REPLAY_N_0X198_4F92,SF2C_REPLAY_N_0X1E0_10BA,SF2C_REPLAY_N_0X228_4737,SF2C_REPLAY_N_0X230_E5E9,SF2C_REPLAY_N_0X234_5C29,SF2C_REPLAY_N_0X238_FF98,SF2C_REPLAY_N_0X23C_5533,SF2C_REPLAY_N_0X284_1C6F};
    words(m, model_words, sizeof model_words / sizeof *model_words);
    fprintf(trace, SF2C_REPLAY_S_FLAGS_U_U_U_U_U_U_U_ANIMATIO_B764,
            byte(m,SF2C_REPLAY_N_0X158_924E),byte(m,SF2C_REPLAY_N_0X159_54D8),byte(m,SF2C_REPLAY_N_0X160_8A14),byte(m,SF2C_REPLAY_N_0X161_6CA8),byte(m,SF2C_REPLAY_N_0X22C_56FC),byte(m,SF2C_REPLAY_N_0X22D_7853),byte(m,SF2C_REPLAY_N_0X241_C8E9));
    const unsigned anim_words[] = {SF2C_REPLAY_N_0X34_806E,SF2C_REPLAY_N_0X38_AE4F,SF2C_REPLAY_N_0X48_0EF0,SF2C_REPLAY_N_0X4C_C1BA,SF2C_REPLAY_N_0X50_4171,SF2C_REPLAY_N_0X54_9433,SF2C_REPLAY_N_0X58_08FB,SF2C_REPLAY_N_0X5C_F9CC,SF2C_REPLAY_N_0X60_3D6E,SF2C_REPLAY_N_0X64_3246,SF2C_REPLAY_N_0X6C_3CD4,SF2C_REPLAY_N_0X70_B0F6,SF2C_REPLAY_N_0X74_D298,SF2C_REPLAY_N_0X78_756D,SF2C_REPLAY_N_0X80_D41C,SF2C_REPLAY_N_0XA8_D409,SF2C_REPLAY_N_0XAC_3360,SF2C_REPLAY_N_0XB0_D785,SF2C_REPLAY_N_0XB4_F215,SF2C_REPLAY_N_0XC0_4E7B,SF2C_REPLAY_N_0XC4_EDF1,SF2C_REPLAY_N_0XD8_8DD9,SF2C_REPLAY_N_0XDC_2690};
    words(animation, anim_words, sizeof anim_words / sizeof *anim_words);
    fputs(SF2C_REPLAY_S_ANIMATION_NAME_5C41,trace);string(ptr(ptr(animation,SF2C_REPLAY_N_0XF0_2A69),SF2C_REPLAY_N_0X40_1AF3));
    fprintf(trace, SF2C_REPLAY_S_SHIFT_A88D); vector(ptr(animation, SF2C_REPLAY_N_0X88_AC70));
    fprintf(trace, SF2C_REPLAY_S_VELOCITY_5137); vector(ptr(animation, SF2C_REPLAY_N_0X90_4255));
    fprintf(trace, SF2C_REPLAY_S_CONTROLLER_U_U_KEYS_444D, word(controller,SF2C_REPLAY_N_0X20_A766),word(controller,SF2C_REPLAY_N_0X38_AE4F));
    uint64_t keys = ptr(controller, SF2C_REPLAY_N_0X18_D47C);
    unsigned n = list_size(keys);
    for (unsigned i=0;i<n;++i) {
        uint64_t key = item(keys,i);
        fprintf(trace, SF2C_REPLAY_S_S_U_U_U_746F, i ? SF2C_REPLAY_S_EMPTY_08B5 : SF2C_REPLAY_S_EMPTY_12AE,word(key,SF2C_REPLAY_N_0X10_BF9C),word(key,SF2C_REPLAY_N_0X14_F942),byte(key,SF2C_REPLAY_N_0X18_D47C));
    }
    fprintf(trace, SF2C_REPLAY_S_KEY_DATA_9DAC);
    uint64_t data = ptr(controller, SF2C_REPLAY_N_0X28_EAB9);
    for (unsigned i=0;i<SF2C_REPLAY_N_3_4E07;++i) { if (i) fputc(SF2C_REPLAY_CH_EMPTY_EE1B,trace); int_list(ptr(data,SF2C_REPLAY_N_0X10_BF9C+i*SF2C_REPLAY_N_8_2C62)); }
    fprintf(trace, SF2C_REPLAY_S_NODES_826F);
    uint64_t object = ptr(m,SF2C_REPLAY_N_0X1C8_EAA2), nodes = ptr(ptr(object,SF2C_REPLAY_N_0X10_BF9C),SF2C_REPLAY_N_0X28_EAB9);
    n=list_size(nodes);
    for (unsigned i=0;i<n;++i) {
        uint64_t node=item(nodes,i);
        fprintf(trace, SF2C_REPLAY_S_S_ID_U_CURRENT_EB9E,i ? SF2C_REPLAY_S_EMPTY_08B5 : SF2C_REPLAY_S_EMPTY_12AE,word(node,SF2C_REPLAY_N_0X3C_4150));vector(ptr(node,SF2C_REPLAY_N_0X20_A766));
        fputs(SF2C_REPLAY_S_PREVIOUS_FB21,trace);vector(ptr(node,SF2C_REPLAY_N_0X28_EAB9));fputc(SF2C_REPLAY_CH_EMPTY_9AC4,trace);
    }
    fprintf(trace, SF2C_REPLAY_S_PHYSICS_FRAMES_U_9DB4,word(ptr(m,SF2C_REPLAY_N_0X200_3790),SF2C_REPLAY_N_0X38_AE4F));
}
static uint64_t hash_bytes(uint64_t h,const void *data,size_t n) {
    const unsigned char *p=data;
    for(size_t i=0;i<n;++i)h=(h^p[i])*SF2C_REPLAY_N_UINT64_C_1099511628211_3F75;
    return h;
}
static void snapshot(Cpu *c) {
    FILE *destination=trace;
    if(lan_enabled()) { trace=hash_file;rewind(trace); }
    uint64_t round=ptr(fight,SF2C_REPLAY_N_0X60_3D6E), models=ptr(fight,SF2C_REPLAY_N_0X40_1AF3);
    fprintf(trace, SF2C_REPLAY_S_KIND_STATE_TICK_U_STATE_STAG_91BB
            SF2C_REPLAY_S_ROUND_U_U_U_U_U_FLAGS_U_U_U__BF5C,
            tick,word(fight,SF2C_REPLAY_N_0X198_4F92),word(fight,SF2C_REPLAY_N_0X28_EAB9),word(round,SF2C_REPLAY_N_0X10_BF9C),word(round,SF2C_REPLAY_N_0X14_F942),word(round,SF2C_REPLAY_N_0X18_D47C),
            word(round,SF2C_REPLAY_N_0X1C_7BDE),byte(round,SF2C_REPLAY_N_0X20_A766),byte(fight,SF2C_REPLAY_N_0X21_A102),byte(fight,SF2C_REPLAY_N_0X91_BC34),byte(fight,SF2C_REPLAY_N_0X92_E9B0),byte(fight,SF2C_REPLAY_N_0X101_83A4),byte(fight,SF2C_REPLAY_N_0X155_338E));
    const unsigned framing_offsets[] = {SF2C_REPLAY_N_0X84_7CD7,SF2C_REPLAY_N_0X78_756D,SF2C_REPLAY_N_0X74_D298,SF2C_REPLAY_N_0X88_AC70,SF2C_REPLAY_N_0X7C_3CC4,SF2C_REPLAY_N_0X8C_65B3};
    unsigned n=list_size(models);
    for (unsigned i=0;i<n;++i) { if(i)fputc(SF2C_REPLAY_CH_EMPTY_EE1B,trace);model_state(c,item(models,i)); }
    fputs(SF2C_REPLAY_S_ARENA_FRAMING_FCA8,trace);
    words(ptr(ptr(fight,SF2C_REPLAY_N_0X78_756D),SF2C_REPLAY_N_0X20_A766),framing_offsets,SF2C_REPLAY_N_6_E7F6);
    fputs(SF2C_REPLAY_S_RANDOM_STREAMS_DFE6,trace);
    for(unsigned i=0;i<stream_count;++i) fprintf(trace,SF2C_REPLAY_S_S_016_9810 PRIx64 SF2C_REPLAY_S_U_U_C6E2,i?SF2C_REPLAY_S_EMPTY_08B5:SF2C_REPLAY_S_EMPTY_12AE,streams[i].key,streams[i].state,streams[i].calls);
    fputs(SF2C_REPLAY_S_EMPTY_21FB,trace);
    long canonical_end=lan_enabled()?ftell(trace):0;
    fprintf(trace, SF2C_REPLAY_S_DIAGNOSTIC_PRESENT_EEDE PRIu64 SF2C_REPLAY_S_ELAPSED_NS_46D3 PRIu64 SF2C_REPLAY_S_FIGHT_FRAME_U_RELATIVE_FRAME_A46E,
            renderer_present_count(),plat_now_ns()-start_ns,word(fight,SF2C_REPLAY_N_0X24_EB21),word(fight,SF2C_REPLAY_N_0X24_EB21)-frame_origin);
    if (ferror(trace) || fflush(trace)) fail(SF2C_REPLAY_S_CANNOT_WRITE_COMBAT_TRACE_57B8);
    if(lan_enabled()) {
        long length=ftell(trace);if(canonical_end<0 || length<canonical_end)fail(SF2C_REPLAY_S_CANNOT_MEASURE_STATE_HASH_19DF);
        rewind(trace);combat_hash=identity_hash;unsigned char buffer[SF2C_REPLAY_N_4096_8B92];long offset=0;
        while(offset<length) {
            size_t n=(size_t)(length-offset);if(n>sizeof buffer)n=sizeof buffer;
            if(fread(buffer,1,n,trace)!=n)fail(SF2C_REPLAY_S_CANNOT_READ_STATE_HASH_B615);
            if(offset<canonical_end) {
                size_t part=(size_t)(canonical_end-offset);if(part>n)part=n;
                combat_hash=hash_bytes(combat_hash,buffer,part);
            }
            if(destination!=hash_file && !lan_initializing && fwrite(buffer,1,n,destination)!=n)fail(SF2C_REPLAY_S_CANNOT_WRITE_LAN_TRACE_773C);
            offset+=(long)n;
        }
        uint32_t frame=word(fight,SF2C_REPLAY_N_0X24_EB21);unsigned char canonical_frame[SF2C_REPLAY_N_4_4B22];
        for(unsigned i=0;i<SF2C_REPLAY_N_4_4B22;++i)canonical_frame[i]=(unsigned char)(frame>>(i*SF2C_REPLAY_N_8_2C62));
        combat_hash=hash_bytes(combat_hash,canonical_frame,sizeof canonical_frame);
        trace=destination;if(trace==hash_file)rewind(trace);
        if(fflush(trace))fail(SF2C_REPLAY_S_CANNOT_FLUSH_LAN_TRACE_3BFD);
    }
}
static void finish(const char *reason) {
    if (completed) return;
    completed=true;
    fprintf(trace,SF2C_REPLAY_S_KIND_COMPLETE_TICKS_U_EVENTS_3ADE,tick,applied_count,reason);
    if (fflush(trace)) fail(SF2C_REPLAY_S_CANNOT_FINISH_TRACE_4009);
    if (record && fflush(record)) fail(SF2C_REPLAY_S_CANNOT_FINISH_RECORDING_F370);
    LOG(SF2C_REPLAY_S_REPLAY_COMPLETED_U_COMBAT_ST_2483,tick,applied_count);
    if(!lan_gui_active())menu_request_quit();
}
static bool bind_fight(Cpu *c,uint64_t current) {
    if(fight)return true;
        if (word(ptr(current,SF2C_REPLAY_N_0X30_8BAD),SF2C_REPLAY_N_0X18_D47C)!=SF2C_REPLAY_N_18_4EC9 || word(current,SF2C_REPLAY_N_0X198_4F92)!=SF2C_REPLAY_N_2_D473) return false;
        players[0]=ptr(current,SF2C_REPLAY_N_0XB8_8F73); players[1]=ptr(current,SF2C_REPLAY_N_0XC0_4E7B);
        if (!players[0] || !players[1]) fail(SF2C_REPLAY_S_PVP_COMBAT_HAS_MISSING_FIGHT_5FD8);
        if(lan_enabled()) { ST8(current+SF2C_REPLAY_N_0X20_A766,0);lan_round=word(ptr(current,SF2C_REPLAY_N_0X60_3D6E),SF2C_REPLAY_N_0X18_D47C); } // local pause cannot stop only one peer
        fight=current; start_ns=plat_now_ns(); frame_origin=word(fight,SF2C_REPLAY_N_0X24_EB21);
        if(!selected) fail(SF2C_REPLAY_S_MISSING_PVP_SELECTION_IDENTI_CA53);
        fputs(SF2C_REPLAY_S_KIND_SETUP_LOCATION_9381,trace);string(ptr(ptr(fight,SF2C_REPLAY_N_0X70_B0F6),SF2C_REPLAY_N_0X18_D47C));
        fputs(SF2C_REPLAY_S_PLAYERS_B46A,trace);selection_string(0);fputc(SF2C_REPLAY_CH_EMPTY_EE1B,trace);selection_string(1);
        fputs(SF2C_REPLAY_S_LOCATION_TEMPLATE_48AC,trace);selection_string(SF2C_REPLAY_N_2_D473);
        fputs(SF2C_REPLAY_S_EQUIPMENT_E719,trace);equipment(ptr(players[0],SF2C_REPLAY_N_0X140_A49E));fputc(SF2C_REPLAY_CH_EMPTY_EE1B,trace);equipment(ptr(players[1],SF2C_REPLAY_N_0X140_A49E));fputs(SF2C_REPLAY_S_N_300B,trace);
        identity_hash=SF2C_REPLAY_N_UINT64_C_1469598103934665603_4020;
        for(unsigned i=0;i<SF2C_REPLAY_N_3_4E07;++i) {
            for(unsigned j=0;j<selection_length[i];++j) {
                unsigned char utf16[SF2C_REPLAY_N_2_D473]={(unsigned char)selection[i][j],(unsigned char)(selection[i][j]>>SF2C_REPLAY_N_8_2C62)};
                identity_hash=hash_bytes(identity_hash,utf16,SF2C_REPLAY_N_2_D473);
            }
            const unsigned char separator=0;identity_hash=hash_bytes(identity_hash,&separator,1);
        }
        lan_initializing=lan_enabled();snapshot(c);lan_initializing=false;
        if(!lan_enabled())LOG(SF2C_REPLAY_S_REPLAY_FIRST_ACTIVE_PVP_COMB_37F1 PRIu64 SF2C_REPLAY_S_N_D7A5,renderer_present_count());
    return true;
}
static void begin_step(Cpu *c, uint64_t current) {
    if (completed) return;
    if(controlled && current==preparing && !fight && warmup==SF2C_REPLAY_N_240_6AF1) {
        transition(c,current,SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6); // original PlayFight, at a logical boundary
    }
    if(lan_enabled() && fight) {
        if(current!=fight)fail(SF2C_REPLAY_S_LAN_FIGHT_CHANGED_BEFORE_SES_7723);
        uint32_t round_number=word(ptr(current,SF2C_REPLAY_N_0X60_3D6E),SF2C_REPLAY_N_0X18_D47C);
        if(round_number!=lan_round) {
            if(round_number!=lan_round+1)fail(SF2C_REPLAY_S_UNEXPECTED_LAN_ROUND_SEQUENC_3F56);
            lan_round=round_number;lan_stance_steps=0;
            players[0]=ptr(current,SF2C_REPLAY_N_0XB8_8F73);players[1]=ptr(current,SF2C_REPLAY_N_0XC0_4E7B);
            transition(c,current,SF2C_REPLAY_N_0X7000CA6DF0ULL_A60A);
            LOG(SF2C_REPLAY_S_LAN_NATIVE_ROUND_U_SYNCHRONI_36F6,lan_round);
        }
        if(lan_stance_steps==SF2C_REPLAY_N_240_6AF1) {
            transition(c,current,SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6);lan_stance_steps=UINT32_MAX;
        }
        ST8(current+SF2C_REPLAY_N_0X20_A766,0);lan_human_fighters(current);
    }
    if(!bind_fight(c,current))return;
    if (current!=fight) fail(SF2C_REPLAY_S_FIGHT_CHANGED_BEFORE_REPLAY__C140);
    if (in_step) fail(SF2C_REPLAY_S_NESTED_COMBAT_STEP_5BD3);
    in_step=true;
    ReplayEvent apply[LAN_MAX_EVENTS*SF2C_REPLAY_N_2_D473];unsigned count=0;
    while(event_next<event_count && events[event_next].tick==tick) {
        ReplayEvent e=events[event_next++];
        if(lan_enabled()) {
            if(e.player!=lan_player())continue;
            if(local_count==LAN_MAX_EVENTS)fail(SF2C_REPLAY_S_TOO_MANY_LOCAL_INPUTS_IN_ONE_C344);
            local_inputs[local_count++]=(LanEvent){(uint8_t)e.player,(uint8_t)e.code,(uint8_t)e.down};
        } else {
            // Offline tapes can contain more than one network packet's worth.
            Cpu saved=*c;injecting=true;c->x[0]=players[e.player];c->x[1]=e.code;c->x[SF2C_REPLAY_N_2_D473]=0;
            guest_call(c,e.down?SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66:SF2C_REPLAY_N_0X7000FF7C40ULL_F462);injecting=false;*c=saved;
            ++applied_count;
            fprintf(trace,SF2C_REPLAY_S_KIND_INPUT_TICK_U_PLAYER_U_C_53D5,tick,e.player+1,e.code,e.down);
        }
    }
    if(lan_enabled()) {
        LanEvent ordered[LAN_MAX_EVENTS*SF2C_REPLAY_N_2_D473];count=lan_step(tick,combat_hash,local_inputs,local_count,ordered);local_count=0;
        for(unsigned i=0;i<count;++i)apply[i]=(ReplayEvent){tick,ordered[i].player,ordered[i].code,ordered[i].down};
    }
    for(unsigned i=0;i<count;++i) {
        ReplayEvent e=apply[i];Cpu saved=*c;injecting=true;
        c->x[0]=players[e.player];c->x[1]=e.code;c->x[SF2C_REPLAY_N_2_D473]=0;
        guest_call(c,e.down?SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66:SF2C_REPLAY_N_0X7000FF7C40ULL_F462);injecting=false;*c=saved;++applied_count;
        fprintf(trace,SF2C_REPLAY_S_KIND_INPUT_TICK_U_PLAYER_U_C_53D5,tick,e.player+1,e.code,e.down);
    }
}
int replay_hook(Cpu *c, uint64_t site) {
    if (!trace) return 0;
    if(site==SF2C_REPLAY_N_0X7000DA027CULL_6732 && lan_enabled() && selected) {
        // LoadFightScene's coroutine has completed its own async loading.
        // Keep its native yield path until both peers can dismiss the loader,
        // before StartFight starts VS, round text and the 240-step stance.
        if(lan_loaded_poll())return 0;
        return lan_enabled()?SF2C_REPLAY_N_2_D473:SF2C_REPLAY_N_3_4E07; // native yield, or end the coroutine on disconnect
    }
    if(lan_enabled() && selected && !injecting && (site==SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66 || site==SF2C_REPLAY_N_0X7000FF7C40ULL_F462)) {
        // Each machine uses its ordinary P1 keyboard/controller. Ownership on
        // the wire assigns it to host P1 or joined P2, never to both fighters.
        if(fight && !playback && !completed && c->x[0]==players[0]) {
            if(local_count==LAN_MAX_EVENTS)fail(SF2C_REPLAY_S_TOO_MANY_LOCAL_INPUTS_IN_ONE_C344);
            if(c->x[1]>SF2C_REPLAY_N_14_8527)fail(SF2C_REPLAY_S_UNKNOWN_LOCAL_FIGHTER_INPUT_DC46);
            local_inputs[local_count++]=(LanEvent){(uint8_t)lan_player(),(uint8_t)c->x[1],site==SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66};
        }
        return 1;
    }
    if(controlled && selected && playback && !injecting &&
       (site==SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66 || site==SF2C_REPLAY_N_0X7000FF7C40ULL_F462)) return 1;
    int control=controlled_hook(c,site);
    if(control>=0)return control;
    if(site==SF2C_REPLAY_N_0X7000D961F0ULL_34E0 && !fight) {
        if(fixed_match) {
            Cpu saved=*c;
            c->x[0]=0;c->x[1]=0;
            guest_call(c,SF2C_REPLAY_N_0X7000CF9830ULL_7401);  // ListSF.Instance, initialized by the PvP menu
            uint64_t settings=ptr(c->x[0],SF2C_REPLAY_N_0X18_D47C);
            *c=saved;
            for(unsigned i=0;i<SF2C_REPLAY_N_3_4E07;++i) {
                uint64_t array=ptr(settings,i==SF2C_REPLAY_N_2_D473 ? SF2C_REPLAY_N_0X20_A766 : SF2C_REPLAY_N_0X18_D47C);
                if(!array || LD64(array+SF2C_REPLAY_N_0X18_D47C)<=match_indices[i]) fail(SF2C_REPLAY_S_FIXED_MATCH_INDEX_IS_OUTSIDE_6D48);
                uint64_t entry=LD64(array+SF2C_REPLAY_N_0X20_A766+match_indices[i]*SF2C_REPLAY_N_8_2C62), value=ptr(entry,SF2C_REPLAY_N_0X18_D47C);
                const uint16_t random_name[]={SF2C_REPLAY_CH_R_5BDE,SF2C_REPLAY_CH_A_D749,SF2C_REPLAY_CH_N_76E4,SF2C_REPLAY_CH_D_8F5F,SF2C_REPLAY_CH_O_5820,SF2C_REPLAY_CH_M_4109};
                if(!value || (word(value,SF2C_REPLAY_N_0X10_BF9C)==SF2C_REPLAY_N_6_E7F6 && !memcmp((void *)(uintptr_t)(value+SF2C_REPLAY_N_0X14_F942),random_name,sizeof random_name))) fail(SF2C_REPLAY_S_FIXED_MATCH_CANNOT_SELECT_RA_8804);
                ST64(c->x[0]+SF2C_REPLAY_N_0XA0_3862+i*SF2C_REPLAY_N_8_2C62,value);
            }
        }
        for(unsigned i=0;i<SF2C_REPLAY_N_3_4E07;++i) {
            uint64_t s=ptr(c->x[0],SF2C_REPLAY_N_0XA0_3862+i*SF2C_REPLAY_N_8_2C62);
            unsigned n=word(s,SF2C_REPLAY_N_0X10_BF9C);
            if(!n || n>SF2C_REPLAY_N_1024_E39E)fail(SF2C_REPLAY_S_INVALID_PVP_SELECTION_IDENTI_798F);
            memcpy(selection[i],(void *)(uintptr_t)(s+SF2C_REPLAY_N_0X14_F942),n*SF2C_REPLAY_N_2_D473);
            selection_length[i]=n;
        }
        selected=true;
    }
    else if (site==SF2C_REPLAY_N_0X7000C9DE18ULL_49F6) begin_step(c,c->x[SF2C_REPLAY_N_19_9400]);
    else if (site==SF2C_REPLAY_N_0X7000C9DE40ULL_6267) {
        if(controlled && c->x[SF2C_REPLAY_N_19_9400]==preparing && !fight && warmup<SF2C_REPLAY_N_240_6AF1) ++warmup;
        if (in_step && c->x[SF2C_REPLAY_N_19_9400]==fight) {
            in_step=false;++tick;
            if(lan_enabled() && lan_stance_steps<SF2C_REPLAY_N_240_6AF1)++lan_stance_steps;
            snapshot(c);
            if(lan_enabled() && byte(fight,SF2C_REPLAY_N_0X91_BC34)) {
                uint64_t winner=ptr(ptr(fight,SF2C_REPLAY_N_0X108_650A),SF2C_REPLAY_N_0X10_BF9C);
                unsigned slot=winner==ptr(players[0],SF2C_REPLAY_N_0X140_A49E)?1:winner==ptr(players[1],SF2C_REPLAY_N_0X140_A49E)?SF2C_REPLAY_N_2_D473:0;
                LOG(SF2C_REPLAY_S_LAN_MATCH_FINISHED_AFTER_NAT_EBF6,lan_round,slot);
                lan_finish(tick,combat_hash);finish(SF2C_REPLAY_S_MATCH_FINISHED_5F09);
            }
            else if (tick==limit) {
                if(lan_enabled())lan_finish(tick,combat_hash);
                finish(SF2C_REPLAY_S_LIMIT_2D05);
            }
        }
    } else if ((site==SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66 || site==SF2C_REPLAY_N_0X7000FF7C40ULL_F462) && fight && !injecting && (c->x[0]==players[0] || c->x[0]==players[1])) {
        if (playback) return 1;  // suppress live/AI input; only tape controls these models
        if (!completed) {
            if(record && in_step) fail(SF2C_REPLAY_S_LIVE_INPUT_OCCURRED_WITHIN_A_E1A2);
            unsigned player=c->x[0]==players[0]?1:SF2C_REPLAY_N_2_D473, down=site==SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66;
            ++applied_count;
            fprintf(trace,SF2C_REPLAY_S_KIND_INPUT_TICK_U_PLAYER_U_C_53D5,tick,player,(uint32_t)c->x[1],down);
            if (record) {
                fprintf(record,SF2C_REPLAY_S_U_U_U_U_N_0E23,tick,player,(uint32_t)c->x[1],down);
                if(fflush(record))fail(SF2C_REPLAY_S_CANNOT_WRITE_INPUT_TAPE_C520);
            }
        }
    }
    return 0;
}

void replay_gui_stop(void) {
    if(fight)ST8(fight+SF2C_REPLAY_N_0X21_A102,1);
    if(trace && trace!=hash_file)fclose(trace);
    if(hash_file)fclose(hash_file);
    trace=hash_file=NULL;controlled=fixed_match=selected=playback=false;
    completed=false;in_step=injecting=false;fight=preparing=players[0]=players[1]=0;
    free(events);events=NULL;event_count=event_next=applied_count=0;
    tick=warmup=stream_count=local_count=0;lan_started=lan_initializing=false;
    lan_stance_steps=UINT32_MAX;lan_round=0;
    memset(streams,0,sizeof streams);memset(selection_length,0,sizeof selection_length);
}
void replay_gui_start(void) {
    replay_gui_stop();
    const LanConfig *cfg=lan_config();controlled=fixed_match=true;seed=cfg->seed;limit=cfg->limit;
    memcpy(match_indices,cfg->match,sizeof match_indices);
    hash_file=tmpfile();if(!hash_file)fail(SF2C_REPLAY_S_CANNOT_CREATE_COMBAT_CHECKPO_7D0D);
    trace=hash_file;
    const char *directory=getenv(SF2C_REPLAY_S_SF2RECOMP_LAN_TRACE_DIR_236B);
    if(directory && *directory) {
        static unsigned serial;
        char path[SF2C_REPLAY_N_1024_E39E];if(snprintf(path,sizeof path,SF2C_REPLAY_S_S_MATCH_U_JSONL_F685,directory,serial++)>=(int)sizeof path)fail(SF2C_REPLAY_S_TRACE_PATH_IS_TOO_LONG_DCE0);
        trace=fopen(path,SF2C_REPLAY_S_WX_6B8F);if(!trace)fail(SF2C_REPLAY_S_CANNOT_CREATE_NATIVE_LAN_DIA_16A2);
        replay_header();
    }
}

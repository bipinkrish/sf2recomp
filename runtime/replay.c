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

enum { MAX_EVENTS = 65536, MAX_LIST = 4096 };
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
static uint64_t fight, players[2], start_ns;
static uint32_t tick, limit, frame_origin;
static uint16_t selection[3][1024];
static unsigned selection_length[3];
static bool selected;
static bool fixed_match;
static uint32_t match_indices[3];
static bool controlled;
static uint32_t seed;
// Guest combat runs on the update thread. Scopes must not leak into other
// guest threads (loading/audio), even while one wrapped guest call is active.
static _Thread_local unsigned simulation_depth;
static _Thread_local uint64_t wrapped, random_scope;
typedef struct { uint64_t key; uint32_t state, calls; } RandomStream;
static RandomStream streams[1024];
static unsigned stream_count;
static _Thread_local bool draw_wrapped, constructor_wrapped, play_wrapped;
static uint64_t preparing;
static uint32_t warmup;
static _Thread_local bool transition_authorized;
extern uint64_t renderer_present_count(void);

static void fail(const char *why) {
    fprintf(stderr, "[replay] %s\n", why);
    if(lan_enabled())lan_close();
    fflush(stderr);_Exit(2);
}
static uint32_t number(const char *s, uint32_t maximum) {
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (!*s || *s == '-' || *s == '+' || *end || errno || value > maximum) fail("invalid numeric option");
    return (uint32_t)value;
}

static void replay_header(void);
void replay_init(void) {
    lan_init();
    const char *out = getenv("SF2_REPLAY_TRACE"), *input = getenv("SF2_REPLAY_INPUT");
    const char *rec = getenv("SF2_REPLAY_RECORD"), *ticks = getenv("SF2_REPLAY_TICKS");
    const char *match = getenv("SF2_REPLAY_MATCH");
    const char *seed_option = getenv("SF2_REPLAY_SEED");
    if (!out && !lan_enabled()) {
        if (input || rec || ticks || match || seed_option) fail("SF2_REPLAY_TRACE is required for replay options");
        return;
    }
    if (input && rec) fail("INPUT and RECORD are mutually exclusive");
    if (out && ((input && !strcmp(input, out)) || (rec && !strcmp(rec, out)))) fail("trace and input/record paths must differ");
    limit = ticks ? number(ticks, 1000000) : (lan_enabled() ? UINT32_MAX : 600);
    if (!limit) fail("SF2_REPLAY_TICKS must be positive");
    controlled = seed_option != NULL;
    seed = seed_option ? number(seed_option, UINT32_MAX) : 0;
    if (match) {
        char copy[64];
        if (strlen(match) >= sizeof copy) fail("invalid fixed match indices");
        strcpy(copy,match);
        char *part=copy;
        for(unsigned i=0;i<3;++i) {
            char *comma=strchr(part,',');
            if ((i<2 && !comma) || (i==2 && comma)) fail("MATCH requires character,character,location indices");
            if(comma)*comma=0;
            match_indices[i]=number(part,255);
            if(comma)part=comma+1;
        }
        fixed_match=true;
    }
    if(lan_enabled()) {
        if(seed_option || match || rec)fail("LAN chooses initialization; replay SEED/MATCH/RECORD are not allowed");
        const LanConfig *cfg=lan_config();controlled=true;fixed_match=true;seed=cfg->seed;limit=cfg->limit;
        memcpy(match_indices,cfg->match,sizeof match_indices);
    }
    if (input) {
        FILE *f = fopen(input, "r");
        if (!f) fail("cannot open input tape");
        char line[256];
        if (!fgets(line, sizeof line, f) || strcmp(line, "SF2_REPLAY 1\n")) fail("invalid input tape header");
        events = calloc(MAX_EVENTS, sizeof *events);
        if (!events) fail("cannot allocate input tape");
        unsigned lineno = 1;
        while (fgets(line, sizeof line, f)) {
            ++lineno;
            if (line[0] == '#' || line[0] == '\n') continue;
            char *fields[5] = {0};
            unsigned count = 0;
            for (char *token = strtok(line, " \t\r\n"); token && count < 5; token = strtok(NULL, " \t\r\n")) fields[count++] = token;
            if (count != 4) fail("input tape requires four unsigned fields");
            unsigned t=number(fields[0],1000000), p=number(fields[1],2);
            unsigned code=number(fields[2],14), down=number(fields[3],1);
            if (
                t >= limit || p < 1 || p > 2 || code > 14 || down > 1 ||
                event_count == MAX_EVENTS || (event_count && t < events[event_count - 1].tick)) {
                fprintf(stderr, "[replay] invalid input tape line %u\n", lineno);
                exit(2);
            }
            events[event_count++] = (ReplayEvent){t, p - 1, code, down};
        }
        if (ferror(f)) fail("cannot read input tape");
        fclose(f);
        playback = true;
    }
    if(lan_enabled()) {
        hash_file=tmpfile();if(!hash_file)fail("cannot create bounded state-hash scratch stream");
    }
    trace = out ? fopen(out, "wx") : hash_file;  // never truncate previous evidence or an input tape
    if (!trace) fail("cannot create trace (output must not exist)");
    if (rec) {
        record = fopen(rec, "wx");
        if (!record) fail("cannot create record tape (output must not exist)");
        fputs("SF2_REPLAY 1\n", record);
        fflush(record);
    }
    replay_header();
    LOG("[replay] %s, %u combat steps, %s\n", playback ? "playback" : "observe/record", limit,
        controlled ? "controlled initialization and scoped random streams" : "original random/timing behavior");
}
static void replay_header(void) {
    fprintf(trace, "{\"kind\":\"header\",\"schema\":1,\"game\":\"update_v131072\","
            "\"limit\":%u,\"playback\":%s,\"events\":%zu,\"fps\":%d,\"aspect\":%d,"
            "\"resolution\":%d,\"upscaler\":%d,\"aa\":%d,\"ai_enabled\":%s,"
            "\"rng_policy\":\"%s\",\"seed\":%u,\"initialization\":\"%s\",\"fixed_match\":%s,"
            "\"combat_geometry\":\"pvp-framing-16:9-v2\",\"coverage\":\"observed-combat-fields-v3\",\"end_on_match\":%s}\n",
            limit, playback ? "true" : "false", event_count, g_settings.target_fps, g_settings.aspect,
            g_settings.res_mode, g_settings.upscaler, g_settings.antialias, g_settings.ai_enabled ? "true" : "false",
            controlled ? "scoped-callsite-lcg-v2" : "original-unmodified", seed,
            lan_enabled()?"lan-two-human-native-stance-240-v1":controlled ? "native-round-stance-240-steps-v1" : "original", fixed_match ? "true" : "false",lan_enabled()?"true":"false");
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
    uint64_t key = ((uint64_t)family << 56) | (caller & 0xffffffffu);
    unsigned i=0;
    while(i<stream_count && streams[i].key<key) ++i;
    if(i==stream_count || streams[i].key!=key) {
        if(stream_count==1024) fail("too many scoped random streams");
        memmove(streams+i+1,streams+i,(stream_count-i)*sizeof *streams);
        // Nearby call sites must not start linearly related LCG streams.
        // Mix the shared seed and the full scope key; retain the native LCG.
        uint32_t mixed=(uint32_t)lan_random_mix(((uint64_t)seed<<32)^key^UINT64_C(0x534632524e475632))&0x7fffffffu;
        streams[i]=(RandomStream){key,mixed,0};++stream_count;
    }
    RandomStream *s=&streams[i];
    s->state=(s->state*0x41c64e6du+0x3039u)&0x7fffffffu;
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
    for(unsigned i=0;i<2;++i) {
        uint64_t model=ptr(current,i?0xc0:0xb8),parameters=ptr(model,0x140);
        if(!parameters)fail("LAN fighter parameters are missing");
        ST8(parameters+0x62,0);ST8(parameters+0x63,1);
    }
}
static int controlled_hook(Cpu *c, uint64_t site) {
    if(!controlled || !selected) return -1;
    if(site==0x7000ca7e80ull && word(ptr(c->x[0],0x30),0x18)==18) return 1;
    if((site==0x7000ca6df0ull || site==0x7000ca7c00ull) && !transition_authorized &&
       word(ptr(c->x[0],0x30),0x18)==18) return 1;
    if(site==0x7000c9dd50ull && !draw_wrapped && word(ptr(c->x[0],0x30),0x18)==18) {
        if(!preparing) {
            // FightHolder can draw before PreFight.InitPreFight initializes
            // ViewerFight.Round. NextRound/ViewerFight.Reset require it.
            // UI loading proceeds on Unity Update while combat stays at birth.
            uint64_t viewer=ptr(ptr(c->x[0],0x1a0),0x80);
            if(!ptr(viewer,0x70)) return 1;
            preparing=c->x[0];
            if(word(preparing,0x24) || word(ptr(preparing,0x60),0x18))
                fail("controlled initialization missed the new fight boundary");
            if(lan_enabled())lan_human_fighters(preparing);
            transition(c,preparing,0x7000ca7100ull); // original NextRound
            if(lan_enabled())lan_human_fighters(preparing);
            transition(c,preparing,0x7000ca6df0ull); // original StartStance
            LOG("[replay] initialized native round and stance; warming 240 combat steps\n");
        }
        if(lan_enabled() && warmup==240) {
            if(!fight) {
                transition(c,c->x[0],0x7000ca7c00ull);
                if(!bind_fight(c,c->x[0]))fail("LAN combat could not initialize");
            }
            if(!lan_ready_poll(combat_hash))return 1;
            if(!lan_started) {
                lan_started=true;start_ns=plat_now_ns();snapshot(c);
                LOG("[lan] ready barrier passed at present %" PRIu64 "\n",renderer_present_count());
            }
        }
        draw_wrapped=true;++simulation_depth;
        guest_call(c,site);
        --simulation_depth;draw_wrapped=false;
        return 1;
    }
    if((site==0x7000c99e30ull && !constructor_wrapped) || (site==0x7000ca7c00ull && !play_wrapped)) {
        bool *guard=site==0x7000c99e30ull ? &constructor_wrapped : &play_wrapped;
        *guard=true;++simulation_depth;guest_call(c,site);--simulation_depth;*guard=false;
        return 1;
    }
    if(!simulation_depth) return -1;
    if(site==0x7001177d10ull) {
        ST32(c->x[0]+0x10,seed);ST32(c->x[0]+0x14,seed);
        return 1;
    }
    if(site==0x7001177d20ull) {
        uint32_t value=random_value(random_scope ? random_scope : c->x[30],1);
        ST32(c->x[0]+0x10,value);c->x[0]=value;
        return 1;
    }
    if(site==0x7001237bc0ull) {
        int64_t lo=(int32_t)c->x[1],hi=(int32_t)c->x[2];
        if(lo>hi) { int64_t tmp=lo;lo=hi;hi=tmp; }
        uint32_t r=random_value(c->x[30],2);
        c->x[0]=(uint32_t)(lo+((uint64_t)r*(uint64_t)(hi-lo)>>31));
        return 1;
    }
    if(site==0x7001237b50ull) {
        float lo,hi;memcpy(&lo,&c->v[0],4);memcpy(&hi,&c->v[1],4);
        if(!isfinite(lo)||!isfinite(hi)) fail("nonfinite controlled Unity random bounds");
        float fraction=(random_value(c->x[30],3)&0xffffffu)/16777215.0f;
        volatile float delta=hi-lo, scaled=delta*fraction;
        float value=lo+scaled;uint32_t bits;memcpy(&bits,&value,4);c->v[0]=bits;
        return 1;
    }
    // Preserve Nekki's native float/int/chance distribution, including its
    // two-draw precision. Only replace the LCG source inside the original call.
    if((site==0x70010ed6a0ull || (site>=0x70010f1450ull && site<=0x70010f1f90ull) || site==0x70010f2de0ull)
       && site!=wrapped && !random_scope) {
        uint64_t saved_wrapped=wrapped;
        wrapped=site;random_scope=c->x[30];
        guest_call(c,site);
        random_scope=0;wrapped=saved_wrapped;
        return 1;
    }
    return -1;
}
static unsigned list_size(uint64_t list) {
    unsigned n = word(list, 0x18);
    if (n > MAX_LIST || (n && !ptr(list, 0x10))) fail("invalid/oversized guest list in combat snapshot");
    return n;
}
static uint64_t item(uint64_t list, unsigned i) { return LD64(ptr(list, 0x10) + 0x20 + i * 8); }
static void string(uint64_t s) {
    fputc('"', trace);
    unsigned n = word(s, 0x10);
    if (n > 1024) fail("invalid guest string in combat snapshot");
    for (unsigned i = 0; i < n; ++i) {
        unsigned ch = LD16(s + 0x14 + i * 2);
        if (ch >= 32 && ch < 127 && ch != '"' && ch != '\\') fputc((int)ch, trace);
        else fprintf(trace, "\\u%04x", ch);
    }
    fputc('"', trace);
}
static void selection_string(unsigned slot) {
    fputc('"',trace);
    for(unsigned i=0;i<selection_length[slot];++i) {
        unsigned ch=selection[slot][i];
        if(ch>=32 && ch<127 && ch!='"' && ch!='\\') fputc((int)ch,trace);
        else fprintf(trace,"\\u%04x",ch);
    }
    fputc('"',trace);
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
    fputc('[', trace);
    for (unsigned i = 0; i < count; ++i) fprintf(trace, "%s%u", i ? "," : "", word(object, offsets[i]));
    fputc(']', trace);
}
static void int_list(uint64_t list) {
    unsigned n = list_size(list);
    fputc('[', trace);
    for (unsigned i = 0; i < n; ++i) fprintf(trace, "%s%u", i ? "," : "", LD32(ptr(list, 0x10) + 0x20 + i * 4));
    fputc(']', trace);
}
static void vector(uint64_t v) {
    const unsigned offsets[] = {0x10, 0x14, 0x18};
    words(v, offsets, 3);  // float bit patterns: exact comparison, including NaNs
}
static void equipment(uint64_t params) {
    fputc('[',trace);
    for(unsigned i=0;i<7;++i) {
        if(i)fputc(',',trace);
        string(ptr(ptr(params,0xa0+i*8),0x10));
    }
    fputc(']',trace);
}
static void model_state(Cpu *c, uint64_t m) {
    uint64_t params = ptr(m, 0x140), animation = ptr(m, 0x210), controller = ptr(m, 0x1d0);
    fprintf(trace, "{\"name\":"); string(ptr(m, 0x1b8));
    fputs(",\"equipment\":",trace);equipment(params);
    fprintf(trace, ",\"health_bits\":%u,\"user\":%u,\"ai\":%u,\"model\":",
            invoke_float_bits(c, 0x7000ffbf90ull, m), byte(params, 0x63), byte(params, 0x62));
    const unsigned model_words[] = {0x118,0x130,0x148,0x15c,0x164,0x168,0x170,0x174,0x190,0x194,0x198,0x1e0,0x228,0x230,0x234,0x238,0x23c,0x284};
    words(m, model_words, sizeof model_words / sizeof *model_words);
    fprintf(trace, ",\"flags\":[%u,%u,%u,%u,%u,%u,%u],\"animation\":",
            byte(m,0x158),byte(m,0x159),byte(m,0x160),byte(m,0x161),byte(m,0x22c),byte(m,0x22d),byte(m,0x241));
    const unsigned anim_words[] = {0x34,0x38,0x48,0x4c,0x50,0x54,0x58,0x5c,0x60,0x64,0x6c,0x70,0x74,0x78,0x80,0xa8,0xac,0xb0,0xb4,0xc0,0xc4,0xd8,0xdc};
    words(animation, anim_words, sizeof anim_words / sizeof *anim_words);
    fputs(",\"animation_name\":",trace);string(ptr(ptr(animation,0xf0),0x40));
    fprintf(trace, ",\"shift\":"); vector(ptr(animation, 0x88));
    fprintf(trace, ",\"velocity\":"); vector(ptr(animation, 0x90));
    fprintf(trace, ",\"controller\":[%u,%u],\"keys\":[", word(controller,0x20),word(controller,0x38));
    uint64_t keys = ptr(controller, 0x18);
    unsigned n = list_size(keys);
    for (unsigned i=0;i<n;++i) {
        uint64_t key = item(keys,i);
        fprintf(trace, "%s[%u,%u,%u]", i ? "," : "",word(key,0x10),word(key,0x14),byte(key,0x18));
    }
    fprintf(trace, "],\"key_data\":[");
    uint64_t data = ptr(controller, 0x28);
    for (unsigned i=0;i<3;++i) { if (i) fputc(',',trace); int_list(ptr(data,0x10+i*8)); }
    fprintf(trace, "],\"nodes\":[");
    uint64_t object = ptr(m,0x1c8), nodes = ptr(ptr(object,0x10),0x28);
    n=list_size(nodes);
    for (unsigned i=0;i<n;++i) {
        uint64_t node=item(nodes,i);
        fprintf(trace, "%s{\"id\":%u,\"current\":",i ? "," : "",word(node,0x3c));vector(ptr(node,0x20));
        fputs(",\"previous\":",trace);vector(ptr(node,0x28));fputc('}',trace);
    }
    fprintf(trace, "],\"physics_frames\":%u}",word(ptr(m,0x200),0x38));
}
static uint64_t hash_bytes(uint64_t h,const void *data,size_t n) {
    const unsigned char *p=data;
    for(size_t i=0;i<n;++i)h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
static void snapshot(Cpu *c) {
    FILE *destination=trace;
    if(lan_enabled()) { trace=hash_file;rewind(trace); }
    uint64_t round=ptr(fight,0x60), models=ptr(fight,0x40);
    fprintf(trace, "{\"kind\":\"state\",\"tick\":%u,\"state\":{\"stage\":%u,\"fight_time\":%u,"
            "\"round\":[%u,%u,%u,%u,%u],\"flags\":[%u,%u,%u,%u,%u],\"models\":[",
            tick,word(fight,0x198),word(fight,0x28),word(round,0x10),word(round,0x14),word(round,0x18),
            word(round,0x1c),byte(round,0x20),byte(fight,0x21),byte(fight,0x91),byte(fight,0x92),byte(fight,0x101),byte(fight,0x155));
    const unsigned framing_offsets[] = {0x84,0x78,0x74,0x88,0x7c,0x8c};
    unsigned n=list_size(models);
    for (unsigned i=0;i<n;++i) { if(i)fputc(',',trace);model_state(c,item(models,i)); }
    fputs("],\"arena_framing\":",trace);
    words(ptr(ptr(fight,0x78),0x20),framing_offsets,6);
    fputs(",\"random_streams\":[",trace);
    for(unsigned i=0;i<stream_count;++i) fprintf(trace,"%s[\"%016" PRIx64 "\",%u,%u]",i?",":"",streams[i].key,streams[i].state,streams[i].calls);
    fputs("]}",trace);
    long canonical_end=lan_enabled()?ftell(trace):0;
    fprintf(trace, ",\"diagnostic\":{\"present\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 ",\"fight_frame\":%u,\"relative_frame\":%u}}\n",
            renderer_present_count(),plat_now_ns()-start_ns,word(fight,0x24),word(fight,0x24)-frame_origin);
    if (ferror(trace) || fflush(trace)) fail("cannot write combat trace");
    if(lan_enabled()) {
        long length=ftell(trace);if(canonical_end<0 || length<canonical_end)fail("cannot measure state hash");
        rewind(trace);combat_hash=identity_hash;unsigned char buffer[4096];long offset=0;
        while(offset<length) {
            size_t n=(size_t)(length-offset);if(n>sizeof buffer)n=sizeof buffer;
            if(fread(buffer,1,n,trace)!=n)fail("cannot read state hash");
            if(offset<canonical_end) {
                size_t part=(size_t)(canonical_end-offset);if(part>n)part=n;
                combat_hash=hash_bytes(combat_hash,buffer,part);
            }
            if(destination!=hash_file && !lan_initializing && fwrite(buffer,1,n,destination)!=n)fail("cannot write LAN trace");
            offset+=(long)n;
        }
        uint32_t frame=word(fight,0x24);unsigned char canonical_frame[4];
        for(unsigned i=0;i<4;++i)canonical_frame[i]=(unsigned char)(frame>>(i*8));
        combat_hash=hash_bytes(combat_hash,canonical_frame,sizeof canonical_frame);
        trace=destination;if(trace==hash_file)rewind(trace);
        if(fflush(trace))fail("cannot flush LAN trace");
    }
}
static void finish(const char *reason) {
    if (completed) return;
    completed=true;
    fprintf(trace,"{\"kind\":\"complete\",\"ticks\":%u,\"events_applied\":%zu,\"reason\":\"%s\"}\n",tick,applied_count,reason);
    if (fflush(trace)) fail("cannot finish trace");
    if (record && fflush(record)) fail("cannot finish recording");
    LOG("[replay] completed %u combat steps, %zu inputs applied\n",tick,applied_count);
    if(!lan_gui_active())menu_request_quit();
}
static bool bind_fight(Cpu *c,uint64_t current) {
    if(fight)return true;
        if (word(ptr(current,0x30),0x18)!=18 || word(current,0x198)!=2) return false;
        players[0]=ptr(current,0xb8); players[1]=ptr(current,0xc0);
        if (!players[0] || !players[1]) fail("PvP combat has missing fighters");
        if(lan_enabled()) { ST8(current+0x20,0);lan_round=word(ptr(current,0x60),0x18); } // local pause cannot stop only one peer
        fight=current; start_ns=plat_now_ns(); frame_origin=word(fight,0x24);
        if(!selected) fail("missing PvP selection identity; cannot compare this match");
        fputs("{\"kind\":\"setup\",\"location\":",trace);string(ptr(ptr(fight,0x70),0x18));
        fputs(",\"players\":[",trace);selection_string(0);fputc(',',trace);selection_string(1);
        fputs("],\"location_template\":",trace);selection_string(2);
        fputs(",\"equipment\":[",trace);equipment(ptr(players[0],0x140));fputc(',',trace);equipment(ptr(players[1],0x140));fputs("]}\n",trace);
        identity_hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<3;++i) {
            for(unsigned j=0;j<selection_length[i];++j) {
                unsigned char utf16[2]={(unsigned char)selection[i][j],(unsigned char)(selection[i][j]>>8)};
                identity_hash=hash_bytes(identity_hash,utf16,2);
            }
            const unsigned char separator=0;identity_hash=hash_bytes(identity_hash,&separator,1);
        }
        lan_initializing=lan_enabled();snapshot(c);lan_initializing=false;
        if(!lan_enabled())LOG("[replay] first active PvP combat step at present %" PRIu64 "\n",renderer_present_count());
    return true;
}
static void begin_step(Cpu *c, uint64_t current) {
    if (completed) return;
    if(controlled && current==preparing && !fight && warmup==240) {
        transition(c,current,0x7000ca7c00ull); // original PlayFight, at a logical boundary
    }
    if(lan_enabled() && fight) {
        if(current!=fight)fail("LAN fight changed before session end");
        uint32_t round_number=word(ptr(current,0x60),0x18);
        if(round_number!=lan_round) {
            if(round_number!=lan_round+1)fail("unexpected LAN round sequence");
            lan_round=round_number;lan_stance_steps=0;
            players[0]=ptr(current,0xb8);players[1]=ptr(current,0xc0);
            transition(c,current,0x7000ca6df0ull);
            LOG("[lan] native round %u; synchronized 240-step stance\n",lan_round);
        }
        if(lan_stance_steps==240) {
            transition(c,current,0x7000ca7c00ull);lan_stance_steps=UINT32_MAX;
        }
        ST8(current+0x20,0);lan_human_fighters(current);
    }
    if(!bind_fight(c,current))return;
    if (current!=fight) fail("fight changed before replay completed");
    if (in_step) fail("nested combat step");
    in_step=true;
    ReplayEvent apply[LAN_MAX_EVENTS*2];unsigned count=0;
    while(event_next<event_count && events[event_next].tick==tick) {
        ReplayEvent e=events[event_next++];
        if(lan_enabled()) {
            if(e.player!=lan_player())continue;
            if(local_count==LAN_MAX_EVENTS)fail("too many local inputs in one step");
            local_inputs[local_count++]=(LanEvent){(uint8_t)e.player,(uint8_t)e.code,(uint8_t)e.down};
        } else {
            // Offline tapes can contain more than one network packet's worth.
            Cpu saved=*c;injecting=true;c->x[0]=players[e.player];c->x[1]=e.code;c->x[2]=0;
            guest_call(c,e.down?0x7000ff7af0ull:0x7000ff7c40ull);injecting=false;*c=saved;
            ++applied_count;
            fprintf(trace,"{\"kind\":\"input\",\"tick\":%u,\"player\":%u,\"code\":%u,\"down\":%u}\n",tick,e.player+1,e.code,e.down);
        }
    }
    if(lan_enabled()) {
        LanEvent ordered[LAN_MAX_EVENTS*2];count=lan_step(tick,combat_hash,local_inputs,local_count,ordered);local_count=0;
        for(unsigned i=0;i<count;++i)apply[i]=(ReplayEvent){tick,ordered[i].player,ordered[i].code,ordered[i].down};
    }
    for(unsigned i=0;i<count;++i) {
        ReplayEvent e=apply[i];Cpu saved=*c;injecting=true;
        c->x[0]=players[e.player];c->x[1]=e.code;c->x[2]=0;
        guest_call(c,e.down?0x7000ff7af0ull:0x7000ff7c40ull);injecting=false;*c=saved;++applied_count;
        fprintf(trace,"{\"kind\":\"input\",\"tick\":%u,\"player\":%u,\"code\":%u,\"down\":%u}\n",tick,e.player+1,e.code,e.down);
    }
}
int replay_hook(Cpu *c, uint64_t site) {
    if (!trace) return 0;
    if(lan_enabled() && selected && !injecting && (site==0x7000ff7af0ull || site==0x7000ff7c40ull)) {
        // Each machine uses its ordinary P1 keyboard/controller. Ownership on
        // the wire assigns it to host P1 or joined P2, never to both fighters.
        if(fight && !playback && !completed && c->x[0]==players[0]) {
            if(local_count==LAN_MAX_EVENTS)fail("too many local inputs in one step");
            if(c->x[1]>14)fail("unknown local fighter input");
            local_inputs[local_count++]=(LanEvent){(uint8_t)lan_player(),(uint8_t)c->x[1],site==0x7000ff7af0ull};
        }
        return 1;
    }
    if(controlled && selected && playback && !injecting &&
       (site==0x7000ff7af0ull || site==0x7000ff7c40ull)) return 1;
    int control=controlled_hook(c,site);
    if(control>=0)return control;
    if(site==0x7000d961f0ull && !fight) {
        if(fixed_match) {
            Cpu saved=*c;
            c->x[0]=0;c->x[1]=0;
            guest_call(c,0x7000cf9830ull);  // ListSF.Instance, initialized by the PvP menu
            uint64_t settings=ptr(c->x[0],0x18);
            *c=saved;
            for(unsigned i=0;i<3;++i) {
                uint64_t array=ptr(settings,i==2 ? 0x20 : 0x18);
                if(!array || LD64(array+0x18)<=match_indices[i]) fail("fixed match index is outside the PvP roster");
                uint64_t entry=LD64(array+0x20+match_indices[i]*8), value=ptr(entry,0x18);
                const uint16_t random_name[]={'r','a','n','d','o','m'};
                if(!value || (word(value,0x10)==6 && !memcmp((void *)(uintptr_t)(value+0x14),random_name,sizeof random_name))) fail("fixed match cannot select Random");
                ST64(c->x[0]+0xa0+i*8,value);
            }
        }
        for(unsigned i=0;i<3;++i) {
            uint64_t s=ptr(c->x[0],0xa0+i*8);
            unsigned n=word(s,0x10);
            if(!n || n>1024)fail("invalid PvP selection identity");
            memcpy(selection[i],(void *)(uintptr_t)(s+0x14),n*2);
            selection_length[i]=n;
        }
        selected=true;
    }
    else if (site==0x7000c9de18ull) begin_step(c,c->x[19]);
    else if (site==0x7000c9de40ull) {
        if(controlled && c->x[19]==preparing && !fight && warmup<240) ++warmup;
        if (in_step && c->x[19]==fight) {
            in_step=false;++tick;
            if(lan_enabled() && lan_stance_steps<240)++lan_stance_steps;
            snapshot(c);
            if(lan_enabled() && byte(fight,0x91)) {
                uint64_t winner=ptr(ptr(fight,0x108),0x10);
                unsigned slot=winner==ptr(players[0],0x140)?1:winner==ptr(players[1],0x140)?2:0;
                LOG("[lan] match finished after native round %u; winner player %u\n",lan_round,slot);
                lan_finish(tick,combat_hash);finish("match_finished");
            }
            else if (tick==limit) {
                if(lan_enabled())lan_finish(tick,combat_hash);
                finish("limit");
            }
        }
    } else if ((site==0x7000ff7af0ull || site==0x7000ff7c40ull) && fight && !injecting && (c->x[0]==players[0] || c->x[0]==players[1])) {
        if (playback) return 1;  // suppress live/AI input; only tape controls these models
        if (!completed) {
            if(record && in_step) fail("live input occurred within a combat step; tape cannot preserve its ordering");
            unsigned player=c->x[0]==players[0]?1:2, down=site==0x7000ff7af0ull;
            ++applied_count;
            fprintf(trace,"{\"kind\":\"input\",\"tick\":%u,\"player\":%u,\"code\":%u,\"down\":%u}\n",tick,player,(uint32_t)c->x[1],down);
            if (record) {
                fprintf(record,"%u %u %u %u\n",tick,player,(uint32_t)c->x[1],down);
                if(fflush(record))fail("cannot write input tape");
            }
        }
    }
    return 0;
}

void replay_gui_stop(void) {
    if(fight)ST8(fight+0x21,1);
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
    hash_file=tmpfile();if(!hash_file)fail("cannot create combat checkpoint stream");
    trace=hash_file;
    const char *directory=getenv("LARECOMP_LAN_TRACE_DIR");
    if(directory && *directory) {
        static unsigned serial;
        char path[1024];if(snprintf(path,sizeof path,"%s/match-%u.jsonl",directory,serial++)>=(int)sizeof path)fail("trace path is too long");
        trace=fopen(path,"wx");if(!trace)fail("cannot create native LAN diagnostic trace");
        replay_header();
    }
}

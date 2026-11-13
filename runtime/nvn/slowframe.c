#include "../constants/nvn.h"
// Slow-frame log: see slowframe.h.
#include "slowframe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform.h"
#include "../rt.h"

#define ADD(v, n) __atomic_fetch_add(&(v), (n), __ATOMIC_RELAXED)
#define GET(v) __atomic_load_n(&(v), __ATOMIC_RELAXED)

static uint64_t n_pipes, pipe_ns, n_hits, n_progs, prog_ns, n_tr, tr_ns, n_uploads, upload_bytes, n_hashes;

void slow_note_build(const char *what, uint64_t ns) {
    if (!strncmp(what, SF2C_SLOWFRAME_S_PIPELINE_F742, SF2C_SLOWFRAME_N_8_2C62)) ADD(n_pipes, 1), ADD(pipe_ns, ns);
    else ADD(n_progs, 1), ADD(prog_ns, ns);
}
void slow_note_cache_hit(void) { ADD(n_hits, 1); }
void slow_note_upload(uint64_t bytes) { ADD(n_uploads, 1), ADD(upload_bytes, bytes); }
void slow_note_full_hash(void) { ADD(n_hashes, 1); }
ShaderInfo *slow_translate(const uint8_t *code, size_t max_size, int stage, const ShaderKey *key) {
    uint64_t t0 = plat_now_ns();
    ShaderInfo *r = maxwell_translate(code, max_size, stage, key);
    ADD(n_tr, 1), ADD(tr_ns, plat_now_ns() - t0);
    return r;
}

typedef struct {
    uint64_t pipes, pipe_ns, hits, progs, prog_ns, tr, tr_ns, uploads, up_bytes, hashes, imports, submits, wait_ns, rcpu, gcpu;
    uint64_t rqw, blocked, present;  // (the game thread's own: waits for the render thread, vsync / pacer waits, presents)
} Snap;

static void snap(Snap *s, uint64_t gcpu) {
    extern uint64_t rq_render_cpu_ns(void);
    s->pipes = GET(n_pipes), s->pipe_ns = GET(pipe_ns), s->hits = GET(n_hits);
    s->progs = GET(n_progs), s->prog_ns = GET(prog_ns);
    s->tr = GET(n_tr), s->tr_ns = GET(tr_ns);
    s->uploads = GET(n_uploads), s->up_bytes = GET(upload_bytes), s->hashes = GET(n_hashes);
    renderer_slow_counters(&s->submits, &s->wait_ns, &s->imports);
    s->rcpu = rq_render_cpu_ns();
    s->gcpu = gcpu;
    extern __thread uint64_t g_rq_wait_ns, g_blocked_ns, g_present_ns;  // (rqueue.c, kernel.c: this thread's)
    s->rqw = g_rq_wait_ns, s->blocked = g_blocked_ns, s->present = g_present_ns;
}

static uint64_t d(uint64_t a, uint64_t b) { return a > b ? a - b : 0; }  // (a backend total reset at a reboot: 0)

#define LOAD_NS SF2C_SLOWFRAME_N_2000000000ULL_C95E     // a level change: its next 2 s are loading / fading in (measured: up to ~1 s)
#define WINDOW_NS SF2C_SLOWFRAME_N_60000000000ULL_4A95  // the rate limit's period
#define LINES_PER_WINDOW SF2C_SLOWFRAME_LINES_PER_WINDOW
// The level now, compared with the one seen last: a change starts a load window. False: not in play (title, file
// select, the start screen, a reboot) - loading as well.
static bool level_check(uint64_t now, uint64_t *changed_at, char *name, size_t n) {
    extern const char *game_level_name(void);
    extern int game_in_play(void);
    static char last[SF2C_SLOWFRAME_N_64_A68B];
    const char *lv = game_level_name();
    if (strncmp(lv, last, sizeof last - 1)) {
        snprintf(last, sizeof last, SF2C_SLOWFRAME_S_S_0030, lv);
        *changed_at = now;
    }
    if (name) snprintf(name, n, SF2C_SLOWFRAME_S_S_0030, last);
    return game_in_play() != 0;
}

void slow_frame_present(uint64_t now, uint64_t gcpu) {
    static int64_t limit = -1;  // ns (0: off)
    if (limit < 0) {
        const char *e = getenv(SF2C_SLOWFRAME_S_SF2RECOMP_SLOW_FRAME_MS_04A4);
        limit = (e ? atoll(e) : SF2C_SLOWFRAME_N_50_1A65) * SF2C_SLOWFRAME_N_1000000LL_CC3A;
        if (limit < 0) limit = 0;
    }
    if (!limit) return;
    static uint64_t last, frames, changed_at, win_start, win_lines, win_busy, win_load;
    static Snap prev;
    Snap cur;
    snap(&cur, gcpu);
    uint64_t gap = last ? now - last : 0;
    last = now;
    frames++;
    if (!(frames & SF2C_SLOWFRAME_N_15_E629)) level_check(now, &changed_at, NULL, 0);  // (sampled: a level change starts a load window)
    if (win_start && now - win_start >= WINDOW_NS) {
        if (win_busy || win_load) {
            char s[SF2C_SLOWFRAME_N_160_A512] = SF2C_SLOWFRAME_S_EMPTY_12AE;
            if (win_busy) snprintf(s, sizeof s, SF2C_SLOWFRAME_S_LLU_PAST_THE_LIMIT_OF_D_LINE_D61C, (unsigned long long)win_busy, LINES_PER_WINDOW);
            if (win_load)
                snprintf(s + strlen(s), sizeof s - strlen(s), SF2C_SLOWFRAME_S_S_LLU_DURING_LOADS_75A4, win_busy ? SF2C_SLOWFRAME_S_EMPTY_F240 : SF2C_SLOWFRAME_S_EMPTY_12AE, (unsigned long long)win_load);
            LOG(SF2C_SLOWFRAME_S_SLOW_LLU_MORE_SLOW_FRAME_S_I_65BE, (unsigned long long)(win_busy + win_load),
                win_busy + win_load == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, s);
        }
        win_start = win_lines = win_busy = win_load = 0;
    }
    if (gap > (uint64_t)limit) {
        char lv[SF2C_SLOWFRAME_N_64_A68B];
        bool play = level_check(now, &changed_at, lv, sizeof lv);
        if (!win_start) win_start = now;
        if (!play || now - changed_at < LOAD_NS) win_load++;
        else if (win_lines >= LINES_PER_WINDOW) win_busy++;
        else {
            win_lines++;
            extern uint64_t renderer_present_count(void);
            char b[SF2C_SLOWFRAME_N_640_3F1B];
            int n = snprintf(b, sizeof b, SF2C_SLOWFRAME_S_SLOW_FRAME_LLU_LLU_MS_IN_S_7EDE, (unsigned long long)renderer_present_count(),
                             (unsigned long long)(gap / SF2C_SLOWFRAME_N_1000000_6CCE), lv);
            const char *sep = SF2C_SLOWFRAME_S_EMPTY_5210;
#define PART(...) do { if (n < (int)sizeof b) n += snprintf(b + n, sizeof b - n, __VA_ARGS__); sep = SF2C_SLOWFRAME_S_EMPTY_F240; } while (0)
            uint64_t v = d(cur.pipes, prev.pipes), hits = d(cur.hits, prev.hits);
            if (v) {
                PART(SF2C_SLOWFRAME_S_S_LLU_PIPELINE_S_BUILT_0F_MS_D1DD, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, d(cur.pipe_ns, prev.pipe_ns) / SF2C_SLOWFRAME_N_1E6_8C96);
                if (hits) PART(SF2C_SLOWFRAME_S_LLU_FROM_CACHE_7470, (unsigned long long)hits);
                PART(SF2C_SLOWFRAME_S_EMPTY_C921);
            }
            if ((v = d(cur.progs, prev.progs)))
                PART(SF2C_SLOWFRAME_S_S_LLU_PROGRAM_S_COMPILED_0F__9C08, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, d(cur.prog_ns, prev.prog_ns) / SF2C_SLOWFRAME_N_1E6_8C96);
            if ((v = d(cur.tr, prev.tr)))
                PART(SF2C_SLOWFRAME_S_S_LLU_SHADER_S_TRANSLATED_0F_D44E, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, d(cur.tr_ns, prev.tr_ns) / SF2C_SLOWFRAME_N_1E6_8C96);
            if ((v = d(cur.uploads, prev.uploads))) {
                uint64_t by = d(cur.up_bytes, prev.up_bytes);
                if (by < SF2C_SLOWFRAME_N_1048576_50B4) PART(SF2C_SLOWFRAME_S_S_LLU_TEXTURE_UPLOAD_S_LLU_K_87EC, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, (unsigned long long)(by + SF2C_SLOWFRAME_N_1023_6629) / SF2C_SLOWFRAME_N_1024_E39E);
                else PART(SF2C_SLOWFRAME_S_S_LLU_TEXTURE_UPLOAD_S_1F_MB_DE9C, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46, by / SF2C_SLOWFRAME_N_1048576_0_F652);
            }
            if ((v = d(cur.hashes, prev.hashes))) PART(SF2C_SLOWFRAME_S_S_LLU_FULL_TEXTURE_HASH_S_D675, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_ES_14BA);
            if ((v = d(cur.imports, prev.imports))) PART(SF2C_SLOWFRAME_S_S_LLU_HEAP_IMPORT_S_BF37, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46);
            v = d(cur.submits, prev.submits);
            PART(SF2C_SLOWFRAME_S_S_LLU_SUBMISSION_S_22FF, sep, (unsigned long long)v, v == 1 ? SF2C_SLOWFRAME_S_EMPTY_12AE : SF2C_SLOWFRAME_S_S_0F46);
            PART(SF2C_SLOWFRAME_S_SGPU_WAIT_0F_MS_F7BC, sep, d(cur.wait_ns, prev.wait_ns) / SF2C_SLOWFRAME_N_1E6_8C96);
            // (render cpu 0: no render thread - that work is the game thread's)
            if (prev.rcpu) PART(SF2C_SLOWFRAME_S_SRENDER_CPU_0F_MS_12B4, sep, d(cur.rcpu, prev.rcpu) / SF2C_SLOWFRAME_N_1E6_8C96);
            PART(SF2C_SLOWFRAME_S_SGAME_CPU_0F_MS_1EAA, sep, d(cur.gcpu, prev.gcpu) / SF2C_SLOWFRAME_N_1E6_8C96);
            // where the rest went: this thread waiting for the render thread, for a vsync / the pacer, in presents;
            // and the longest wait the render thread itself reported (rqueue.c rq_watch: nextDrawable, a GPU wait)
            PART(SF2C_SLOWFRAME_S_SQUEUE_WAIT_0F_MS_8449, sep, d(cur.rqw, prev.rqw) / SF2C_SLOWFRAME_N_1E6_8C96);
            PART(SF2C_SLOWFRAME_S_SVSYNC_PACE_WAIT_0F_MS_E801, sep, d(cur.blocked, prev.blocked) / SF2C_SLOWFRAME_N_1E6_8C96);
            if (d(cur.present, prev.present) >= SF2C_SLOWFRAME_N_1000000_6CCE) PART(SF2C_SLOWFRAME_S_SPRESENT_0F_MS_3EAC, sep, d(cur.present, prev.present) / SF2C_SLOWFRAME_N_1E6_8C96);
            extern uint64_t rq_render_longest(const char **what);
            const char *rw = NULL;
            uint64_t rwn = rq_render_longest(&rw);
            if (rw && rwn >= SF2C_SLOWFRAME_N_5000000_2618) PART(SF2C_SLOWFRAME_S_SRENDER_THREAD_LONGEST_S_0F__2DD6, sep, rw, rwn / SF2C_SLOWFRAME_N_1E6_8C96);
#undef PART
            LOG(SF2C_SLOWFRAME_S_S_N_1F0E, b);
        }
    }
    prev = cur;
}

#include "../../constants/nvn_vulkan.h"
// Executes recorded NVN command lists with Vulkan (port of renderer.m; same structure, same behaviour).
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_timer.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __linux__
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "../../kernel.h"
#include "shaders.h"
#include "vk.h"
#include "record.h"
#include "../renderer.h"
#include "../screen_filter.h"
#include "../shader_prep.h"
#include "../../settings.h"

static uint64_t stat_draws, stat_skipped, stat_passes, stat_dispatches, stat_wait_ns, stat_flushes, stat_feedback_copies;
static bool trace_render;

// (the guest heap for shaders in chunks: with the heap import below)
static uint32_t gc_shift;
static uint32_t gmem_chunk_shift(void);
static VkDescriptorSetLayout gmem_chunk_layout(void);
static void gc_scan_pointers(const uint32_t *w, uint64_t bytes);
static bool gc_lazy;
static int gc_imported;
static uint64_t gc_imported_bytes, stat_gc_misses;
static uint64_t gmem_heap_size(void);
static void gc_frame(void);
static uint64_t stat_upload_bytes;
static uint64_t prof_cb_reused, prof_cb_copied;  // (VK_PROF) lean constant blocks: copies reused, made (cbuf_lean)
static bool cbuf_lean(void);
static uint64_t prof_prog_memo, prof_pipe_memo, prof_key_draws;  // (VK_PROF) key memo answers (get_program), draws
static bool key_memo_on(void);
static uint64_t prof_sets_reused, prof_sets_new;  // (VK_PROF) set reuse (allocations: record.c RecStats)
static bool set_reuse_on(void);
static uint64_t prof_slot_wait;  // (VK_PROF) sub(): ring slot waits (ns; GPU waits, inside "pre" when begin_pass starts a submission)
// LARECOMP_VK_PROF=1: render-thread time per category in the fps log (exec = whole command lists, draw = draw(),
// sets = descriptor sets incl. cbuf copies, vtx = index scan + vertex copies, submit = vkQueueSubmit2)
static int prof = -1;
static uint64_t prof_exec, prof_draw, prof_sets, prof_vtx, prof_submit, prof_pre, prof_key, prof_dyn, prof_tex, prof_up, prof_gpu;
static VkQueryPool prof_qp;  // the GPU timer's timestamps (3 per ring slot: start, end, window image acquired)
static uint64_t prof_win, prof_span, prof_wframes;  // (profiling) the window image's submissions: GPU ns, frame spans, count
static uint64_t cum_exec, cum_wait, cum_draws, cum_submits;  // (profiling) never reset: hitch log
static uint32_t prof_end_line[SF2C_EXEC_N_4096_8B92];  // passes ended, by the exec.c line that ended them (0: submit / present)
uint64_t prof_views_created;  // textures.c vk_image_view cache misses
uint64_t prof_up_ns = ~SF2C_EXEC_N_0ULL_DF1B;  // textures.c upload checks (~0: profiling off)
#define PROF_T0() uint64_t prof_t0_ = prof ? SDL_GetTicksNS() : 0
#define PROF_ADD(v) do { if (prof) v += SDL_GetTicksNS() - prof_t0_; } while (0)
static _Atomic int stat_compiles;
static uint64_t op_counts[CMD_COUNT];
bool g_debug_frame;
static char stats_text[SF2C_EXEC_N_2048_BFA0];
void vk_collect_dead(void);

// ================================================================== submissions
// A submission = optional upload command buffer + the main one, signalling the timeline with its number.
// Every call on a command buffer, the descriptor pools and the queue goes through record.c (the recorder thread, or
// at once with it off): this file decides, the recorder calls. submit_seq counts what this thread handed to the
// recorder (what it will have submitted, in this order); rec_submitted() is what the driver has.
#define SUBS VK_SUBS
typedef struct {
    VkCommandBuffer main, upload;       // (allocated once, vk_exec_init: the pool is the recorder's from then on)
    bool main_begun, upload_begun, ts;  // ts: GPU timestamps written (the GPU timer)
    bool ts_skip;                       // (waits on the window image: not counted, see vk_gpu_total_ns)
    bool ts_win;                        // the window image's submission: a 3rd timestamp after its acquire wait
    uint64_t seq;
    // (its descriptor pools: record.c, per slot)
} Sub;
static Sub subs[SUBS];
static int cur_sub;
static void ss_forget(void);
static uint64_t submit_seq;  // last handed to the recorder (= submitted, with the recorder off)
static pthread_mutex_t seq_lock = PTHREAD_MUTEX_INITIALIZER;
static bool in_pass;
static int passes_in_sub;  // render passes in the submission being recorded (LARECOMP_CHUNK_PASSES, begin_pass)

// CPU / GPU overlap (as renderer.m: the Metal backend, metal-overlap). The game's frame loop submits a frame, presents
// it, and at once waits on that frame's fence (every SyncWait is on the newest submission), so it builds frame N+1
// only once the GPU has finished frame N: a frame costs CPU + GPU time in series. Here the wait is taken one fence
// late: SyncWait(fence N) waits for fence N-1's submission and returns, and the next SyncWait waits for N. The CPU
// builds frame N+1 while the GPU draws N; at most two frames of GPU work are queued; a fence already complete is never
// waited for. Everything this backend recycles is keyed on the timeline (sub(), vk_transient, the set pools after the
// slot's wait, readbacks and screenshots wait on their own submission), not on the game's fence, so only the game
// thread's wait changes. What the game may touch in that frame while the GPU still reads it: its per-frame vertex /
// index data in the guest heap (bound in place; an engine built to overlap double-buffers it anyway), uniform data is
// copied at translation (guest_copy), GPU results the CPU reads (texture / buffer copies) are flushed synchronously,
// and the storage buffers it rewrites one frame ahead get copies (snap_storage). The timing counters are read a frame
// before their GPU time is known: in this mode an end counter reports the latest completed frame's GPU span
// (cq_resolve keeps it host side and no longer writes the game's memory late - the game may have reused the slot).
// LARECOMP_GPU_OVERLAP=0: off (the wait on the fence's own submission, as before); =1: on.
#define VK_OVERLAP_DEFAULT 1  // on where the write watch can run (overlap_on: off by itself without page write tracking)
static uint64_t deferred_seq;   // (seq_lock) the fence the game was told is done but whose submission wasn't waited for
static uint64_t stat_deferred;  // SyncWaits returned without waiting (stats)
static uint64_t stat_snap_bytes, stat_snap_copies;  // storage copies made (snap_storage; stats, per period)
// The overlap needs the storage write watch (snap_storage: the OS's page write tracking), the only thing that catches
// the game rewriting its light lists with the same bytes while the GPU reads them. Where the OS gives no tracking
// (Linux over a userptr or dma-buf heap - AMD, the Steam Deck -, a kernel before 6.7 without PAGEMAP_SCAN, a
// container without userfaultfd, a Windows window reserved without write watch) the overlap turns itself off: the
// full fence wait as before. LARECOMP_GPU_OVERLAP=1 forces it on anyway (tests; the hash probe stands in for the
// watch then), =0 turns it off.
static int pages_cpu_written(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now);  // (the page tables below)
static const char *heap_tracking_reason(void);  // (why the heap has no page write tracking, for the log)
static bool overlap_on(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_GPU_OVERLAP_B13C), *w = getenv(SF2C_EXEC_S_LARECOMP_OVERLAP_WATCH_FEAE);
        uint32_t g;
        bool tracked = (!w || atoi(w) != 0) && pages_cpu_written(HEAP_BASE, SF2C_EXEC_N_4096_8B92, 0, &g) >= 0;
        if (e) on = atoi(e) != 0;
        else if (VK_OVERLAP_DEFAULT && !tracked) {
            on = 0;
            LOG(SF2C_EXEC_S_VK_GPU_OVERLAP_OFF_NO_PAGE_W_0515,
                w && atoi(w) == 0 ? SF2C_EXEC_S_LARECOMP_OVERLAP_WATCH_0_14BB : heap_tracking_reason());
            return on;
        } else on = VK_OVERLAP_DEFAULT;
        LOG(SF2C_EXEC_S_VK_GPU_OVERLAP_S_THE_GAME_S__C69C, on ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B,
            !on, on ? SF2C_EXEC_S_OFF_9F3B : SF2C_EXEC_S_ON_990C, on && !tracked ? SF2C_EXEC_S_FORCED_WITHOUT_PAGE_WRITE_TR_F335 : SF2C_EXEC_S_EMPTY_12AE);
    }
    return on;
}
static _Atomic uint64_t g_last_gpu_frame_ticks;  // (overlap) the latest resolved counter pair's GPU span, in Switch GPU ticks
static const uint64_t *cq_last_out;              // (cq_lock) the counter resolved last and its GPU ticks: a pair may straddle
static uint64_t cq_last_ticks;                   // two submissions
static uint64_t snap_ring_end(void);             // (snap_storage: the copies' ring after the game's heap, for gmem_heap_size)

uint64_t vk_completed_seq(void) {
    uint64_t v = 0;
    VK_CHECK(vkGetSemaphoreCounterValue(g_vk.dev, g_vk.timeline, &v));
    return v;
}

// (LARECOMP_VK_PROF) waits on the GPU by cause: the caller names it in wait_why just before waiting
static __thread const char *wait_why;
static struct { const char *why; uint64_t ns, n; } wait_tab[SF2C_EXEC_N_12_6B51];
static pthread_mutex_t wait_tab_lock = PTHREAD_MUTEX_INITIALIZER;
static void wait_account(uint64_t ns) {
    const char *why = wait_why ? wait_why : SF2C_EXEC_S_OTHER_D448;
    wait_why = NULL;
    pthread_mutex_lock(&wait_tab_lock);
    for (int i = 0; i < SF2C_EXEC_N_12_6B51; i++)
        if (!wait_tab[i].why || wait_tab[i].why == why) { wait_tab[i].why = why; wait_tab[i].ns += ns; wait_tab[i].n++; break; }
    pthread_mutex_unlock(&wait_tab_lock);
}

static void cq_resolve_done(uint64_t done);

void vk_wait_seq(uint64_t seq) {
    if (!seq) { wait_why = NULL; return; }
    if (seq > rec_submitted()) {  // not handed to the driver yet: the recorder first (a timeline value can't be waited for sooner)
        pthread_mutex_lock(&seq_lock);
        uint64_t issued = submit_seq;
        pthread_mutex_unlock(&seq_lock);
        if (seq > issued) {  // (not even queued: a caller's mistake - everything queued so far instead, logged)
            static int n;
            if (n++ < SF2C_EXEC_N_5_EF2D) LOG(SF2C_EXEC_S_VK_WAIT_FOR_SUBMISSION_LLU_O_E49F, (unsigned long long)seq, (unsigned long long)issued);
            seq = issued;
            if (!seq) { wait_why = NULL; return; }
        }
        rec_wait_submitted(seq);
    }
    VkSemaphoreWaitInfo wi = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &g_vk.timeline,
                              .pValues = &seq};
    uint64_t t0 = SDL_GetTicksNS();
    extern void rq_watch(const char *what, uint64_t arg);
    rq_watch(SF2C_EXEC_S_VKWAITSEMAPHORES_GPU_SUBMISS_EE9A, seq);
    VkResult r = vkWaitSemaphores(g_vk.dev, &wi, SF2C_EXEC_N_10ULL_D9E2 * SF2C_EXEC_N_1000000000ULL_E860);
    rq_watch(NULL, 0);
    uint64_t waited = SDL_GetTicksNS() - t0;  // (the game thread's fence waits and the render thread's: atomic sums)
    __atomic_fetch_add(&stat_wait_ns, waited, __ATOMIC_RELAXED);
    __atomic_fetch_add(&cum_wait, waited, __ATOMIC_RELAXED);
    wait_account(waited);
    // Callers recycle command buffers and allocations after this returns; a timeout is not completion.
    if (r == VK_TIMEOUT) {
        uint64_t done = 0;
        VkResult status = vkGetSemaphoreCounterValue(g_vk.dev, g_vk.timeline, &done);
        LOG(SF2C_EXEC_S_VK_TIMED_OUT_WAITING_FOR_LLU_72AF,
            (unsigned long long)seq, (unsigned long long)done, status, (unsigned long long)rec_submitted());
        extern void vk_devlost_dump(void);
        vk_devlost_dump();
        vk_device_fault_report();
        FATAL(SF2C_EXEC_S_VK_GPU_DID_NOT_FINISH_SUBMIS_9AF1,
              (unsigned long long)seq);
    } else if (r == VK_SUCCESS) cq_resolve_done(seq);  // (the game reads its timing counters after waiting)
    else {
        extern void vk_devlost_dump(void);
        vk_devlost_dump();
        vk_device_fault_report();
        FATAL(SF2C_EXEC_S_VKWAITSEMAPHORES_D_DEVICE_LO_4FF8, r);
    }
}

uint64_t vk_pending_seq(void) { return submit_seq + 1; }

// ---- the game's GPU timing counters (nvnCommandBufferReportCounter, two per frame)
// The game times its GPU work with these and drops to 30 fps when frames look slow. They used to get the host clock
// at translation time, so our command translation counted as GPU time: busy scenes (outdoors, ~400 draws) looked slow
// and the game locked itself at 30 even on fast GPUs, at any resolution. Now each counter also records a GPU
// timestamp at its place in the command stream; once that submission is done (the game waits on its fence before
// reading - on the Switch the GPU writes them too) the GPU time replaces the provisional host value, in the host
// clock's terms (offset calibrated at each resolve: a timestamp can't be later than the moment its submission is seen
// done). LARECOMP_VK_COUNTER=cpu: host clock only; =frozen: one value per presented frame (test); =legacy: the host
// clock in ns instead of GPU ticks, as before (test: reproduces the locked 30).
#define CQ_MAX SF2C_EXEC_CQ_MAX
typedef struct { VkQueryPool pool; uint32_t n; uint64_t seq; uint64_t *out[CQ_MAX]; } CounterQ;
static CounterQ cq[SUBS];
static pthread_mutex_t cq_lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t cq_offset;  // host ns - GPU ns (the smallest seen)
// Counter timestamps are in the Switch GPU's ticks (614.4 MHz): the game converts the difference to ns itself
// (x 625 / 384). Writing ns made every GPU time look 1.63x as long.
static uint64_t ns_to_gpu_ticks(uint64_t ns) { return (uint64_t)((unsigned __int128)ns * SF2C_EXEC_N_384_37B7 / SF2C_EXEC_N_625_AD72); }
static bool cq_calibrated;
static int counter_mode = -1;  // 0 cpu, 1 frozen, 2 gpu
static uint64_t stat_counters_gpu;

static int vk_counter_mode(void) {
    if (counter_mode < 0) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_COUNTER_DD98);
        counter_mode = e && !strcmp(e, SF2C_EXEC_S_CPU_C4FC) ? 0 : e && !strcmp(e, SF2C_EXEC_S_FROZEN_5756) ? 1 : e && !strcmp(e, SF2C_EXEC_S_LEGACY_E4C1) ? SF2C_EXEC_N_3_4E07 : SF2C_EXEC_N_2_D473;
        if (counter_mode == SF2C_EXEC_N_2_D473 && !g_vk.props.limits.timestampComputeAndGraphics) counter_mode = 0;
    }
    return counter_mode;
}

// ---- frame pacing (runtime/nvn/pace.c): the game's 30 fps mode when frames don't fit 60, through these counters
static void cq_resolve(int i) {  // (cq_lock held) submission slot i is done: its counters get their GPU times
    CounterQ *q = &cq[i];
    if (!q->n) return;
    uint64_t t[CQ_MAX];
    if (vkGetQueryPoolResults(g_vk.dev, q->pool, 0, q->n, sizeof t, t, SF2C_EXEC_N_8_2C62, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        double per = g_vk.props.limits.timestampPeriod;
        int64_t cand = (int64_t)plat_now_ns() - (int64_t)((double)t[q->n - 1] * per);
        if (!cq_calibrated || cand < cq_offset) cq_offset = cand, cq_calibrated = true;
        static int log = -1;  // LARECOMP_LOG_COUNTER=1: the host (provisional) and GPU times of each counter
        if (log < 0) log = getenv(SF2C_EXEC_S_LARECOMP_LOG_COUNTER_8119) != NULL;
        for (uint32_t k = 0; k < q->n; k++) {
            uint64_t gpu = ns_to_gpu_ticks((uint64_t)((int64_t)((double)t[k] * per) + cq_offset));
            if (log) LOG(SF2C_EXEC_S_COUNTER_SEQ_LLU_U_AT_LLX_HOS_B703, (unsigned long long)q->seq, k,
                         (unsigned long long)(uintptr_t)q->out[k], q->out[k][1] * SF2C_EXEC_N_625_0_6E29 / SF2C_EXEC_N_384_37B7 / SF2C_EXEC_N_1E6_8C96, gpu * SF2C_EXEC_N_625_0_6E29 / SF2C_EXEC_N_384_37B7 / SF2C_EXEC_N_1E6_8C96);
            if (overlap_on()) {
                // kept host side: the pair's span becomes the next end counter's reading (the game has read this pair
                // already - its fence wait returned a frame early - and may have reused the slot for the next frame)
                if (cq_last_out && q->out[k] - cq_last_out == SF2C_EXEC_N_2_D473 && gpu >= cq_last_ticks && gpu - cq_last_ticks < SF2C_EXEC_N_614400000ULL_F766)
                    atomic_store(&g_last_gpu_frame_ticks, gpu - cq_last_ticks);  // (an end counter 16 bytes after its start; under a second)
                cq_last_out = q->out[k], cq_last_ticks = gpu;
            } else {
                q->out[k][1] = gpu;
            }
        }
        stat_counters_gpu += q->n;
    }
    q->n = 0;
}

static void cq_resolve_done(uint64_t done) {  // every slot whose submission is done, in submission order
    pthread_mutex_lock(&cq_lock);
    for (;;) {
        int best = -1;
        for (int i = 0; i < SUBS; i++)
            if (cq[i].n && cq[i].seq <= done && (best < 0 || cq[i].seq < cq[best].seq)) best = i;
        if (best < 0) break;
        cq_resolve(best);
    }
    pthread_mutex_unlock(&cq_lock);
}

// ---- GPU timer (dynamic resolution, runtime/nvn/output_scale.c; and LARECOMP_VK_PROF's "gpu"): a timestamp at the
// start and the end of each submission's main command buffer, read when its ring slot comes round again (so a few
// submissions late; the controller averages half a second). Submissions overlap a little (the next one's commands
// may start before the previous one's end): their union is counted, the time the GPU was busy.
// The window image's submission (device.c: the game's image drawn into the window - FSR / scaling - and the menu) waits
// for the image, which can be a vsync's wait, so its start timestamp isn't its work's: it has a 3rd timestamp, written
// after the barrier that waits for the image (vk_gpu_timer_window_start), and its time is counted from there - in a
// total of its own (vk_gpu_split_ns): device.c adds it to what dynamic resolution sees unless
// LARECOMP_DYNRES_COUNT_PRESENT=0. Also per frame: the span from the GPU starting the frame's first game submission
// to the end of its window submission; span - game - window = the GPU idle / doing work no timestamp covers
// (uploads ahead of a submission's main command buffer, waiting for the window image).
static uint64_t gpu_total_ns, gpu_last_end;  // ns counted; (ticks) the latest submission end seen
static uint64_t gpu_win_ns, gpu_span_ns, gpu_frames, span_from;  // window submissions: ns, frame spans' ns, count; (ticks)
static bool gpu_timer_on;

static void gpu_timer_add(const uint64_t *t, bool skip, bool win) {
    double per = g_vk.props.limits.timestampPeriod;
    uint64_t t0 = t[0], t1 = t[1], tw = win ? t[SF2C_EXEC_N_2_D473] : 0;
    if (g_vk.ts_bits < SF2C_EXEC_N_64_A68B) { uint64_t m = (SF2C_EXEC_N_1ULL_622E << g_vk.ts_bits) - 1; t0 &= m, t1 &= m, tw &= m; }
    if (t1 < t0 || (t1 - t0) * per > SF2C_EXEC_N_1E9_EFAC) { span_from = 0; return; }  // (wrapped, or nonsense)
    if (gpu_last_end > t0 && (gpu_last_end - t0) * per > SF2C_EXEC_N_1E9_EFAC) gpu_last_end = 0;  // (the counter wrapped)
    if (span_from > t1 || (span_from && (t1 - span_from) * per > SF2C_EXEC_N_1E9_EFAC)) span_from = 0;
    uint64_t prev_end = gpu_last_end;
    uint64_t from = t0 > gpu_last_end ? t0 : gpu_last_end;
    if (t1 > gpu_last_end) gpu_last_end = t1;
    if (win) {  // from after the image was acquired (and the work before it ended), to the end
        if (tw >= t0 && tw <= t1) {
            uint64_t wf = tw > prev_end ? tw : prev_end;
            if (t1 > wf) {
                uint64_t ns = (uint64_t)((double)(t1 - wf) * per);
                __atomic_add_fetch(&gpu_win_ns, ns, __ATOMIC_RELAXED);
                if (prof) prof_win += ns;
            }
        }
        if (span_from && t1 > span_from) {
            uint64_t ns = (uint64_t)((double)(t1 - span_from) * per);
            __atomic_add_fetch(&gpu_span_ns, ns, __ATOMIC_RELAXED);
            __atomic_add_fetch(&gpu_frames, 1, __ATOMIC_RELAXED);
            if (prof) prof_span += ns, prof_wframes++;
        }
        span_from = 0;
        return;
    }
    if (!span_from && !skip) span_from = from;  // the frame's first game submission (from where the GPU got to it)
    if (skip || t1 <= from) return;
    uint64_t ns = (uint64_t)((double)(t1 - from) * per);
    __atomic_add_fetch(&gpu_total_ns, ns, __ATOMIC_RELAXED);
    if (prof) prof_gpu += ns;
}

// GPU time of all counted submissions so far (ns); -1: no GPU timer on this device
int64_t vk_gpu_total_ns(void) { return gpu_timer_on ? (int64_t)__atomic_load_n(&gpu_total_ns, __ATOMIC_RELAXED) : -1; }
// (cumulative) out: game submissions' GPU ns, window submissions' GPU ns, frame spans' ns, frames with a span.
// false: no GPU timer.
bool vk_gpu_split_ns(uint64_t out[SF2C_EXEC_N_4_4B22]) {
    if (!gpu_timer_on) return false;
    out[0] = __atomic_load_n(&gpu_total_ns, __ATOMIC_RELAXED);
    out[1] = __atomic_load_n(&gpu_win_ns, __ATOMIC_RELAXED);
    out[SF2C_EXEC_N_2_D473] = __atomic_load_n(&gpu_span_ns, __ATOMIC_RELAXED);
    out[SF2C_EXEC_N_3_4E07] = __atomic_load_n(&gpu_frames, __ATOMIC_RELAXED);
    return true;
}
uint64_t vk_cum_wait_ns(void) { return __atomic_load_n(&cum_wait, __ATOMIC_RELAXED); }  // (all vk_wait_seq waits so far)

static Sub *sub(void) {
    Sub *s = &subs[cur_sub];
    if (s->seq != submit_seq + 1) {  // slot reused: its previous submission must be done
        wait_why = SF2C_EXEC_S_RING_SLOT_01C8;
        uint64_t tw = prof ? SDL_GetTicksNS() : 0;
        vk_wait_seq(s->seq);
        if (prof) prof_slot_wait += SDL_GetTicksNS() - tw;
        cq_resolve_done(s->seq);
        if (s->ts) {  // GPU time of that submission
            uint64_t t[SF2C_EXEC_N_3_4E07];
            uint32_t nq = s->ts_win ? SF2C_EXEC_N_3_4E07 : SF2C_EXEC_N_2_D473;
            if (vkGetQueryPoolResults(g_vk.dev, prof_qp, (uint32_t)cur_sub * SF2C_EXEC_N_3_4E07, nq, sizeof t, t, SF2C_EXEC_N_8_2C62, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
                gpu_timer_add(t, s->ts_skip, s->ts_win);
            s->ts = s->ts_skip = s->ts_win = false;
        }
        rec_reset_pools(cur_sub);  // (its sets went with the submission; the recorder resets in order)
        pthread_mutex_lock(&seq_lock);
        s->main_begun = s->upload_begun = false;
        s->seq = submit_seq + 1;
        pthread_mutex_unlock(&seq_lock);
    }
    return s;
}

static void begin_cb(VkCommandBuffer *cb, bool *begun) {
    if (*begun) return;
    rec_BeginCommandBuffer(*cb);
    pthread_mutex_lock(&seq_lock);
    *begun = true;
    pthread_mutex_unlock(&seq_lock);
}

static void gp_mark(VkCommandBuffer cb, const char *fmt, ...);
VkCommandBuffer vk_cmd(void) {
    Sub *s = sub();
    if (!s->main_begun) {
        begin_cb(&s->main, &s->main_begun);
        ss_forget();  // (a new command buffer starts with no state)
        if (vk_counter_mode() == SF2C_EXEC_N_2_D473) {  // this slot's timing counter queries (reset outside any render pass)
            CounterQ *q = &cq[cur_sub];
            if (!q->pool) {
                VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                            .queryCount = CQ_MAX};
                if (vkCreateQueryPool(g_vk.dev, &qi, NULL, &q->pool) != VK_SUCCESS) q->pool = VK_NULL_HANDLE;
            }
            pthread_mutex_lock(&cq_lock);
            q->n = 0;  // (resolved when the slot was freed: sub())
            q->seq = s->seq;
            pthread_mutex_unlock(&cq_lock);
            if (q->pool) rec_CmdResetQueryPool(s->main, q->pool, 0, CQ_MAX);
        }
        gp_mark(s->main, SF2C_EXEC_S_SUBMISSION_START_INCLUDES_GP_BC4D);
        if (prof_qp) {
            rec_CmdResetQueryPool(s->main, prof_qp, (uint32_t)cur_sub * SF2C_EXEC_N_3_4E07, SF2C_EXEC_N_3_4E07);
            rec_CmdWriteTimestamp2(s->main, VK_PIPELINE_STAGE_2_NONE, prof_qp, (uint32_t)cur_sub * SF2C_EXEC_N_3_4E07);
            s->ts = true;
        }
    }
    return s->main;
}

// (device.c vk_frame_begin) the window image's submission: its work starts here, after the barrier that waits for the
// image (ALL_COMMANDS: after it, so not while the GPU waits for the image)
void vk_gpu_timer_window_start(void) {
    Sub *s = sub();
    if (!s->ts || s->ts_win) return;
    rec_CmdWriteTimestamp2(s->main, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, prof_qp, (uint32_t)cur_sub * SF2C_EXEC_N_3_4E07 + SF2C_EXEC_N_2_D473);
    s->ts_win = true;
}

VkCommandBuffer vk_upload_cmd(void) {
    Sub *s = sub();
    if (!s->upload_begun) {
        begin_cb(&s->upload, &s->upload_begun);
        // after everything submitted before it: a submission's commands may start while the previous one's still run,
        // and these copies overwrite what those may read (textures being replaced, vertex cache pages)
        vk_full_barrier(s->upload);
    }
    return s->upload;
}

void vk_full_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    rec_CmdPipelineBarrier2(cb, &di);
}

static void ring_retire(void);
void vc_flush(void);

// ---- LARECOMP_GPUPROF_AT=<frame>: GPU time per pass / dispatch / copy of that frame, logged as "[gpuprof]".
// A timestamp (ALL_COMMANDS: after everything before it) at each mark; a row = the work since the previous mark.
bool g_gpuprof_frame;
static VkQueryPool gp_qp;
static int gp_n;
static char gp_label[SF2C_EXEC_N_512_94F8][SF2C_EXEC_N_112_B155];
static uint32_t gp_draws[SF2C_EXEC_N_512_94F8];
// LARECOMP_VK_DEVLOST_TRACE=1: the last 64 GPU operations, dumped on device loss (with LARECOMP_SYNC_SUBMIT=1 the
// dump ends with the command list that faulted).
static char dl_ring[SF2C_EXEC_N_64_A68B][SF2C_EXEC_N_128_2747];
static unsigned dl_n;
static int dl_on = -1;
static void dl_note(const char *fmt, ...) {
    if (dl_on < 0) dl_on = getenv(SF2C_EXEC_S_LARECOMP_VK_DEVLOST_TRACE_28EE) ? atoi(getenv(SF2C_EXEC_S_LARECOMP_VK_DEVLOST_TRACE_28EE)) : 0;
    if (!dl_on) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dl_ring[dl_n++ & SF2C_EXEC_N_63_DA4E], sizeof dl_ring[0], fmt, ap);
    va_end(ap);
}
void vk_dl_note_upload(void *img, uint32_t w, uint32_t h, uint32_t lv, VkFormat fmt, uint64_t off, uint32_t row) {
    dl_note(SF2C_EXEC_S_UPLOAD_P_UX_U_LV_U_FMT_D_OFF_C2EA, img, w, h, lv, fmt, (unsigned long long)off, row);
}
void vk_devlost_dump(void) {
    if (!dl_on) return;
    LOG(SF2C_EXEC_S_VK_LAST_GPU_OPERATIONS_BEFOR_913C);
    for (unsigned i = dl_n > SF2C_EXEC_N_64_A68B ? dl_n - SF2C_EXEC_N_64_A68B : 0; i < dl_n; i++) LOG(SF2C_EXEC_S_VK_S_N_6E47, dl_ring[i & SF2C_EXEC_N_63_DA4E]);
}
static void gp_mark(VkCommandBuffer cb, const char *fmt, ...) {
    if (!g_gpuprof_frame || gp_n >= SF2C_EXEC_N_511_2C69) return;
    if (!gp_qp) {
        VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = SF2C_EXEC_N_512_94F8};
        if (vkCreateQueryPool(g_vk.dev, &qi, NULL, &gp_qp) != VK_SUCCESS) { g_gpuprof_frame = false; return; }
    }
    if (!gp_n) rec_CmdResetQueryPool(cb, gp_qp, 0, SF2C_EXEC_N_512_94F8);
    rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, gp_qp, (uint32_t)gp_n);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(gp_label[gp_n], sizeof gp_label[0], fmt, ap);
    va_end(ap);
    gp_draws[gp_n] = 0;
    gp_n++;
}
void renderer_flush(void);
void vk_gpuprof_report(void) {
    if (!gp_n) return;
    renderer_flush();
    uint64_t t[SF2C_EXEC_N_512_94F8];
    if (vkGetQueryPoolResults(g_vk.dev, gp_qp, 0, (uint32_t)gp_n, sizeof t, t, SF2C_EXEC_N_8_2C62, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        double per = g_vk.props.limits.timestampPeriod / SF2C_EXEC_N_1E6_8C96, total = (t[gp_n - 1] - t[0]) * per;
        LOG(SF2C_EXEC_S_GPUPROF_D_MARKS_2F_MS_FIRST__03A8, gp_n, total);
        for (int i = 0; i + 1 < gp_n; i++) {
            double ms = (t[i + 1] - t[i]) * per;
            LOG(SF2C_EXEC_S_GPUPROF_6_3F_MS_5_1F_S_S_0D9C, ms, SF2C_EXEC_N_100_AD57 * ms / (total > 0 ? total : 1), gp_label[i], gp_draws[i] ? SF2C_EXEC_S_EMPTY_12AE : SF2C_EXEC_S_N_D7A5);
            if (gp_draws[i]) LOG(SF2C_EXEC_S_U_DRAWS_N_B6FC, gp_draws[i]);
        }
    }
    gp_n = 0;
}

// Submit the pending work (if any). wait/signal: binary semaphores for presentation (may be null).
void vk_submit_extra(VkSemaphore wait, VkSemaphore signal, VkCommandBuffer extra) {
    (void)extra;
    vk_end_pass();
    Sub *s = sub();
    if (!s->main_begun && !s->upload_begun && !wait && !signal) return;
    VkCommandBufferSubmitInfo cbs[SF2C_EXEC_N_2_D473];
    int n = 0;
    vc_flush();
    if (s->upload_begun) {
        vk_full_barrier(s->upload);  // uploads complete before anything in the main buffer reads them
        rec_EndCommandBuffer(s->upload);
        cbs[n++] = (VkCommandBufferSubmitInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = s->upload};
    }
    vk_cmd();  // (an empty main buffer still carries the semaphores)
    s->ts_skip = wait != VK_NULL_HANDLE;
    if (s->ts_win && !s->ts_skip) s->ts_win = false;  // (not the window image's after all: counted as game work)
    if (s->ts) rec_CmdWriteTimestamp2(s->main, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, prof_qp, (uint32_t)cur_sub * SF2C_EXEC_N_3_4E07 + 1);
    rec_EndCommandBuffer(s->main);
    cbs[n++] = (VkCommandBufferSubmitInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = s->main};
    VkSemaphoreSubmitInfo sig[SF2C_EXEC_N_2_D473] = {{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = g_vk.timeline,
                                     .value = submit_seq + 1, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
                                    {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = signal,
                                     .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
    VkSemaphoreSubmitInfo wt = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = wait,
                                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo2 si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = (uint32_t)n, .pCommandBufferInfos = cbs,
                        .signalSemaphoreInfoCount = signal ? SF2C_EXEC_N_2_D473 : 1, .pSignalSemaphoreInfos = sig,
                        .waitSemaphoreInfoCount = wait ? 1 : 0, .pWaitSemaphoreInfos = &wt};
    dl_note(SF2C_EXEC_S_SUBMIT_LLU_D490, (unsigned long long)submit_seq + 1);
    rec_QueueSubmit2(&si, submit_seq + 1);  // (the recorder: in order with the commands; its vkQueueSubmit2 time is in RecStats)
    pthread_mutex_lock(&seq_lock);
    submit_seq++;
    cur_sub = (cur_sub + 1) % SUBS;
    pthread_mutex_unlock(&seq_lock);
    passes_in_sub = 0;
    stat_flushes++;
    cum_submits++;
    ring_retire();
    vk_collect_dead();
    if (signal) gc_frame();  // (a present: once a frame)
}

void renderer_submit(void) { vk_submit_extra(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE); }

static void overlap_clear(void) {  // (a full wait was made: everything queued so far is done)
    pthread_mutex_lock(&seq_lock);
    deferred_seq = 0;
    pthread_mutex_unlock(&seq_lock);
}

void renderer_flush(void) {
    renderer_submit();
    if (!wait_why) wait_why = SF2C_EXEC_S_FLUSH_D030;
    vk_wait_seq(submit_seq);
    overlap_clear();
}

// The latest submission the driver has (rqueue.h): with the recorder, behind renderer_issued_seq until it catches up.
uint64_t renderer_submit_seq(void) { return rec_submitted(); }

// The latest submission this backend queued (rqueue.c: an item's work ends with it; renderer_wait_seq waits for the
// recorder to hand it to the driver, then for the GPU)
uint64_t renderer_issued_seq(void) {
    pthread_mutex_lock(&seq_lock);
    uint64_t s = submit_seq;
    pthread_mutex_unlock(&seq_lock);
    return s;
}

void renderer_sync(void) {
    pthread_mutex_lock(&seq_lock);
    uint64_t s = submit_seq;
    pthread_mutex_unlock(&seq_lock);
    wait_why = SF2C_EXEC_S_QUEUEFINISH_SYNC_ALL_585E;
    vk_wait_seq(s);
    overlap_clear();
}

// (overlap) The game's SyncWait on submission seq: done is the latest complete submission, *deferred the one a previous
// call returned early for (0: none). Returns the submission to wait for now (0: none); updates *deferred. A fence
// that is complete is never waited for; a newer fence than the deferred one waits for the deferred one and takes its
// place; the deferred fence itself, or an older one, is waited for. (tools/test_vulkan_overlap.py)
static uint64_t overlap_defer(uint64_t seq, uint64_t done, uint64_t *deferred) {
    uint64_t prev = *deferred;
    if (seq <= done) {  // complete (in order: a deferred one no newer than it is complete too)
        if (seq >= prev) *deferred = 0;
        return 0;
    }
    if (seq > prev) {  // a newer fence: the deferred one is waited for now, this one at the next call
        *deferred = seq;
        return prev > done ? prev : 0;
    }
    if (seq == prev) *deferred = 0;  // the deferred fence again, or an older one: a real wait on it
    return seq;
}

uint64_t renderer_fence_seq(void) {
    pthread_mutex_lock(&seq_lock);
    Sub *s = &subs[cur_sub];
    // A reused slot still has its old begun flags until sub() prepares it. An empty queue fence must refer to
    // the last submission, not a future value that submit's empty-work fast path will never signal.
    bool pending = s->seq == submit_seq + 1 && (s->main_begun || s->upload_begun);
    uint64_t q = submit_seq + (pending ? 1 : 0);
    pthread_mutex_unlock(&seq_lock);
    return q;
}

void renderer_wait_seq(uint64_t seq) {
    if (!seq) return;
    pthread_mutex_lock(&seq_lock);
    uint64_t s = submit_seq;
    pthread_mutex_unlock(&seq_lock);
    wait_why = seq > s ? SF2C_EXEC_S_SYNCWAIT_FENCE_NOT_SUBMITTED_27F2 : SF2C_EXEC_S_SYNCWAIT_42FB;
    if (seq > s) seq = s;  // not queued yet (other thread): everything queued so far (vk_wait_seq: the recorder, then the GPU)
    if (overlap_on()) {  // (see deferred_seq) wait for the fence deferred last time, defer this one
        uint64_t done = vk_completed_seq();  // (the timeline's value: a submission the recorder hasn't made yet isn't done)
        pthread_mutex_lock(&seq_lock);
        uint64_t w = overlap_defer(seq, done, &deferred_seq);
        pthread_mutex_unlock(&seq_lock);
        if (!w) {
            __atomic_fetch_add(&stat_deferred, 1, __ATOMIC_RELAXED);  // (the game thread; stats read on the render thread)
            wait_why = NULL;
            cq_resolve_done(done);  // (the game reads its timing counters now: what is complete so far)
            return;
        }
        seq = w;
    }
    vk_wait_seq(seq);
}

// A descriptor set for the pending submission (record.c: the slot's pools, allocated in batches per layout). With
// the recorder on it is a stand-in, to be written and bound through rec_* only, and only for this submission.
VkDescriptorSet vk_alloc_set(VkDescriptorSetLayout layout) {
    sub();
    return rec_alloc_set(cur_sub, layout);
}

// ================================================================== transient memory ring
// 64 MB host-visible chunks (device-local when the GPU exposes resizable BAR); a chunk is reused once every
// submission that allocated from it has completed. Without resizable BAR the device-local host-visible memory is a
// small window (256 MB on a GTX 1060): once it's full, later chunks go to system memory (issue #45: it stopped the
// game with "out of device memory").
#define CHUNK_SIZE (SF2C_EXEC_N_64ULL_DE1A << SF2C_EXEC_N_20_F5CA)
typedef struct { VkBuffer buf; VmaAllocation mem; uint8_t *ptr; uint64_t size, used, seq; } Chunk;
static Chunk *chunks;
static int nchunks, active = -1;

static int new_chunk(uint64_t size) {
    chunks = realloc(chunks, (nchunks + 1) * sizeof *chunks);
    Chunk *c = &chunks[nchunks];
    memset(c, 0, sizeof *c);
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    static bool sysmem;  // (the device-local window ran out: system memory from then on)
    // Without resizable BAR the device-local host-visible window is ~256 MB, shared with other programs and the driver:
    // two 64 MB chunks took 128 of its 132 MB in use while its budget fell to 81 MB (RTX 3080 Ti), and a GTX 1060 ran
    // out (issue #45). A chunk goes there only while the window's use stays under half its budget; past that, system
    // memory (~1 ms more GPU time a frame on a 3080 Ti at 1x). The first chunks, which most frames use, stay fast.
    // LARECOMP_VK_RING_BAR=1: no budget check.
    bool spill = false;
    if (!sysmem && !getenv(SF2C_EXEC_S_LARECOMP_VK_RING_BAR_E550)) {
        uint32_t type;
        if (vmaFindMemoryTypeIndexForBufferInfo(g_vk.vma, &bi, &ai, &type) == VK_SUCCESS) {
            const VkPhysicalDeviceMemoryProperties *mp;
            vmaGetMemoryProperties(g_vk.vma, &mp);
            uint32_t heap = mp->memoryTypes[type].heapIndex;
            VmaBudget bud[VK_MAX_MEMORY_HEAPS];
            vmaGetHeapBudgets(g_vk.vma, bud);
            spill = (mp->memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                    bud[heap].usage + size > bud[heap].budget / SF2C_EXEC_N_2_D473;
            static bool logged;
            if (spill && !logged) {
                logged = true;
                LOG(SF2C_EXEC_S_VK_TRANSIENT_RING_DEVICE_LOC_9CB6,
                    (unsigned long long)(bud[heap].usage >> SF2C_EXEC_N_20_F5CA), (unsigned long long)(bud[heap].budget >> SF2C_EXEC_N_20_F5CA));
            }
        }
    }
    if (sysmem || spill || getenv(SF2C_EXEC_S_LARECOMP_VK_RING_SYSMEM_6E5D)) ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;  // (test: a GPU without resizable BAR)
    VmaAllocationInfo info;
    VkResult r = vmaCreateBuffer(g_vk.vma, &bi, &ai, &c->buf, &c->mem, &info);
    bool moved = false;
    // (a driver may report the full window as out of host memory too; if memory is really gone, the retry fails as well)
    if ((r == VK_ERROR_OUT_OF_DEVICE_MEMORY || r == VK_ERROR_OUT_OF_HOST_MEMORY) && ai.usage != VMA_MEMORY_USAGE_AUTO_PREFER_HOST) {
        LOG(SF2C_EXEC_S_VK_TRANSIENT_RING_DEVICE_LOC_07EC, nchunks);
        vk_log_memory(SF2C_EXEC_S_RING_CHUNK_FAILED_D33A);
        sysmem = moved = true;
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        r = vmaCreateBuffer(g_vk.vma, &bi, &ai, &c->buf, &c->mem, &info);
    }
    if (r != VK_SUCCESS) {  // (system memory too: vk_transient waits for a chunk to come free instead - issue #45)
        LOG(SF2C_EXEC_S_VK_TRANSIENT_RING_NO_MEMORY__D41B, nchunks, (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), r);
        vk_log_memory(SF2C_EXEC_S_RING_CHUNK_FAILED_D33A);
        return -1;
    }
    c->ptr = info.pMappedData;
    c->size = size;
    static bool spilled;  // (the first chunk in system memory: logged too)
    if (!nchunks || moved || (spill && !spilled)) {  // where the ring lives: device-local (resizable BAR) or system memory read across PCIe
        if (spill) spilled = true;
        VkMemoryPropertyFlags f;
        vmaGetMemoryTypeProperties(g_vk.vma, info.memoryType, &f);
        LOG(SF2C_EXEC_S_VK_TRANSIENT_RING_MEMORY_TYP_1CAD, info.memoryType, f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? SF2C_EXEC_S_DEVICE_LOCAL_A2AD : SF2C_EXEC_S_SYSTEM_MEMORY_1624,
            f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? SF2C_EXEC_S_HOST_VISIBLE_1697 : SF2C_EXEC_S_EMPTY_12AE, f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? SF2C_EXEC_S_CACHED_ADBA : SF2C_EXEC_S_EMPTY_12AE);
    }
    return nchunks++;
}

static void ring_retire(void) {}  // chunks are recycled lazily in vk_transient

VkSpan vk_transient(uint64_t size, uint64_t align) {
    if (align < SF2C_EXEC_N_16_B17E) align = SF2C_EXEC_N_16_B17E;
    uint64_t pending = vk_pending_seq();
    if (active >= 0) {
        Chunk *c = &chunks[active];
        uint64_t off = (c->used + align - 1) & ~(align - 1);
        if (off + size <= c->size) {
            c->used = off + size;
            c->seq = pending;
            stat_upload_bytes += size;
            return (VkSpan){c->buf, off, c->ptr + off};
        }
    }
    uint64_t done = vk_completed_seq();
    int pick = -1;
    for (int i = 0; i < nchunks; i++)
        if (i != active && chunks[i].seq <= done && chunks[i].size >= size) { pick = i; break; }
    if (pick < 0) pick = new_chunk(size > CHUNK_SIZE ? size : CHUNK_SIZE);
    if (pick < 0) {  // no memory for another chunk (a GTX 1060 with 6 GB: issue #45): wait for the work already submitted,
        // which frees the chunks only it used, and take one of those (the recording in progress keeps its own)
        vk_wait_seq(submit_seq);
        done = vk_completed_seq();
        for (int i = 0; i < nchunks; i++)
            if (i != active && chunks[i].seq <= done && chunks[i].size >= size) { pick = i; break; }
        if (pick < 0) FATAL(SF2C_EXEC_S_THE_GRAPHICS_CARD_RAN_OUT_OF_9F0C,
                            (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), nchunks);
        static int nlog;
        if (nlog++ < SF2C_EXEC_N_5_EF2D) LOG(SF2C_EXEC_S_VK_TRANSIENT_RING_REUSED_CHU_A956, pick);
    }
    active = pick;
    chunks[pick].used = size;
    chunks[pick].seq = pending;
    stat_upload_bytes += size;
    return (VkSpan){chunks[pick].buf, 0, chunks[pick].ptr};
}

// GPU -> CPU copies: the ring is write-combined (often device-local over the BAR), where CPU reads crawl (a
// 1280x720 screenshot took 7 s). Readbacks use one host-cached buffer instead. Every user waits for the GPU and
// reads the data before the next readback (render thread), so a single grow-only buffer suffices.
VkSpan vk_readback(uint64_t size) {
    static VkBuffer buf;
    static VmaAllocation mem;
    static uint8_t *ptr;
    static uint64_t cap;
    if (size > cap) {
        if (buf) { vk_wait_seq(submit_seq); vmaDestroyBuffer(g_vk.vma, buf, mem); }
        cap = (size + (SF2C_EXEC_N_16_B17E << SF2C_EXEC_N_20_F5CA) - 1) & ~((SF2C_EXEC_N_16ULL_001B << SF2C_EXEC_N_20_F5CA) - 1);
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = cap, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        VK_CHECK(vmaCreateBuffer(g_vk.vma, &bi, &ai, &buf, &mem, &info));
        ptr = info.pMappedData;
    }
    return (VkSpan){buf, 0, ptr};
}

VkSpan vk_transient_copy(const void *data, uint64_t size, uint64_t align) {
    VkSpan s = vk_transient(size ? size : SF2C_EXEC_N_16_B17E, align);
    if (size) memcpy(s.ptr, data, size);
    return s;
}

// Per-draw constants that usually repeat the previous draw's (scale table, driver constants): reuse that copy
// while it's identical, within one submission. slot: one per kind of data.
enum { DEDUPE_RSCALE_V, DEDUPE_RSCALE_F, DEDUPE_DRV_V, DEDUPE_DRV_F, DEDUPE_DRV_C, DEDUPE_SLOTS };
static VkSpan copy_dedupe(int slot, const void *data, uint64_t size, uint64_t align) {
    static struct { uint64_t seq, size; uint8_t data[SF2C_EXEC_N_0X800_D398]; VkSpan span; } last[DEDUPE_SLOTS];
    uint64_t seq = vk_pending_seq();
    if (size > sizeof last[0].data) return vk_transient_copy(data, size, align);
    if (last[slot].seq == seq && last[slot].size == size && !memcmp(last[slot].data, data, size)) return last[slot].span;
    VkSpan s = vk_transient_copy(data, size, align);
    last[slot].seq = seq; last[slot].size = size; memcpy(last[slot].data, data, size); last[slot].span = s;
    return s;
}

// Guest memory copied once per submission: draws binding the same range share the copy.
#define GCOPY_TAB SF2C_EXEC_GCOPY_TAB
static struct { uint64_t addr, size, seq; VkSpan span; uint64_t scanned; } gcopy[GCOPY_TAB];  // (scanned: heap size then, 0 not)
static uint64_t gcopy_gen;  // bumped to drop every shared copy (guest memory written mid-list)
static void guest_copy_flush(void) { gcopy_gen++; }
// scan: (lazy heap chunks, a uniform buffer of a shader with global memory) the range is also scanned for heap
// pointers (gc_scan_pointers), once per shared copy: its contents can't change while the copy is shared (again if the
// heap grew since: pointers past its end were passed over).
static bool gc_scan_needed(void);
static uint64_t prof_scan_calls, prof_scan_bytes, prof_scan_skipped;  // (VK_PROF, lazy chunks) pointer scans
static VkSpan guest_copy_ex(uint64_t addr, uint64_t size, bool scan) {
    uint64_t seq = vk_pending_seq() + (gcopy_gen << SF2C_EXEC_N_40_D59E), h = (addr * SF2C_EXEC_N_0X9E3779B97F4A7C15ULL_8CC5) ^ (size * SF2C_EXEC_N_0XC2B2AE3D27D4EB4FULL_ACE8);
    uint32_t slot = (uint32_t)(h ^ (h >> SF2C_EXEC_N_32_E29C)) & (GCOPY_TAB - 1);
    bool hit = gcopy[slot].seq == seq && gcopy[slot].addr == addr && gcopy[slot].size == size;
    uint64_t heap = 0;
    if (scan) {
        heap = gmem_heap_size();
        if ((!hit || gcopy[slot].scanned != heap) && gc_scan_needed()) gc_scan_pointers((const uint32_t *)(uintptr_t)addr, size);
        else prof_scan_skipped++;
    }
    if (hit) {
        if (scan) gcopy[slot].scanned = heap;
        return gcopy[slot].span;
    }
    VkSpan s = vk_transient_copy((const void *)(uintptr_t)addr, size, SF2C_EXEC_N_256_51E8);
    gcopy[slot].addr = addr; gcopy[slot].size = size; gcopy[slot].seq = seq; gcopy[slot].span = s; gcopy[slot].scanned = heap;
    return s;
}
static VkSpan guest_copy(uint64_t addr, uint64_t size) { return guest_copy_ex(addr, size, false); }

// ================================================================== worker threads (shader / pipeline builds)
// First-use compiles run off the render thread; draws needing them are skipped until ready (as on Metal).
// LARECOMP_SYNC_SHADERS=1 builds on first use, blocking (deterministic tests).
typedef struct Job { void (*fn)(void *); void *arg; struct Job *next; } Job;
static Job *jobs_head, *jobs_tail;
static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t jobs_cond = PTHREAD_COND_INITIALIZER;

static void *worker(void *a) {
    (void)a;
    for (;;) {
        pthread_mutex_lock(&jobs_lock);
        while (!jobs_head) pthread_cond_wait(&jobs_cond, &jobs_lock);
        Job *j = jobs_head;
        jobs_head = j->next;
        if (!jobs_head) jobs_tail = NULL;
        pthread_mutex_unlock(&jobs_lock);
        j->fn(j->arg);
        free(j);
    }
    return NULL;
}

static bool sync_shaders(void) {
    static int v = -1;
    if (v < 0) v = getenv(SF2C_EXEC_S_LARECOMP_SYNC_SHADERS_2655) != NULL;
    return v;
}

// Shader translation: one dedicated thread (the translator isn't re-entrant - translate.c's RSC2 string ring - and
// translating on the game thread cost up to ~90 ms per frame on entering an area). Compiles go on to the workers.
static pthread_mutex_t translate_lock = PTHREAD_MUTEX_INITIALIZER;  // (compute shaders translate on the game thread)
static Job *tjobs_head, *tjobs_tail;
static pthread_mutex_t tjobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tjobs_cond = PTHREAD_COND_INITIALIZER;
static void *translator(void *a) {
    (void)a;
    for (;;) {
        pthread_mutex_lock(&tjobs_lock);
        while (!tjobs_head) pthread_cond_wait(&tjobs_cond, &tjobs_lock);
        Job *j = tjobs_head;
        tjobs_head = j->next;
        if (!tjobs_head) tjobs_tail = NULL;
        pthread_mutex_unlock(&tjobs_lock);
        j->fn(j->arg);
        free(j);
    }
    return NULL;
}
static bool sync_shaders(void);
static void run_translate(void (*fn)(void *), void *arg) {
    if (sync_shaders()) { fn(arg); return; }
    static bool started;
    if (!started) { started = true; pthread_t t; pthread_create(&t, NULL, translator, NULL); }
    Job *j = malloc(sizeof *j);
    j->fn = fn; j->arg = arg; j->next = NULL;
    pthread_mutex_lock(&tjobs_lock);
    if (tjobs_tail) tjobs_tail->next = j; else tjobs_head = j;
    tjobs_tail = j;
    pthread_cond_signal(&tjobs_cond);
    pthread_mutex_unlock(&tjobs_lock);
}

static void run_job(void (*fn)(void *), void *arg) {
    if (sync_shaders()) { fn(arg); return; }
    static bool started;
    if (!started) {
        started = true;
        // first entry into an area builds dozens of pipelines at once: use most of the CPU (draws wait for them)
        int n = plat_cpu_count();  // (the process's affinity mask: a process held to a few cores, few workers)
        if (n <= 0) n = SDL_GetNumLogicalCPUCores();
        n -= SF2C_EXEC_N_4_4B22;
        if (n < SF2C_EXEC_N_4_4B22) n = SF2C_EXEC_N_4_4B22;
        if (n > SF2C_EXEC_N_16_B17E) n = SF2C_EXEC_N_16_B17E;
        for (int i = 0; i < n; i++) { pthread_t t; pthread_create(&t, NULL, worker, NULL); }
    }
    Job *j = malloc(sizeof *j);
    j->fn = fn; j->arg = arg; j->next = NULL;
    pthread_mutex_lock(&jobs_lock);
    if (jobs_tail) jobs_tail->next = j; else jobs_head = j;
    jobs_tail = j;
    pthread_cond_signal(&jobs_cond);
    pthread_mutex_unlock(&jobs_lock);
}

// ================================================================== state
typedef struct {
    NvnTexture *color[SF2C_EXEC_N_8_2C62];
    NvnTextureView cview[SF2C_EXEC_N_8_2C62];
    uint8_t has_cview[SF2C_EXEC_N_8_2C62];
    int ncolor;
    NvnTexture *depth;
    NvnTextureView dview;
    uint8_t has_dview;
} RtState;

typedef struct { uint64_t addr, size; } Range;

static struct {
    RtState rt;
    bool rt_dirty;
    NvnProgram *program;
    NvnProgram combined_program;
    uint8_t blend[SF2C_EXEC_N_8_2C62][SF2C_EXEC_N_8_2C62];
    uint32_t channel_mask;
    uint8_t color_state[SF2C_EXEC_N_4_4B22], ds[SF2C_EXEC_N_8_2C62], ms[SF2C_EXEC_N_0X18_D47C], poly[SF2C_EXEC_N_4_4B22];
    float poly_offset[SF2C_EXEC_N_3_4E07];
    uint32_t attrib[SF2C_EXEC_N_16_B17E];
    int nattrib;
    uint32_t stream[SF2C_EXEC_N_16_B17E][SF2C_EXEC_N_2_D473];
    int nstream;
    Range vbuf[SF2C_EXEC_N_16_B17E], ubuf[SF2C_EXEC_N_6_E7F6][SF2C_EXEC_N_18_4EC9], sbuf[SF2C_EXEC_N_6_E7F6][SF2C_EXEC_N_16_B17E];
    uint64_t tex[SF2C_EXEC_N_6_E7F6][SF2C_EXEC_N_32_E29C], img[SF2C_EXEC_N_6_E7F6][SF2C_EXEC_N_8_2C62];
    NvnDescPool *tpool, *spool;
    float viewport[SF2C_EXEC_N_16_B17E][SF2C_EXEC_N_4_4B22];
    int32_t scissor[SF2C_EXEC_N_16_B17E][SF2C_EXEC_N_4_4B22];
    float depth_range[SF2C_EXEC_N_16_B17E][SF2C_EXEC_N_2_D473];
    float blend_color[SF2C_EXEC_N_4_4B22];
    uint32_t stencil_ref[SF2C_EXEC_N_2_D473], stencil_mask[SF2C_EXEC_N_2_D473], stencil_vmask[SF2C_EXEC_N_2_D473];
    bool prim_restart;
    uint32_t restart_index;
    uint64_t copy_row_stride, copy_image_stride;
} st;

uint32_t nvn_window_origin(void);
uint32_t nvn_depth_mode(void);
bool renderer_origin_lower_left(void) { return nvn_window_origin() == 0; }

// (LARECOMP_VK_PROF) called once per presented frame: frames over 50 ms are logged with where their time went.
// present_ns: time in backend_present's acquire/blit/present.
void vk_prof_frame(uint64_t frame, uint64_t present_ns) {
    if (!prof) return;
    static uint64_t last_t, l_exec, l_wait, l_draws, l_subs;
    uint64_t now = SDL_GetTicksNS();
    if (last_t && now - last_t > SF2C_EXEC_N_50000000_C546) {
        uint64_t dt = now - last_t, ex = cum_exec - l_exec, wt = cum_wait - l_wait;
        LOG(SF2C_EXEC_S_HITCH_FRAME_LLU_0F_MS_BACKEN_301E
            SF2C_EXEC_S_LLU_SUBMITS_COMPILES_D_PENDI_452F, (unsigned long long)frame, dt / SF2C_EXEC_N_1E6_8C96, ex / SF2C_EXEC_N_1E6_8C96, wt / SF2C_EXEC_N_1E6_8C96, present_ns / SF2C_EXEC_N_1E6_8C96,
            ((double)dt - ex - present_ns) / SF2C_EXEC_N_1E6_8C96, (unsigned long long)(cum_draws - l_draws), (unsigned long long)(cum_submits - l_subs),
            atomic_load(&stat_compiles));
    }
    last_t = now; l_exec = cum_exec; l_wait = cum_wait; l_draws = cum_draws; l_subs = cum_submits;
}

// (the slow-frame log, slowframe.h; the game thread) running totals this file keeps anyway
void renderer_slow_counters(uint64_t *submits, uint64_t *gpu_wait_ns, uint64_t *imports) {
    *submits = __atomic_load_n(&cum_submits, __ATOMIC_RELAXED);
    *gpu_wait_ns = __atomic_load_n(&cum_wait, __ATOMIC_RELAXED);
    *imports = (uint64_t)__atomic_load_n(&gc_imported, __ATOMIC_RELAXED);
}

// LARECOMP_LOG_SKIPS=1: the skipped draws and dispatches of each stats interval by reason, and the top offenders
// (shader code addresses and content hashes, as in "[shader] slow build" / shader_variants.txt, and how long they
// have been skipped without a break). "building" for tens of seconds is a build that never finishes.
enum { SK_NO_PROGRAM, SK_NO_STAGES, SK_PROG_BUILDING, SK_PROG_FAILED, SK_PIPE_BUILDING, SK_PIPE_FAILED, SK_CS_BUILDING,
       SK_CS_FAILED, SK_TOPOLOGY, SK_DEBUG_SKIP, SK_HIDDEN, SK_N };
static const char *const skip_names[SK_N] = {
    SF2C_EXEC_S_NO_PROGRAM_73CD, SF2C_EXEC_S_PROGRAM_MISSING_A_STAGE_2000, SF2C_EXEC_S_PROGRAM_BUILDING_483F, SF2C_EXEC_S_PROGRAM_FAILED_D651, SF2C_EXEC_S_PIPELINE_BUILDING_60C2, SF2C_EXEC_S_PIPELINE_FAILED_5BBD,
    SF2C_EXEC_S_COMPUTE_BUILDING_7684, SF2C_EXEC_S_COMPUTE_FAILED_02A4, SF2C_EXEC_S_TOPOLOGY_025D, SF2C_EXEC_S_DEBUG_SKIP_LIST_AFA5, SF2C_EXEC_S_HIDDEN_ON_PURPOSE_BE0D};
static int log_skips = -1;  // (-1: not read yet: the first skip reads it)
static uint64_t skip_n[SK_N];
#define SKIP_TOP SF2C_EXEC_SKIP_TOP
static struct { int why; uint64_t a, b, ha, hb, n, first_ns; } skip_top[SKIP_TOP];
static int skip_ntop;
static void skip_note(int why, bool compute) {
    if (log_skips < 0) log_skips = getenv(SF2C_EXEC_S_LARECOMP_LOG_SKIPS_F37B) && atoi(getenv(SF2C_EXEC_S_LARECOMP_LOG_SKIPS_F37B)) != 0;
    if (!log_skips) return;
    skip_n[why]++;
    const NvnProgram *p = st.program;
    uint64_t a = p ? p->stages[compute ? SF2C_EXEC_N_5_EF2D : 0].code_addr : 0, b = p && !compute ? p->stages[1].code_addr : 0;
    int j = 0;
    while (j < skip_ntop && !(skip_top[j].why == why && skip_top[j].a == a && skip_top[j].b == b)) j++;
    if (j == skip_ntop) {
        if (skip_ntop < SKIP_TOP) skip_ntop++;
        else {  // (full: the entry skipped least this interval goes)
            j = 0;
            for (int i = 1; i < SKIP_TOP; i++) if (skip_top[i].n < skip_top[j].n) j = i;
        }
        skip_top[j].why = why; skip_top[j].a = a; skip_top[j].b = b; skip_top[j].n = 0;
        skip_top[j].ha = p ? shader_stage_hash(&p->stages[compute ? SF2C_EXEC_N_5_EF2D : 0]) : 0;
        skip_top[j].hb = p && !compute ? shader_stage_hash(&p->stages[1]) : 0;
        skip_top[j].first_ns = SDL_GetTicksNS();
    }
    skip_top[j].n++;
}
#define SKIP(why) do { stat_skipped++; LOG(SF2C_EXEC_S_SKIP_DRAW_WHY_S_D_N_44B1, skip_names[why], why); skip_note(why, false); } while (0)
#define SKIP_CS(why) do { stat_skipped++; LOG(SF2C_EXEC_S_SKIP_CS_WHY_S_D_N_8479, skip_names[why], why); skip_note(why, true); } while (0)
static void skip_log(void) {
    if (log_skips <= 0) return;
    uint64_t total = 0;
    for (int i = 0; i < SK_N; i++) total += skip_n[i];
    if (total) {
        LOG(SF2C_EXEC_S_SKIPS_LLU_9A93, (unsigned long long)total);
        for (int i = 0; i < SK_N; i++) if (skip_n[i]) LOG(SF2C_EXEC_S_S_LLU_7040, skip_names[i], (unsigned long long)skip_n[i]);
        uint64_t now = SDL_GetTicksNS();
        bool shown[SKIP_TOP] = {0};
        for (int t = 0; t < SF2C_EXEC_N_4_4B22; t++) {  // the top offenders of this interval
            int best = -1;
            for (int i = 0; i < skip_ntop; i++)
                if (skip_top[i].n && !shown[i] && (best < 0 || skip_top[i].n > skip_top[best].n)) best = i;
            if (best < 0) break;
            shown[best] = true;
            bool cs = skip_top[best].why == SK_CS_BUILDING || skip_top[best].why == SK_CS_FAILED;
            LOG(SF2C_EXEC_S_S_S_S_LLX_016LLX_764E, t ? SF2C_EXEC_S_EMPTY_08B5 : SF2C_EXEC_S_TOP_66D8, skip_names[skip_top[best].why], cs ? SF2C_EXEC_S_CS_DE6E : SF2C_EXEC_S_VS_35ED,
                (unsigned long long)skip_top[best].a, (unsigned long long)skip_top[best].ha);
            if (!cs) LOG(SF2C_EXEC_S_FS_LLX_016LLX_57DB, (unsigned long long)skip_top[best].b, (unsigned long long)skip_top[best].hb);
            LOG(SF2C_EXEC_S_X_LLU_FOR_LLU_S_BCD4, (unsigned long long)skip_top[best].n,
                (unsigned long long)((now - skip_top[best].first_ns) / SF2C_EXEC_N_1000000000ULL_E860));
        }
        LOG(SF2C_EXEC_S_N_D7A5);
    }
    memset(skip_n, 0, sizeof skip_n);
    int k = 0;  // (an offender not skipped this interval is dropped: its "for N s" starts again if it comes back)
    for (int i = 0; i < skip_ntop; i++)
        if (skip_top[i].n) { skip_top[k] = skip_top[i]; skip_top[k++].n = 0; }
    skip_ntop = k;
}

// Builds in flight (programs, graphics / compute pipelines): one still running after 10 s is logged once, with its
// shaders' code addresses. "[shader] slow build" only comes when a build finishes, so one that never does (its draws
// or dispatches skipped for good, compile 0 in the stats) left no trace.
#define BUILDS_MAX SF2C_EXEC_BUILDS_MAX
static pthread_mutex_t builds_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { const char *what; uint64_t c0, c1, t0; bool logged; } builds[BUILDS_MAX];
static int build_begin(const char *what, uint64_t c0, uint64_t c1) {
    pthread_mutex_lock(&builds_lock);
    int s = 0;
    while (s < BUILDS_MAX && builds[s].what) s++;
    if (s < BUILDS_MAX) { builds[s].what = what; builds[s].c0 = c0; builds[s].c1 = c1; builds[s].t0 = SDL_GetTicksNS(); builds[s].logged = false; }
    pthread_mutex_unlock(&builds_lock);
    return s < BUILDS_MAX ? s : -1;
}
static void build_end(int s) {
    if (s < 0) return;
    pthread_mutex_lock(&builds_lock);
    builds[s].what = NULL;
    pthread_mutex_unlock(&builds_lock);
}
static void builds_check(void) {
    uint64_t now = SDL_GetTicksNS();
    pthread_mutex_lock(&builds_lock);
    for (int s = 0; s < BUILDS_MAX; s++) {
        if (!builds[s].what || builds[s].logged || now - builds[s].t0 < SF2C_EXEC_N_10000000000ULL_9E01) continue;
        builds[s].logged = true;
        LOG(SF2C_EXEC_S_SHADER_STILL_BUILDING_AFTER__E527, (unsigned long long)((now - builds[s].t0) / SF2C_EXEC_N_1000000000ULL_E860),
            builds[s].what, (unsigned long long)builds[s].c0, (unsigned long long)builds[s].c1);
    }
    pthread_mutex_unlock(&builds_lock);
}

const char *renderer_stats(void) {
    skip_log();
    builds_check();
    static int dumps;
    if (dumps++ < SF2C_EXEC_N_3_4E07) {
        LOG(SF2C_EXEC_S_VK_COMMAND_COUNTS_82F8);
        for (int i = 0; i < CMD_COUNT; i++) if (op_counts[i]) LOG(SF2C_EXEC_S_D_LLU_B4D0, i, (unsigned long long)op_counts[i]);
        LOG(SF2C_EXEC_S_N_D7A5);
    }
    snprintf(stats_text, sizeof stats_text,
             SF2C_EXEC_S_LLU_DRAWS_LLU_SKIPPED_LLU_PA_3710
             SF2C_EXEC_S_DEFERRED_LLU_SNAP_LLUKB_LLU_79C8,
             (unsigned long long)stat_draws, (unsigned long long)stat_skipped, (unsigned long long)stat_passes,
             (unsigned long long)stat_dispatches, (unsigned long long)stat_flushes, (double)stat_wait_ns / SF2C_EXEC_N_1E6_8C96,
             atomic_exchange(&stat_compiles, 0), (unsigned long long)stat_feedback_copies, (double)stat_upload_bytes / SF2C_EXEC_N_1048576_0_F652,
             (unsigned long long)__atomic_exchange_n(&stat_deferred, 0, __ATOMIC_RELAXED), (unsigned long long)(stat_snap_bytes >> SF2C_EXEC_N_10_4A44),
             (unsigned long long)stat_snap_copies);  // (overlap: SyncWaits returned early; storage copies made)
    stat_feedback_copies = stat_upload_bytes = stat_snap_bytes = stat_snap_copies = 0;
    {
        extern void vk_alias_stats(void);
        vk_alias_stats();
    }
    if (prof) {
        prof_up = prof_up_ns;
        prof_up_ns = 0;
        RecStats rs;  // (the recorder's counters for this interval: record.c)
        rec_stats_take(&rs);
        prof_submit = rs.submit_ns;
        size_t n = strlen(stats_text);
        snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_GPU_0F_CPU_EXEC_0F_DRAW_0F_P_AA0B,
                 prof_gpu / SF2C_EXEC_N_1E6_8C96, prof_exec / SF2C_EXEC_N_1E6_8C96, prof_draw / SF2C_EXEC_N_1E6_8C96, prof_pre / SF2C_EXEC_N_1E6_8C96, prof_key / SF2C_EXEC_N_1E6_8C96, prof_dyn / SF2C_EXEC_N_1E6_8C96, prof_sets / SF2C_EXEC_N_1E6_8C96,
                 prof_tex / SF2C_EXEC_N_1E6_8C96, prof_up / SF2C_EXEC_N_1E6_8C96, prof_vtx / SF2C_EXEC_N_1E6_8C96, prof_submit / SF2C_EXEC_N_1E6_8C96);
        extern uint64_t prof_tex_checks, prof_tex_samples, prof_tex_full, prof_tex_full_bytes, prof_tex_dyn;
        n = strlen(stats_text);
        snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_TEX_CHECKS_LLU_SAMPLES_LLU_F_0993,
                 (unsigned long long)prof_tex_checks, (unsigned long long)prof_tex_samples, (unsigned long long)prof_tex_full,
                 prof_tex_full_bytes / SF2C_EXEC_N_1048576_0_F652, (unsigned long long)prof_tex_dyn);
        n = strlen(stats_text);
        snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_PASS_ENDS_7C32);
        for (int l = 0; l < SF2C_EXEC_N_4096_8B92; l++)
            if (prof_end_line[l] > SF2C_EXEC_N_20_F5CA) { n = strlen(stats_text); snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_D_U_1954, l, prof_end_line[l]); }
        memset(prof_end_line, 0, sizeof prof_end_line);
        n = strlen(stats_text);
        if (gc_shift) {
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_HEAP_CHUNKS_D_IMPORTED_LLU_M_38AD, gc_imported,
                     (unsigned long long)gc_imported_bytes >> SF2C_EXEC_N_20_F5CA, (unsigned long long)stat_gc_misses);
            n = strlen(stats_text);
            if (gc_lazy) {
                snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_POINTER_SCANS_LLU_2F_MB_LLU__5A86,
                         (unsigned long long)prof_scan_calls, prof_scan_bytes / SF2C_EXEC_N_1048576_0_F652, (unsigned long long)prof_scan_skipped);
                n = strlen(stats_text);
            }
            prof_scan_calls = prof_scan_bytes = prof_scan_skipped = 0;
        }
        snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_VIEWS_CREATED_LLU_COUNTERS_G_313E, (unsigned long long)prof_views_created,
                 (unsigned long long)stat_counters_gpu);
        stat_counters_gpu = 0;
        {
            extern uint64_t prof_vc_hits, prof_vc_bypass, prof_vc_copied, prof_vc_queries, prof_vc_scan_pages, prof_vc_scan_ns;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n,  SF2C_EXEC_S_VCACHE_LLU_HIT_LLU_IN_PLACE__E4FF,
                     (unsigned long long)prof_vc_hits, (unsigned long long)prof_vc_bypass, prof_vc_copied / SF2C_EXEC_N_1048576_0_F652,
                     (unsigned long long)prof_vc_queries, prof_vc_scan_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)prof_vc_scan_pages);
            prof_vc_hits = prof_vc_bypass = prof_vc_copied = prof_vc_queries = prof_vc_scan_pages = prof_vc_scan_ns = 0;
            extern uint64_t prof_tex_watch_skips;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_TEX_UNWRITTEN_LLU_857E, (unsigned long long)prof_tex_watch_skips);
            prof_tex_watch_skips = 0;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_CONST_BLOCKS_S_LLU_REUSED_LL_B03B, cbuf_lean() ? SF2C_EXEC_S_LEAN_F189 : SF2C_EXEC_S_FULL_1E59,
                     (unsigned long long)prof_cb_reused, (unsigned long long)prof_cb_copied);
            prof_cb_reused = prof_cb_copied = 0;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_KEY_MEMO_S_PROGRAM_LLU_PIPEL_AC8B, key_memo_on() ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B,
                     (unsigned long long)prof_prog_memo, (unsigned long long)prof_pipe_memo, (unsigned long long)prof_key_draws);
            prof_prog_memo = prof_pipe_memo = prof_key_draws = 0;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_SETS_REUSE_S_BATCH_S_RECYCLE_EBB1
                     SF2C_EXEC_S_LLU_NEW_FROM_THE_POOLS_IN_LL_4925,
                     set_reuse_on() ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B, rec_set_batch() ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B, rec_set_recycle() ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B,
                     (unsigned long long)prof_sets_reused, (unsigned long long)prof_sets_new, (unsigned long long)rs.set_allocs,
                     (unsigned long long)rs.sets_fresh, (unsigned long long)rs.set_calls, (unsigned long long)rs.pool_resets,
                     rs.pool_reset_ns / SF2C_EXEC_N_1E6_8C96, rec_on() ? SF2C_EXEC_S_ON_THE_RECORDER_EE9B : SF2C_EXEC_S_EMPTY_12AE);
            prof_sets_reused = prof_sets_new = 0;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_SLOT_WAIT_1F_MS_DB95, prof_slot_wait / SF2C_EXEC_N_1E6_8C96);
            prof_slot_wait = 0;
            n = strlen(stats_text);
            // the recorder thread (record.c): packets queued, the translating thread's time queuing them, the recorder's
            // time making the calls, and the waits on it (drain: ImGui / idle waits; seq: GPU waits that had to wait for a
            // submission to be handed to the driver first; present: the next frame's acquire waiting for the last present;
            // full: the ring full)
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_RECORDER_S_LLU_PACKETS_1F_MB_FFCD
                     SF2C_EXEC_S_DRAIN_LLUX_1F_MS_SEQ_LLUX_1F_765D,
                     rec_on() ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B, (unsigned long long)rs.packets, rs.bytes / SF2C_EXEC_N_1048576_0_F652, rs.enqueue_ns / SF2C_EXEC_N_1E6_8C96, rs.busy_ns / SF2C_EXEC_N_1E6_8C96,
                     (unsigned long long)rs.wakeups, (unsigned long long)rs.drains, rs.drain_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)rs.seq_waits,
                     rs.seq_wait_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)rs.present_waits, rs.present_wait_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)rs.full_waits,
                     rs.full_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)rs.pipe_waits, rs.pipe_wait_ns / SF2C_EXEC_N_1E6_8C96, (unsigned long long)rs.draws_dropped);
        }
        {   // GPU time per frame by part (see gpu_timer_add), CPU waits per frame by place (device.c g_vk_frame_waits)
            static uint64_t lw[SF2C_EXEC_N_4_4B22];
            uint64_t dw[SF2C_EXEC_N_4_4B22];
            for (int i = 0; i < SF2C_EXEC_N_4_4B22; i++) {  // ([2], [3]: the recorder's)
                uint64_t v = __atomic_load_n(&g_vk_frame_waits[i], __ATOMIC_RELAXED);
                dw[i] = v - lw[i], lw[i] = v;
            }
            double nf = prof_wframes ? (double)prof_wframes : 1, np = dw[SF2C_EXEC_N_3_4E07] ? (double)dw[SF2C_EXEC_N_3_4E07] : 1;
            double gap = ((double)prof_span - (double)prof_gpu - (double)prof_win) / nf / SF2C_EXEC_N_1E6_8C96;
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_GPU_FRAME_GAME_1F_WINDOW_1F__FEC3
                     SF2C_EXEC_S_CPU_WAIT_FRAME_SLOT_1F_ACQUI_9F7D,
                     prof_gpu / nf / SF2C_EXEC_N_1E6_8C96, prof_win / nf / SF2C_EXEC_N_1E6_8C96, prof_span / nf / SF2C_EXEC_N_1E6_8C96, prof_wframes ? gap : 0.0,
                     (unsigned long long)prof_wframes, dw[0] / np / SF2C_EXEC_N_1E6_8C96, dw[1] / np / SF2C_EXEC_N_1E6_8C96, dw[SF2C_EXEC_N_2_D473] / np / SF2C_EXEC_N_1E6_8C96);
            prof_win = prof_span = prof_wframes = 0;
        }
        pthread_mutex_lock(&wait_tab_lock);
        for (int i = 0; i < SF2C_EXEC_N_12_6B51 && wait_tab[i].why; i++) {
            n = strlen(stats_text);
            snprintf(stats_text + n, sizeof stats_text - n, SF2C_EXEC_S_S_S_LLUX_0F_MS_AB41, i ? SF2C_EXEC_S_EMPTY_08B5 : SF2C_EXEC_S_WAITS_3A30, wait_tab[i].why,
                     (unsigned long long)wait_tab[i].n, wait_tab[i].ns / SF2C_EXEC_N_1E6_8C96);
        }
        memset(wait_tab, 0, sizeof wait_tab);
        pthread_mutex_unlock(&wait_tab_lock);
        prof_views_created = 0;
        prof_tex_checks = prof_tex_samples = prof_tex_full = prof_tex_full_bytes = prof_tex_dyn = 0;
        prof_exec = prof_draw = prof_sets = prof_vtx = prof_submit = prof_pre = prof_key = prof_dyn = prof_tex = prof_up = prof_gpu = 0;
    }
    stat_draws = stat_skipped = stat_passes = stat_dispatches = stat_flushes = stat_wait_ns = 0;
    return stats_text;
}

static void st_defaults(void) {
    memset(&st, 0, sizeof st);
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
        st.viewport[i][SF2C_EXEC_N_2_D473] = SF2C_EXEC_N_1280_6F13; st.viewport[i][SF2C_EXEC_N_3_4E07] = SF2C_EXEC_N_720_D829;
        st.scissor[i][SF2C_EXEC_N_2_D473] = SF2C_EXEC_N_16384_CA90; st.scissor[i][SF2C_EXEC_N_3_4E07] = SF2C_EXEC_N_16384_CA90;
        st.depth_range[i][1] = SF2C_EXEC_N_1_0F_CA71;
    }
    st.channel_mask = SF2C_EXEC_N_0XFFFFFFFF_BEFA;
    st.stencil_mask[0] = st.stencil_mask[1] = SF2C_EXEC_N_0XFF_7312;
    st.stencil_vmask[0] = st.stencil_vmask[1] = SF2C_EXEC_N_0XFF_7312;
    st.ds[1] = 1; st.ds[SF2C_EXEC_N_2_D473] = SF2C_EXEC_N_2_D473;
    st.poly[1] = 1; st.poly[SF2C_EXEC_N_2_D473] = SF2C_EXEC_N_2_D473;
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) { st.blend[i][1] = SF2C_EXEC_N_2_D473; st.blend[i][SF2C_EXEC_N_2_D473] = 1; st.blend[i][SF2C_EXEC_N_3_4E07] = SF2C_EXEC_N_2_D473; st.blend[i][SF2C_EXEC_N_4_4B22] = 1; st.blend[i][SF2C_EXEC_N_5_EF2D] = 1; st.blend[i][SF2C_EXEC_N_6_E7F6] = 1; }
}

static VkPipeline pipe_resolve(void *obj, bool wait);
void vk_exec_init(void) {
    trace_render = getenv(SF2C_EXEC_S_SF2_TRACE_RENDER_5E88) != NULL;
    prof = getenv(SF2C_EXEC_S_LARECOMP_VK_PROF_CC43) != NULL;
    if (prof) prof_up_ns = 0;
    // the GPU timer (LARECOMP_VK_GPU_TIMER=0: off - dynamic resolution then goes by the frame times)
    const char *gt = getenv(SF2C_EXEC_S_LARECOMP_VK_GPU_TIMER_3DAF);
    if (g_vk.ts_bits && g_vk.props.limits.timestampComputeAndGraphics && g_vk.props.limits.timestampPeriod > 0 && !(gt && !atoi(gt))) {
        VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = SUBS * SF2C_EXEC_N_3_4E07};
        if (vkCreateQueryPool(g_vk.dev, &qi, NULL, &prof_qp) != VK_SUCCESS) prof_qp = VK_NULL_HANDLE;
    }
    gpu_timer_on = prof_qp != VK_NULL_HANDLE;
    LOG(SF2C_EXEC_S_VK_GPU_TIMER_S_DYNAMIC_RESOL_571C, gpu_timer_on ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_9F3B, gpu_timer_on ? SF2C_EXEC_S_GPU_TIME_F5FD : SF2C_EXEC_S_FRAME_TIMES_A79B);
    st_defaults();
    // The heap's chunk / lazy decision now, on this thread, before the render thread and the build workers exist:
    // they read gc_shift / gc_lazy directly (a buffer copy can come before any shader was translated)
    gmem_chunk_shift();
    // The command buffers of every slot, from the pool, before the recorder thread exists: from then on the pool and
    // the buffers are the recorder's (begun, recorded, ended and submitted in packet order)
    for (int i = 0; i < SUBS; i++) {
        VkCommandBuffer cbs[SF2C_EXEC_N_2_D473];
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g_vk.pool,
                                          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = SF2C_EXEC_N_2_D473};
        VK_CHECK(vkAllocateCommandBuffers(g_vk.dev, &ai, cbs));
        subs[i].main = cbs[0];
        subs[i].upload = cbs[1];
    }
    rec_set_pipe_resolver(pipe_resolve);
    rec_init(prof != 0);
}

void renderer_texture_scale(NvnTexture *t, float *sx, float *sy);
void renderer_texture_fraction(NvnTexture *t, float *fx, float *fy);  // (textures.c: the part the contents occupy)
bool renderer_texture_presented(NvnTexture *t);
bool renderer_texture_first_write(NvnTexture *t);
bool renderer_texture_frame_fresh(NvnTexture *t);
void renderer_texture_mark_presented(NvnTexture *t);
uint64_t renderer_present_count(void);
void renderer_save_texture_tex(TexBackend *b, const char *path);
static float pass_sx = 1, pass_sy = 1;
static float vp_sx = 1, vp_sy = 1, vp_ox = 0, vp_oy = 0;
static float tex_sc[SF2C_EXEC_N_2_D473][SF2C_EXEC_N_64_A68B][SF2C_EXEC_N_2_D473];
static float tex_fr[SF2C_EXEC_N_2_D473][SF2C_EXEC_N_64_A68B][SF2C_EXEC_N_2_D473];  // per bound texture slot: the part of the texture the contents occupy (Dynamic)
static uint32_t tex_wh[SF2C_EXEC_N_2_D473][SF2C_EXEC_N_64_A68B][SF2C_EXEC_N_2_D473];

// ================================================================== render passes
static VkFormat pass_color[SF2C_EXEC_N_8_2C62], pass_depth, pass_stencil;
static uint32_t pass_ncolor, pass_samples = 1, pass_w, pass_h;
static int debug_pass_no;

// A draw's vertex layout. Without dynamic vertex input it is copied into the PipeKey (each layout its own pipeline);
// with it (g_vk.vids) the key leaves it out and draw_ sets it (vkCmdSetVertexInputEXT) when it changes.
typedef struct {
    uint32_t attr_mask, used_streams;
    uint32_t attr_fmt[SF2C_EXEC_N_32_E29C], attr_off[SF2C_EXEC_N_32_E29C], attr_bind[SF2C_EXEC_N_32_E29C];
    uint32_t stride[SF2C_EXEC_N_17_4523], divisor[SF2C_EXEC_N_17_4523];  // divisor 0: per vertex; else per instance
} VtxLayout;

// Redundant-state filter: what draw() last set in the current command buffer, so unchanged pipeline binds, dynamic
// state and vertex / index buffer binds are not recorded again (a draw used to issue ~20 vkCmd* calls; most repeat the
// previous draw's). Forgotten (ss_forget) whenever something else may have recorded state: a new command buffer, a new
// render pass (present, ImGui, post-processing and copies all run between game passes) and the clear pipeline.
// Descriptor sets are always bound (sharing identical sets within a submission was tried: a third matched, but
// comparing them cost more than allocating and writing them on NVIDIA; with dynamic uniform buffers most sets repeat
// their program's previous one, which stage_set keeps: "set reuse").
// LARECOMP_VK_STATE_FILTER=0: every draw sets everything, as before.
enum { SS_PIPE = 1 << 0, SS_DT = 1 << 1, SS_DW = 1 << SF2C_EXEC_N_2_D473, SS_DOP = 1 << SF2C_EXEC_N_3_4E07, SS_STEN = 1 << SF2C_EXEC_N_4_4B22, SS_SFACE0 = 1 << SF2C_EXEC_N_5_EF2D, SS_SFACE1 = 1 << SF2C_EXEC_N_6_E7F6,
       SS_BC = 1 << SF2C_EXEC_N_7_7902, SS_CULL = 1 << SF2C_EXEC_N_8_2C62, SS_FF = 1 << SF2C_EXEC_N_9_1958, SS_TOPO = 1 << SF2C_EXEC_N_10_4A44, SS_PRE = 1 << SF2C_EXEC_N_11_4FC8, SS_DBE = 1 << SF2C_EXEC_N_12_6B51, SS_DB = 1 << SF2C_EXEC_N_13_3FDB,
       SS_VP = 1 << SF2C_EXEC_N_14_8527, SS_SC = 1 << SF2C_EXEC_N_15_E629, SS_IB = 1 << SF2C_EXEC_N_16_B17E, SS_VI = 1 << SF2C_EXEC_N_17_4523 };
static struct {
    uint32_t known;  // SS_* bits: the value below is what the command buffer has
    uint32_t vb_known;  // per vertex binding
    const void *pipe;   // the Pipe bound (a building one has no handle yet: WAIT_PIPES)
    VkBool32 dt, dw, sten, pre, dbe;
    VkCompareOp dop;
    uint32_t sface[SF2C_EXEC_N_2_D473][SF2C_EXEC_N_7_7902];  // fail, pass, depth fail, compare, compare mask, write mask, reference
    float bc[SF2C_EXEC_N_4_4B22], db[SF2C_EXEC_N_3_4E07];
    VkCullModeFlags cull;
    VkFrontFace ff;
    VkPrimitiveTopology topo;
    VkViewport vp;
    VkRect2D sc;
    VkBuffer ib; VkDeviceSize io; VkIndexType it;
    VkBuffer vb[SF2C_EXEC_N_17_4523]; VkDeviceSize vo[SF2C_EXEC_N_17_4523];
    VtxLayout vi;  // (dynamic vertex input)
} ss;
static int ss_on = -1;
static void ss_forget(void) { ss.known = 0; ss.vb_known = 0; }
static bool ss_enabled(void) {
    if (ss_on < 0) { const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_STATE_FILTER_DE2C); ss_on = !e || atoi(e) != 0; }
    return ss_on;
}
// true: the state must be recorded (and is remembered as known)
static inline bool ss_need(uint32_t bit, bool same) {
    if (ss_on && (ss.known & bit) && same) return false;
    ss.known |= bit;
    return true;
}

static void vk_end_pass_at(int line);
void vk_end_pass(void) { vk_end_pass_at(0); }
#define vk_end_pass() vk_end_pass_at(__LINE__)
static void vk_end_pass_at(int line) {
    if (!in_pass) return;
    if (prof && line < SF2C_EXEC_N_4096_8B92) prof_end_line[line]++;
    rec_CmdEndRendering(vk_cmd());
    in_pass = false;
    if (g_debug_frame && st.rt.ncolor && st.rt.color[0] && st.rt.color[0]->backend) {
        char path[SF2C_EXEC_N_256_51E8];
        static uint64_t debug_pass_frame = ~SF2C_EXEC_N_0ULL_DF1B;
        static int multi = -1;
        if (multi < 0) multi = getenv(SF2C_EXEC_S_LARECOMP_DEBUG_FRAMES_8FB4) != NULL;
        if (multi && renderer_present_count() != debug_pass_frame) { debug_pass_frame = renderer_present_count(); debug_pass_no = 0; }
        if (multi) snprintf(path, sizeof path, SF2C_EXEC_S_BUILD_PASS_LLU_03D_PNG_DD88, (unsigned long long)debug_pass_frame, debug_pass_no++);
        else snprintf(path, sizeof path, SF2C_EXEC_S_BUILD_PASS_03D_PNG_48B9, debug_pass_no++);
        renderer_save_texture_tex(st.rt.color[0]->backend, path);
    }
}

static uint32_t level_extent(uint32_t v, uint32_t lv) { v >>= lv; return v ? v : 1; }
// The part of a level extent `e` a scalable image's contents occupy (Dynamic resolution: drawn at scale `sc` into an
// image allocated at scale `al`, the top-left part); the whole extent otherwise.
static int content_extent(int e, bool scalable, float sc, float al) {
    if (!scalable || sc == al || al <= 0) return e;
    int c = (int)lroundf(e * sc / al);
    return c < 1 ? 1 : c > e ? e : c;
}

static bool heap_whole_userptr(void);
static void begin_pass(void) {
    if (in_pass && !st.rt_dirty) return;
    vk_end_pass();
    if (dl_on == SF2C_EXEC_N_2_D473) renderer_flush();  // (LARECOMP_VK_DEVLOST_TRACE=2: each pass its own submission)
    else {
        // Submit the frame in chunks (as Metal, renderer.m begin_pass): the game puts a whole frame in one command
        // list and waits for its fence early in the next frame, so with one submission per list the GPU sat idle
        // while the list was translated, and the game's wait covered translation + GPU time - with the render thread
        // the translation left the game thread but came back as that wait. LARECOMP_CHUNK_PASSES=<n>: a submission
        // every n render passes (0 = one per list). Default 8 with the render thread (RTX 3080 Ti + i9, Field_11C:
        // gpu wait 0.6 -> 0.25 ms, game thread busy 3.4-3.5 ms vs 4.9 without the render thread); without it,
        // off (chunks cost that thread 0.1-0.3 ms there). Also off by default while the whole heap is one userptr
        // import (heap_whole_userptr): amdgpu checks all of its pages again on every submission, ~5 ms each for the
        // 3.2 GB heap, so ten chunks a frame cost ~50 ms (issue #92: RX 6800 XT, 12 fps at the title screen, where
        // 0c26936 - one submission per list - got 29-41).
        static int chunk = -1;
        static bool chunk_env;
        if (chunk < 0) {
            extern bool rq_on(void);
            const char *e = getenv(SF2C_EXEC_S_LARECOMP_CHUNK_PASSES_088B);
            chunk_env = e != NULL;
            chunk = e ? atoi(e) : rq_on() ? SF2C_EXEC_N_8_2C62 : 0;
        }
        int c = chunk;
        if (c > 0 && !chunk_env && heap_whole_userptr()) {  // (decided at boot: asked here, not cached before it)
            static bool logged;
            if (!logged) { logged = true; LOG(SF2C_EXEC_S_VK_SUBMISSION_CHUNKS_OFF_THE_8F06); }
            c = 0;
        }
        if (c > 0 && ++passes_in_sub >= c) renderer_submit();
    }
    st.rt_dirty = false;
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);  // passes, copies and dispatches see each other's results
    {
        NvnTexture *t0n = st.rt.ncolor ? st.rt.color[0] : NULL;
        dl_note(SF2C_EXEC_S_PASS_D_COLOR_UX_U_FMT_X_DEPT_69B2, st.rt.ncolor, t0n ? t0n->width : 0, t0n ? t0n->height : 0, t0n ? t0n->format : 0,
                (void *)st.rt.depth);
    }
    if (g_gpuprof_frame) {
        NvnTexture *t0 = st.rt.ncolor ? st.rt.color[0] : NULL;
        // (color: the game's size for the target; drawn: what is rendered - the render scale applied, as the pass's area)
        const TexBackend *tb = t0 ? t0->backend : NULL;
        float dsx = tb && tb->scalable ? tb->sx : 1, dsy = tb && tb->scalable ? tb->sy : 1;
        gp_mark(cb, SF2C_EXEC_S_PASS_D_COLOR_UX_U_DRAWN_0FX__8E5F, st.rt.ncolor, t0 ? t0->width : 0,
                t0 ? t0->height : 0, t0 ? t0->width * dsx : 0, t0 ? t0->height * dsy : 0, t0 ? t0->format : 0,
                st.rt.depth ? SF2C_EXEC_S_YES_6C76 : SF2C_EXEC_S_NO_04A0);
    }
    VkRenderingAttachmentInfo ca[SF2C_EXEC_N_8_2C62], da = {0}, sa = {0};
    uint32_t w = 0, minw = UINT32_MAX, minh = UINT32_MAX;  // w: first attachment seen
    pass_ncolor = (uint32_t)st.rt.ncolor;
    pass_samples = 1;
    for (int i = 0; i < st.rt.ncolor; i++) {
        NvnTexture *t = st.rt.color[i];
        ca[i] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        pass_color[i] = VK_FORMAT_UNDEFINED;
        if (!t) continue;
        renderer_mark_gpu_owned(t);
        VkImageView v = vk_view(t, &st.rt.cview[i], st.rt.has_cview[i], true, TEXTYPE_2D);
        TexBackend *b = t->backend;
        if (!v) continue;
        ca[i].imageView = v;
        ca[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        ca[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        bool fresh = renderer_texture_frame_fresh(t);
        if (renderer_texture_first_write(t) || fresh) {
            ca[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ca[i].clearValue.color.float32[SF2C_EXEC_N_3_4E07] = fresh ? SF2C_EXEC_N_0_0F_6895 : SF2C_EXEC_N_1_0F_CA71;
        }
        pass_color[i] = st.rt.has_cview[i] && (st.rt.cview[i].setmask & SF2C_EXEC_N_4_4B22) && vk_format(t->format) == b->format
                      ? vk_format(st.rt.cview[i].format) : b->format;
        pass_color[i] = vk_view_format(b, pass_color[i]);  // (g_vk.lean_usage: the format the view really has)
        uint32_t lv = st.rt.has_cview[i] && (st.rt.cview[i].setmask & 1) ? st.rt.cview[i].base_level : 0;
        uint32_t ew = level_extent(b->width, lv), eh = level_extent(b->height, lv);
        if (!w) { renderer_texture_scale(t, &pass_sx, &pass_sy); w = ew; }
        if (ew < minw) minw = ew;
        if (eh < minh) minh = eh;
        pass_samples = b->samples;
    }
    // Bound color slots without a texture / view stay as VK_NULL_HANDLE attachments (legal), but Intel's Windows
    // driver for UHD 630 (Vulkan 1.3.215) crashes in vkCmdBeginRendering reading near a null pointer on the title
    // screen's first clear (issues #8, #46). Empty slots at the end are left out (pipelines and clears follow
    // pass_ncolor); one in the middle, or a pass with no attachment at all, is logged once for the next report.
    while (pass_ncolor && !ca[pass_ncolor - 1].imageView) pass_ncolor--;
    static bool logged_hole;
    for (uint32_t i = 0; i < pass_ncolor && !logged_hole; i++)
        if (!ca[i].imageView) {
            logged_hole = true;
            LOG(SF2C_EXEC_S_VK_PASS_WITH_AN_EMPTY_COLOR__CD96, i, pass_ncolor, st.rt.color[i] ? SF2C_EXEC_S_WITHOUT_A_VIEW_AA42 : SF2C_EXEC_S_NONE_30A0);
        }
    pass_depth = pass_stencil = VK_FORMAT_UNDEFINED;
    bool has_d = false, has_s = false;
    if (st.rt.depth) {
        renderer_mark_gpu_owned(st.rt.depth);
        VkImageView v = vk_view(st.rt.depth, &st.rt.dview, st.rt.has_dview, true, TEXTYPE_2D);
        TexBackend *b = st.rt.depth->backend;
        if (v && renderer_format_is_depth(st.rt.depth->format)) {
            VkRenderingAttachmentInfo a = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = v,
                                           .imageLayout = VK_IMAGE_LAYOUT_GENERAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
            if (b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) { da = a; has_d = true; pass_depth = b->format; }
            if (b->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) { sa = a; has_s = true; pass_stencil = b->format; }
            uint32_t lv = st.rt.has_dview && (st.rt.dview.setmask & 1) ? st.rt.dview.base_level : 0;
            uint32_t ew = level_extent(b->width, lv), eh = level_extent(b->height, lv);
            if (!w) { renderer_texture_scale(st.rt.depth, &pass_sx, &pass_sy); w = ew; }
            if (ew < minw) minw = ew;
            if (eh < minh) minh = eh;
            if (!pass_ncolor) pass_samples = b->samples;
        }
    }
    if (!w) {
        pass_sx = pass_sy = 1; minw = minh = SF2C_EXEC_N_16_B17E;
        static bool logged_none;
        if (!logged_none) logged_none = true, LOG(SF2C_EXEC_S_VK_PASS_WITH_NO_ATTACHMENTS__8C1F, st.rt.ncolor);
    }
    // A pass with depth / stencil but no color attachment gets a throwaway one on Intel (LARECOMP_VK_PLACEHOLDER_COLOR=0 /
    // 1 overrides): the UHD 630's Windows driver (Vulkan 1.3.215) crashes in the vkCmdBeginRendering after such a pass,
    // reading near a null pointer (issues #8, #46). Pipelines and clears follow pass_ncolor / pass_color as usual.
    static int placeholder = -1;
    if (placeholder < 0) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_PLACEHOLDER_COLO_DDE6);
        placeholder = e ? atoi(e) != 0 : g_vk.props.vendorID == SF2C_EXEC_N_0X8086_D5E6;
    }
    bool placed = false;
    if (placeholder && !pass_ncolor && (has_d || has_s)) {
        VkImageView v = vk_dummy_color_view(minw, minh, pass_samples);
        if (v) {
            ca[0] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = v,
                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE};
            pass_color[0] = VK_FORMAT_R8_UNORM;
            pass_ncolor = 1;
            placed = true;
        }
    }
    pass_w = minw; pass_h = minh;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {minw, minh}}, .layerCount = 1,
                          .colorAttachmentCount = pass_ncolor, .pColorAttachments = pass_ncolor ? ca : NULL,
                          .pDepthAttachment = has_d ? &da : NULL, .pStencilAttachment = has_s ? &sa : NULL};
    // LARECOMP_VK_LOG_PASSES=<n> (default 300 with =1): each of the first n passes before it begins - an Intel crash
    // inside vkCmdBeginRendering (issues #8, #46) ends the log on the pass the driver choked on
    static long log_passes = -1;
    if (log_passes < 0) {
        log_passes = getenv(SF2C_EXEC_S_LARECOMP_VK_LOG_PASSES_B952) ? atol(getenv(SF2C_EXEC_S_LARECOMP_VK_LOG_PASSES_B952)) : 0;
        if (log_passes == 1) log_passes = SF2C_EXEC_N_300_983B;
    }
    if (log_passes > 0) {
        log_passes--;
        LOG(SF2C_EXEC_S_VK_PASS_UX_U_U_COLOR_F7E7, minw, minh, pass_ncolor);
        for (uint32_t i = 0; i < pass_ncolor; i++) {
            TexBackend *b = !placed && st.rt.color[i] ? st.rt.color[i]->backend : NULL;
            LOG(SF2C_EXEC_S_U_FMT_D_VIEW_S_S_AAD7, i, pass_color[i], ca[i].imageView ? SF2C_EXEC_S_YES_6C76 : SF2C_EXEC_S_NONE_30A0, placed ? SF2C_EXEC_S_PLACEHOLDER_C493
                : ca[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? SF2C_EXEC_S_CLEAR_0CD2 : SF2C_EXEC_S_LOAD_A4FC);
            // (the image under it: its own format, size, levels / layers, samples; the view's first level and layer)
            if (b) LOG(SF2C_EXEC_S_IMAGE_FMT_D_UX_U_LV_U_LY_U_S_0DF8, b->format, b->width, b->height, b->levels,
                       b->layers, b->samples, st.rt.has_cview[i] && (st.rt.cview[i].setmask & 1) ? st.rt.cview[i].base_level : 0,
                       st.rt.has_cview[i] && (st.rt.cview[i].setmask & SF2C_EXEC_N_2_D473) ? st.rt.cview[i].min_layer : 0);
            LOG(SF2C_EXEC_S_EMPTY_78C1);
        }
        if (has_d || has_s)
            LOG(SF2C_EXEC_S_DEPTH_S_FMT_D_STENCIL_S_FMT__1A81, has_d ? SF2C_EXEC_S_YES_6C76 : SF2C_EXEC_S_NO_04A0,
                pass_depth, has_s ? SF2C_EXEC_S_YES_6C76 : SF2C_EXEC_S_NO_04A0, pass_stencil, has_d && has_s && da.imageView == sa.imageView ? SF2C_EXEC_S_YES_6C76 : SF2C_EXEC_S_NO_04A0,
                has_d ? da.imageLayout : sa.imageLayout, has_d ? da.loadOp : -1, has_s ? sa.loadOp : -1);
        else LOG(SF2C_EXEC_S_NO_DEPTH_STENCIL_BAAE);
        LOG(SF2C_EXEC_S_SAMPLES_U_N_0746, pass_samples);
    }
    rec_CmdBeginRendering(cb, &ri);
    ss_forget();  // (whatever ran between the passes may have set its own state)
    in_pass = true;
    vp_sx = pass_sx; vp_sy = pass_sy; vp_ox = 0; vp_oy = 0;
    stat_passes++;
}

static void pass_size(int *w, int *h) {
    *w = *h = 0;
    for (int i = 0; i < st.rt.ncolor && !*w; i++)
        if (st.rt.color[i]) { *w = (int)st.rt.color[i]->width; *h = (int)st.rt.color[i]->height; }
    if (!*w && st.rt.depth) { *w = (int)st.rt.depth->width; *h = (int)st.rt.depth->height; }
    *w = (int)lroundf(*w * pass_sx);
    *h = (int)lroundf(*h * pass_sy);
}

static VkRect2D scissor_rect(void) {
    int w, h;
    pass_size(&w, &h);
    if (!w) return (VkRect2D){{0, 0}, {pass_w, pass_h}};
    int x = st.scissor[0][0], y = st.scissor[0][1], sw = st.scissor[0][SF2C_EXEC_N_2_D473], sh = st.scissor[0][SF2C_EXEC_N_3_4E07];
    if (vp_sx != 1 || vp_sy != 1 || vp_ox != 0 || vp_oy != 0) {
        double x0 = floor(x * (double)vp_sx + vp_ox), y0 = floor(y * (double)vp_sy + vp_oy);
        double x1 = ceil(((double)x + sw) * vp_sx + vp_ox), y1 = ceil(((double)y + sh) * vp_sy + vp_oy);
        if (sw >= SF2C_EXEC_N_16384_CA90) x1 = SF2C_EXEC_N_1E9_EFAC;
        if (sh >= SF2C_EXEC_N_16384_CA90) y1 = SF2C_EXEC_N_1E9_EFAC;
        if (x1 > SF2C_EXEC_N_1E9_EFAC) x1 = SF2C_EXEC_N_1E9_EFAC;
        if (y1 > SF2C_EXEC_N_1E9_EFAC) y1 = SF2C_EXEC_N_1E9_EFAC;
        x = (int)x0; y = (int)y0; sw = (int)(x1 - x0); sh = (int)(y1 - y0);
    }
    if (x < 0) { sw += x; x = 0; }
    if (y < 0) { sh += y; y = 0; }
    if (w > (int)pass_w) w = (int)pass_w;
    if (h > (int)pass_h) h = (int)pass_h;
    if (x >= w || y >= h) { x = y = 0; sw = sh = 0; }
    if (x + sw > w) sw = w - x;
    if (y + sh > h) sh = h - y;
    if (sw < 0) sw = 0;
    if (sh < 0) sh = 0;
    return (VkRect2D){{x, y}, {(uint32_t)sw, (uint32_t)sh}};
}

static void apply_scissor(void) {
    VkRect2D r = scissor_rect();
    if (!ss_need(SS_SC, !memcmp(&r, &ss.sc, sizeof r))) return;
    ss.sc = r;
    rec_CmdSetScissor(vk_cmd(), 0, 1, &r);
}

// Metal convention (y up in NDC, origin top-left) through a negative-height viewport.
static void set_viewport(float x, float y, float w, float h, float zn, float zf) {
    if (w <= 0) w = 1;
    if (h == 0) h = 1;
    VkViewport vp = {x, y + h, w, -h, fminf(fmaxf(zn, 0), 1), fminf(fmaxf(zf, 0), 1)};
    if (!ss_need(SS_VP, !memcmp(&vp, &ss.vp, sizeof vp))) return;
    ss.vp = vp;
    rec_CmdSetViewport(vk_cmd(), 0, 1, &vp);
}

// ================================================================== clears
static VkPipelineLayout pc_layout(void) {  // push constants only (clears)
    static VkPipelineLayout l;
    if (!l) {
        VkPushConstantRange r = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_32_E29C};
        VkPipelineLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1,
                                         .pPushConstantRanges = &r};
        VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &ci, NULL, &l));
    }
    return l;
}

static const VkDynamicState DYN[] = {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE};
#define NDYN (sizeof DYN / sizeof DYN[0])

// Fixed-function state that isn't part of a pipeline is set explicitly on every bind (it persists across pipelines).
static void dyn_defaults(VkCommandBuffer cb) {
    rec_CmdSetDepthBias(cb, 0, 0, 0);
    rec_CmdSetDepthBiasEnable(cb, VK_FALSE);
    float bc[SF2C_EXEC_N_4_4B22] = {0, 0, 0, 0};
    rec_CmdSetBlendConstants(cb, bc);
    rec_CmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, SF2C_EXEC_N_0XFF_7312);
    rec_CmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, SF2C_EXEC_N_0XFF_7312);
    rec_CmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    rec_CmdSetCullMode(cb, VK_CULL_MODE_NONE);
    rec_CmdSetFrontFace(cb, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    rec_CmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    rec_CmdSetDepthTestEnable(cb, VK_FALSE);
    rec_CmdSetDepthWriteEnable(cb, VK_FALSE);
    rec_CmdSetDepthCompareOp(cb, VK_COMPARE_OP_ALWAYS);
    rec_CmdSetStencilTestEnable(cb, VK_FALSE);
    rec_CmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                      VK_COMPARE_OP_ALWAYS);
    rec_CmdSetPrimitiveRestartEnable(cb, VK_FALSE);
}

typedef struct { uint64_t key; VkPipeline pipe; } PipeEnt;
#define CLEAR_TAB SF2C_EXEC_CLEAR_TAB
static PipeEnt clear_tab[CLEAR_TAB];

// This AMD driver draws nothing from a pipeline with fixed vertex input while dynamic vertex input is enabled.
// These triangles are generated from gl_VertexIndex and have no attributes, so the dynamic state is an empty layout.
// Without it the movie and the upscaler present uninitialised memory (red noise) while the audio plays.
static void fullscreen_vertex_dyn(VkDynamicState *dyn, uint32_t *n, uint32_t cap) {
    if (g_vk.vids && *n < cap) dyn[(*n)++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
}
static void fullscreen_vertex_bind(VkCommandBuffer cb) {
    if (!g_vk.vids) return;
    VkVertexInputBindingDescription2EXT b = {.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT};
    VkVertexInputAttributeDescription2EXT a = {.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT};
    rec_CmdSetVertexInputEXT(cb, 0, &b, 0, &a);
    ss_forget();  // the next game draw must set its own layout again
}

static VkPipeline clear_pipeline(int index, int kind, uint32_t mask, bool depth, bool stencil) {
    uint64_t key = SF2C_EXEC_N_0XCBF29CE484222325ULL_7EE8;
#define MIX(v) do { uint64_t v_ = (uint64_t)(v); for (int b_ = 0; b_ < SF2C_EXEC_N_8_2C62; b_++) { key ^= (v_ >> (b_ * SF2C_EXEC_N_8_2C62)) & SF2C_EXEC_N_0XFF_7312; key *= SF2C_EXEC_N_0X100000001B3ULL_3E10; } } while (0)
    MIX(index); MIX(kind); MIX(mask); MIX(depth); MIX(stencil); MIX(pass_ncolor); MIX(pass_depth); MIX(pass_stencil); MIX(pass_samples);
    for (uint32_t i = 0; i < pass_ncolor; i++) MIX(pass_color[i]);
    uint32_t slot = (uint32_t)key & (CLEAR_TAB - 1);
    for (int p = 0; p < CLEAR_TAB; p++, slot = (slot + 1) & (CLEAR_TAB - 1)) {
        if (clear_tab[slot].key == key) return clear_tab[slot].pipe;
        if (!clear_tab[slot].key) break;
    }
    char fsname[SF2C_EXEC_N_32_E29C];
    if (index >= 0) snprintf(fsname, sizeof fsname, SF2C_EXEC_S_CLEAR_FRAG_C_D_B905, kind == 1 ? SF2C_EXEC_CH_I_088D : kind == SF2C_EXEC_N_2_D473 ? SF2C_EXEC_CH_U_8E20 : SF2C_EXEC_CH_F_CE92, index);
    else snprintf(fsname, sizeof fsname, SF2C_EXEC_S_CLEAR_FRAG_NONE_2562);
    VkPipelineShaderStageCreateInfo stages[SF2C_EXEC_N_2_D473] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vk_builtin_shader(SF2C_EXEC_S_CLEAR_VERT_6EF9), .pName = SF2C_EXEC_S_MAIN_E4A9},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = vk_builtin_shader(fsname), .pName = SF2C_EXEC_S_MAIN_E4A9}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = (VkSampleCountFlagBits)pass_samples};
    VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState cba[SF2C_EXEC_N_8_2C62] = {0};
    for (uint32_t i = 0; i < pass_ncolor; i++)
        if ((int)i == index) cba[i].colorWriteMask = mask & SF2C_EXEC_N_0XF_7419;
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                              .attachmentCount = pass_ncolor, .pAttachments = cba};
    VkDynamicState dyn[NDYN + 1];
    memcpy(dyn, DYN, sizeof DYN);
    uint32_t ndyn = NDYN;
    fullscreen_vertex_dyn(dyn, &ndyn, NDYN + 1);
    VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                           .dynamicStateCount = ndyn, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo rinfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = pass_ncolor,
                                           .pColorAttachmentFormats = pass_color, .depthAttachmentFormat = pass_depth,
                                           .stencilAttachmentFormat = pass_stencil};
    VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rinfo, .stageCount = SF2C_EXEC_N_2_D473,
                                       .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vs,
                                       .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds,
                                       .pColorBlendState = &cb, .pDynamicState = &dy, .layout = pc_layout()};
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &pi, NULL, &p) != VK_SUCCESS) LOG(SF2C_EXEC_S_VK_CLEAR_PIPELINE_FAILED_N_0252);
    clear_tab[slot].key = key;
    clear_tab[slot].pipe = p;
    return p;
}

static void clear(int index, int kind, const void *color, uint32_t mask, bool depth, float dval, bool stencil, uint32_t sval,
                  uint32_t smask) {
    begin_pass();
    VkCommandBuffer cb = vk_cmd();
    vp_sx = pass_sx; vp_sy = pass_sy; vp_ox = 0; vp_oy = 0;
    VkRect2D rect = scissor_rect();
    if (!rect.extent.width || !rect.extent.height) return;
    if (index >= 0 && (index >= (int)pass_ncolor || pass_color[index] == VK_FORMAT_UNDEFINED)) return;
    bool simple = (index < 0 || (mask & SF2C_EXEC_N_0XF_7419) == SF2C_EXEC_N_0XF_7419) && (!stencil || (smask & SF2C_EXEC_N_0XFF_7312) == SF2C_EXEC_N_0XFF_7312);
    if (simple) {
        VkClearAttachment a[SF2C_EXEC_N_2_D473];
        uint32_t n = 0;
        if (index >= 0) {
            a[n] = (VkClearAttachment){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = (uint32_t)index};
            memcpy(a[n].clearValue.color.float32, color, SF2C_EXEC_N_16_B17E);
            n++;
        }
        VkImageAspectFlags dsa = (depth && pass_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) | (stencil && pass_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        if (dsa) a[n++] = (VkClearAttachment){.aspectMask = dsa, .clearValue.depthStencil = {dval, sval}};
        VkClearRect cr = {rect, 0, 1};
        if (n) rec_CmdClearAttachments(cb, n, a, 1, &cr);
        return;
    }
    VkPipeline p = clear_pipeline(index, kind, mask, depth, stencil);
    if (!p) return;
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    ss_forget();  // (its own pipeline and state: the next draw sets everything again)
    dyn_defaults(cb);
    if (depth) { rec_CmdSetDepthTestEnable(cb, VK_TRUE); rec_CmdSetDepthWriteEnable(cb, VK_TRUE); }
    if (stencil) {
        rec_CmdSetStencilTestEnable(cb, VK_TRUE);
        rec_CmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE,
                          VK_COMPARE_OP_ALWAYS);
        rec_CmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, smask);
        rec_CmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, sval);
    }
    int w, h;
    pass_size(&w, &h);
    VkViewport vp = {0, 0, (float)(w ? w : (int)pass_w), (float)(h ? h : (int)pass_h), 0, 1};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &rect);
    float pc[SF2C_EXEC_N_8_2C62] = {0};
    if (color) memcpy(pc, color, SF2C_EXEC_N_16_B17E);
    pc[SF2C_EXEC_N_4_4B22] = dval;
    rec_CmdPushConstants(cb, pc_layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_32_E29C, pc);
    fullscreen_vertex_bind(cb);
    rec_CmdDraw(cb, SF2C_EXEC_N_3_4E07, 1, 0, 0);
}

// ================================================================== programs
#define MAX_BIND SF2C_EXEC_MAX_BIND
typedef struct {
    ShaderInfo *vi, *fi;
    uint64_t code[SF2C_EXEC_N_2_D473], cmax[SF2C_EXEC_N_2_D473];  // translation inputs (copied: translated on the translator thread)
    ShaderKey vkey, fkey;
    VkShaderModule vs, fs;
    VkShaderModule fs_late;  // (lazy chunks) fs without early fragment tests, for alpha to coverage pipelines; or NULL
    VkBindingInfo binds[MAX_BIND];
    int nbinds;
    VkDescriptorSetLayout set_layout[SF2C_EXEC_N_3_4E07];  // [2] (compute: [1]): the heap chunk set (gmem_chunk_layout)
    VkPipelineLayout layout;
    bool vs_uses_vid;
    _Atomic int state;  // 0 building, 1 ready, 2 failed
    uint64_t prep_vh, prep_fh;  // (prepared programs: their code hashes, for LARECOMP_PREP_CHECK)
    ShaderFp fp[SF2C_EXEC_N_2_D473];             // (stale-program detector, shader_prep.h; compute: [0]; touched by the replay only)
    struct PipeMemo *pmemo;     // (render thread) draw_'s pipeline key memo for this program
    bool reuse;                 // set reuse (stage_set): some uniform buffers are dynamic, sets are kept while their contents repeat
    struct SetCache *scache[SF2C_EXEC_N_2_D473]; // (render thread) stage_set's last set per set index
} Prog;

// ---- descriptor set reuse (graphics programs; port of vk-draw-cpu-2 d50bbce)
// Every draw allocated and wrote two descriptor sets (~20 descriptors) although most repeat the previous draw's of
// the same program: the textures and samplers are the same, and the uniform buffers are copies in the same transient
// ring buffer at new offsets. With the uniform buffers that move per draw declared dynamic (their offsets go to
// vkCmdBindDescriptorSets), the set's contents (buffers, ranges, views, samplers) repeat, so stage_set keeps the last
// set of each program and set index and binds it again while what it would write is identical. A kept set belongs to
// the pending submission (its pools are reset with the slot) and to the current view generation; buffers bound in
// sets are destroyed deferred (after the submissions using them), or at a reboot (ds_gen).
// LARECOMP_VK_NO_SET_REUSE=1: no dynamic buffers, a new set per draw, as before.
static int set_reuse = -1;
static uint32_t ds_gen;  // bumped when a buffer bound in sets may be destroyed at once (reboot)
static bool set_reuse_on(void) {
    if (set_reuse < 0) set_reuse = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_SET_REUSE_7972) == NULL;
    return set_reuse;
}
static bool is_ubo(VkDescriptorType t) { return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; }
// Up to the device's limit for the whole pipeline layout (8 or more; NVIDIA 15, AMD 16), the uniform buffers whose
// copies move most: the game's constant buffers (bindings 2-16), the scale table (18), the driver constants (0),
// then the compiler constants (1: one copy per submission). (Build workers: before the program is ready.)
static void mark_dynamic(Prog *p) {
    if (!set_reuse_on()) return;
    uint32_t lim = g_vk.props.limits.maxDescriptorSetUniformBuffersDynamic, n = 0;
    if (lim > SF2C_EXEC_N_32_E29C) lim = SF2C_EXEC_N_32_E29C;
    static const int8_t tier_of[SF2C_EXEC_N_19_9400] = {SF2C_EXEC_N_2_D473, SF2C_EXEC_N_3_4E07, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1};
    for (int tier = 0; tier <= SF2C_EXEC_N_3_4E07; tier++)
        for (int i = 0; i < p->nbinds && n < lim; i++) {
            VkBindingInfo *b = &p->binds[i];
            if (b->set > 1 || b->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b->binding >= SF2C_EXEC_N_19_9400 || tier_of[b->binding] != tier) continue;
            b->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            n++;
        }
    p->reuse = true;
    static _Atomic int logged, logged_left;  // (the limit once; programs left with static buffers: the first 4)
    int left = 0;
    for (int i = 0; i < p->nbinds; i++) left += p->binds[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && p->binds[i].set <= 1;
    if (!atomic_exchange(&logged, 1) || (left && atomic_fetch_add(&logged_left, 1) < SF2C_EXEC_N_4_4B22))
        LOG(SF2C_EXEC_S_VK_SET_REUSE_U_DYNAMIC_UNIFO_C87C, lim, n, left);
}

static VkPipelineLayout make_layout(Prog *p, int nsets, VkShaderStageFlags s0, VkShaderStageFlags s1) {
    p->set_layout[0] = vk_set_layout(p->binds, p->nbinds, 0, s0);
    if (nsets > 1) p->set_layout[1] = vk_set_layout(p->binds, p->nbinds, 1, s1);
    bool gm = gc_shift && ((p->vi && p->vi->uses_gmem) || (p->fi && p->fi->uses_gmem));
    if (gm) p->set_layout[nsets++] = gmem_chunk_layout();
    // (lazy chunks, vertex / fragment: the storage buffers' chunks as push constants, shader.h GMEM_DIRECT_MAX)
    VkPushConstantRange gd = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_16_B17E * GMEM_DIRECT_MAX};
    bool direct = gm && gc_lazy && s1 == VK_SHADER_STAGE_FRAGMENT_BIT;
    VkPipelineLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = (uint32_t)nsets,
                                     .pSetLayouts = p->set_layout, .pushConstantRangeCount = direct ? 1 : 0,
                                     .pPushConstantRanges = direct ? &gd : NULL};
    VkPipelineLayout l;
    VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &ci, NULL, &l));
    return l;
}

static void save_glsl(const char *src, uint64_t addr, const char *suffix) {
    if (!getenv(SF2C_EXEC_S_LARECOMP_SAVE_MSL_AF49)) return;
    mkdir(SF2C_EXEC_S_BUILD_GLSL_6A40, SF2C_EXEC_N_0755_64A5);
    char path[SF2C_EXEC_N_256_51E8];
    snprintf(path, sizeof path, SF2C_EXEC_S_BUILD_GLSL_LLX_S_GLSL_B1A8, (unsigned long long)addr, suffix);
    FILE *f = fopen(path, SF2C_EXEC_S_W_7265);
    if (f) { fputs(src, f); fclose(f); }
}

void slow_note_build(const char *what, uint64_t ns);  // (slowframe.h: the slow-frame log's counts; every build,
void slow_note_cache_hit(void);                       // and the pipelines the driver's cache had)
ShaderInfo *slow_translate(const uint8_t *code, size_t max_size, int stage, const ShaderKey *key);
#define maxwell_translate slow_translate  // (the translations below: counted and timed for the slow-frame log)
#define SLOW_FEEDBACK(info) VkPipelineCreationFeedback fb_ = {0}; VkPipelineCreationFeedbackCreateInfo fbi_ = \
    {.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO, .pNext = (info).pNext, .pPipelineCreationFeedback = &fb_}; \
    (info).pNext = &fbi_
#define SLOW_FEEDBACK_HIT() do { if (fb_.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) slow_note_cache_hit(); } while (0)

// A build the driver takes over a second for is logged with its shaders: draws and dispatches are skipped until it
// is done, which looked like a rendering bug (the chain chomps' compute with the heap in chunks: never ready).
static void slow_build(uint64_t t0, const char *what, uint64_t code0, uint64_t code1) {
    slow_note_build(what, SDL_GetTicksNS() - t0);
    uint64_t ms = (SDL_GetTicksNS() - t0) / SF2C_EXEC_N_1000000_6CCE;
    if (ms < SF2C_EXEC_N_1000_4051) return;
    static int n;
    if (n++ < SF2C_EXEC_N_40_D59E) LOG(SF2C_EXEC_S_SHADER_SLOW_BUILD_S_LLX_LLX__D441, what, (unsigned long long)code0, (unsigned long long)code1, (unsigned long long)ms);
}

// A fragment shader the translator gave early fragment tests (lazy chunks, translate.c glsl_early_tests): removes the
// OpExecutionMode EarlyFragmentTests (in place; true if there was one). Alpha to coverage drops samples after the
// shader, and with early tests their depth / stencil would already be written: those pipelines take this module.
static bool spirv_strip_early_tests(uint32_t *w, size_t *nwords) {
    for (size_t i = SF2C_EXEC_N_5_EF2D; i < *nwords;) {
        uint32_t n = w[i] >> SF2C_EXEC_N_16_B17E, op = w[i] & SF2C_EXEC_N_0XFFFF_F51F;
        if (!n || i + n > *nwords) return false;
        if (op == SF2C_EXEC_N_54_2FCA) return false;  // OpFunction: past the execution modes
        if (op == SF2C_EXEC_N_16_B17E && n == SF2C_EXEC_N_3_4E07 && w[i + SF2C_EXEC_N_2_D473] == SF2C_EXEC_N_9_1958) {  // OpExecutionMode %main EarlyFragmentTests
            memmove(w + i, w + i + n, (*nwords - i - n) * sizeof *w);
            *nwords -= n;
            return true;
        }
        i += n;
    }
    return false;
}

static void compile_program(void *arg) {
    Prog *p = arg;
    uint64_t t0 = SDL_GetTicksNS();
    int bs = build_begin(SF2C_EXEC_S_MODULES_VS_FS_97C5, p->code[0] ? p->code[0] : p->prep_vh, p->code[1] ? p->code[1] : p->prep_fh);
    size_t nv = 0, nf = 0;
    uint32_t *sv = vk_glsl_to_spirv(p->vi->msl, STAGE_VERTEX, SF2C_EXEC_S_VS_35ED, &nv);
    uint32_t *sf = sv ? vk_glsl_to_spirv(p->fi->msl, STAGE_FRAGMENT, SF2C_EXEC_S_FS_FEFF, &nf) : NULL;
    if (!sv || !sf) { free(sv); free(sf); atomic_store(&p->state, SF2C_EXEC_N_2_D473); build_end(bs); return; }
    p->nbinds = vk_reflect(sv, nv, p->binds, MAX_BIND);
    p->nbinds += vk_reflect(sf, nf, p->binds + p->nbinds, MAX_BIND - p->nbinds);
    p->vs = vk_module(sv, nv);
    p->fs = vk_module(sf, nf);
    if (spirv_strip_early_tests(sf, &nf)) p->fs_late = vk_module(sf, nf);
    free(sv);
    free(sf);
    mark_dynamic(p);
    p->layout = make_layout(p, SF2C_EXEC_N_2_D473, VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT);
    atomic_fetch_add(&stat_compiles, 1);
    if (!p->vs || !p->fs) LOG(SF2C_EXEC_S_SHADER_VS_FS_LLX_LLX_SHADER__FFF4, (unsigned long long)p->code[0], (unsigned long long)p->code[1]);
    atomic_store(&p->state, p->vs && p->fs ? 1 : SF2C_EXEC_N_2_D473);
    build_end(bs);
    slow_build(t0, SF2C_EXEC_S_MODULES_VS_FS_97C5, p->code[0], p->code[1]);
}

// Translates both stages (false: failed, logged). Holds the translator for the whole program.
static bool translate_program_now(Prog *p) {
    maxwell_gmem_base = HEAP_BASE;
    maxwell_gmem_size = HEAP_MAX;
    maxwell_gmem_chunk_shift = gmem_chunk_shift();
    pthread_mutex_lock(&translate_lock);
    p->vi = maxwell_translate((const uint8_t *)(uintptr_t)p->code[0], p->cmax[0], STAGE_VERTEX, &p->vkey);
    if (p->vi->msl) {
        p->fkey.vs_out_mask = p->vi->out_mask;
        p->fi = maxwell_translate((const uint8_t *)(uintptr_t)p->code[1], p->cmax[1], STAGE_FRAGMENT, &p->fkey);
    }
    pthread_mutex_unlock(&translate_lock);
    if (!p->vi->msl) {
        LOG(SF2C_EXEC_S_SHADER_VS_TRANSLATE_FAILED_S_F4C4, p->vi->error, (unsigned long long)p->code[0]);
        atomic_store(&p->state, SF2C_EXEC_N_2_D473);
        return false;
    }
    if (!p->fi->msl) {
        LOG(SF2C_EXEC_S_SHADER_FS_TRANSLATE_FAILED_S_679A, p->fi->error);
        atomic_store(&p->state, SF2C_EXEC_N_2_D473);
        return false;
    }
    p->vs_uses_vid = strstr(p->vi->msl, SF2C_EXEC_S_GL_VERTEXINDEX_2D99) != NULL;
    return true;
}

static void translate_program(void *arg) {
    Prog *p = arg;
    if (!translate_program_now(p)) return;
    save_glsl(p->vi->msl, p->code[0], SF2C_EXEC_S_VS_35ED);
    save_glsl(p->fi->msl, p->code[1], SF2C_EXEC_S_FS_FEFF);
    run_job(compile_program, p);
}

// ================================================================== prepared objects (shader_prep.h)
// Built before the game from the shipped variant list; found by content (code hashes + keys) at first use, as code
// addresses differ from run to run. Kept for the whole process (reboots included). prep_lock guards the tables.
static pthread_mutex_t prep_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct { uint64_t key; void *obj; } PrepEnt;
static PrepEnt *prep_progs, *prep_pipes, *prep_cs;  // program / pipeline / compute content key -> Prog / Pipe / CsProg
static size_t prep_progs_cap, prep_pipes_cap, prep_cs_cap, prep_progs_n, prep_pipes_n, prep_cs_n;
static _Atomic int prep_pending;

static void *prep_get(PrepEnt *t, size_t cap, uint64_t key) {
    if (!cap) return NULL;
    if (!key) key = 1;
    for (size_t j = key & (cap - 1); t[j].key; j = (j + 1) & (cap - 1))
        if (t[j].key == key) return t[j].obj;
    return NULL;
}
static void prep_put(PrepEnt **t, size_t *cap, size_t *n, uint64_t key, void *obj) {
    if (!key) key = 1;
    if (*n * SF2C_EXEC_N_2_D473 >= *cap) {
        size_t nc = *cap ? *cap * SF2C_EXEC_N_2_D473 : SF2C_EXEC_N_1024_E39E;
        PrepEnt *nt = calloc(nc, sizeof *nt);
        for (size_t i = 0; i < *cap; i++)
            if ((*t)[i].key) {
                size_t j = (*t)[i].key & (nc - 1);
                while (nt[j].key) j = (j + 1) & (nc - 1);
                nt[j] = (*t)[i];
            }
        free(*t);
        *t = nt, *cap = nc;
    }
    size_t j = key & (*cap - 1);
    while ((*t)[j].key && (*t)[j].key != key) j = (j + 1) & (*cap - 1);
    if (!(*t)[j].key) (*n)++;
    (*t)[j] = (PrepEnt){key, obj};
}
#define PREP_GET(tab, key) ({ pthread_mutex_lock(&prep_lock); void *o_ = prep_get(tab, tab##_cap, key); pthread_mutex_unlock(&prep_lock); o_; })
#define PREP_PUT(tab, key, obj) do { pthread_mutex_lock(&prep_lock); prep_put(&tab, &tab##_cap, &tab##_n, key, obj); pthread_mutex_unlock(&prep_lock); } while (0)

static uint64_t prog_content_key(uint64_t vh, uint64_t fh, const ShaderKey *vkey, const ShaderKey *fkey) {
    uint64_t h = SF2C_EXEC_N_0XCBF29CE484222325ULL_7EE8;
    const uint64_t c[SF2C_EXEC_N_2_D473] = {vh, fh};
    const uint8_t *b = (const uint8_t *)c;
    for (size_t i = 0; i < sizeof c; i++) { h ^= b[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    b = (const uint8_t *)vkey;
    for (size_t i = 0; i < sizeof *vkey; i++) { h ^= b[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) { h ^= fkey->rt_kind[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    return h;
}

#define PROG_TAB SF2C_EXEC_PROG_TAB
static struct { uint64_t key; Prog *prog; } prog_tab[PROG_TAB];

static void prog_fp_check(Prog *p, NvnProgram *np) {  // (the lookups below are by code address: shader_prep.h)
    shader_fp_check(&p->fp[0], &np->stages[0], SF2C_EXEC_S_VS_35ED);
    shader_fp_check(&p->fp[1], &np->stages[1], SF2C_EXEC_S_FS_FEFF);
}

// Key memo: draws mostly repeat the program and pipeline state of a recent draw. draw_ compares the inputs of the
// program lookup with the previous lookup's, and those of the pipeline lookup with the last ones of that program
// (plain compares of everything the keys are made from), and reuses the answer instead of hashing the program key
// byte by byte and filling + hashing the ~700-byte PipeKey. The answers are the objects the tables give. key_gen:
// bumped by vk_exec_reset (tables cleared). LARECOMP_VK_NO_KEY_MEMO=1: off.
static int key_memo = -1;
static uint32_t key_gen = 1;
static bool key_memo_on(void) {
    if (key_memo < 0) key_memo = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_KEY_MEMO_ED46) == NULL;
    return key_memo;
}

static Prog *get_program_(NvnProgram *np, ShaderKey *vkey, ShaderKey *fkey);
static Prog *get_program(NvnProgram *np, ShaderKey *vkey, ShaderKey *fkey) {
    static struct { uint64_t code[SF2C_EXEC_N_2_D473]; ShaderKey vkey; uint8_t rt[SF2C_EXEC_N_8_2C62]; uint32_t gen; Prog *prog; } m;
    bool on = key_memo_on();
    if (on && m.prog && m.gen == key_gen && (np->stage_mask & SF2C_EXEC_N_3_4E07) == SF2C_EXEC_N_3_4E07 && m.code[0] == np->stages[0].code_addr &&
        m.code[1] == np->stages[1].code_addr && !memcmp(&m.vkey, vkey, sizeof *vkey) && !memcmp(m.rt, fkey->rt_kind, SF2C_EXEC_N_8_2C62)) {
        prof_prog_memo++;
        prog_fp_check(m.prog, np);  // (as a table hit)
        return m.prog;
    }
    Prog *p = get_program_(np, vkey, fkey);
    if (on && p) {
        m.code[0] = np->stages[0].code_addr; m.code[1] = np->stages[1].code_addr;
        m.vkey = *vkey; memcpy(m.rt, fkey->rt_kind, SF2C_EXEC_N_8_2C62); m.gen = key_gen; m.prog = p;
    }
    return p;
}

static Prog *get_program_(NvnProgram *np, ShaderKey *vkey, ShaderKey *fkey) {
    if (!np) return NULL;
    if (!(np->stage_mask & 1) || !(np->stage_mask & SF2C_EXEC_N_2_D473)) {
        static int nlog_mask;
        if (nlog_mask++ < SF2C_EXEC_N_10_4A44) LOG(SF2C_EXEC_S_VK_GET_PROGRAM_MISSING_STAGE_1F8E,
            np, np->stage_mask, np->stages[0].code_addr, np->stages[1].code_addr, np->stages[SF2C_EXEC_N_5_EF2D].code_addr);
        return NULL;
    }
    uint64_t h = SF2C_EXEC_N_0XCBF29CE484222325ULL_7EE8;
    for (int i = 0; i < SF2C_EXEC_N_2_D473; i++) { uint64_t a = np->stages[i].code_addr; for (int k = 0; k < SF2C_EXEC_N_8_2C62; k++) { h ^= (a >> (k * SF2C_EXEC_N_8_2C62)) & SF2C_EXEC_N_0XFF_7312; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; } }
    const uint8_t *vb = (const uint8_t *)vkey;
    for (size_t i = 0; i < sizeof *vkey; i++) { h ^= vb[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) { h ^= fkey->rt_kind[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    if (!h) h = 1;
    uint32_t slot = (uint32_t)(h ^ (h >> SF2C_EXEC_N_31_EB1E)) & (PROG_TAB - 1);
    while (prog_tab[slot].key && prog_tab[slot].key != h) slot = (slot + 1) & (PROG_TAB - 1);
    if (prog_tab[slot].key) {
        prog_fp_check(prog_tab[slot].prog, np);
        return prog_tab[slot].prog;
    }
    {   // prepared before the game (the same code and keys)
        uint64_t vh = shader_stage_hash(&np->stages[0]), fh = shader_stage_hash(&np->stages[1]);
        uint64_t ck = prog_content_key(vh, fh, vkey, fkey);
        Prog *pp = vh && fh && prep_progs_n ? PREP_GET(prep_progs, ck) : NULL;
        if (pp) {
            prog_tab[slot].key = h;
            prog_tab[slot].prog = pp;
            prog_fp_check(pp, np);
            return pp;
        }
        shader_first_visit(SF2C_EXEC_S_PROGRAM_7AB0, vh, fh, ck);
    }
    Prog *p = calloc(1, sizeof *p);
    prog_fp_check(p, np);  // (takes the fingerprint)
    prog_tab[slot].key = h;
    prog_tab[slot].prog = p;
    for (int i = 0; i < SF2C_EXEC_N_2_D473; i++) {
        p->code[i] = np->stages[i].code_addr;
        p->cmax[i] = np->stages[i].const_offset ? np->stages[i].const_offset : SF2C_EXEC_N_0X10000_4939;
    }
    p->vkey = *vkey;
    p->fkey = *fkey;
    run_translate(translate_program, p);
    return p;
}

// ================================================================== vertex formats / fixed function tables
static int attr_kind(uint32_t fmt) {
    switch (fmt) {
    case SF2C_EXEC_N_0X03_9D1B: case SF2C_EXEC_N_0X08_1EC8: case SF2C_EXEC_N_0X0B_C106: case SF2C_EXEC_N_0X0F_1F1D: case SF2C_EXEC_N_0X14_F942: case SF2C_EXEC_N_0X17_A99E: case SF2C_EXEC_N_0X1B_E0FA: case SF2C_EXEC_N_0X20_A766: case SF2C_EXEC_N_0X23_A029:
    case SF2C_EXEC_N_0X27_D7EC: case SF2C_EXEC_N_0X2C_8A63: case SF2C_EXEC_N_0X2F_21A6: case SF2C_EXEC_N_0X3E_D948:
        return SF2C_EXEC_N_2_D473;
    case SF2C_EXEC_N_0X04_B03A: case SF2C_EXEC_N_0X09_59B7: case SF2C_EXEC_N_0X0C_49AA: case SF2C_EXEC_N_0X10_BF9C: case SF2C_EXEC_N_0X15_5FB5: case SF2C_EXEC_N_0X18_D47C: case SF2C_EXEC_N_0X1C_E409: case SF2C_EXEC_N_0X21_A102: case SF2C_EXEC_N_0X24_EB21:
    case SF2C_EXEC_N_0X28_EAB9: case SF2C_EXEC_N_0X2D_A7B5: case SF2C_EXEC_N_0X30_8BAD:
        return 1;
    default: return 0;
    }
}

static VkFormat vertex_format(uint32_t f) {
    switch (f) {
    case SF2C_EXEC_N_0X01_B51C: return VK_FORMAT_R8_UNORM;
    case SF2C_EXEC_N_0X02_E6BD: return VK_FORMAT_R8_SNORM;
    case SF2C_EXEC_N_0X03_9D1B: return VK_FORMAT_R8_UINT;
    case SF2C_EXEC_N_0X04_B03A: return VK_FORMAT_R8_SINT;
    case SF2C_EXEC_N_0X05_4ED3: return VK_FORMAT_R16_SFLOAT;
    case SF2C_EXEC_N_0X06_9AE3: return VK_FORMAT_R16_UNORM;
    case SF2C_EXEC_N_0X07_A022: return VK_FORMAT_R16_SNORM;
    case SF2C_EXEC_N_0X08_1EC8: return VK_FORMAT_R16_UINT;
    case SF2C_EXEC_N_0X09_59B7: return VK_FORMAT_R16_SINT;
    case SF2C_EXEC_N_0X0A_AF2D: return VK_FORMAT_R32_SFLOAT;
    case SF2C_EXEC_N_0X0B_C106: return VK_FORMAT_R32_UINT;
    case SF2C_EXEC_N_0X0C_49AA: return VK_FORMAT_R32_SINT;
    case SF2C_EXEC_N_0X0D_106A: return VK_FORMAT_R8G8_UNORM;
    case SF2C_EXEC_N_0X0E_D851: return VK_FORMAT_R8G8_SNORM;
    case SF2C_EXEC_N_0X0F_1F1D: return VK_FORMAT_R8G8_UINT;
    case SF2C_EXEC_N_0X10_BF9C: return VK_FORMAT_R8G8_SINT;
    case SF2C_EXEC_N_0X11_A3D6: return VK_FORMAT_R16G16_SFLOAT;
    case SF2C_EXEC_N_0X12_9AE3: return VK_FORMAT_R16G16_UNORM;
    case SF2C_EXEC_N_0X13_3ABB: return VK_FORMAT_R16G16_SNORM;
    case SF2C_EXEC_N_0X14_F942: return VK_FORMAT_R16G16_UINT;
    case SF2C_EXEC_N_0X15_5FB5: return VK_FORMAT_R16G16_SINT;
    case SF2C_EXEC_N_0X16_D7D4: return VK_FORMAT_R32G32_SFLOAT;
    case SF2C_EXEC_N_0X17_A99E: return VK_FORMAT_R32G32_UINT;
    case SF2C_EXEC_N_0X18_D47C: return VK_FORMAT_R32G32_SINT;
    case SF2C_EXEC_N_0X19_6E00: return VK_FORMAT_R8G8B8_UNORM;
    case SF2C_EXEC_N_0X1A_7631: return VK_FORMAT_R8G8B8_SNORM;
    case SF2C_EXEC_N_0X1B_E0FA: return VK_FORMAT_R8G8B8_UINT;
    case SF2C_EXEC_N_0X1C_E409: return VK_FORMAT_R8G8B8_SINT;
    case SF2C_EXEC_N_0X1D_A216: return VK_FORMAT_R16G16B16_SFLOAT;
    case SF2C_EXEC_N_0X1E_43F5: return VK_FORMAT_R16G16B16_UNORM;
    case SF2C_EXEC_N_0X1F_A571: return VK_FORMAT_R16G16B16_SNORM;
    case SF2C_EXEC_N_0X20_A766: return VK_FORMAT_R16G16B16_UINT;
    case SF2C_EXEC_N_0X21_A102: return VK_FORMAT_R16G16B16_SINT;
    case SF2C_EXEC_N_0X22_9F8A: return VK_FORMAT_R32G32B32_SFLOAT;
    case SF2C_EXEC_N_0X23_A029: return VK_FORMAT_R32G32B32_UINT;
    case SF2C_EXEC_N_0X24_EB21: return VK_FORMAT_R32G32B32_SINT;
    case SF2C_EXEC_N_0X25_2611: return VK_FORMAT_R8G8B8A8_UNORM;
    case SF2C_EXEC_N_0X26_50D1: return VK_FORMAT_R8G8B8A8_SNORM;
    case SF2C_EXEC_N_0X27_D7EC: return VK_FORMAT_R8G8B8A8_UINT;
    case SF2C_EXEC_N_0X28_EAB9: return VK_FORMAT_R8G8B8A8_SINT;
    case SF2C_EXEC_N_0X29_F941: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case SF2C_EXEC_N_0X2A_9EC1: return VK_FORMAT_R16G16B16A16_UNORM;
    case SF2C_EXEC_N_0X2B_28A6: return VK_FORMAT_R16G16B16A16_SNORM;
    case SF2C_EXEC_N_0X2C_8A63: return VK_FORMAT_R16G16B16A16_UINT;
    case SF2C_EXEC_N_0X2D_A7B5: return VK_FORMAT_R16G16B16A16_SINT;
    case SF2C_EXEC_N_0X2E_E0C5: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case SF2C_EXEC_N_0X2F_21A6: return VK_FORMAT_R32G32B32A32_UINT;
    case SF2C_EXEC_N_0X30_8BAD: return VK_FORMAT_R32G32B32A32_SINT;
    case SF2C_EXEC_N_0X3D_4B02: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case SF2C_EXEC_N_0X69_3172: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case SF2C_EXEC_N_0X3F_AD9F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case SF2C_EXEC_N_0X40_1AF3: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case SF2C_EXEC_N_0X51_E75E: return VK_FORMAT_R8_UINT;
    default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

// 3-component 8/16-bit vertex formats aren't universally supported: read 4 components instead.
static VkFormat vertex_format_supported(VkFormat f) {
    static uint8_t known[SF2C_EXEC_N_256_51E8];  // 1 ok, 2 not
    uint32_t i = f < SF2C_EXEC_N_256_51E8 ? f : 0;
    if (!known[i]) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
        known[i] = (p.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) ? 1 : SF2C_EXEC_N_2_D473;
    }
    if (known[i] == 1) return f;
    { static uint8_t told[SF2C_EXEC_N_256_51E8]; if (!told[i]++) LOG(SF2C_EXEC_S_VK_VERTEX_FORMAT_D_UNSUPPORT_0476, f); }
    switch (f) {
    case VK_FORMAT_R8G8B8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R8G8B8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_R16G16B16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case VK_FORMAT_R16G16B16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16G16B16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
    default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

static VkBlendFactor blend_factor(uint8_t f) {
    switch (f) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case SF2C_EXEC_N_2_D473: return VK_BLEND_FACTOR_ONE;
    case SF2C_EXEC_N_3_4E07: return VK_BLEND_FACTOR_SRC_COLOR;
    case SF2C_EXEC_N_4_4B22: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case SF2C_EXEC_N_5_EF2D: return VK_BLEND_FACTOR_SRC_ALPHA;
    case SF2C_EXEC_N_6_E7F6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case SF2C_EXEC_N_7_7902: return VK_BLEND_FACTOR_DST_ALPHA;
    case SF2C_EXEC_N_8_2C62: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case SF2C_EXEC_N_9_1958: return VK_BLEND_FACTOR_DST_COLOR;
    case SF2C_EXEC_N_10_4A44: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case SF2C_EXEC_N_11_4FC8: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case SF2C_EXEC_N_0X10_BF9C: return VK_BLEND_FACTOR_SRC1_COLOR;
    case SF2C_EXEC_N_0X11_A3D6: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case SF2C_EXEC_N_0X12_9AE3: return VK_BLEND_FACTOR_SRC1_ALPHA;
    case SF2C_EXEC_N_0X13_3ABB: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    case SF2C_EXEC_N_0X61_DB35: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case SF2C_EXEC_N_0X62_88BC: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case SF2C_EXEC_N_0X63_A6C6: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case SF2C_EXEC_N_0X64_3246: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default: {
        static uint8_t warned[SF2C_EXEC_N_256_51E8];
        if (!warned[f]++) LOG(SF2C_EXEC_S_VK_UNKNOWN_BLEND_FACTOR_X_N_098A, f);
        return VK_BLEND_FACTOR_ONE;
    }
    }
}

static VkBlendOp blend_op(uint8_t e) {
    switch (e) {
    case SF2C_EXEC_N_2_D473: return VK_BLEND_OP_SUBTRACT;
    case SF2C_EXEC_N_3_4E07: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case SF2C_EXEC_N_4_4B22: return VK_BLEND_OP_MIN;
    case SF2C_EXEC_N_5_EF2D: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp compare_fn(uint8_t f) {
    switch (f) {
    case 1: return VK_COMPARE_OP_NEVER;
    case SF2C_EXEC_N_2_D473: return VK_COMPARE_OP_LESS;
    case SF2C_EXEC_N_3_4E07: return VK_COMPARE_OP_EQUAL;
    case SF2C_EXEC_N_4_4B22: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case SF2C_EXEC_N_5_EF2D: return VK_COMPARE_OP_GREATER;
    case SF2C_EXEC_N_6_E7F6: return VK_COMPARE_OP_NOT_EQUAL;
    case SF2C_EXEC_N_7_7902: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

static VkStencilOp stencil_op(uint8_t o) {
    switch (o) {
    case SF2C_EXEC_N_2_D473: return VK_STENCIL_OP_ZERO;
    case SF2C_EXEC_N_3_4E07: return VK_STENCIL_OP_REPLACE;
    case SF2C_EXEC_N_4_4B22: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case SF2C_EXEC_N_5_EF2D: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case SF2C_EXEC_N_6_E7F6: return VK_STENCIL_OP_INVERT;
    case SF2C_EXEC_N_7_7902: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case SF2C_EXEC_N_8_2C62: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

static int rt_kind(uint32_t f) {
    switch (f) {
    case SF2C_EXEC_N_0X03_9D1B: case SF2C_EXEC_N_0X08_1EC8: case SF2C_EXEC_N_0X0B_C106: case SF2C_EXEC_N_0X0F_1F1D: case SF2C_EXEC_N_0X14_F942: case SF2C_EXEC_N_0X17_A99E: case SF2C_EXEC_N_0X27_D7EC: case SF2C_EXEC_N_0X2C_8A63: case SF2C_EXEC_N_0X2F_21A6: case SF2C_EXEC_N_0X3E_D948: return SF2C_EXEC_N_2_D473;
    case SF2C_EXEC_N_0X04_B03A: case SF2C_EXEC_N_0X09_59B7: case SF2C_EXEC_N_0X0C_49AA: case SF2C_EXEC_N_0X10_BF9C: case SF2C_EXEC_N_0X15_5FB5: case SF2C_EXEC_N_0X18_D47C: case SF2C_EXEC_N_0X28_EAB9: case SF2C_EXEC_N_0X2D_A7B5: case SF2C_EXEC_N_0X30_8BAD: return 1;
    default: return 0;
    }
}

// ================================================================== graphics pipelines
// Everything a pipeline bakes in, captured by value so it can be built on a worker thread.
#define ZERO_BINDING SF2C_EXEC_ZERO_BINDING
typedef struct {
    Prog *prog;
    VkFormat color[SF2C_EXEC_N_8_2C62], depth, stencil;
    uint32_t ncolor, samples;
    uint32_t attr_fmt[SF2C_EXEC_N_32_E29C], attr_off[SF2C_EXEC_N_32_E29C], attr_bind[SF2C_EXEC_N_32_E29C];
    uint32_t attr_mask;
    uint32_t stride[SF2C_EXEC_N_17_4523], divisor[SF2C_EXEC_N_17_4523], used_streams;
    uint8_t blend[SF2C_EXEC_N_8_2C62][SF2C_EXEC_N_8_2C62];
    uint8_t blend_on, a2c, topo_class, poly_fill;
    uint32_t write_mask[SF2C_EXEC_N_8_2C62];
} PipeKey;

typedef struct { PipeKey k; uint64_t hash; VkPipeline pipe; _Atomic int state; } Pipe;  // state: 0 building, 1 ready, 2 failed
// (record.c, at the bind) the pipeline of a Pipe: NULL while it builds (wait: sleeps until it is done) or failed
static VkPipeline pipe_resolve(void *obj, bool wait) {
    Pipe *pp = obj;
    int st_;
    while ((st_ = atomic_load(&pp->state)) == 0) {
        if (!wait) return VK_NULL_HANDLE;
        struct timespec ts = {0, SF2C_EXEC_N_200000_B552};
        nanosleep(&ts, NULL);
    }
    return st_ == 1 ? pp->pipe : VK_NULL_HANDLE;
}
// (key memo, see get_program) everything pipe_key_fill reads besides the program: the pass's formats and the PipeIn.
// One per program (draws often alternate between a few programs): the last pipeline that program drew with.
struct PipeMemo {
    uint32_t gen, ncolor, samples, channel_mask;
    int nattrib, nstream;
    VkFormat color[SF2C_EXEC_N_8_2C62], depth, stencil;
    uint32_t attrib[SF2C_EXEC_N_16_B17E], stream[SF2C_EXEC_N_16_B17E][SF2C_EXEC_N_2_D473], rt_fmt[SF2C_EXEC_N_8_2C62];
    uint8_t blend[SF2C_EXEC_N_8_2C62][SF2C_EXEC_N_8_2C62], blend_on, a2c, cls, poly_fill;
    Pipe *pp;
    VtxLayout vl;  // the layout pipe_key_fill made from these inputs (dynamic vertex input leaves it out of pp's key)
};
#define PIPE_TAB SF2C_EXEC_PIPE_TAB
static struct { uint64_t key; Pipe *pipe; } pipe_tab[PIPE_TAB];

// 8 bytes a step (PipeKey is ~700 bytes, hashed per draw); in-memory tables only - hmix values are compared with
// stored constants (fragment-shader hashes) and must not change.
static uint64_t hash_words(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (; n >= SF2C_EXEC_N_8_2C62; n -= SF2C_EXEC_N_8_2C62, b += SF2C_EXEC_N_8_2C62) { uint64_t w; memcpy(&w, b, SF2C_EXEC_N_8_2C62); h = (h ^ w) * SF2C_EXEC_N_0X9E3779B97F4A7C15ULL_8CC5; h ^= h >> SF2C_EXEC_N_29_3513; }
    for (; n; n--, b++) { h = (h ^ *b) * SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    return h ^ (h >> SF2C_EXEC_N_32_E29C);
}

static uint64_t hmix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    return h;
}

static VkPrimitiveTopology topo_class_base(uint8_t c) {
    return c == 0 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : c == 1 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

// What a pipeline is built from, besides its program and the pass's attachment formats: the draw's state (draw_) or a
// variant record (renderer_prepare)
typedef struct {
    const uint32_t *attrib; int nattrib;
    const uint32_t (*stream)[SF2C_EXEC_N_2_D473]; int nstream;
    const uint32_t *rt_fmt;         // color target texture formats (0: none)
    uint32_t channel_mask;
    uint8_t blend_on;               // color state blend enables
    const uint8_t (*blend)[SF2C_EXEC_N_8_2C62];
    uint8_t a2c, topo_class, poly_fill;
} PipeIn;

// vl (optional): the vertex layout, also when the key leaves it out (dynamic vertex input)
static void pipe_key_fill(PipeKey *k, Prog *pr, const PipeIn *in, const VkFormat color[SF2C_EXEC_N_8_2C62], uint32_t ncolor, VkFormat depth,
                          VkFormat stencil, uint32_t samples, VtxLayout *vl) {
    memset(k, 0, sizeof *k);
    k->prog = pr;
    k->ncolor = ncolor;
    // (only the pass's own slots: begin_pass leaves the slots past them as the last pass with more targets set them -
    // a depth-only pass's key carried the previous pass's color formats, so prepared pipelines never matched it)
    for (uint32_t i = 0; i < ncolor && i < SF2C_EXEC_N_8_2C62; i++) k->color[i] = color[i];
    k->depth = depth;
    k->stencil = stencil;
    k->samples = samples;
    VtxLayout l_, *l = vl ? vl : &l_;
    memset(l, 0, sizeof *l);
    for (int i = 0; i < SF2C_EXEC_N_32_E29C; i++) {
        if (!(pr->vi->attr_mask & (SF2C_EXEC_N_1U_B8E6 << i))) continue;
        l->attr_mask |= SF2C_EXEC_N_1U_B8E6 << i;
        uint32_t a = i < in->nattrib ? in->attrib[i] : SF2C_EXEC_N_0X2E_E0C5;
        uint32_t fmt = a & SF2C_EXEC_N_0XFF_7312, stream = (a >> SF2C_EXEC_N_8_2C62) & SF2C_EXEC_N_0X1F_A571, off = a >> SF2C_EXEC_N_13_3FDB;
        if (i >= in->nattrib || fmt == 0 || stream >= SF2C_EXEC_N_16_B17E) {  // unbound attribute: (0, 0, 0, 1) from a constant stream
            l->attr_fmt[i] = VK_FORMAT_R32G32B32A32_SFLOAT;
            l->attr_bind[i] = ZERO_BINDING;
            l->used_streams |= SF2C_EXEC_N_1U_B8E6 << ZERO_BINDING;
            continue;
        }
        l->attr_fmt[i] = vertex_format_supported(vertex_format(fmt));
        l->attr_off[i] = off;
        l->attr_bind[i] = stream;
        l->used_streams |= SF2C_EXEC_N_1U_B8E6 << stream;
    }
    for (int s = 0; s < SF2C_EXEC_N_16_B17E; s++) {
        if (!(l->used_streams & (SF2C_EXEC_N_1U_B8E6 << s))) continue;
        l->stride[s] = s < in->nstream ? in->stream[s][0] : 0;
        l->divisor[s] = s < in->nstream && l->stride[s] ? in->stream[s][1] : 0;
        if (l->divisor[s] > 1 && !g_vk.divisor) l->divisor[s] = 1;
    }
    k->attr_mask = l->attr_mask;  // (the program's: the same with either path)
    if (!g_vk.vids) {             // the layout is part of the pipeline
        memcpy(k->attr_fmt, l->attr_fmt, sizeof k->attr_fmt);
        memcpy(k->attr_off, l->attr_off, sizeof k->attr_off);
        memcpy(k->attr_bind, l->attr_bind, sizeof k->attr_bind);
        memcpy(k->stride, l->stride, sizeof k->stride);
        memcpy(k->divisor, l->divisor, sizeof k->divisor);
        k->used_streams = l->used_streams;
    }
    for (uint32_t i = 0; i < k->ncolor && i < SF2C_EXEC_N_8_2C62; i++) {
        if (!in->rt_fmt[i] || k->color[i] == VK_FORMAT_UNDEFINED) continue;
        // Targets the fragment shader doesn't output keep their contents on Metal, but are undefined in Vulkan
        // (NVIDIA writes black): mask them. Foliage depth pre-passes left black silhouettes (user 2026-10-08).
        k->write_mask[i] = ((pr->fi->out_mask >> (i * SF2C_EXEC_N_4_4B22)) & SF2C_EXEC_N_0XF_7419) ? (in->channel_mask >> (i * SF2C_EXEC_N_4_4B22)) & SF2C_EXEC_N_0XF_7419 : 0;
        if (((in->blend_on >> i) & 1) && !rt_kind(in->rt_fmt[i])) {
            k->blend_on |= (uint8_t)(SF2C_EXEC_N_1U_B8E6 << i);
            memcpy(k->blend[i], in->blend[i], SF2C_EXEC_N_8_2C62);
        }
    }
    k->a2c = in->a2c;
    k->topo_class = in->topo_class;
    k->poly_fill = in->poly_fill;
}

// A pipeline's create info from its key (the structures point into each other and into the key: filled in place)
typedef struct {
    VkPipelineShaderStageCreateInfo stages[SF2C_EXEC_N_2_D473];
    VkVertexInputAttributeDescription va[SF2C_EXEC_N_32_E29C];
    VkVertexInputBindingDescription vb[SF2C_EXEC_N_17_4523];
    VkVertexInputBindingDivisorDescriptionEXT vd[SF2C_EXEC_N_17_4523];
    VkPipelineVertexInputDivisorStateCreateInfoEXT dvi;
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vs;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState cba[SF2C_EXEC_N_8_2C62];
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dyn[NDYN + 1];  // (+ the vertex layout with dynamic vertex input: draw_ sets it)
    VkPipelineDynamicStateCreateInfo dy;
    VkPipelineRenderingCreateInfo ri;
    VkGraphicsPipelineCreateInfo pi;
} PipeCI;

static void pipe_ci_fill(PipeCI *c, const PipeKey *k) {
    memset(c, 0, sizeof *c);
    c->stages[0] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = k->prog->vs, .pName = SF2C_EXEC_S_MAIN_E4A9};
    c->stages[1] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = k->a2c && k->prog->fs_late ? k->prog->fs_late : k->prog->fs, .pName = SF2C_EXEC_S_MAIN_E4A9};
    uint32_t na = 0, nb = 0, nd = 0;
    for (int i = 0; i < SF2C_EXEC_N_32_E29C; i++)
        if (k->attr_mask & (SF2C_EXEC_N_1U_B8E6 << i)) c->va[na++] = (VkVertexInputAttributeDescription){(uint32_t)i, k->attr_bind[i], k->attr_fmt[i], k->attr_off[i]};
    for (uint32_t s = 0; s < SF2C_EXEC_N_17_4523; s++) {
        if (!(k->used_streams & (SF2C_EXEC_N_1U_B8E6 << s))) continue;
        bool inst = k->divisor[s] != 0;
        c->vb[nb++] = (VkVertexInputBindingDescription){s, k->stride[s], inst ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
        if (inst && k->divisor[s] > 1 && g_vk.divisor) c->vd[nd++] = (VkVertexInputBindingDivisorDescriptionEXT){s, k->divisor[s]};
    }
    c->dvi = (VkPipelineVertexInputDivisorStateCreateInfoEXT){.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
                                                              .vertexBindingDivisorCount = nd, .pVertexBindingDivisors = c->vd};
    c->vi = (VkPipelineVertexInputStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .pNext = nd ? &c->dvi : NULL,
                                                   .vertexBindingDescriptionCount = nb, .pVertexBindingDescriptions = c->vb,
                                                   .vertexAttributeDescriptionCount = na, .pVertexAttributeDescriptions = c->va};
    c->ia = (VkPipelineInputAssemblyStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                     .topology = topo_class_base(k->topo_class)};
    c->vs = (VkPipelineViewportStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    c->rs = (VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = k->poly_fill ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE, .lineWidth = 1};
    c->ms = (VkPipelineMultisampleStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                   .rasterizationSamples = (VkSampleCountFlagBits)k->samples, .alphaToCoverageEnable = k->a2c};
    c->ds = (VkPipelineDepthStencilStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    for (uint32_t i = 0; i < k->ncolor; i++) {
        c->cba[i].colorWriteMask = k->write_mask[i];
        if ((k->blend_on >> i) & 1) {
            const uint8_t *b = k->blend[i];
            c->cba[i].blendEnable = VK_TRUE;
            c->cba[i].srcColorBlendFactor = blend_factor(b[1]);
            c->cba[i].dstColorBlendFactor = blend_factor(b[SF2C_EXEC_N_2_D473]);
            c->cba[i].srcAlphaBlendFactor = blend_factor(b[SF2C_EXEC_N_3_4E07]);
            c->cba[i].dstAlphaBlendFactor = blend_factor(b[SF2C_EXEC_N_4_4B22]);
            c->cba[i].colorBlendOp = blend_op(b[SF2C_EXEC_N_5_EF2D]);
            c->cba[i].alphaBlendOp = blend_op(b[SF2C_EXEC_N_6_E7F6]);
        }
    }
    c->cb = (VkPipelineColorBlendStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                  .attachmentCount = k->ncolor, .pAttachments = c->cba};
    memcpy(c->dyn, DYN, sizeof DYN);
    if (g_vk.vids) c->dyn[NDYN] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
    c->dy = (VkPipelineDynamicStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                               .dynamicStateCount = NDYN + g_vk.vids, .pDynamicStates = c->dyn};
    c->ri = (VkPipelineRenderingCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = k->ncolor,
                                            .pColorAttachmentFormats = k->color, .depthAttachmentFormat = k->depth,
                                            .stencilAttachmentFormat = k->stencil};
    c->pi = (VkGraphicsPipelineCreateInfo){.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &c->ri, .stageCount = SF2C_EXEC_N_2_D473,
                                           .pStages = c->stages, .pVertexInputState = g_vk.vids ? NULL : &c->vi,
                                           .pInputAssemblyState = &c->ia, .pViewportState = &c->vs, .pRasterizationState = &c->rs,
                                           .pMultisampleState = &c->ms, .pDepthStencilState = &c->ds, .pColorBlendState = &c->cb,
                                           .pDynamicState = &c->dy, .layout = k->prog->layout};
}

static void build_pipeline(void *arg) {
    Pipe *pp = arg;
    PipeKey *k = &pp->k;
    PipeCI c;
    pipe_ci_fill(&c, k);
    uint64_t t0 = SDL_GetTicksNS();
    int bs = build_begin(SF2C_EXEC_S_PIPELINE_VS_FS_918F, k->prog->code[0] ? k->prog->code[0] : k->prog->prep_vh,
                         k->prog->code[1] ? k->prog->code[1] : k->prog->prep_fh);
    SLOW_FEEDBACK(c.pi);
    VkResult r = vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &c.pi, NULL, &pp->pipe);
    SLOW_FEEDBACK_HIT();
    build_end(bs);
    if (r != VK_SUCCESS) {
        static int n;
        if (n++ < SF2C_EXEC_N_10_4A44) LOG(SF2C_EXEC_S_VK_PIPELINE_CREATION_FAILED__BD32, r);
    }
    atomic_fetch_add(&stat_compiles, 1);
    atomic_store(&pp->state, r == VK_SUCCESS ? 1 : SF2C_EXEC_N_2_D473);
    slow_build(t0, SF2C_EXEC_S_PIPELINE_VS_FS_918F, k->prog->code[0], k->prog->code[1]);
}

// Dynamic vertex input (g_vk.vids): the draw's layout, as build_pipeline would bake it in
static void set_vertex_input(VkCommandBuffer cb, const VtxLayout *l) {
    VkVertexInputAttributeDescription2EXT va[SF2C_EXEC_N_32_E29C];
    VkVertexInputBindingDescription2EXT vb[SF2C_EXEC_N_17_4523];
    uint32_t na = 0, nb = 0;
    for (int i = 0; i < SF2C_EXEC_N_32_E29C; i++)
        if (l->attr_mask & (SF2C_EXEC_N_1U_B8E6 << i))
            va[na++] = (VkVertexInputAttributeDescription2EXT){.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
                .location = (uint32_t)i, .binding = l->attr_bind[i], .format = l->attr_fmt[i], .offset = l->attr_off[i]};
    for (uint32_t s = 0; s < SF2C_EXEC_N_17_4523; s++) {
        if (!(l->used_streams & (SF2C_EXEC_N_1U_B8E6 << s))) continue;
        bool inst = l->divisor[s] != 0;
        vb[nb++] = (VkVertexInputBindingDescription2EXT){.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
            .binding = s, .stride = l->stride[s], .inputRate = inst ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX,
            .divisor = inst ? l->divisor[s] : 1};
    }
    rec_CmdSetVertexInputEXT(cb, nb, vb, na, va);
}

// ================================================================== feedback snapshots (renderer.m resolve_feedback)
static NvnTexture *feedback_tex[SF2C_EXEC_N_4_4B22];
static VkImageView feedback_view[SF2C_EXEC_N_4_4B22];
static int nfeedback;
static uint64_t g_cur_fs_hash;
static NvnTexture *snap_tex[SF2C_EXEC_N_8_2C62];
static bool snap_ok[SF2C_EXEC_N_8_2C62];
static const char *snap_why[SF2C_EXEC_N_8_2C62];
static const char *snap_reason = SF2C_EXEC_S_EMPTY_15C5;
static void snapshots_invalidate(NvnTexture *t) {
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) if ((!t || snap_tex[i] == t) && snap_ok[i]) { snap_ok[i] = false; snap_why[i] = snap_reason; }
}

typedef struct { NvnTexture *key; TexBackend copy; } Snap;
static Snap snaps[SF2C_EXEC_N_16_B17E];
static int nsnaps;

static NvnTexture *shader_texture_object(int stage, ShaderTexture *st_) {
    int slot = ((int)st_->handle_offset - SF2C_EXEC_N_0X20_A766) / SF2C_EXEC_N_8_2C62;
    uint64_t h = (slot >= 0 && slot < SF2C_EXEC_N_32_E29C) ? st.tex[stage][slot] : 0;
    uint32_t tic = h & SF2C_EXEC_N_0XFFFFF_9137;
    if (!h || !st.tpool || tic >= (uint32_t)st.tpool->count) return NULL;
    return ((NvnTexEntry *)st.tpool->entries)[tic].tex;
}

static bool is_attachment(NvnTexture *t) {
    if (!t) return false;
    if (t == st.rt.depth) return true;
    for (int i = 0; i < st.rt.ncolor; i++) if (t == st.rt.color[i]) return true;
    return false;
}

static TexBackend *snapshot_image(NvnTexture *t, TexBackend *src) {
    Snap *s = NULL;
    for (int i = 0; i < nsnaps; i++) if (snaps[i].key == t) s = &snaps[i];
    if (!s) {
        if (nsnaps == SF2C_EXEC_N_16_B17E) { static int rr; s = &snaps[rr++ & SF2C_EXEC_N_15_E629]; } else s = &snaps[nsnaps++];
        s->key = t;
    }
    TexBackend *d = &s->copy;
    if (d->image && (d->width != src->width || d->height != src->height || d->format != src->format)) {
        vk_views_forget(d);  // (resolution change)
        vk_defer_image(d->image, d->mem);
        d->image = VK_NULL_HANDLE;
        d->mem = VK_NULL_HANDLE;
    }
    if (!d->image) {
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = src->format,
                                .extent = {src->width, src->height, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = (VkSampleCountFlagBits)src->samples, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &d->image, &d->mem, NULL));
        d->format = src->format; d->width = src->width; d->height = src->height; d->depth = 1; d->layers = 1; d->levels = 1;
        d->samples = src->samples; d->aspect = src->aspect; d->base_type = VK_IMAGE_VIEW_TYPE_2D;
        vk_image_init_layout(vk_cmd(), d->image, d->aspect);
    }
    return d;
}

// (debug frames) a depth image's depth values as a greyscale PGM, stretched over its own min..max
static void debug_save_depth(TexBackend *b, const char *path) {
    if (!b || !b->image || !(b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || b->format != VK_FORMAT_D32_SFLOAT_S8_UINT) return;
    uint64_t n = (uint64_t)b->width * b->height;
    VkSpan out = vk_readback(n * SF2C_EXEC_N_4_4B22);
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    VkBufferImageCopy c = {.bufferOffset = out.off, .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                           .imageExtent = {b->width, b->height, 1}};
    rec_CmdCopyImageToBuffer(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, out.buf, 1, &c);
    renderer_flush();
    const float *d = out.ptr;
    float lo = SF2C_EXEC_N_1E30F_4848, hi = -SF2C_EXEC_N_1E30F_4848;
    for (uint64_t i = 0; i < n; i++) { if (d[i] < lo) lo = d[i]; if (d[i] > hi) hi = d[i]; }
    FILE *f = fopen(path, SF2C_EXEC_S_WB_3D6A);
    if (!f) return;
    fprintf(f, SF2C_EXEC_S_P5_U_U_255_N_A629, b->width, b->height);
    for (uint64_t i = 0; i < n; i++) fputc(hi > lo ? (int)((d[i] - lo) / (hi - lo) * SF2C_EXEC_N_255_0F_96C1) : 0, f);
    fclose(f);
    LOG(SF2C_EXEC_S_VK_SAVED_S_UX_U_DEPTH_6F_6F__EB88, path, b->width, b->height, lo, hi);
}

// (debug) LARECOMP_COPY_TRACE=<frame>: draws' attachments / sampled textures, copies and snapshots of that frame ("[ct]")
static bool ct_on(void) {
    static const char *e = (const char *)1;
    if (e == (const char *)1) e = getenv(SF2C_EXEC_S_LARECOMP_COPY_TRACE_ACED);
    if (!e) return false;
    static uint64_t memo_f = ~SF2C_EXEC_N_0ULL_DF1B;
    static bool memo;
    uint64_t f = renderer_present_count();
    if (f == memo_f) return memo;
    memo_f = f;
    memo = false;
    for (const char *p = e; *p;) {
        if ((uint64_t)strtoll(p, (char **)&p, SF2C_EXEC_N_10_4A44) == f) { memo = true; break; }
        if (*p == SF2C_EXEC_CH_EMPTY_EE1B) p++; else break;
    }
    if (memo) LOG(SF2C_EXEC_S_CT_FRAME_LLU_N_DC21, (unsigned long long)f);
    return memo;
}
static void ct_draw(Prog *pr, const char *what) {
    LOG(SF2C_EXEC_S_CT_S_FS_016LLX_RT_7961, what, (unsigned long long)g_cur_fs_hash);
    for (int i = 0; i < st.rt.ncolor; i++) if (st.rt.color[i]) LOG(SF2C_EXEC_S_C_D_P_X_1588, i, (void *)st.rt.color[i], st.rt.color[i]->format);
    if (st.rt.depth) LOG(SF2C_EXEC_S_D_P_X_S_43A8, (void *)st.rt.depth, st.rt.depth->format, st.ds[0] && st.ds[1] ? SF2C_EXEC_S_W_BF22 : SF2C_EXEC_S_EMPTY_12AE);
    LOG(SF2C_EXEC_S_CM_08X_VP_0F_0F_0F_0F_SC_D_D_D0CB, st.channel_mask, st.viewport[0][0], st.viewport[0][1], st.viewport[0][SF2C_EXEC_N_2_D473],
        st.viewport[0][SF2C_EXEC_N_3_4E07], st.scissor[0][0], st.scissor[0][1], st.scissor[0][SF2C_EXEC_N_2_D473], st.scissor[0][SF2C_EXEC_N_3_4E07]);
    ShaderInfo *infos[SF2C_EXEC_N_2_D473] = {pr->vi, pr->fi};
    for (int sg = 0; sg < SF2C_EXEC_N_2_D473; sg++)
        for (int i = 0; i < (infos[sg] ? infos[sg]->ntex : 0); i++) {
            NvnTexture *t = shader_texture_object(sg, &infos[sg]->tex[i]);
            if (t) LOG(SF2C_EXEC_S_S_P_X_6B90, sg ? SF2C_EXEC_S_EMPTY_12AE : SF2C_EXEC_S_V_F192, (void *)t, t->format);
        }
    LOG(SF2C_EXEC_S_N_D7A5);
}

// ================================================================== copy elision (depth copies)
// The game copies its main depth buffer whole, every frame, into textures that are then only sampled while the
// source keeps its depth (the water / transparency depth input), or that get their depth cleared right away
// (stencil kept). At 3x internal resolution each such copy is ~0.15 ms of pure bandwidth. Instead of copying, the
// destination becomes an alias: sampling it (and copying from it) reads the source's image. The alias ends before
// anything changes either side:
//   - the source is about to be overwritten whole (both aspects cleared): the two images swap - the destination
//     keeps the contents without a copy, the source is cleared in the destination's old image;
//   - the destination is about to be overwritten in some aspects (its depth cleared): only the other aspects are
//     copied (stencil, a quarter of the bytes);
//   - anything else (a draw writing depth / stencil, a partial clear, a copy or upload into either, the
//     destination bound as an attachment, a texture freed or reallocated): the real copy is made first.
// Only same-size, same-format, single-level, single-sample depth/stencil images, whole-image copies.
// LARECOMP_VK_COPY_OPT=0: always copy (the old behaviour).
typedef struct { NvnTexture *src, *dst; VkImage simg, dimg; } CopyAlias;
extern uint32_t vk_view_gen;
static CopyAlias aliases[SF2C_EXEC_N_8_2C62];
static int naliases;
static uint64_t stat_alias_made, stat_alias_renamed, stat_alias_partial, stat_alias_copied;

static int copy_opt(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_COPY_OPT_AFEA); on = e && *e ? atoi(e) : 1; }
    return on;
}

// still valid: both textures keep the images the alias was made with (a resolution change reallocates them)
static bool alias_valid(const CopyAlias *a) {
    const TexBackend *s = a->src->backend, *d = a->dst->backend;
    return s && d && s->image == a->simg && d->image == a->dimg;
}

static void alias_drop(int i) {
    aliases[i] = aliases[--naliases];
    vk_view_gen++;  // (exec.c's per-frame view memo: the destination's views change)
}

// the real copy of the aspects asked for, into the destination's own image
static void alias_copy(CopyAlias *a, VkImageAspectFlags aspects, const char *why) {
    TexBackend *s = a->src->backend, *d = a->dst->backend;
    aspects &= d->aspect;
    if (!aspects) return;
    vk_end_pass();
    st.rt_dirty = true;
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    gp_mark(cb, SF2C_EXEC_S_COPY_TEXTURE_DEFERRED_S_S_UX_6301, why, aspects == d->aspect ? SF2C_EXEC_S_EMPTY_12AE : SF2C_EXEC_S_STENCIL_ONLY_1588, a->dst->width, a->dst->height);
    VkImageCopy c = {.srcSubresource = {aspects, 0, 0, 1}, .dstSubresource = {aspects, 0, 0, 1}, .extent = {d->width, d->height, 1}};
    rec_CmdCopyImage(cb, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    if (aspects == d->aspect) stat_alias_copied++; else stat_alias_partial++;
    if (ct_on()) LOG(SF2C_EXEC_S_CT_ALIAS_COPY_P_P_ASPECTS_X__8408, (void *)a->src, (void *)a->dst, aspects, why);
}

// t is about to be written; full: the aspects of it the write overwrites whole (0: unknown / partial)
static void alias_before_write(NvnTexture *t, VkImageAspectFlags full, const char *why) {
    if (!naliases || !t) return;
    for (int i = naliases - 1; i >= 0; i--) {
        CopyAlias *a = &aliases[i];
        if (!alias_valid(a)) { alias_drop(i); continue; }
        if (a->dst != t) continue;
        // (LARECOMP_VK_COPY_OPT=2: all aspects even then - for comparing the stencil-only copy's cost)
        alias_copy(a, ((TexBackend *)t->backend)->aspect & (copy_opt() == SF2C_EXEC_N_2_D473 ? ~SF2C_EXEC_N_0U_86B4 : ~full), why);
        alias_drop(i);
    }
    NvnTexture *heir = NULL;  // t overwritten whole: one destination can take its image instead of a copy
    if (t->backend && (((TexBackend *)t->backend)->aspect & ~full) == 0)
        for (int i = 0; i < naliases && !heir; i++) if (aliases[i].src == t) heir = aliases[i].dst;
    for (int i = naliases - 1; i >= 0; i--) {
        CopyAlias *a = &aliases[i];
        if (a->src != t || a->dst == heir) continue;
        alias_copy(a, ((TexBackend *)a->dst->backend)->aspect, why);
        alias_drop(i);
    }
    int rename = -1;
    for (int i = 0; i < naliases; i++) if (heir && aliases[i].src == t && aliases[i].dst == heir) rename = i;
    if (rename >= 0) {
        CopyAlias *a = &aliases[rename];
        TexBackend *s = a->src->backend, *d = a->dst->backend;
        vk_end_pass();
        st.rt_dirty = true;
        VkImage im = s->image; s->image = d->image; d->image = im;
        VmaAllocation m = s->mem; s->mem = d->mem; d->mem = m;
        vk_views_forget(s);
        vk_views_forget(d);
        stat_alias_renamed++;
        if (ct_on()) LOG(SF2C_EXEC_S_CT_ALIAS_RENAME_P_P_S_N_1A91, (void *)a->src, (void *)a->dst, why);
        alias_drop(rename);
    }
}

// the texture whose image holds t's contents (t itself unless it is an alias)
static NvnTexture *alias_read(NvnTexture *t) {
    for (int i = 0; i < naliases; i++)
        if (aliases[i].dst == t) {
            if (alias_valid(&aliases[i])) return aliases[i].src;
            alias_drop(i);
            return t;
        }
    return t;
}

// copy_texture: a whole-image copy that can be an alias instead (true: done)
static bool alias_make(NvnTexture *src, NvnTexture *dst, TexBackend *s, TexBackend *d) {
    if (!copy_opt() || src == dst) return false;
    if (!(s->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) return false;
    if (s->format != d->format || s->width != d->width || s->height != d->height || s->aspect != d->aspect || s->levels != 1 ||
        d->levels != 1 || s->layers != 1 || d->layers != 1 || s->depth != 1 || d->depth != 1 || s->samples != 1 || d->samples != 1 ||
        s->base_type != d->base_type || s->presented || d->presented || !s->image || !d->image || !s->gpu_owned || !d->gpu_owned)
        return false;
    // the destination's previous contents (or alias) are replaced whole; a destination that is itself the source of
    // aliases gets those resolved first
    alias_before_write(dst, d->aspect, SF2C_EXEC_S_COPY_DST_792B);
    src = alias_read(src);  // (a copy of an alias: of its source)
    s = src->backend;
    if (src == dst) return true;  // (copied back onto itself)
    if (naliases == (int)(sizeof aliases / sizeof aliases[0])) return false;
    aliases[naliases++] = (CopyAlias){src, dst, s->image, d->image};
    vk_view_gen++;
    stat_alias_made++;
    return true;
}

// textures.c: t is being freed - an alias source hands its contents over first
void vk_alias_texture_destroyed(NvnTexture *t) {
    if (naliases) alias_before_write(t, 0, SF2C_EXEC_S_FREED_D1BB);
}
void vk_alias_reset(void) { naliases = 0; }  // (reboot: every texture goes)

// (renderer_stats, once per stats interval) copies made aliases / renamed / partly or fully copied later
void vk_alias_stats(void) {
    static int n;
    if (!stat_alias_made || (n++ > SF2C_EXEC_N_5_EF2D && n % SF2C_EXEC_N_30_624B)) return;
    LOG(SF2C_EXEC_S_VK_COPY_ALIAS_LLU_MADE_LLU_R_C0A2,
        (unsigned long long)stat_alias_made, (unsigned long long)stat_alias_renamed, (unsigned long long)stat_alias_partial,
        (unsigned long long)stat_alias_copied, naliases);
    stat_alias_made = stat_alias_renamed = stat_alias_partial = stat_alias_copied = 0;
}

// draws: attachments written (or a destination bound at all) end the aliases involved
static void alias_draw_check(void) {
    if (!naliases) return;
    for (int i = 0; i < st.rt.ncolor; i++)
        if (st.rt.color[i]) alias_before_write(st.rt.color[i], 0, SF2C_EXEC_S_COLOR_TARGET_8BF5);
    NvnTexture *d = st.rt.depth;
    if (!d) return;
    bool writes = (st.ds[0] && st.ds[1]) || (st.ds[SF2C_EXEC_N_3_4E07] && ((st.stencil_mask[0] | st.stencil_mask[1]) & SF2C_EXEC_N_0XFF_7312));
    bool is_dst = false;
    for (int i = 0; i < naliases; i++) is_dst |= aliases[i].dst == d;
    if (writes || is_dst) alias_before_write(d, 0, writes ? SF2C_EXEC_S_DEPTH_WRITE_0765 : SF2C_EXEC_S_DST_BOUND_2C5E);
}

static void resolve_feedback(Prog *pr) {
    nfeedback = 0;
    static int off = -1;
    if (off < 0) off = getenv(SF2C_EXEC_S_LARECOMP_NO_FEEDBACK_AC2E) != NULL;
    if (off) return;
    ShaderInfo *infos[SF2C_EXEC_N_2_D473] = {pr->vi, pr->fi};
    for (int sg = 0; sg < SF2C_EXEC_N_2_D473; sg++)
        for (int i = 0; i < infos[sg]->ntex; i++) {
            NvnTexture *t = shader_texture_object(sg, &infos[sg]->tex[i]);
            if (!is_attachment(t)) continue;
            bool have = false;
            for (int k = 0; k < nfeedback; k++) have |= feedback_tex[k] == t;
            if (have || nfeedback == SF2C_EXEC_N_4_4B22) continue;
            static int nosnap = -1;
            if (nosnap < 0) nosnap = getenv(SF2C_EXEC_S_LARECOMP_NO_SNAPSHOT_REUSE_E42A) != NULL;
            int si = -1;
            for (int k = 0; k < SF2C_EXEC_N_8_2C62; k++) if (snap_tex[k] == t) si = k;
            bool self_write = t == st.rt.depth && st.ds[0] && st.ds[1];
            TexBackend *src = vk_tex(t, true);
            Snap *existing = NULL;
            for (int k = 0; k < nsnaps; k++) if (snaps[k].key == t && snaps[k].copy.image) existing = &snaps[k];
            VkImageAspectFlags sa = src->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : src->aspect;
            static const VkComponentMapping id = {0};
            if (!nosnap && si >= 0 && (snap_ok[si] || self_write) && existing) {
                feedback_tex[nfeedback] = t;
                feedback_view[nfeedback++] = vk_image_view(&existing->copy, VK_IMAGE_VIEW_TYPE_2D, src->format, 0, 1, 0, 1, id, sa);
                continue;
            }
            stat_feedback_copies++;
            if (ct_on()) LOG(SF2C_EXEC_S_CT_SNAPSHOT_P_UX_U_WAS_S_SI__1660, (void *)t, src->width, src->height, si >= 0 ? snap_why[si] : SF2C_EXEC_S_NEW_8027, si);
            vk_end_pass();
            TexBackend *dst = snapshot_image(t, src);
            VkCommandBuffer cb = vk_cmd();
            vk_full_barrier(cb);
            gp_mark(cb, SF2C_EXEC_S_FEEDBACK_SNAPSHOT_UX_U_FMT_D_E0D2, src->width, src->height, src->format);
            VkImageCopy rg = {.srcSubresource = {src->aspect, 0, 0, 1}, .dstSubresource = {src->aspect, 0, 0, 1},
                              .extent = {src->width, src->height, 1}};
            rec_CmdCopyImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
            if (g_debug_frame && (src->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) {
                static int nd;
                char path[SF2C_EXEC_N_128_2747];
                snprintf(path, sizeof path, SF2C_EXEC_S_BUILD_FBDEPTH_03D_SRC_PGM_FE81, nd);
                debug_save_depth(src, path);
                snprintf(path, sizeof path, SF2C_EXEC_S_BUILD_FBDEPTH_03D_SNAP_PGM_03E6, nd++);
                debug_save_depth(dst, path);
                cb = vk_cmd();
            }
            feedback_tex[nfeedback] = t;
            feedback_view[nfeedback++] = vk_image_view(dst, VK_IMAGE_VIEW_TYPE_2D, src->format, 0, 1, 0, 1, id, sa);
            if (si < 0) { static int rr; si = rr++ & SF2C_EXEC_N_7_7902; snap_tex[si] = t; }
            snap_ok[si] = true;
            st.rt_dirty = true;
        }
}

// ================================================================== resource binding
static VkDescriptorBufferInfo ubo_span(VkSpan s, uint64_t size) {
    uint64_t max = g_vk.props.limits.maxUniformBufferRange;
    return (VkDescriptorBufferInfo){s.buf, s.off, size > max ? max : size};
}

// Lean constant blocks: the scale table (66 rows, 1 KB) and the driver constants (up to 2 KB) were zero-filled in
// full and compared in full per stage and draw, although only a few rows / entries carry anything. These build and
// compare only those (the scale table's rows 0..ntex and 65; the driver constants' storage buffer entries below the
// size the shader reads) and write the same bytes as before. LARECOMP_VK_NO_CBUF_LEAN=1: the full blocks, as before.
static int cbuf_lean_v = -1;
static bool cbuf_lean(void) {
    if (cbuf_lean_v < 0) cbuf_lean_v = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_CBUF_LEAN_6E8F) == NULL;
    return cbuf_lean_v;
}
// The scale table of a graphics stage: rows 0..nt are set, the rest are zero (not read from rs).
static VkSpan rscale_dedupe(int sg, const float rs[SF2C_EXEC_N_65_108C][SF2C_EXEC_N_4_4B22], int nt) {
    static struct { uint64_t seq; int nt; float r[SF2C_EXEC_N_65_108C][SF2C_EXEC_N_4_4B22]; VkSpan span; } last[SF2C_EXEC_N_2_D473];
    uint64_t seq = vk_pending_seq();
    size_t used = (size_t)(nt + 1) * SF2C_EXEC_N_16_B17E;
    if (last[sg].seq == seq && last[sg].nt == nt && !memcmp(last[sg].r, rs, used)) {
        prof_cb_reused++;
        return last[sg].span;
    }
    VkSpan s = vk_transient(SF2C_EXEC_N_65_108C * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_256_51E8);
    memcpy(s.ptr, rs, used);
    if (nt < SF2C_EXEC_N_64_A68B) memset((uint8_t *)s.ptr + used, 0, (size_t)(SF2C_EXEC_N_64_A68B - nt) * SF2C_EXEC_N_16_B17E);
    last[sg].seq = seq; last[sg].nt = nt; memcpy(last[sg].r, rs, used); last[sg].span = s;
    prof_cb_copied++;
    return s;
}
// ---- storage buffers the game rewrites one frame ahead (overlap; renderer.m snap_storage)
// The game's tiled light / exposure lists: each frame its CPU memcpys the list for the frame after this one into the
// buffer the frame being drawn reads (7281e33500+32640 and 7281e3bd00+2040, alternating by frame), which the Switch's
// fence made safe. With the fence deferred that copy lands while the GPU still reads the list: screen-aligned tiles
// without shadow, a dark saturated picture when the exposure program (72a8da6000) reads it - worse the slower the GPU.
// Other storage data (the skinning program 72a8da0000's outputs, read in place by the exposure program and by draws
// binding them as vertex data from an offset into the pool: BowWow's body) is only ever written by the GPU and must
// stay where the game put it.
// Which is which is decided per memory range, with no wait and no probe of the programs (renderer.m 660bead). A range
// a draw reads as storage is shader-consumed game data (the lists): COPY for good - every binder of it, the compute
// dispatches that read or modify it too, works on a copy taken at translation, refreshed whenever its data changes
// (hash) and otherwise kept, so the GPU's own modifications stay. The CPU's rewrite can't be told from the data:
// standing still it rewrites the same bytes, and the boxes come from the GPU reading the list while it is rewritten
// (an M6 at 4x in the woods), so no hash decides this case - and on the title screen a probe would have settled the
// lists in place for good. A range only compute slots bind gets a one-frame PROBE on its first use in a level (all
// binders on the copy, the GPU can't touch the original) and its next use looks at the original: changed, the CPU
// rewrites it - COPY; unchanged, a GPU output or static (the skinning outputs, which draws then bind as vertex data
// from an offset into the pool) - IN PLACE for good, probed again when the level changes or after 10 s unused. With
// the write watch below (the default where the heap import allows it) a compute-only range starts in place and is
// copied from the first CPU store into it instead - the probe is the fallback; a copied range is never demoted. Each
// range's decision is logged once ("[snap] ...", the small per-object ranges capped). ~100 KB copied and hashed per
// frame in the field.
// Where the copies live: shaders address storage by guest address through the heap import (one buffer, lazy chunks,
// gfast's copy of the bound ranges), which covers only the heap the game set up (gmem_heap_size; Metal has the whole
// range in one buffer and uses its top). So the ring sits right after the game's heap and gmem_heap_size includes it:
// the one-buffer import takes it in, lazy mode imports its chunk when a copy is first bound (gmem_chunk_bind touches
// the effective addresses), gfast copies from it. Windows commits it (a userptr import needs committed pages).
// The ring is two halves: copies fill one; when it is full the submission pending then is noted for it and the other
// half is taken, after waiting for the submission noted when that half was left - so its freed slots are idle. Live
// copies stay where they are across switches (the allocator walks past them; rs_copy_make), so the GPU's own writes
// into a copy survive; a copy unused for 10 s is released. At ~100 KB a frame that wait is long satisfied; a half
// filling within one submission (not a real case) binds the original for that draw.
// LARECOMP_OVERLAP_SNAP=0: off (everything in place, as before: shows the race).
#define SNAP_HALF (SF2C_EXEC_N_8ULL_682F << SF2C_EXEC_N_20_F5CA)
#define SNAP_RING (SF2C_EXEC_N_2_D473 * SNAP_HALF)
enum { RS_PROBE, RS_COPY, RS_PLACE };
typedef struct { uint64_t addr, size, copy, pristine, hash, frame; uint32_t gen, wgen; uint8_t mode; bool cs_bound, draw_bound; } RangeState;  // (wgen: watch_check; pristine: rs_copy_make)
static RangeState rs_tab[SF2C_EXEC_N_512_94F8];
static uint64_t rs_store_frame[SF2C_EXEC_N_512_94F8];  // (watch: the frame of the previous CPU write seen on a range's own pages)
static int nrs;
static int snap_state = -1;        // -1 not decided (the heap not set up yet), 0 off, 1 on
static uint64_t snap_base;         // heap offset of the ring (on)
static uint64_t snap_off;          // used bytes of the current half
static int snap_half;              // the half being filled
static uint64_t snap_half_seq[SF2C_EXEC_N_2_D473];  // the submission pending when each half was last left (its copies may be in use until then)
static uint32_t snap_gen = 1;      // (bumped at a half switch: older copies are never bound again)
static Range sb_eff[SF2C_EXEC_N_6_E7F6][SF2C_EXEC_N_16_B17E];        // the current draw's / dispatch's storage table with the copies' addresses (snap_apply)
static bool sb_eff_on;
static inline const Range *sbufs(int stage) { return sb_eff_on ? sb_eff[stage] : st.sbuf[stage]; }

static uint64_t range_hash(uint64_t addr, uint64_t size) {
    const uint64_t *w = (const uint64_t *)(uintptr_t)addr;
    uint64_t h = SF2C_EXEC_N_1469598103934665603ULL_7B90, n = size / SF2C_EXEC_N_8_2C62;
    for (uint64_t i = 0; i < n; i++) h = (h ^ w[i]) * SF2C_EXEC_N_1099511628211ULL_D3BD;
    return h;
}
static void snap_ring_import_tail(void);  // (with the lazy chunks below)
// Decided once the game's heap is set up (SetHeapSize), which is before anything is drawn: the ring follows the heap.
static bool snap_on(void) {
    if (snap_state >= 0) return snap_state;
    uint64_t heap = kernel_heap_size();
    if (!heap) return false;  // (not decided yet)
    const char *e = getenv(SF2C_EXEC_S_LARECOMP_OVERLAP_SNAP_4B30);
    snap_state = (!e || atoi(e) != 0) && overlap_on();
    if (snap_state) {
        snap_base = (heap + SF2C_EXEC_N_0XFFFF_F51F) & ~SF2C_EXEC_N_0XFFFFULL_2BD3;  // (minImportedHostPointerAlignment: 64 KB at most)
        if (snap_base + SNAP_RING > HEAP_MAX) {
            snap_state = 0;
            LOG(SF2C_EXEC_S_VK_STORAGE_SNAPSHOTS_OFF_THE_7386);
        } else {
            plat_commit(HEAP_BASE + snap_base, SNAP_RING);  // (Windows: backed before the import pins it)
            LOG(SF2C_EXEC_S_VK_STORAGE_BUFFERS_THE_CPU_R_FDBF,
                (unsigned long long)(HEAP_BASE + snap_base), (unsigned long long)(SNAP_RING >> SF2C_EXEC_N_20_F5CA));
            snap_ring_import_tail();  // (lazy chunks: one imported short of the ring already is imported again over it)
        }
    }
    return snap_state;
}
static uint64_t snap_ring_end(void) { return snap_on() ? snap_base + SNAP_RING : 0; }  // (heap offset; 0: no ring)
static void snap_reset(void) {  // (vk_exec_reset: a reboot - the heap is set up again, its pages replaced)
    snap_state = -1;
    snap_base = snap_off = 0;
    snap_half = 0;
    snap_half_seq[0] = snap_half_seq[1] = 0;
    snap_gen++;
    nrs = 0;
    sb_eff_on = false;
}
static void rs_log(RangeState *e, const char *what, bool cs_slot, uint64_t f) {  // (rare: a new range, a decision)
    static int n;
    if (e->size >= SF2C_EXEC_N_2040_DF34 ? n < SF2C_EXEC_N_100000_3BB7 : n++ < SF2C_EXEC_N_200_27BA)  // (the small per-object ranges are many: capped)
        LOG(SF2C_EXEC_S_SNAP_LLX_LLU_S_S_BIND_FRAME__A29B, (unsigned long long)e->addr, (unsigned long long)e->size, what, cs_slot ? SF2C_EXEC_S_COMPUTE_9B08 : SF2C_EXEC_S_DRAW_D939, (unsigned long long)f);
}
// A copy lives until its entry remakes it (the original changed) or releases it (unused for 10 s): a half switch no
// longer remakes every copy from its original, which lost the GPU's own writes into a copy - the exposure program's
// into the lists - for a frame (renderer.m 1ea4b16). The allocator moves forward through the current half and walks
// past live copies; a slot freed by a remake (the GPU may still read it) is reused only after the half is left and
// taken again, which waits for the submission pending when it was left - the same guarantee as before.
// The offset in the current half where `need` bytes fit from `at` on, skipping live copies (with their pristine
// images); ~0 when none
static uint64_t rs_ring_fit(uint64_t at, uint64_t need) {
    uint64_t half_lo = HEAP_BASE + snap_base + (uint64_t)snap_half * SNAP_HALF;
    for (bool moved = true; moved && at + need <= SNAP_HALF;) {
        moved = false;
        for (int i = 0; i < nrs; i++) {
            const RangeState *o = &rs_tab[i];
            if (!o->copy || o->copy < half_lo || o->copy >= half_lo + SNAP_HALF) continue;
            uint64_t olo = o->copy - half_lo, ohi = (olo + o->size * (o->pristine ? SF2C_EXEC_N_2_D473 : 1) + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76;
            if (at < ohi && at + need > olo) { at = ohi; moved = true; }
        }
    }
    return at + need <= SNAP_HALF ? at : ~SF2C_EXEC_N_0ULL_DF1B;
}
// `need` bytes (256-aligned) of the ring past the live copies: their guest address, or 0 when none can be taken now
// (see the half switch above). A copy being remade is skipped too: the merge in rs_copy_make reads it.
static uint64_t ring_take(uint64_t need) {
    uint64_t at = rs_ring_fit((snap_off + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76, need);
    if (at == ~SF2C_EXEC_N_0ULL_DF1B) {  // this half is full: the other one, once the GPU is done with what it held
        int other = snap_half ^ 1;
        uint64_t seq = snap_half_seq[other], done = vk_completed_seq();
        if (seq > done) {
            if (seq > renderer_issued_seq()) return 0;  // (still being recorded: can't be waited for - in place this once)
            wait_why = SF2C_EXEC_S_STORAGE_SNAPSHOT_RING_F600;
            vk_wait_seq(seq);
        }
        snap_half_seq[snap_half] = vk_pending_seq();
        snap_half = other;
        snap_off = 0;
        at = rs_ring_fit(0, need);
        if (at == ~SF2C_EXEC_N_0ULL_DF1B) {  // (the half is all live copies: not in practice - 512 entries, most far smaller)
            static int warned;
            if (warned++ < SF2C_EXEC_N_3_4E07) LOG(SF2C_EXEC_S_SNAP_COPY_RING_FULL_LLU_BYTE_4F0D, (unsigned long long)need);
            return 0;
        }
    }
    snap_off = at + need;
    return HEAP_BASE + snap_base + (uint64_t)snap_half * SNAP_HALF + at;
}
// A copy a compute dispatch binds carries the GPU's own writes into it (the exposure program 72a8da6000 keeps its
// adaptation state in the 2040-byte exposure list it reads and writes; the lists' tile data beside it is the CPU's).
// When the CPU changes the original the copy is remade from it - and the GPU's state went back to whatever the
// original held when the copy was first taken: the screen at half brightness for one frame (the path by the Face
// Shrine; two frames three apart, one per list buffer; renderer.m 69a3524). So such a copy keeps a pristine image of
// what it was copied from right behind it, and a remake merges: a byte the GPU changed (copy != pristine) that the
// CPU didn't (original == pristine) keeps the GPU's value; everything else is the original's, the CPU's write winning
// where both changed (its write is the later one, as in place). Draw-only copies have no GPU state and no image.
// A copy of e's range in the ring: its guest address, or 0 when none can be made now
static uint64_t rs_copy_make(RangeState *e, uint64_t h) {
    bool keep = e->cs_bound;
    uint64_t old = e->copy, old_p = e->pristine;
    uint64_t at = ring_take((e->size * (keep ? SF2C_EXEC_N_2_D473 : 1) + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76);
    if (!at) return 0;
    uint8_t *dst = (uint8_t *)(uintptr_t)at;
    const uint8_t *src = (const uint8_t *)(uintptr_t)e->addr;
    memcpy(dst, src, e->size);
    if (keep && old && old_p) {
        const uint8_t *oc = (const uint8_t *)(uintptr_t)old, *op = (const uint8_t *)(uintptr_t)old_p;
        uint64_t kept = 0;
        for (uint64_t i = 0; i < e->size; i++) if (oc[i] != op[i] && src[i] == op[i]) { dst[i] = oc[i]; kept++; }
        if (kept) {
            static int n;
            extern uint64_t renderer_exec_frame(void);
            if (n++ < SF2C_EXEC_N_20_F5CA || getenv(SF2C_EXEC_S_LARECOMP_LOG_BINDS_8A9A))
                LOG(SF2C_EXEC_S_SNAP_LLX_LLU_REMADE_LLU_BYTE_DB50, (unsigned long long)e->addr,
                    (unsigned long long)e->size, (unsigned long long)kept, (unsigned long long)renderer_exec_frame());
        }
    }
    e->copy = at;
    if (keep) { e->pristine = at + e->size; memcpy((void *)(uintptr_t)e->pristine, src, e->size); }
    else e->pristine = 0;
    e->hash = h, e->gen = snap_gen;
    stat_snap_bytes += e->size;
    stat_snap_copies++;
    return e->copy;
}
static void rs_release_unused(uint64_t f) {  // (every 600 frames) copies of entries unbound for 10 s go back to the ring
    static uint64_t last;
    if (f - last < SF2C_EXEC_N_600_284B) return;
    last = f;
    for (int i = 0; i < nrs; i++) if (rs_tab[i].copy && f - rs_tab[i].frame > SF2C_EXEC_N_600_284B) rs_tab[i].copy = rs_tab[i].pristine = 0;
}
// Write watch (compute-only ranges; renderer.m cf98fff / c3580a8). The CPU's rewrite of a range can't be seen in its
// bytes (the light lists are rewritten with the same bytes while the view stands still, and the race is the GPU reading
// them mid-rewrite), so a range bound only by compute slots starts in place and the OS's page write tracking tells,
// at its first bind of each frame, whether the CPU wrote any of its pages since the last look: then it is copied from
// then on. The tracking is the vertex cache's and the texture upload checks' (plat_written_pages, the shared page
// generations vc_page_gen): Windows GetWriteWatch over the guest window reserved with MEM_WRITE_WATCH, Linux the
// userfaultfd asynchronous write-protect bits read with PAGEMAP_SCAN. Neither changes a page's protection, and
// neither sees the GPU's own writes (DMA sets no dirty bit): a GPU output stays in place. The Metal backend
// write-protects the pages and catches the store in the fault handler instead; on Windows that (VirtualProtect on
// pages imported with VK_EXT_external_memory_host, NVIDIA) left the GPU reading wrong data - the picture at half
// brightness, grass and HUD digits missing - so the Vulkan backend never changes the protection of imported memory.
// Where the OS gives no tracking (Linux over a userptr or dma-buf heap: the tracking is off there, a kernel without
// PAGEMAP_SCAN, a Windows window reserved without write watch) the one-frame hash probe is the fallback, logged.
// Page granularity (4 KB; 16 KB and 64 KB kernels on arm64 Linux as the kernel reports): a store into a neighbour on
// the same page marks the range too (the lists share a page) - copied, the safe side - except a page shared with a
// small parameter block (watch_check), and a single write (an initialisation) doesn't copy (snap_storage).
// LARECOMP_OVERLAP_WATCH=0: off (compute-only ranges get the one-frame hash probe, as before).
static bool heap_is_userptr(void);  // (with the heap import below)
static int pages_cpu_written(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now);  // (the page tables below)
#define WATCH_PAGE SF2C_EXEC_N_4096ULL_F12B  // (the page tables' page, VC_PAGE below)
static bool watch_on(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_OVERLAP_WATCH_FEAE);
        uint32_t g;
        on = !e || atoi(e) != 0;
        if (on && pages_cpu_written(HEAP_BASE, SF2C_EXEC_N_4096_8B92, 0, &g) < 0) {
            on = 0;
            LOG(SF2C_EXEC_S_VK_STORAGE_WRITE_WATCH_OFF_N_7718, heap_is_userptr() ? SF2C_EXEC_S_OVER_A_USERPTR_HEAP_IMPORT_43F2 : SF2C_EXEC_S_EMPTY_12AE);
        } else LOG(SF2C_EXEC_S_VK_STORAGE_WRITE_WATCH_S_COM_0106,
                   on ? SF2C_EXEC_S_ON_990C : SF2C_EXEC_S_OFF_LARECOMP_OVERLAP_WATCH_0_557F);
    }
    return on;
}
// Did the CPU write any page of the range since the range last looked (e->wgen)? The look stamps the pages (every
// look resets the OS bits, so the generation table carries what other lookers saw). 1 written, 0 not, -1 no tracking.
// A page dirty bit can't tell which range on the page was stored to. A store into a small parameter block (the
// skinning dispatch's 12-byte slot 5, rewritten every frame) says nothing about the buffers beside it: counted as
// theirs, it copied BowWow's skinning output - a GPU output the CPU reads back every frame (his chain) - so the
// dispatch wrote the copy, the CPU read the frozen original, and his body came apart as a companion indoors (Metal
// 1ea4b16). So a range of 256 bytes or more counts only its dirty pages that no watched range under 256 bytes shares;
// the light / exposure lists (32640 / 2040 bytes, one allocation the CPU memcpys) share pages only with each other
// and keep their coverage. A small range counts every dirty page of its own.
static int watch_check(RangeState *e) {
    uint32_t since = e->wgen, now = e->wgen;
    int w = pages_cpu_written(e->addr, e->size, since, &now);
    e->wgen = now;
    if (w != 1 || e->size < SF2C_EXEC_N_256_51E8) return w;
    for (uint64_t plo = e->addr & ~(WATCH_PAGE - 1); plo < e->addr + e->size; plo += WATCH_PAGE) {
        bool small_mate = false;
        for (int k = 0; k < nrs && !small_mate; k++) {
            const RangeState *o = &rs_tab[k];
            if (o != e && o->size < SF2C_EXEC_N_256_51E8 && o->cs_bound && o->addr < plo + WATCH_PAGE && plo < o->addr + o->size) small_mate = true;
        }
        if (small_mate) continue;
        uint32_t pg_now;  // (this page written since the range's last look? The look above stamped it: no OS bits left)
        if (pages_cpu_written(plo, WATCH_PAGE, since, &pg_now) == 1) return 1;  // (a dirty page of its own)
    }
    return 0;  // (only pages shared with small parameter blocks were written: theirs)
}

// The address a binder of [addr, addr+size) uses: the original or its copy (cs_slot: bound by a compute dispatch)
static uint64_t snap_storage(uint64_t addr, uint64_t size, bool cs_slot) {
    if (!snap_on() || !addr || !size || addr < HEAP_BASE || addr - HEAP_BASE + size > snap_base) return addr;
    if (size > SNAP_HALF / SF2C_EXEC_N_2_D473) {  // (the tile lists grow with the render resolution: ~1.2 MB at 7680x4320; a half holds two of these)
        static int logged;
        if (logged++ < SF2C_EXEC_N_4_4B22) LOG(SF2C_EXEC_S_VK_STORAGE_RANGE_LLX_LLU_TOO_B84F, (unsigned long long)addr, (unsigned long long)size);
        return addr;
    }
    if (kernel_heap_size() > snap_base) {  // (the game grew its heap into the ring: off from now on)
        static bool logged;
        if (!logged) { logged = true; LOG(SF2C_EXEC_S_VK_STORAGE_SNAPSHOTS_OFF_THE_F76A); }
        snap_state = 0;
        return addr;
    }
    extern int nvn_pool_flags_at(uint64_t addr);
    int pf = nvn_pool_flags_at(addr);
    if (pf < 0 || (pf & 1)) return addr;  // (CPU_NO_ACCESS: static data, nothing to race with)
    extern uint64_t renderer_exec_frame(void);
    uint64_t f = renderer_exec_frame();
    rs_release_unused(f);
    // A draw's storage read from the game's per-frame pools (CPU_UNCACHED | GPU_UNCACHED, 0x12: the 64-byte per-object
    // blocks at changing addresses, hundreds a frame) is copied without an entry: a fresh range every frame filled the
    // table in a few minutes, and the table starting over remade every copy from its original - the exposure list's
    // GPU state with it (one dark frame per list buffer, see rs_copy_make). Its slot is free again at once: reused only
    // after the half is left and taken again (the wait in ring_take), as a remade copy's old slot.
    if (!cs_slot && (pf & SF2C_EXEC_N_0X13_3ABB) == SF2C_EXEC_N_0X12_9AE3 && size <= SF2C_EXEC_N_65536_0F25) {
        uint64_t at = ring_take((size + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76);
        if (!at) return addr;
        memcpy((void *)(uintptr_t)at, (const void *)(uintptr_t)addr, size);
        stat_snap_bytes += size;
        stat_snap_copies++;
        return at;
    }
    RangeState *e = NULL;
    for (int i = 0; i < nrs; i++) if (rs_tab[i].addr == addr && rs_tab[i].size == size) { e = &rs_tab[i]; break; }
    uint64_t c;
    if (!e) {
        int k = nrs;
        if (nrs == SF2C_EXEC_N_512_94F8) {  // table full: the entry bound longest ago makes room (never the whole table at once)
            k = 0;
            for (int i = 1; i < nrs; i++) if (rs_tab[i].frame < rs_tab[k].frame) k = i;
            rs_log(&rs_tab[k], SF2C_EXEC_S_EVICTED_TABLE_FULL_BOUND_LON_C9DD, cs_slot, f);
        } else nrs++;
        e = &rs_tab[k];
        memset(e, 0, sizeof *e);
        rs_store_frame[e - rs_tab] = 0;
        e->addr = addr, e->size = size, e->mode = RS_PROBE, e->frame = f;
        e->cs_bound = cs_slot;
        if (!cs_slot) e->mode = RS_COPY, e->draw_bound = true;  // a draw's storage read: copied for good (see above)
        else if (watch_on()) {  // a compute-only range: in place; the first CPU write its pages' tracking shows makes it a copy
            e->mode = RS_PLACE;
            watch_check(e);  // (from now: the look stamps its pages)
            rs_log(e, SF2C_EXEC_S_NEW_IN_PLACE_CPU_WRITES_TRAC_FDEA, cs_slot, f);
            return addr;
        }
        // the probe frame (or the first copy): every binder this frame takes this copy (frame == f below); a probe's
        // decision is at the next frame's use, when a CPU rewrite can have happened
        c = rs_copy_make(e, range_hash(addr, size));
        if (!c) {  // (no copy possible now: the entry is dropped - not probed yet - and the range is created again next time)
            rs_log(e, SF2C_EXEC_S_NO_COPY_NOW_THE_RING_S_OTHER_B881, cs_slot, f);
            if (k != --nrs) { rs_tab[k] = rs_tab[nrs]; rs_store_frame[k] = rs_store_frame[nrs]; }  // (an evicted slot: the last entry moves in)
            return addr;
        }
        rs_log(e, cs_slot ? SF2C_EXEC_S_NEW_PROBE_FA53 : SF2C_EXEC_S_NEW_COPY_A_DRAW_S_STORAGE_RE_E48C, cs_slot, f);
        return c;
    }
    if (cs_slot) e->cs_bound = true; else e->draw_bound = true;
    if (e->frame == f) {  // (bound again this frame)
        if (e->mode == RS_PLACE && cs_slot) return addr;
        if (!cs_slot && e->mode != RS_COPY) {  // (a draw's storage read of a range probed / settled in place this frame: copied for good)
            e->mode = RS_COPY;
            rs_log(e, SF2C_EXEC_S_COPY_A_DRAW_S_STORAGE_READ_97C2, cs_slot, f);
            c = rs_copy_make(e, range_hash(addr, size));
            return c ? c : addr;
        }
        if (e->copy) return e->copy;
        c = rs_copy_make(e, e->hash);
        return c ? c : addr;
    }
    // Probe again after 10 s unused, only for the hash fallback's in-place ranges: a copied range stays
    // copied (a draw's storage read, a CPU store seen), a watched one just gets its pages armed again
    bool again = !watch_on() && e->mode == RS_PLACE && (f - e->frame > SF2C_EXEC_N_600_284B);
    e->frame = f;
    if (!cs_slot && e->mode != RS_COPY) {  // a draw reads it as storage: copied for good (see above)
        e->mode = RS_COPY;
        rs_log(e, SF2C_EXEC_S_COPY_A_DRAW_S_STORAGE_READ_97C2, cs_slot, f);
        c = rs_copy_make(e, range_hash(addr, size));
        return c ? c : addr;
    }
    if (e->mode == RS_PLACE && e->cs_bound && watch_on()) {  // (tracked: the OS's dirty bits decide, no hash)
        if (watch_check(e) != 1) return addr;
        // A write once (an initialisation: BowWow's skinning buffers at his spawn) stays in place; writes that keep
        // coming (within 8 frames of the previous one) are the CPU rewriting it: copied from now on (renderer.m 1ea4b16)
        int k = (int)(e - rs_tab);
        if (!rs_store_frame[k] || f - rs_store_frame[k] > SF2C_EXEC_N_8_2C62) { rs_store_frame[k] = f; return addr; }
        e->mode = RS_COPY;
        rs_log(e, SF2C_EXEC_S_COPY_CPU_WRITES_KEEP_COMING__1589, cs_slot, f);
        c = rs_copy_make(e, range_hash(addr, size));
        return c ? c : addr;
    }
    if (e->mode == RS_PLACE && !again && e->cs_bound) return addr;
    uint64_t h = range_hash(addr, size);
    if (again && !e->draw_bound) {  // (a draw-read range stays a copy across levels)
        e->mode = RS_PROBE;
        rs_log(e, SF2C_EXEC_S_PROBE_AGAIN_LEVEL_UNUSED_ADA9, cs_slot, f);
        c = rs_copy_make(e, h);
        return c ? c : addr;
    }
    switch (e->mode) {
    case RS_PLACE:  // (never a compute slot's: a change can only be the CPU's)
        if (h == e->hash) return addr;
        e->mode = RS_COPY;
        rs_log(e, SF2C_EXEC_S_IN_PLACE_COPY_CHANGED_LATER_599A, cs_slot, f);
        break;
    case RS_PROBE:  // the use after the probe: did the CPU touch the original meanwhile?
        if (h == e->hash) { e->mode = RS_PLACE; e->copy = e->pristine = 0; rs_log(e, SF2C_EXEC_S_PROBE_IN_PLACE_UNCHANGED_DB17, cs_slot, f); return addr; }
        e->mode = RS_COPY;
        rs_log(e, SF2C_EXEC_S_PROBE_COPY_THE_CPU_REWROTE_I_45C4, cs_slot, f);
        break;
    default:  // RS_COPY
        if (h == e->hash && e->copy) return e->copy;  // unchanged: the copy (maybe GPU-written: its state) is current
        break;
    }
    c = rs_copy_make(e, h);
    return c ? c : addr;
}

// (debug) LARECOMP_LOG_BINDS=1: guest memory the GPU reads in place, per frame (bytes bound; distinct ranges once),
// and the ranges of the previous frame whose contents the game rewrote by the time this frame is translated: with
// the frame fence deferred (overlap) the GPU may still read them - the racing data. kind: 0 vertex, 1 index,
// 2 storage (a draw's read), 3 compute slot.
static void bind_note(int kind, uint64_t addr, uint64_t size) {
    static int on = -1;
    if (on < 0) on = getenv(SF2C_EXEC_S_LARECOMP_LOG_BINDS_8A9A) != NULL;
    if (!on || addr < HEAP_BASE || addr - HEAP_BASE + size > HEAP_MAX || size < SF2C_EXEC_N_8_2C62) return;
    extern int nvn_pool_flags_at(uint64_t addr);
    enum { REC = SF2C_EXEC_N_8192_864A };
    static struct { uint64_t addr, size, hash; uint8_t kind; } rec[REC];
    static int nrec;
    static uint64_t frame, all[SF2C_EXEC_N_4_4B22], uniq[SF2C_EXEC_N_4_4B22], wr[SF2C_EXEC_N_4_4B22];
    static uint32_t n[SF2C_EXEC_N_4_4B22];
    extern uint64_t renderer_exec_frame(void);
    uint64_t f = renderer_exec_frame();
    if (f != frame) {
        // the previous frame's ranges, rewritten since?
        uint64_t chg[SF2C_EXEC_N_4_4B22] = {0}, chgn[SF2C_EXEC_N_4_4B22] = {0};
        int shown = 0;
        for (int i = 0; i < nrec; i++) {
            if (range_hash(rec[i].addr, rec[i].size) == rec[i].hash) continue;
            chg[rec[i].kind] += rec[i].size; chgn[rec[i].kind]++;
            if (frame % SF2C_EXEC_N_120_2ABA == 0 && shown++ < SF2C_EXEC_N_12_6B51)
                LOG(SF2C_EXEC_S_RACE_FRAME_LLU_S_LLX_LLU_POO_F38D, (unsigned long long)frame,
                    rec[i].kind == 0 ? SF2C_EXEC_S_VERTEX_C188 : rec[i].kind == 1 ? SF2C_EXEC_S_INDEX_8A76 : rec[i].kind == SF2C_EXEC_N_2_D473 ? SF2C_EXEC_S_STORAGE_6545 : SF2C_EXEC_S_COMPUTE_9B08,
                    (unsigned long long)rec[i].addr, (unsigned long long)rec[i].size, nvn_pool_flags_at(rec[i].addr));
        }
        if (frame % SF2C_EXEC_N_120_2ABA == 0 || chgn[0] + chgn[1] + chgn[SF2C_EXEC_N_2_D473] + chgn[SF2C_EXEC_N_3_4E07])
            LOG(SF2C_EXEC_S_BINDS_FRAME_LLU_VERTEX_LLU_K_5137,
                (unsigned long long)frame, (unsigned long long)(all[0] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(uniq[0] >> SF2C_EXEC_N_10_4A44), n[0], (unsigned long long)(all[1] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(uniq[1] >> SF2C_EXEC_N_10_4A44),
                (unsigned long long)(all[SF2C_EXEC_N_2_D473] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(uniq[SF2C_EXEC_N_2_D473] >> SF2C_EXEC_N_10_4A44), n[SF2C_EXEC_N_2_D473], (unsigned long long)(all[SF2C_EXEC_N_3_4E07] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(uniq[SF2C_EXEC_N_3_4E07] >> SF2C_EXEC_N_10_4A44),
                (unsigned long long)(wr[0] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(wr[1] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(wr[SF2C_EXEC_N_2_D473] >> SF2C_EXEC_N_10_4A44), (unsigned long long)(wr[SF2C_EXEC_N_3_4E07] >> SF2C_EXEC_N_10_4A44),
                (unsigned long long)(chg[0] >> SF2C_EXEC_N_10_4A44), (unsigned long long)chgn[0], (unsigned long long)(chg[1] >> SF2C_EXEC_N_10_4A44), (unsigned long long)chgn[1],
                (unsigned long long)(chg[SF2C_EXEC_N_2_D473] >> SF2C_EXEC_N_10_4A44), (unsigned long long)chgn[SF2C_EXEC_N_2_D473], (unsigned long long)(chg[SF2C_EXEC_N_3_4E07] >> SF2C_EXEC_N_10_4A44), (unsigned long long)chgn[SF2C_EXEC_N_3_4E07]);
        nrec = 0; memset(all, 0, sizeof all); memset(uniq, 0, sizeof uniq); memset(n, 0, sizeof n); memset(wr, 0, sizeof wr);
        frame = f;
    }
    all[kind] += size; n[kind]++;
    for (int i = 0; i < nrec; i++) if (rec[i].addr == addr && rec[i].size == size) return;
    if (nrec == REC) return;
    rec[nrec].addr = addr; rec[nrec].size = size; rec[nrec].kind = (uint8_t)kind;
    rec[nrec].hash = range_hash(addr, size);
    nrec++;
    uniq[kind] += size;
    int pf = nvn_pool_flags_at(addr);
    if (pf < 0 || !(pf & 1)) wr[kind] += size;  // (distinct bytes in pools the CPU can write: what a snapshot would copy)
}

// Before a draw (stages 0 and 1: the slots the shader reads addresses of, si->ssbo_read) or a dispatch (stage 5: every
// bound slot): the stage's effective storage table, with the copies' addresses where a range is snapshotted. Every
// reader of the table in the draw / dispatch (drv_dedupe, cbuf_info, gmem_direct_pick, gmem_chunk_bind, gfast_prepare,
// vc_mark_sbufs) takes sbufs(stage); st.sbuf itself keeps the game's bindings (rehashed next frame).
static void snap_apply(int stage, uint32_t read_mask) {
    bool on = snap_on();
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++)
        if (st.sbuf[stage][i].addr && ((read_mask >> i) & 1)) bind_note(stage == SF2C_EXEC_N_5_EF2D ? SF2C_EXEC_N_3_4E07 : SF2C_EXEC_N_2_D473, st.sbuf[stage][i].addr, st.sbuf[stage][i].size);
    if (!on) { sb_eff_on = false; return; }
    sb_eff_on = true;
    memcpy(sb_eff[stage], st.sbuf[stage], sizeof sb_eff[stage]);
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
        Range *r = &sb_eff[stage][i];
        if (r->addr && ((read_mask >> i) & 1)) r->addr = snap_storage(r->addr, r->size, stage == SF2C_EXEC_N_5_EF2D);
    }
}

// The driver constants of a graphics stage (cbuf 0, size bytes): zero but for the stage's storage buffer table at
// base (16 bytes an entry: address low, high, size). Compared by the entries within size.
static VkSpan drv_dedupe(int stage, uint64_t size) {
    static struct { uint64_t seq, size; int stage; Range sb[SF2C_EXEC_N_16_B17E]; VkSpan span; } last[SF2C_EXEC_N_2_D473];
    int slot = stage == 1;
    uint32_t base = SF2C_EXEC_N_0X110_8C83 + SF2C_EXEC_N_0X100_BE6E * (stage == 1 ? SF2C_EXEC_N_4_4B22 : 0);
    int ne = size > base ? (int)((size - base + SF2C_EXEC_N_15_E629) / SF2C_EXEC_N_16_B17E) : 0;
    if (ne > SF2C_EXEC_N_16_B17E) ne = SF2C_EXEC_N_16_B17E;
    uint64_t seq = vk_pending_seq();
    const Range *sb = sbufs(stage);  // (overlap: a snapshotted range's copy)
    if (last[slot].seq == seq && last[slot].size == size && last[slot].stage == stage &&
        !memcmp(last[slot].sb, sb, (size_t)ne * sizeof(Range))) {
        prof_cb_reused++;
        return last[slot].span;
    }
    uint32_t drv[SF2C_EXEC_N_0X800_D398 / SF2C_EXEC_N_4_4B22];
    memset(drv, 0, size);
    for (int i = 0; i < ne; i++) {  // (an entry may run past size: drv is 0x800 bytes, only size of them are copied)
        uint32_t *e = &drv[(base + SF2C_EXEC_N_16_B17E * i) / SF2C_EXEC_N_4_4B22];
        e[0] = (uint32_t)sb[i].addr;
        e[1] = (uint32_t)(sb[i].addr >> SF2C_EXEC_N_32_E29C);
        e[SF2C_EXEC_N_2_D473] = (uint32_t)sb[i].size;
    }
    VkSpan s = vk_transient_copy(drv, size, SF2C_EXEC_N_256_51E8);
    last[slot].seq = seq; last[slot].size = size; last[slot].stage = stage; last[slot].span = s;
    memcpy(last[slot].sb, sb, (size_t)ne * sizeof(Range));
    prof_cb_copied++;
    return s;
}

// Constant buffer k of a stage (renderer.m bind_cbufs), copied into transient memory.
static VkDescriptorBufferInfo cbuf_info(int stage, ShaderInfo *si, int k, uint32_t gx, uint32_t gy, uint32_t gz) {
    static const uint8_t zeros[SF2C_EXEC_N_0X10000_4939];
    if (k == 0) {
        if (stage != SF2C_EXEC_N_5_EF2D && cbuf_lean()) {  // (the size as below)
            uint64_t size = si->cbuf_size[0] > SF2C_EXEC_N_0X800_D398 ? SF2C_EXEC_N_0X800_D398 : (si->cbuf_size[0] ? si->cbuf_size[0] : SF2C_EXEC_N_256_51E8);
            return ubo_span(drv_dedupe(stage, size), size);
        }
        uint32_t drv[SF2C_EXEC_N_0X800_D398 / SF2C_EXEC_N_4_4B22] = {0};
        const Range *sb = sbufs(stage);  // (overlap: a snapshotted range's copy)
        if (stage == SF2C_EXEC_N_5_EF2D) {
            drv[SF2C_EXEC_N_0X200_3790 / SF2C_EXEC_N_4_4B22] = gx; drv[SF2C_EXEC_N_0X204_AE29 / SF2C_EXEC_N_4_4B22] = gy; drv[SF2C_EXEC_N_0X208_E3F7 / SF2C_EXEC_N_4_4B22] = gz;
            for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
                uint32_t *u = &drv[(SF2C_EXEC_N_0X210_7579 + SF2C_EXEC_N_16_B17E * i) / SF2C_EXEC_N_4_4B22];
                u[0] = (uint32_t)st.ubuf[SF2C_EXEC_N_5_EF2D][i].addr; u[1] = (uint32_t)(st.ubuf[SF2C_EXEC_N_5_EF2D][i].addr >> SF2C_EXEC_N_32_E29C); u[SF2C_EXEC_N_2_D473] = (uint32_t)st.ubuf[SF2C_EXEC_N_5_EF2D][i].size;
                uint32_t *e = &drv[(SF2C_EXEC_N_0X310_AC85 + SF2C_EXEC_N_16_B17E * i) / SF2C_EXEC_N_4_4B22];
                e[0] = (uint32_t)sb[i].addr; e[1] = (uint32_t)(sb[i].addr >> SF2C_EXEC_N_32_E29C); e[SF2C_EXEC_N_2_D473] = (uint32_t)sb[i].size;
            }
        } else {
            uint32_t base = SF2C_EXEC_N_0X110_8C83 + SF2C_EXEC_N_0X100_BE6E * (stage == 1 ? SF2C_EXEC_N_4_4B22 : 0);
            for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
                uint32_t *e = &drv[(base + SF2C_EXEC_N_16_B17E * i) / SF2C_EXEC_N_4_4B22];
                e[0] = (uint32_t)sb[i].addr;
                e[1] = (uint32_t)(sb[i].addr >> SF2C_EXEC_N_32_E29C);
                e[SF2C_EXEC_N_2_D473] = (uint32_t)sb[i].size;
            }
        }
        uint64_t size = si->cbuf_size[0] > sizeof drv ? sizeof drv : (si->cbuf_size[0] ? si->cbuf_size[0] : SF2C_EXEC_N_256_51E8);
        if (size < SF2C_EXEC_N_0X500_E9D0 && stage == SF2C_EXEC_N_5_EF2D) size = SF2C_EXEC_N_0X500_E9D0;
        return ubo_span(copy_dedupe(stage == SF2C_EXEC_N_5_EF2D ? DEDUPE_DRV_C : stage == 1 ? DEDUPE_DRV_F : DEDUPE_DRV_V, drv, size, SF2C_EXEC_N_256_51E8), size);
    }
    if (k == 1) {  // compiler constants stored after the shader code
        NvnShaderStage *ss = &st.program->stages[stage];
        uint64_t size = ss->const_size ? ss->const_size : SF2C_EXEC_N_16_B17E;
        if (size < si->cbuf_size[1]) size = si->cbuf_size[1];
        return ubo_span(guest_copy(ss->code_addr + ss->const_offset, size), size);  // (constant: shared per submission)
    }
    int idx = k - SF2C_EXEC_N_3_4E07;  // cbuf_base
    Range r = (idx >= 0 && idx < SF2C_EXEC_N_18_4EC9) ? st.ubuf[stage][idx] : (Range){0, 0};
    uint64_t size = si->cbuf_size[k] ? si->cbuf_size[k] : SF2C_EXEC_N_256_51E8;
    if (!r.addr) return ubo_span(vk_transient_copy(zeros, size, SF2C_EXEC_N_256_51E8), size);
    if ((si->cbuf_dynamic & (SF2C_EXEC_N_1U_B8E6 << k)) && r.size) size = r.size > SF2C_EXEC_N_0X10000_4939 ? SF2C_EXEC_N_0X10000_4939 : r.size;
    if (size > r.size && r.size) size = r.size;
    if (size < SF2C_EXEC_N_16_B17E) size = SF2C_EXEC_N_16_B17E;
    // snapshot: later CPU writes must not leak in. Shared by the draws of one submission binding the same range:
    // within a command list nothing writes guest memory (no inline UpdateUniformBuffer: that writes through below
    // and drops the shared copies).
    // (lazy heap chunks) pointers in it import their chunks: scanned once per shared copy (LARECOMP_VK_NO_SCAN_ONCE=1:
    // at every draw, as before)
    static int scan_once = -1;
    if (scan_once < 0) scan_once = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_SCAN_ONCE_88EA) == NULL;
    if (gc_lazy && si->uses_gmem && scan_once) return ubo_span(guest_copy_ex(r.addr, size, true), size);
    if (gc_lazy && si->uses_gmem) gc_scan_pointers((const uint32_t *)(uintptr_t)r.addr, size);
    return ubo_span(guest_copy(r.addr, size), size);
}

// Global memory (shader LDG/STG, maxwell_gmem_base = HEAP_BASE): the guest heap imported as a GPU buffer
// (VK_EXT_external_memory_host), zero-copy like metal.m's g_guest_buffer, so compute results land in guest
// memory. The import pins its pages, so it covers only the heap the game set up (SetHeapSize), made on first use.
// LARECOMP_VK_NO_GMEM=1: no import, reads return zero.
static VkBuffer gmem_buf;
static VkDeviceMemory gmem_mem;
static uint64_t gmem_size;

// Materialize every page of [host, host+size) with a write, before the driver pins the range: on the guest heap's
// lazy MAP_NORESERVE|MAP_PRIVATE window, the first CPU write into an untouched page takes a copy-on-write fault,
// and the kernel's HMM invalidation notifier ran from inside that fault on an AMD kernel driver (an oops in
// amdgpu_hmm_invalidate_gfx killed the running thread with no userspace trace; /var/log/kern.log said so). All
// touched pages are ordinary resident pages first, and the driver's read/write never crosses a COW.
static void pretouch_host(void *host, uint64_t size) {
    uintptr_t at = (uintptr_t)host, end = at + size;
    uintptr_t page = PLAT_PAGE;
#ifdef __linux__
    page = (uintptr_t)sysconf(_SC_PAGESIZE);
#elif defined(_WIN32)
    page = SF2C_EXEC_N_4096_8B92; // Windows x64 host pages; every imported page must be materialized.
#endif
    for (; at < end; at = (at | (page - 1)) + 1) {
        // This is LIVE game memory, including MethodInfo pointers and heap metadata.
        // A zero store corrupts the first byte of each page. An atomic RMW forces
        // COW materialization without losing a concurrent guest write or changing data.
        __atomic_fetch_or((volatile uint8_t *)at, (uint8_t)0, __ATOMIC_RELAXED);
    }
}

// Host memory [host, host + size) as a buffer (VK_EXT_external_memory_host; page-aligned). The allocation's result:
// out of memory tells whole_import_probe_oom to go lazy.
static VkResult host_import_ptr(void *host, uint64_t size, VkBuffer *out_buf, VkDeviceMemory *out_mem, bool bda) {
    if (g_vk.managed_heap) FATAL(SF2C_EXEC_S_HOST_POINTER_IMPORT_ATTEMPTE_884A);
    PFN_vkGetMemoryHostPointerPropertiesEXT props =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(g_vk.dev, SF2C_EXEC_S_VKGETMEMORYHOSTPOINTERPROPER_96C4);
    VkMemoryHostPointerPropertiesEXT mp = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (!props || props(g_vk.dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, host, &mp) != VK_SUCCESS || !mp.memoryTypeBits)
        return VK_ERROR_INITIALIZATION_FAILED;
    pretouch_host(host, size);  // (the lazy guest heap window: a pinned range must have no copy-on-write pages left)
    VkImportMemoryHostPointerInfoEXT imp = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, .pHostPointer = host};
    VkMemoryAllocateFlagsInfo fl = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = &imp, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = bda ? (void *)&fl : (void *)&imp, .allocationSize = size,
                               .memoryTypeIndex = (uint32_t)__builtin_ctz(mp.memoryTypeBits)};
    VkDeviceMemory mem;
    VkResult r = vkAllocateMemory(g_vk.dev, &ai, NULL, &mem);
    if (r != VK_SUCCESS) return r;
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &eb, .size = size,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                      (bda ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0)};
    VkBuffer buf;
    r = vkCreateBuffer(g_vk.dev, &bi, NULL, &buf);
    if (r != VK_SUCCESS) { vkFreeMemory(g_vk.dev, mem, NULL); return r; }
    r = vkBindBufferMemory(g_vk.dev, buf, mem, 0);
    if (r != VK_SUCCESS) { vkDestroyBuffer(g_vk.dev, buf, NULL); vkFreeMemory(g_vk.dev, mem, NULL); return r; }
    *out_buf = buf;
    *out_mem = mem;
    return VK_SUCCESS;
}

// Heap bytes [off, off + size) as a buffer (a page-aligned range of the mapped heap).
static bool host_import(uint64_t off, uint64_t size, VkBuffer *out_buf, VkDeviceMemory *out_mem, bool bda) {
    return host_import_ptr((void *)(uintptr_t)(HEAP_BASE + off), size, out_buf, out_mem, bda) == VK_SUCCESS;
}

// One import of the whole heap, tried once at the start where it should fit but not by much (device.c
// whole_import_fits: g_vk.heap_probe): HEAP_MAX of scratch host memory imported and used by one submission, then
// freed. True: it ran out of memory (the import or the submission: issue #205's driver took the import and failed
// the first submission), and the heap is imported in lazy chunks instead - decided here, at device creation (vk_exec_init),
// before any shader is translated or the game starts. Costs: the import pins HEAP_MAX of RAM for a moment (well under a
// second). Any other failure: logged, and the heap is imported as one buffer as before.
// LARECOMP_VK_SIM_HEAP_OOM=1: the probe runs, then reports out of memory (tests the fallback on any GPU).
static bool whole_import_probe_oom(void) {
    const uint64_t size = HEAP_MAX, align = SF2C_EXEC_N_64_A68B << SF2C_EXEC_N_10_4A44;  // (minImportedHostPointerAlignment: 4 KB here, 64 KB at most)
    const char *sim = getenv(SF2C_EXEC_S_LARECOMP_VK_SIM_HEAP_OOM_49B8);
    rec_drain();  // (the queue below is the recorder's: idle and empty while this runs)
    uint64_t t0 = plat_now_ns();
    VkResult r = VK_ERROR_OUT_OF_HOST_MEMORY;
    const char *step = SF2C_EXEC_S_HOST_MEMORY_F00D;
    void *raw = malloc(size + align);
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    if (raw) {
        step = SF2C_EXEC_S_IMPORT_E41A;
        pretouch_host((void *)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1)), size);
        r = host_import_ptr((void *)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1)), size, &buf, &mem, false);
    }
    if (r == VK_SUCCESS) {  // a submission that uses it: where the driver makes it resident
        step = SF2C_EXEC_S_SUBMISSION_EEA6;
        VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = g_vk.qfam};
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkCommandBuffer cb = VK_NULL_HANDLE;
        r = vkCreateCommandPool(g_vk.dev, &pi, NULL, &pool);
        if (r == VK_SUCCESS) r = vkCreateFence(g_vk.dev, &fi, NULL, &fence);
        if (r == VK_SUCCESS) {
            VkCommandBufferAllocateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                              .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
            r = vkAllocateCommandBuffers(g_vk.dev, &ci, &cb);
        }
        if (r == VK_SUCCESS) {
            VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                           .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(cb, &bi);
            vkCmdFillBuffer(cb, buf, 0, SF2C_EXEC_N_256_51E8, 0);         // (zeros into zeroed memory: the start and the end of it)
            vkCmdFillBuffer(cb, buf, size - SF2C_EXEC_N_256_51E8, SF2C_EXEC_N_256_51E8, 0);
            r = vkEndCommandBuffer(cb);
        }
        if (r == VK_SUCCESS) {
            VkCommandBufferSubmitInfo cbs = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb};
            VkSubmitInfo2 si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbs};
            r = vkQueueSubmit2(g_vk.queue, 1, &si, fence);
            if (r == VK_SUCCESS) r = vkWaitForFences(g_vk.dev, 1, &fence, VK_TRUE, SF2C_EXEC_N_10000000000ULL_9E01);
        }
    }
    if (r == VK_ERROR_DEVICE_LOST) vk_devlost_dump();
    if (fence) vkDestroyFence(g_vk.dev, fence, NULL);
    if (pool) vkDestroyCommandPool(g_vk.dev, pool, NULL);
    if (buf) vkDestroyBuffer(g_vk.dev, buf, NULL);
    if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
    free(raw);
    bool oom = r == VK_ERROR_OUT_OF_DEVICE_MEMORY || r == VK_ERROR_OUT_OF_HOST_MEMORY;
    if (r == VK_SUCCESS && sim && atoi(sim)) { oom = true; r = VK_ERROR_OUT_OF_DEVICE_MEMORY; step = SF2C_EXEC_S_SIMULATED_LARECOMP_VK_SIM_HE_EDBD; }
    unsigned ms = (unsigned)((plat_now_ns() - t0) / SF2C_EXEC_N_1000000_6CCE);
    if (oom)
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_ONE_IMPORT_OF__CA8E
            SF2C_EXEC_S_IMPORTED_AS_THE_GAME_BINDS_T_D6CF, (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), step, r, ms);
    else if (r != VK_SUCCESS)
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_ONE_IMPORT_OF__2EEB,
            (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), step, r, ms);
    else
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_ONE_IMPORT_OF__57E2, (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), ms);
    return oom;
}

// Preparing the first shader heap can fault in/pin several GB. Start it after
// the first movie present is queued, alongside the guest's startup parsing. Only this
// worker's private handles are written off-thread; the render thread adopts them
// at first use, and reset joins before any guest pages can be replaced.
static struct {
    pthread_t thread;
    bool started, attempted;
    uint64_t size;
    VkBuffer buf;
    VkDeviceMemory mem;
    bool ok;
} gmem_preload;

static void *gmem_preload_worker(void *arg) {
    (void)arg;
    uint64_t start = plat_now_ns();
    gmem_preload.ok = host_import(0, gmem_preload.size, &gmem_preload.buf, &gmem_preload.mem, false);
    LOG(SF2C_EXEC_S_VK_BACKGROUND_GUEST_HEAP_PRE_B388,
        gmem_preload.ok ? SF2C_EXEC_S_FINISHED_90FC : SF2C_EXEC_S_UNAVAILABLE_EC83,
        (unsigned long long)(gmem_preload.size >> SF2C_EXEC_N_20_F5CA),
        (unsigned long long)((plat_now_ns() - start) / SF2C_EXEC_N_1000000_6CCE));
    return NULL;
}

void vk_guest_heap_preload_end(void) {
    if (gmem_preload.started) pthread_join(gmem_preload.thread, NULL);
    if (gmem_preload.buf) vkDestroyBuffer(g_vk.dev, gmem_preload.buf, NULL);
    if (gmem_preload.mem) vkFreeMemory(g_vk.dev, gmem_preload.mem, NULL);
    memset(&gmem_preload, 0, sizeof gmem_preload);
}

static bool gmem_preload_take(uint64_t size, VkBuffer *buf, VkDeviceMemory *mem) {
    if (!gmem_preload.started) return false;
    pthread_join(gmem_preload.thread, NULL);
    gmem_preload.started = false;
    bool ready = gmem_preload.ok && gmem_preload.size == size;
    if (ready) {
        *buf = gmem_preload.buf;
        *mem = gmem_preload.mem;
        gmem_preload.buf = VK_NULL_HANDLE;
        gmem_preload.mem = VK_NULL_HANDLE;
    } else {
        // Heap growth or a failed preload keeps the existing first-use path.
        vk_guest_heap_preload_end();
        gmem_preload.attempted = true;
    }
    return ready;
}

void vk_guest_heap_preload(void) {
    if (gmem_preload.attempted || gmem_buf || !g_vk.host_import ||
        g_vk.managed_heap || getenv(SF2C_EXEC_S_LARECOMP_VK_NO_GMEM_2D48)) return;
    gmem_chunk_shift();
    if (gc_lazy) return;  // lazy chunks retain their demand-based memory policy
    uint64_t size = gmem_heap_size();
    if (!size) return;
    gmem_preload.attempted = true;
    gmem_preload.size = size;
    int error = pthread_create(&gmem_preload.thread, NULL, gmem_preload_worker, NULL);
    gmem_preload.started = error == 0;
    if (error) LOG(SF2C_EXEC_S_VK_CANNOT_START_BACKGROUND_G_2EB2, strerror(error));
    else LOG(SF2C_EXEC_S_VK_PREPARING_GUEST_HEAP_DURI_B2F7, (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA));
}

static bool gmem_import(uint64_t size) {
    VkBuffer buf;
    VkDeviceMemory mem;
    bool preloaded = gmem_preload_take(size, &buf, &mem);
    if (!preloaded && !host_import(0, size, &buf, &mem, false)) return false;
    if (gmem_buf) vk_defer_buffer(gmem_buf, gmem_mem);  // the heap grew: the pending submission may still use the old import
    gmem_buf = buf;
    gmem_mem = mem;
    gmem_size = size;
    LOG(SF2C_EXEC_S_VK_GUEST_HEAP_IMPORTED_FOR_S_5141,
        (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), preloaded ? SF2C_EXEC_S_PREPARED_IN_BACKGROUND_9D4C : SF2C_EXEC_S_EMPTY_12AE);
    return true;
}

// Linux, AMD (RADV): the import above is a userptr buffer, and amdgpu checks every page of a userptr buffer again on
// each submission - 3.2 GB of heap, ~45 ms of driver time a frame on a ROG Ally X, Steam Deck, Radeon laptops (issues
// #40, #51). Here the heap is instead allocated by the driver (cached system memory, exported as a dma-buf) and that
// memory is mapped at HEAP_BASE before the game starts, so the GPU sees an ordinary buffer. Called from boot (main.c)
// on the main thread before any guest thread exists; a reboot frees it with gmem_buf (vk_exec_reset) and the guest
// window's reset unmaps it, so the next boot gets fresh zeroed memory. Costs: the whole HEAP_MAX is committed once the
// GPU first uses it, and Linux write tracking can't watch this mapping (the heap is then re-checked like on Beta 4).
// Nothing writes the heap before boot (the start screen doesn't use guest memory): the mapping replaces it unseen.
// The mapping: where the driver has VK_EXT_map_memory_placed (RADV, Mesa 24.1+), it maps the memory at HEAP_BASE
// itself, through its own DRM file as for any vkMapMemory. Else the memory is exported as a dma-buf and that is
// mmapped there - which some systems refuse (issue #92: RX 6800 XT, Mesa 25.0.7, "Permission denied"), and then
// the whole heap was a userptr import again.
// Default on for RADV; LARECOMP_VK_HEAP_DMABUF=0 off, =1 on any Linux driver with dma-buf export or placed maps.
// LARECOMP_VK_HEAP_PLACED=0: the dma-buf mapping even where placed maps exist (tests).
#ifdef __linux__
static bool gmem_dmabuf;  // the heap is the driver's memory mapped at HEAP_BASE
static bool gmem_placed;  // ... by the driver (vkMapMemory2KHR, placed): unmapped through it at a reboot

// The driver maps `mem` (HEAP_MAX, one allocation) at HEAP_BASE. NULL, or why not; HEAP_BASE is ordinary guest
// memory again after a failure.
static const char *heap_map_placed(VkDeviceMemory mem) {
#ifndef VK_EXT_map_memory_placed
    (void)mem;
    return SF2C_EXEC_S_BUILT_WITHOUT_VK_EXT_MAP_MEM_1663;  // (g_vk.map_placed is never set then)
#else
    PFN_vkMapMemory2KHR map2 = (PFN_vkMapMemory2KHR)vkGetDeviceProcAddr(g_vk.dev, SF2C_EXEC_S_VKMAPMEMORY2KHR_74A8);
    PFN_vkUnmapMemory2KHR unmap2 = (PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(g_vk.dev, SF2C_EXEC_S_VKUNMAPMEMORY2KHR_DE08);
    if (!map2 || !unmap2) return SF2C_EXEC_S_NO_VKMAPMEMORY2KHR_7E3B;
    // First mapped anywhere, to know the driver can map it at all: a placed map replaces what's at HEAP_BASE before
    // the driver's own mmap can fail
    VkMemoryMapInfoKHR mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR, .memory = mem, .offset = 0, .size = VK_WHOLE_SIZE};
    VkMemoryUnmapInfoKHR ui = {.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR, .memory = mem};
    void *p = NULL;
    if (map2(g_vk.dev, &mi, &p) != VK_SUCCESS || !p) return SF2C_EXEC_S_THE_DRIVER_CAN_T_MAP_IT_FD94;
    unmap2(g_vk.dev, &ui);
    VkMemoryMapPlacedInfoEXT pl = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT, .pPlacedAddress = (void *)(uintptr_t)HEAP_BASE};
    mi.flags = VK_MEMORY_MAP_PLACED_BIT_EXT;
    mi.pNext = &pl;
    p = NULL;
    VkResult r = map2(g_vk.dev, &mi, &p);
    if (r == VK_SUCCESS && p == (void *)(uintptr_t)HEAP_BASE) return NULL;
    if (r == VK_SUCCESS) unmap2(g_vk.dev, &ui);
    // (the old mapping may be gone: plain zeroed memory again, registered for write tracking like a reboot's)
    if (!plat_reset_guest(HEAP_BASE, HEAP_BASE + HEAP_MAX)) FATAL(SF2C_EXEC_S_CANNOT_RESTORE_THE_GUEST_HEA_BF54);
    return SF2C_EXEC_S_THE_DRIVER_S_PLACED_MAPPING__FFA7;
#endif
}

// The driver memory heap at a reboot (vk_exec_reset, GPU idle, guest threads stopped): the driver's mapping is left
// as a reserved range (or unmapped), then HEAP_BASE gets fresh zeroed guest memory at once, so no hole is left in the
// guest window until the window's reset.
static void heap_unmap_placed(void) {
    if (!gmem_placed) return;
    PFN_vkUnmapMemory2KHR unmap2 = (PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(g_vk.dev, SF2C_EXEC_S_VKUNMAPMEMORY2KHR_DE08);
    VkMemoryUnmapInfoKHR ui = {.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR, .memory = gmem_mem,
                               .flags = 0};
#ifdef VK_EXT_map_memory_placed
    if (g_vk.unmap_reserve) ui.flags = VK_MEMORY_UNMAP_RESERVE_BIT_EXT;
#endif
    if (unmap2) unmap2(g_vk.dev, &ui);
    gmem_placed = false;
}

static bool guest_heap_dmabuf(void) {
    if (gmem_buf || !g_vk.dev || !(g_vk.dmabuf || g_vk.map_placed) || getenv(SF2C_EXEC_S_LARECOMP_VK_NO_GMEM_2D48)) return false;
    const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_HEAP_DMABUF_419D);
    if (!g_vk.managed_heap && (e ? !atoi(e) : !strstr(g_vk.props.deviceName, SF2C_EXEC_S_RADV_6945))) return false;
    const uint64_t size = HEAP_MAX;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT;  // (cached: the game's CPU work runs in it)
    int type = -1;
    uint64_t largest = 0;  // (the largest heap with a cached host-visible type, for the log when none is big enough)
    for (uint32_t i = 0; i < mp.memoryTypeCount && type < 0; i++) {
        if ((mp.memoryTypes[i].propertyFlags & want) != want) continue;
        uint64_t hs = mp.memoryHeaps[mp.memoryTypes[i].heapIndex].size;
        if (hs >= size) type = (int)i;
        else if (hs > largest) largest = hs;
    }
    if (type < 0) {
        if (largest)  // (Steam Deck: a 3072 MB system heap)
            LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_THE_2F34
                SF2C_EXEC_S_LLU_MB_N_CBDC, (unsigned long long)(largest >> SF2C_EXEC_N_20_F5CA), (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA));
        else LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_NO__2EA9);
        return false;
    }
    VkExternalMemoryBufferCreateInfo eb = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                           .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &eb, .size = size,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    const char *pe = getenv(SF2C_EXEC_S_LARECOMP_VK_HEAP_PLACED_F170);
    bool placed = g_vk.map_placed && (!pe || atoi(pe));
    if (!placed && !g_vk.dmabuf) { LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_NO__1874); return false; }
    bi.pNext = placed ? NULL : &eb;  // (an exportable buffer only for the dma-buf mapping)
    VkBuffer buf;
    if (vkCreateBuffer(g_vk.dev, &bi, NULL, &buf) != VK_SUCCESS) { LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_BUF_39EC); return false; }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.dev, buf, &req);
    if (!(req.memoryTypeBits & (SF2C_EXEC_N_1U_B8E6 << type))) {
        LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_MEM_FE06, type);
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        return false;
    }
    if (req.size != size) {  // (the mapping must cover exactly the heap: guest memory follows it)
        LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_THE_5F26, (unsigned long long)req.size,
            (unsigned long long)size);
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        return false;
    }
    VkMemoryDedicatedAllocateInfo ded = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = buf};
    VkExportMemoryAllocateInfo ex = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .pNext = &ded,
                                     .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = placed ? (void *)&ded : (void *)&ex,
                               .allocationSize = req.size, .memoryTypeIndex = (uint32_t)type};
    VkDeviceMemory mem;
    VkResult r = vkAllocateMemory(g_vk.dev, &ai, NULL, &mem);
    if (r != VK_SUCCESS) { LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_ALL_6748, r); vkDestroyBuffer(g_vk.dev, buf, NULL); return false; }
    // Bind before publishing a mapping to the guest. An allocation/bind failure must not replace live guest pages.
    r = vkBindBufferMemory(g_vk.dev, buf, mem, 0);
    if (r != VK_SUCCESS) {
        LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_BIN_1092, r);
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        vkFreeMemory(g_vk.dev, mem, NULL);
        return false;
    }
    const char *why = NULL;
    // Unregister the old anonymous window before replacing it with a DRM mapping. Write protection must never
    // be applied to the driver-owned heap, nor survive into a userptr fallback after a partial failure.
    plat_write_tracking_off(SF2C_EXEC_S_PREPARING_THE_DRIVER_MANAGED_7DEF);
    if (placed) {
        why = heap_map_placed(mem);
        if (why) {
            LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_PLA_14B1,
                (unsigned long long)HEAP_BASE, why);
            vkDestroyBuffer(g_vk.dev, buf, NULL);
            vkFreeMemory(g_vk.dev, mem, NULL);
            return false;
        }
        gmem_placed = true;
        goto mapped;
    }
    PFN_vkGetMemoryFdKHR get_fd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(g_vk.dev, SF2C_EXEC_S_VKGETMEMORYFDKHR_1FD4);
    VkMemoryGetFdInfoKHR fi = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = mem,
                               .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    int fd = -1;
    if (!get_fd || get_fd(g_vk.dev, &fi, &fd) != VK_SUCCESS || fd < 0) why = SF2C_EXEC_S_NO_DMA_BUF_FOR_IT_63C9;
    // First mapped anywhere, to know the driver lets it be mapped at all: MAP_FIXED unmaps what's at HEAP_BASE before
    // the driver is asked, so a refusal there would leave the heap unmapped (review of 2c27f3d)
    if (!why) {
        void *probe = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (probe == MAP_FAILED) why = strerror(errno);
        else munmap(probe, size);
        if (probe == MAP_FAILED) LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_THE_A644, why);
    }
    if (!why && mmap((void *)(uintptr_t)HEAP_BASE, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) !=
                    (void *)(uintptr_t)HEAP_BASE) {
        why = strerror(errno);
        // (the old mapping is gone: plain zeroed memory again, registered for write tracking like a reboot's)
        if (!plat_reset_guest(HEAP_BASE, HEAP_BASE + size)) FATAL(SF2C_EXEC_S_CANNOT_RESTORE_THE_GUEST_HEA_BF54);
    }
    if (fd >= 0) close(fd);  // (the mapping keeps the dma-buf)
    if (why) {
        LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_MAP_8B9B,
            (unsigned long long)HEAP_BASE, why);
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        vkFreeMemory(g_vk.dev, mem, NULL);
        return false;
    }
mapped:;
    // the game expects a zeroed heap; the kernel hands out zeroed pages, but 8 spread pages are checked to be sure
    static const uint8_t zero_page[SF2C_EXEC_N_4096_8B92];
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++)
        if (memcmp((const void *)(uintptr_t)(HEAP_BASE + size / SF2C_EXEC_N_8_2C62 * i), zero_page, sizeof zero_page)) {
            LOG(SF2C_EXEC_S_VK_HEAP_AS_DRIVER_MEMORY_NOT_D927);
            memset((void *)(uintptr_t)HEAP_BASE, 0, size);
            break;
        }
    gmem_buf = buf;
    gmem_mem = mem;
    gmem_size = size;
    gmem_dmabuf = true;
    LOG(SF2C_EXEC_S_VK_GUEST_HEAP_AS_DRIVER_MEMO_3E74
        SF2C_EXEC_S_ONCE_THE_GPU_FIRST_USES_IT_N_4C34, gmem_placed ? SF2C_EXEC_S_PLACED_MAP_A0DE : SF2C_EXEC_S_DMA_BUF_7882, type, (unsigned long long)(size >> SF2C_EXEC_N_20_F5CA), (unsigned long long)HEAP_BASE);
    return true;
}
#endif

// The guest heap's GPU memory on Linux: the driver's own (above) where it can be, else host-pointer imports - the
// whole heap (gmem_import) or 16 MB chunks as draws first bind them (gc_lazy: RADV without the dma-buf heap, Steam
// Deck; GPUs whose storage buffers can't span the heap). On Mesa's drivers every such import is a userptr buffer, and
// page write tracking (platform/linux.c: userfaultfd write protection, PAGEMAP_SCAN) changes the protection of pages
// inside them: each tracking query over vertex or texture memory is an MMU-notifier invalidation of the imports it
// overlaps, so the kernel driver waits for the GPU to finish with them and checks their pages again at the next
// submission (amdgpu; the CPU and GPU stop overlapping - Steam Deck, issue #181: 30 fps on Beta 4, 20 with tracking
// over the whole-heap import). Chunks only make the re-check smaller, not the wait. So with any userptr import,
// tracking is off, as on Beta 4 (the vertex cache and the texture checks go without it). NVIDIA's own driver pins
// the pages instead and keeps it. Decided here, at boot, before any query protected a page. LARECOMP_WRITE_TRACKING=1:
// on anyway.
static bool gmem_userptr;  // the heap is imported by host pointer: userptr (vertex cache default, vcache_lookup)
static bool heap_is_userptr(void) { return gmem_userptr; }  // (the storage write watch: off over a userptr import)
static const char *heap_tracking_reason(void) {  // (overlap_on: why there is no page write tracking here)
    if (gmem_userptr) return SF2C_EXEC_S_THE_GUEST_HEAP_IS_A_USERPTR__4476;
#ifdef __linux__
    if (gmem_dmabuf) return SF2C_EXEC_S_THE_GUEST_HEAP_IS_DRIVER_MEM_4D5E;
    return SF2C_EXEC_S_NO_USERFAULTFD_WRITE_TRACKIN_3E1F;
#else
    return SF2C_EXEC_S_THE_GUEST_WINDOW_HAS_NO_WRIT_C3B8;
#endif
}
// The whole heap as one userptr import (not chunks) on AMD: each submission has amdgpu check all of its pages
static bool heap_whole_userptr(void) { return gmem_userptr && !gc_lazy && g_vk.props.vendorID == SF2C_EXEC_N_0X1002_D106; }

void backend_guest_heap_prepare(void) {
    gmem_userptr = false;
#ifdef __linux__
    guest_heap_dmabuf();
    if (g_vk.managed_heap && !gmem_dmabuf)
        FATAL(SF2C_EXEC_S_CANNOT_ALLOCATE_MAP_THE_DRIV_1484
              SF2C_EXEC_S_USE_TOOLS_RUN_LINUX_SH_SOFTW_6D0D);
#endif
    if (!g_vk.dev) return;
    gmem_chunk_shift();  // (the chunk / lazy decision, before the first draw binds anything)
#ifdef __linux__
    // driver memory: write tracking can't watch that mapping (its queries over the heap would fail; PAGEMAP_SCAN
    // refuses a range without userfaultfd write protection), so it's off there too
    if (gmem_dmabuf) { plat_write_tracking_off(SF2C_EXEC_S_THE_GUEST_HEAP_IS_DRIVER_MEM_6720); return; }
    if (!g_vk.host_import || getenv(SF2C_EXEC_S_LARECOMP_VK_NO_GMEM_2D48) || g_vk.props.vendorID == SF2C_EXEC_N_0X10DE_112D) return;
    gmem_userptr = true;
    plat_write_tracking_off(gc_lazy ? SF2C_EXEC_S_THE_GUEST_HEAP_IS_IMPORTED_I_A089 : SF2C_EXEC_S_THE_GUEST_HEAP_IS_A_USERPTR__E393);
#endif
}

static uint64_t gmem_tried;  // heap size the import was last attempted for (gmem_info imports / re-imports)

static VkDescriptorBufferInfo gmem_info(void) {
    static int off = -1;
    if (off < 0) off = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_GMEM_2D48) != NULL || !g_vk.host_import || gc_lazy;  // (lazy: chunks instead)
    uint64_t want = gmem_heap_size();
    if (!off && want > gmem_size && !gmem_import(want)) {
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_IMPORT_FAILED__2726, (unsigned long long)(want >> SF2C_EXEC_N_20_F5CA));
        off = 1;
    }
    if (gmem_buf) return (VkDescriptorBufferInfo){gmem_buf, 0, gmem_size};
    static uint32_t zero[SF2C_EXEC_N_1024_E39E];
    VkSpan s = vk_transient_copy(zero, sizeof zero, SF2C_EXEC_N_256_51E8);
    return (VkDescriptorBufferInfo){s.buf, s.off, sizeof zero};
}

// ---- the guest heap for shaders in chunks (shader.h maxwell_gmem_chunk_shift)
// Chunks for two reasons:
// - a GPU whose storage buffers can't span HEAP_MAX (maxStorageBufferRange: 1 GB on Intel): binding one range past
//   that limit is out of spec, and the UHD 630's driver then read garbage beyond 1 GB: dark lighting, black objects
//   (issue #8). Lazy chunks as below (device.c lazy_heap_predicted); where they aren't possible (or with
//   LARECOMP_VK_GMEM_LAZY=0), windows of the one imported heap, one binding each;
// - lazy import (gmem_lazy): on Linux/AMD the import is a userptr buffer and amdgpu checks every page of such a buffer
//   again on each submission - 3.2 GB of heap, ~45 ms of driver time a frame (issues #40, #51). The dma-buf heap
//   (guest_heap_dmabuf) avoids that, but needs a cached host-visible memory type that can hold HEAP_MAX, and APUs
//   have none (Steam Deck: a 3072 MB system heap; Ryzen 3200G: 3172 MB, so the 3220 MB import is also over its GTT
//   budget: issues #40, #92). The GPU itself reads only a little of the heap in place: 73 MB over a four-area tour
//   (vertex and index data 67 MB, storage buffers 9 MB), 15-28 MB a frame, scattered over the whole 3.25 GB (measured
//   on the Metal backend, which binds the same ranges). So each 16 MB chunk is imported on its own when a draw or
//   dispatch first binds a range in it (23 chunks, 368 MB, over that tour), and only the imported chunks exist for the
//   driver to check: a fraction of the pages, under the GTT budget, and still the game's own memory (nothing to copy
//   back). Vertex and index data bind their chunk directly; shaders (LDG/STG) take the chunk's device address and
//   size from a table (GChunks, rewritten when a chunk is imported) and go through a buffer reference after a bounds
//   check: a chunk not imported has size 0, the access reads 0 / drops the store, and the shader counts it in GMiss
//   with the lowest word index hit: such a chunk (a pointer the game keeps in a uniform buffer, which no bind told us
//   about) is imported for the next frame, and the count is in the stats (after a chunk's first use it should stay 0).
//   Buffer device addresses are enabled only where this mode will be used (device.c lazy_heap_predicted): on RADV
//   they put every allocation on the driver's global BO list.
//   Default on RADV when the dma-buf heap doesn't apply, wherever one storage buffer can't span the heap and where the
//   system memory heap (or its budget) can't hold one import of it (8 GB laptops: issue #205), and where one import
//   was tried at the start and ran out of memory (whole_import_probe_oom; LARECOMP_VK_SIM_HEAP_OOM=1 simulates it);
//   LARECOMP_VK_GMEM_LAZY=1 anywhere (tests), =0 off.
// LARECOMP_VK_GMEM_CHUNK_MB=<n> forces chunks of n MB (a power of two, 1 GB at most), =0 one buffer.
#define GC_MAX GMEM_CHUNKS_MAX
static struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    uint64_t size, addr;  // (addr: the chunk's device address, lazy mode)
} gc[GC_MAX];
static int gc_n;             // chunks over HEAP_MAX
static uint32_t gc_shift;    // 0: one buffer
static bool gc_lazy;         // imports per chunk (else windows of gmem_buf)
static VkBuffer gc_dummy, gc_miss_buf;
static VmaAllocation gc_dummy_mem, gc_miss_mem;
static uint32_t *gc_miss;    // GMiss, host-visible: per chunk, accesses that found no chunk, then the lowest word index
static VkDescriptorSetLayout gc_layout;
static VkDescriptorSet gc_set;
static uint64_t gc_set_seq;
static uint32_t gc_gen, gc_set_gen;  // (gen: bumped when a chunk is imported; the set is rebuilt per submission and gen)
static int gc_scanned;                // chunks imported for a pointer found in a uniform buffer
static uint8_t gc_remembered[GC_MAX]; // chunks shaders reached without a bind (a miss, a pointer): imported again after a reboot
                                      // and, through cache/gmem_chunks.txt, at the next start (a machine misses a chunk once)
static bool gc_file_loaded;
static uint64_t gc_remembered_heap;   // the heap size the remembered chunks were imported at this boot (0: not yet)

// cache/gmem_chunks.txt: one heap byte offset (hex) per line of a chunk shaders reached without a bind. Offsets, not
// chunk numbers: the chunk size depends on the GPU. Loaded once, into gc_remembered; written whenever one is added.
static void gc_remembered_load(void) {
    if (gc_file_loaded) return;
    gc_file_loaded = true;
    FILE *f = fopen(SF2C_EXEC_S_CACHE_GMEM_CHUNKS_TXT_75EB, SF2C_EXEC_S_R_2659);
    if (!f) return;
    char line[SF2C_EXEC_N_64_A68B];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long off = strtoull(line, NULL, SF2C_EXEC_N_16_B17E);
        int k = (int)(off >> gc_shift);
        if (k >= 0 && k < gc_n && !gc_remembered[k]) { gc_remembered[k] = 1; n++; }
    }
    fclose(f);
    if (n) LOG(SF2C_EXEC_S_VK_GUEST_HEAP_D_CHUNKS_SHADE_E494, n);
}
static void gc_remembered_save(void) {
    mkdir(SF2C_EXEC_S_CACHE_C87A, SF2C_EXEC_N_0755_64A5);
    FILE *f = fopen(SF2C_EXEC_S_CACHE_GMEM_CHUNKS_TXT_TMP_C322, SF2C_EXEC_S_W_7265);
    if (!f) return;
    for (int k = 0; k < gc_n; k++)
        if (gc_remembered[k]) fprintf(f, SF2C_EXEC_S_LLX_N_232C, (unsigned long long)((uint64_t)k << gc_shift));
    fclose(f);
    remove(SF2C_EXEC_S_CACHE_GMEM_CHUNKS_TXT_75EB);
    rename(SF2C_EXEC_S_CACHE_GMEM_CHUNKS_TXT_TMP_C322, SF2C_EXEC_S_CACHE_GMEM_CHUNKS_TXT_75EB);
}
static void gc_remember(int k) {
    if (gc_remembered[k]) return;
    gc_remembered[k] = 1;
    gc_remembered_save();
}
static int gc_imported;
static uint64_t gc_imported_bytes, stat_gc_misses;

static bool gmem_lazy(void) {
    static int lazy = -1;
    if (lazy >= 0) return lazy;
    lazy = g_vk.lazy_heap;
    if (!lazy && g_vk.heap_probe && !getenv(SF2C_EXEC_S_LARECOMP_VK_NO_GMEM_2D48)) lazy = whole_import_probe_oom();
    if (lazy && (!g_vk.host_import || !g_vk.bda || !g_vk.ssbo_chunks)) {
        LOG(SF2C_EXEC_S_VK_LAZY_HEAP_IMPORT_NOT_POSS_E18A, !g_vk.host_import ? SF2C_EXEC_S_NO_HOST_MEMORY_IMPORT_6FD9 : !g_vk.bda ? SF2C_EXEC_S_NO_BUFFER_DEVICE_ADDRESSES_AD4D : SF2C_EXEC_S_NO_STORES_FROM_FRAGMENT_VERT_7E67);
        lazy = 0;
    }
    return lazy;
}

// Decided once (pthread_once): the render thread's draws, the translation and pipeline workers and the boot
// (backend_guest_heap_prepare) all ask, and a second caller must not see a half-made decision (shift is 0 until the
// fit loop below ends - a shader translated then would use one buffer while draws bind chunks).
static int gc_shift_decided = -1;
static void gmem_chunk_decide(void);
static pthread_once_t gc_decide_once = PTHREAD_ONCE_INIT;
static uint32_t gmem_chunk_shift(void) {
    pthread_once(&gc_decide_once, gmem_chunk_decide);
    return (uint32_t)gc_shift_decided;
}
static void gmem_chunk_decide(void) {
    int shift;
    const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_CHUNK_MB_B29B);
    uint64_t lim = g_vk.props.limits.maxStorageBufferRange;
    gc_lazy = gmem_lazy();
    shift = 0;
    if (e) { uint64_t b = strtoull(e, NULL, SF2C_EXEC_N_10_4A44) << SF2C_EXEC_N_20_F5CA; if (b) shift = SF2C_EXEC_N_63_DA4E - __builtin_clzll(b); }
    else if (gc_lazy) shift = SF2C_EXEC_N_24_C235;
    else if (HEAP_MAX > lim && lim) shift = SF2C_EXEC_N_63_DA4E - __builtin_clzll(lim);
    if (shift > SF2C_EXEC_N_30_624B) shift = SF2C_EXEC_N_30_624B;  // (1 GB at most: NVIDIA computes a block's length in signed 32 bits, a 2 GB range reads as negative)
    // The table holds GC_MAX chunks at most, and a stage's storage buffers (the windows, gfast, binding 17) its limit
    // (Intel: 200): a chunk size that doesn't fit grows until it does (4 MB asked for -> 16 MB). One buffer only where
    // one buffer is within maxStorageBufferRange.
    int asked = shift, n = 0;
    uint32_t per_stage = g_vk.props.limits.maxPerStageDescriptorStorageBuffers, per_set = g_vk.props.limits.maxDescriptorSetStorageBuffers;
    if (shift && gc_lazy && !g_vk.ssbo_chunks) {
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_CHUNKS_NOT_POS_40E9);
        shift = 0;
    }
    while (shift) {
        n = (int)((HEAP_MAX + (SF2C_EXEC_N_1ULL_622E << shift) - 1) >> shift);
        bool fits = n <= GC_MAX && (gc_lazy || ((uint32_t)n + SF2C_EXEC_N_8_2C62 <= per_stage && (uint32_t)n + 1 <= per_set));
        if (fits) break;
        if ((SF2C_EXEC_N_1ULL_622E << shift) >= HEAP_MAX) { shift = 0; break; }  // (not even one chunk fits: one buffer)
        shift++;
    }
    if (shift != asked && asked)
        LOG(SF2C_EXEC_S_VK_GUEST_HEAP_CHUNKS_OF_LLU__C778,
            (unsigned long long)(SF2C_EXEC_N_1ULL_622E << asked) >> SF2C_EXEC_N_20_F5CA, GC_MAX, per_stage, per_set, shift ? SF2C_EXEC_S_LARGER_CHUNKS_99E3 : SF2C_EXEC_S_ONE_BUFFER_67F6);
    if (!shift && HEAP_MAX > lim && lim) LOG(SF2C_EXEC_S_VK_WARNING_ONE_STORAGE_BUFFE_827A, (unsigned long long)lim >> SF2C_EXEC_N_20_F5CA);
    if (!shift) gc_lazy = false;
    gc_shift = (uint32_t)shift;
    gc_n = shift ? n : 0;
    maxwell_gmem_lazy = gc_lazy;
    maxwell_gmem_fs_miss = gc_lazy && getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_FS_MISS_999B) && atoi(getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_FS_MISS_999B)) != 0;
    if (maxwell_gmem_fs_miss) LOG(SF2C_EXEC_S_VK_LAZY_CHUNKS_EVERY_FRAGMEN_6FE6);
    if (shift) LOG(SF2C_EXEC_S_VK_GUEST_HEAP_BOUND_TO_SHADE_B153,
                   (unsigned long long)(SF2C_EXEC_N_1ULL_622E << shift) >> SF2C_EXEC_N_20_F5CA, gc_lazy ? SF2C_EXEC_S_CHUNKS_IMPORTED_AS_THE_GAME__E3A9 : SF2C_EXEC_S_WINDOWS_E294, (unsigned long long)lim >> SF2C_EXEC_N_20_F5CA);
    gc_shift_decided = shift;
}

// The heap mapped so far, as the imports see it (SetHeapSize; pages beyond it don't exist yet).
static uint64_t gmem_heap_size(void) {
    uint64_t want = (kernel_heap_size() + SF2C_EXEC_N_0XFFFF_F51F) & ~SF2C_EXEC_N_0XFFFFULL_2BD3;
    uint64_t ring = snap_ring_end();  // (overlap: the storage copies' ring right after the heap is the GPU's too)
    if (ring > want) want = ring;
    return want > HEAP_MAX ? HEAP_MAX : want;
}

static bool host_import(uint64_t off, uint64_t size, VkBuffer *buf, VkDeviceMemory *mem, bool bda);

// Lazy mode: chunk k imported (again, if the heap grew into it since). False: not mapped yet / the import failed.
static bool gc_import(int k) {
    uint64_t off = (uint64_t)k << gc_shift, heap = gmem_heap_size();
    if (k < 0 || k >= gc_n || off >= heap) return false;
    uint64_t size = heap - off < (SF2C_EXEC_N_1ULL_622E << gc_shift) ? heap - off : SF2C_EXEC_N_1ULL_622E << gc_shift;
    if (gc[k].buf && gc[k].size >= size) return true;
    VkBuffer buf;
    VkDeviceMemory mem;
    if (!host_import(off, size, &buf, &mem, true)) {
        static int warned;
        if (warned++ < SF2C_EXEC_N_8_2C62) LOG(SF2C_EXEC_S_VK_GUEST_HEAP_CHUNK_D_LLU_MB_9B74, k, (unsigned long long)size >> SF2C_EXEC_N_20_F5CA, (unsigned long long)off >> SF2C_EXEC_N_20_F5CA);
        return false;
    }
    if (gc[k].buf) {  // (the heap grew into the chunk: the pending submission may still use the old import)
        vk_defer_buffer(gc[k].buf, gc[k].mem);
        gc_imported_bytes -= gc[k].size;
    } else {
        gc_imported++;
    }
    VkBufferDeviceAddressInfo ai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
    gc[k].buf = buf; gc[k].mem = mem; gc[k].size = size;
    gc[k].addr = vkGetBufferDeviceAddress(g_vk.dev, &ai);
    gc_imported_bytes += size;
    gc_gen++;
    return true;
}

// (snap_on, lazy chunks) The chunk holding the end of the game's heap may have been imported before the ring was
// decided, short of it: imported again over the ring (gc_import replaces it; the old import goes once its submissions
// are done). snap_state is set before this runs, so gmem_heap_size's answer includes the ring here.
static void snap_ring_import_tail(void) {
    if (!gc_lazy || !gc_shift) return;
    for (int k = (int)(snap_base >> gc_shift); k <= (int)((snap_base + SNAP_RING - 1) >> gc_shift) && k < gc_n; k++)
        if (gc[k].buf && gc[k].size < gmem_heap_size() - ((uint64_t)k << gc_shift)) gc_import(k);
}

// Pointers the game keeps in its uniform buffers (the lighting pass's light list, the grass's instance tables): the
// chunk a shader reaches that way has no storage buffer bound over it, so it is imported from the uniform buffer's
// contents - which the per-draw copy reads anyway - before the draw. Without this the first frame in such a chunk
// read 0 (chunk 47 at +752 MB at every scene entry, chunk 50 at the first grass cut: 576 misses over a tour). An
// 8-byte-aligned word pair that happens to look like a heap address imports a chunk for nothing: bounded, and logged.
#define GC_SCAN_MAX SF2C_EXEC_GC_SCAN_MAX
static int gc_scan_warned;
static void gc_scan_pointers(const uint32_t *w, uint64_t bytes) {
    uint64_t heap = gmem_heap_size();
    prof_scan_calls++;
    prof_scan_bytes += bytes;
    for (uint64_t i = 0; i + 1 < bytes / SF2C_EXEC_N_4_4B22; i += SF2C_EXEC_N_2_D473) {
        uint64_t v = (uint64_t)w[i] | (uint64_t)w[i + 1] << SF2C_EXEC_N_32_E29C;
        if (v < HEAP_BASE || v - HEAP_BASE >= heap) continue;
        int k = (int)((v - HEAP_BASE) >> gc_shift);
        if (k >= gc_n || gc[k].buf) continue;
        if (gc_scanned >= GC_SCAN_MAX) {
            if (!gc_scan_warned++) LOG(SF2C_EXEC_S_VK_GUEST_HEAP_D_CHUNKS_IMPOR_287B, gc_scanned);
            return;
        }
        if (gc_import(k)) {
            gc_scanned++;
            gc_remember(k);
            LOG(SF2C_EXEC_S_VK_GUEST_HEAP_CHUNK_D_LLU_MB_B1DB, k,
                (unsigned long long)((uint64_t)k << gc_shift) >> SF2C_EXEC_N_20_F5CA, (unsigned long long)v);
        }
    }
}

// Whether a pointer scan can still import anything: some chunk of the heap mapped so far isn't imported (and the
// scan's import limit isn't used up). Once every chunk is in, the scans are skipped (guest_copy_ex).
static bool gc_scan_needed(void) {
    static uint64_t c_heap;
    static uint32_t c_gen;
    static int c_scanned = -1, c_warned;
    static bool c_need = true;
    uint64_t heap = gmem_heap_size();
    if (heap != c_heap || gc_gen != c_gen || gc_scanned != c_scanned || gc_scan_warned != c_warned) {
        c_heap = heap; c_gen = gc_gen; c_scanned = gc_scanned; c_warned = gc_scan_warned;
        c_need = false;
        if (gc_scanned < GC_SCAN_MAX || !gc_scan_warned) {
            int n = (int)((heap + (SF2C_EXEC_N_1ULL_622E << gc_shift) - 1) >> gc_shift);
            if (n > gc_n) n = gc_n;
            for (int k = 0; k < n && !c_need; k++) c_need = !gc[k].buf;
        }
    }
    return c_need;
}

// A heap range a draw or dispatch binds: its chunks get imported (lazy mode), and the chunk after it as well - the
// game's storage buffers are bound with sizes the shaders read past (the lighting pass's tile list: the misses sat
// 1-9 MB into the chunk after the bound range), and a size of 0 means "the rest of the buffer".
static void gc_touch(uint64_t off, uint64_t size) {
    if (!gc_lazy || off >= HEAP_MAX) return;
    int k0 = (int)(off >> gc_shift), k1 = (int)((off + (size ? size : 1) - 1) >> gc_shift) + 1;
    if (k1 >= gc_n) k1 = gc_n - 1;
    for (int k = k0; k <= k1; k++) if (!gc[k].buf) gc_import(k);
}

// The chunk set's layout: lazy mode the table (GChunks), GMiss and the draw's chunks (GDir<k>); windows one storage
// binding each (shader.h).
// (once: pipeline layouts are made on the build workers, sets on the render thread)
static pthread_once_t gc_layout_once = PTHREAD_ONCE_INIT;
static void gmem_chunk_layout_make(void);
static VkDescriptorSetLayout gmem_chunk_layout(void) {
    pthread_once(&gc_layout_once, gmem_chunk_layout_make);
    return gc_layout;
}
static void gmem_chunk_layout_make(void) {
    gmem_chunk_shift();  // (gc_lazy / gc_n decided)
    VkShaderStageFlags st_ = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    static VkDescriptorSetLayoutBinding lb[GC_MAX];
    uint32_t nb = 0;
    if (gc_lazy) {
        lb[nb++] = (VkDescriptorSetLayoutBinding){GMEM_CHUNK_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, st_, NULL};
        lb[nb++] = (VkDescriptorSetLayoutBinding){GMEM_MISS_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, st_, NULL};
        for (int k = 0; k < GMEM_DIRECT_MAX; k++)
            lb[nb++] = (VkDescriptorSetLayoutBinding){GMEM_DIRECT_BINDING + k, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, st_, NULL};
    } else {
        for (int k = 0; k < gc_n; k++) lb[nb++] = (VkDescriptorSetLayoutBinding){GMEM_CHUNK_BINDING + k, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, st_, NULL};
    }
    VkDescriptorSetLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = nb, .pBindings = lb};
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &ci, NULL, &gc_layout));
}

// The chunk set for the pending submission: the table of imported chunks, GMiss and the draw's chunks dk[k] (-1: none)
// as GDir<k> (lazy), or the windows. Lazy sets are kept per submission and chunk generation, keyed by dk: a frame has a
// handful of storage buffer chunk combinations, so a draw binds a set it already has (no allocation, no update).
#define GC_SET_CACHE SF2C_EXEC_GC_SET_CACHE
static uint64_t gc_direct_bytes(int k) {  // (GDir<k>'s range: the chunk, within the storage buffer range limit)
    uint64_t lim = g_vk.props.limits.maxStorageBufferRange;
    return lim && gc[k].size > lim ? lim & ~SF2C_EXEC_N_3ULL_BD3E : gc[k].size;
}
static VkDescriptorSet gmem_chunk_set(const int dk[GMEM_DIRECT_MAX]) {
    if (!gc_dummy) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = SF2C_EXEC_N_4096_8B92,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO, .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        VK_CHECK(vmaCreateBuffer(g_vk.vma, &bi, &ai, &gc_dummy, &gc_dummy_mem, &info));
        memset(info.pMappedData, 0, SF2C_EXEC_N_4096_8B92);
        bi.size = sizeof(uint32_t) * SF2C_EXEC_N_2_D473 * GC_MAX;  // n[GC_MAX] accesses, w[GC_MAX] lowest word index hit
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VK_CHECK(vmaCreateBuffer(g_vk.vma, &bi, &ai, &gc_miss_buf, &gc_miss_mem, &info));
        gc_miss = info.pMappedData;
        memset(gc_miss, 0, sizeof(uint32_t) * GC_MAX);
        memset(gc_miss + GC_MAX, SF2C_EXEC_N_0XFF_7312, sizeof(uint32_t) * GC_MAX);
    }
    uint64_t seq = vk_pending_seq();
    // A boot, or a reboot: the chunks shaders reached without a bind last time (this process, or cache/gmem_chunks.txt),
    // before they miss again. Once a boot per heap size (a chunk beyond the heap mapped so far comes when it grows) -
    // not "while nothing is imported": copies and storage buffers import chunks before the first set is built.
    if (gc_lazy && gc_remembered_heap != gmem_heap_size()) {
        gc_remembered_heap = gmem_heap_size();
        gc_remembered_load();
        for (int k = 0; k < gc_n; k++) if (gc_remembered[k] && !gc[k].buf) gc_import(k);
    }
    static struct { VkDescriptorSet set; int dk[GMEM_DIRECT_MAX]; } cache[GC_SET_CACHE];
    static int ncache, next;
    static VkSpan tab_span;  // (the table for this submission and generation, shared by its sets)
    // (windows: also when the heap was imported again since - guest_direct / gfast_prepare re-import as it grows
    // mid-submission, and a set still on the old import read zeros past its end for the rest of the submission)
    static VkBuffer gc_set_heap;
    if (gc_set_seq != seq || gc_set_gen != gc_gen || (!gc_lazy && gc_set_heap != gmem_buf)) { ncache = 0; next = 0; gc_set = VK_NULL_HANDLE; }
    if (!gc_lazy) {
        if (gc_set) return gc_set;
    } else {
        for (int j = 0; j < ncache; j++)
            if (!memcmp(cache[j].dk, dk, sizeof cache[j].dk)) return cache[j].set;
    }
    static VkDescriptorBufferInfo bi[GC_MAX + SF2C_EXEC_N_2_D473 + GMEM_DIRECT_MAX];
    static VkWriteDescriptorSet w[GC_MAX + SF2C_EXEC_N_2_D473 + GMEM_DIRECT_MAX];
    VkDescriptorSet set = vk_alloc_set(gmem_chunk_layout());
    uint32_t nw = 0;
    if (gc_lazy) {  // GChunks: uvec4 per chunk = device address lo, hi, words (0: not imported), 0
        if (!ncache) {
            static uint32_t tab[GC_MAX * SF2C_EXEC_N_4_4B22];
            for (int k = 0; k < gc_n; k++) {
                tab[k * SF2C_EXEC_N_4_4B22 + 0] = (uint32_t)gc[k].addr; tab[k * SF2C_EXEC_N_4_4B22 + 1] = (uint32_t)(gc[k].addr >> SF2C_EXEC_N_32_E29C);
                tab[k * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_2_D473] = gc[k].buf ? (uint32_t)(gc[k].size >> SF2C_EXEC_N_2_D473) : 0; tab[k * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_3_4E07] = 0;
            }
            tab_span = vk_transient_copy(tab, sizeof tab, SF2C_EXEC_N_256_51E8);
        }
        bi[0] = (VkDescriptorBufferInfo){tab_span.buf, tab_span.off, sizeof(uint32_t) * GC_MAX * SF2C_EXEC_N_4_4B22};
        bi[1] = (VkDescriptorBufferInfo){gc_miss_buf, 0, sizeof(uint32_t) * SF2C_EXEC_N_2_D473 * GC_MAX};
        w[nw++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = GMEM_CHUNK_BINDING,
                                         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi[0]};
        w[nw++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = GMEM_MISS_BINDING,
                                         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[1]};
        for (int j = 0; j < GMEM_DIRECT_MAX; j++) {  // (GDir<j>: the chunk's own import, whole; unused: the zero buffer)
            int k = dk[j];
            bi[SF2C_EXEC_N_2_D473 + j] = k >= 0 && k < gc_n && gc[k].buf ? (VkDescriptorBufferInfo){gc[k].buf, 0, gc_direct_bytes(k)}
                                                       : (VkDescriptorBufferInfo){gc_dummy, 0, SF2C_EXEC_N_4096_8B92};
            w[nw++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = GMEM_DIRECT_BINDING + j,
                                             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[SF2C_EXEC_N_2_D473 + j]};
        }
        int slot = ncache < GC_SET_CACHE ? ncache++ : (next++ % GC_SET_CACHE);
        cache[slot].set = set;
        memcpy(cache[slot].dk, dk, sizeof cache[slot].dk);
    } else {  // windows of the one import; one past it is the dummy (reads 0, as the zero buffer before the import)
        VkDescriptorBufferInfo all = gmem_info();
        for (int k = 0; k < gc_n; k++) {
            uint64_t off = (uint64_t)k << gc_shift;
            if (all.buffer == gmem_buf && gmem_buf && off < gmem_size)
                bi[k] = (VkDescriptorBufferInfo){gmem_buf, off, gmem_size - off < (SF2C_EXEC_N_1ULL_622E << gc_shift) ? gmem_size - off : SF2C_EXEC_N_1ULL_622E << gc_shift};
            else bi[k] = (VkDescriptorBufferInfo){gc_dummy, 0, SF2C_EXEC_N_4096_8B92};
            w[nw++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = GMEM_CHUNK_BINDING + k,
                                             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[k]};
        }
    }
    rec_UpdateDescriptorSets(nw, w);
    gc_set = set; gc_set_seq = seq; gc_set_gen = gc_gen; gc_set_heap = gmem_buf;
    return set;
}

// Lazy mode, a draw: the imported chunks under its vertex / fragment storage buffers, in slot order (shader.h
// GMEM_DIRECT_MAX): gd[] for the shaders' GDirect push constants (the range each GDir<k> covers), dk[] the chunks for
// the set's GDir<k> bindings. A range over two chunks gives both. Accesses there are storage buffer loads, not the table.
// LARECOMP_VK_GMEM_DIRECT=0: none (every access through the table, as before; no shader change).
static void gmem_direct_pick(uint32_t stages, uint32_t gd[GMEM_DIRECT_MAX * SF2C_EXEC_N_4_4B22], int dk[GMEM_DIRECT_MAX]) {
    static int on = -1;
    if (on < 0) {
        on = !getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_DIRECT_5127) || atoi(getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_DIRECT_5127)) != 0;
        if (!on) LOG(SF2C_EXEC_S_VK_LAZY_CHUNKS_STORAGE_BUFFE_9506);
    }
    memset(gd, 0, sizeof(uint32_t) * GMEM_DIRECT_MAX * SF2C_EXEC_N_4_4B22);
    for (int j = 0; j < GMEM_DIRECT_MAX; j++) dk[j] = -1;
    int n = 0;
    for (int stage = 0; on && stage < SF2C_EXEC_N_2_D473; stage++) {
        if (!(stages & (SF2C_EXEC_N_1U_B8E6 << stage))) continue;
        for (int i = 0; i < SF2C_EXEC_N_16_B17E && n < GMEM_DIRECT_MAX; i++) {
            uint64_t a = sbufs(stage)[i].addr, sz = sbufs(stage)[i].size;  // (overlap: a snapshotted range's copy)
            if (a < HEAP_BASE || a - HEAP_BASE >= HEAP_MAX) continue;
            uint64_t off = a - HEAP_BASE;
            int k0 = (int)(off >> gc_shift), k1 = (int)((off + (sz ? sz : 1) - 1) >> gc_shift);
            for (int k = k0; k <= k1 && k < gc_n && n < GMEM_DIRECT_MAX; k++) {
                if (!gc[k].buf) continue;
                int j = 0;
                while (j < n && dk[j] != k) j++;
                if (j < n) continue;
                dk[n] = k;
                gd[n * SF2C_EXEC_N_4_4B22 + 0] = (uint32_t)(((uint64_t)k << gc_shift) >> SF2C_EXEC_N_2_D473);
                gd[n * SF2C_EXEC_N_4_4B22 + 1] = (uint32_t)(gc_direct_bytes(k) >> SF2C_EXEC_N_2_D473);
                gd[n * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_2_D473] = (uint32_t)gc[k].addr;
                gd[n * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_3_4E07] = (uint32_t)(gc[k].addr >> SF2C_EXEC_N_32_E29C);
                n++;
            }
        }
    }
}

// Binds the chunk set for a draw / dispatch whose shaders (stages: a mask of 1 << stage) use global memory: their storage
// buffers' chunks first.
static void gmem_chunk_bind(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout layout, uint32_t set_index, uint32_t stages) {
    static int log_sb = -1;  // LARECOMP_LOG_GMEM_SBUF=1: each distinct storage buffer bound to a gmem shader, once
    if (log_sb < 0) log_sb = getenv(SF2C_EXEC_S_LARECOMP_LOG_GMEM_SBUF_407A) != NULL;
    for (int stage = 0; stage < SF2C_EXEC_N_6_E7F6; stage++) {
        if (!(stages & (SF2C_EXEC_N_1U_B8E6 << stage))) continue;
        for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
            uint64_t a = sbufs(stage)[i].addr, sz = sbufs(stage)[i].size;  // (overlap: a snapshotted range's copy - its chunk)
            if (a < HEAP_BASE || a - HEAP_BASE >= HEAP_MAX) continue;
            if (log_sb) {
                static uint64_t seen[SF2C_EXEC_N_64_A68B]; static int nseen;
                uint64_t key = a ^ (sz << SF2C_EXEC_N_40_D59E) ^ ((uint64_t)(stage * SF2C_EXEC_N_16_B17E + i) << SF2C_EXEC_N_56_7688);
                int j = 0; while (j < nseen && seen[j] != key) j++;
                if (j == nseen && nseen < SF2C_EXEC_N_64_A68B) { seen[nseen++] = key; LOG(SF2C_EXEC_S_VK_GMEM_SBUF_STAGE_D_SLOT_D__7BED, stage, i, (unsigned long long)a, (unsigned long long)sz, (int)((a - HEAP_BASE) >> gc_shift)); }
            }
            gc_touch(a - HEAP_BASE, sz);
        }
    }
    uint32_t gd[GMEM_DIRECT_MAX * SF2C_EXEC_N_4_4B22];
    int dk[GMEM_DIRECT_MAX] = {-1, -1, -1, -1};
    bool direct = gc_lazy && bp == VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (direct) gmem_direct_pick(stages, gd, dk);
    VkDescriptorSet set = gmem_chunk_set(dk);
    rec_CmdBindDescriptorSets(cb, bp, layout, set_index, 1, &set, 0, NULL);
    if (direct) rec_CmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof gd, gd);
}

// Once a frame (the present's submission): a chunk shaders missed is imported for the next frame; the lowest address
// that missed is logged (a pointer the game keeps in a uniform buffer, which no bind told us about).
static void gc_frame(void) {
    if (!gc_shift || !gc_miss || !gc_lazy) return;
    for (int k = 0; k < gc_n; k++) {
        uint32_t m = gc_miss[k], w = gc_miss[GC_MAX + k];
        if (!m) continue;
        gc_miss[k] = 0;
        gc_miss[GC_MAX + k] = SF2C_EXEC_N_0XFFFFFFFFU_9601;
        stat_gc_misses += m;
        static int nlog;
        if (nlog++ < SF2C_EXEC_N_40_D59E) LOG(SF2C_EXEC_S_VK_GUEST_HEAP_CHUNK_D_LLU_MB_76E8, k,
                             (unsigned long long)((uint64_t)k << gc_shift) >> SF2C_EXEC_N_20_F5CA, m, (unsigned long long)HEAP_BASE + ((uint64_t)w << SF2C_EXEC_N_2_D473));
        gc_touch((uint64_t)k << gc_shift, 1);
        gc_remember(k);
    }
}

// In lazy mode the heap-side offsets of a copy are in chunks: one vkCmdCopyBuffer per chunk piece. other = the GPU side.
static void gmem_copy(VkCommandBuffer cb, VkBuffer other, const VkBufferCopy *cp, int n, bool to_heap) {
    if (!gc_lazy) {
        if (to_heap) rec_CmdCopyBuffer(cb, other, gmem_buf, (uint32_t)n, cp);
        else rec_CmdCopyBuffer(cb, gmem_buf, other, (uint32_t)n, cp);
        return;
    }
    for (int i = 0; i < n; i++) {
        uint64_t h = to_heap ? cp[i].dstOffset : cp[i].srcOffset, o = to_heap ? cp[i].srcOffset : cp[i].dstOffset, left = cp[i].size;
        while (left) {
            int k = (int)(h >> gc_shift);
            uint64_t in = h - ((uint64_t)k << gc_shift), piece = (SF2C_EXEC_N_1ULL_622E << gc_shift) - in < left ? (SF2C_EXEC_N_1ULL_622E << gc_shift) - in : left;
            if (k < gc_n && (gc[k].buf || gc_import(k)) && in + piece <= gc[k].size) {
                VkBufferCopy c = to_heap ? (VkBufferCopy){o, in, piece} : (VkBufferCopy){in, o, piece};
                if (to_heap) rec_CmdCopyBuffer(cb, other, gc[k].buf, 1, &c);
                else rec_CmdCopyBuffer(cb, gc[k].buf, other, 1, &c);
            }
            h += piece; o += piece; left -= piece;
        }
    }
}

// The heap is there for the GPU over this range (one import, or lazy chunks: imported on demand).
static bool gmem_covers(uint64_t off, uint64_t size) {
    if (gc_lazy) {
        if (off + size > gmem_heap_size()) return false;
        gc_touch(off, size);
        for (int k = (int)(off >> gc_shift); k <= (int)((off + size - 1) >> gc_shift); k++) if (!gc[k].buf) return false;
        return true;
    }
    return gmem_buf && off + size <= gmem_size;
}


// Compute dispatches that use global memory: the imported heap is host memory across PCIe on a discrete GPU, and a
// shader storing to it in a loop crawls (the little chain chomps in Madam MeowMeow's house: ~8 ms a frame on a GTX 1660
// Super and on an RTX 3080 Ti alike, which the game then waits for on its fence - 49-53 fps there on the 1660). Before
// such a dispatch its bound storage buffers (sorted, merged) are copied into a device-local buffer, the address table
// (binding GMAP_BUFFER_INDEX) tells the shader (translate.c, GLSL compute) where each range sits, loads and stores
// inside them use that copy, and a shader that stores gets the ranges copied back to the heap right after it (the
// game alternates its buffer sets per frame: the CPU isn't writing them meanwhile). Accesses outside the table use
// the heap as before. LARECOMP_VK_GMEM_FAST=0: no copy (empty table: everything goes to the heap, the old behaviour).
#define GFAST_SIZE (SF2C_EXEC_N_32ULL_6898 << SF2C_EXEC_N_20_F5CA)
static VkBuffer gfast_buf;
static VmaAllocation gfast_mem;
static uint32_t gmap_tab[(GMAP_MAX + 1) * SF2C_EXEC_N_4_4B22];  // this dispatch's table (stage_set copies it into binding 20)
static VkBufferCopy gfast_cp[GMAP_MAX];       // its copies heap -> gfast_buf (reversed after a storing dispatch)
static int gfast_n;

static VkDescriptorBufferInfo gfast_info(void) {
    if (!gfast_buf) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = GFAST_SIZE,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateBuffer(g_vk.vma, &bi, &ai, &gfast_buf, &gfast_mem, NULL));
    }
    return (VkDescriptorBufferInfo){gfast_buf, 0, GFAST_SIZE};
}

static void gfast_prepare(VkCommandBuffer cb) {
    static int on = -1;
    if (on < 0) { const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_GMEM_FAST_4C7B); on = !e || atoi(e) != 0; }
    memset(gmap_tab, 0, sizeof gmap_tab);
    gfast_n = 0;
    if (!gc_lazy) gmem_info();  // (the heap import, made on first use)
    gfast_info();
    if (!on || (!gmem_buf && !gc_lazy)) return;
    struct { uint64_t lo, hi; } r[SF2C_EXEC_N_16_B17E];
    int n = 0;
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {  // heap offsets of the bound storage buffers, 256-aligned
        uint64_t a = sbufs(SF2C_EXEC_N_5_EF2D)[i].addr, sz = sbufs(SF2C_EXEC_N_5_EF2D)[i].size;  // (overlap: a snapshotted range's copy is what gets copied)
        if (!a || !sz || a < HEAP_BASE || !gmem_covers(a - HEAP_BASE, sz)) continue;
        uint64_t lo = (a - HEAP_BASE) & ~SF2C_EXEC_N_255ULL_DE76, hi = (a - HEAP_BASE + sz + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76, top = gc_lazy ? gmem_heap_size() : gmem_size;
        if (hi > top) hi = top;
        int k = n++;
        while (k > 0 && r[k - 1].lo > lo) { r[k] = r[k - 1]; k--; }  // (insertion sort by start)
        r[k].lo = lo; r[k].hi = hi;
    }
    int m = 0;
    for (int i = 0; i < n; i++) {  // merge overlapping / nearby ranges (a small gap costs less than another table entry)
        if (m && r[i].lo <= r[m - 1].hi + SF2C_EXEC_N_4096_8B92) { if (r[i].hi > r[m - 1].hi) r[m - 1].hi = r[i].hi; }
        else r[m++] = r[i];
    }
    VkBufferCopy cp[SF2C_EXEC_N_16_B17E];
    uint64_t off = 0;
    for (int i = 0; i < m; i++) {
        uint64_t len = r[i].hi - r[i].lo;
        if (m > GMAP_MAX || off + len > GFAST_SIZE) return;  // (too much: the heap as before)
        cp[i] = (VkBufferCopy){r[i].lo, off, len};
        gmap_tab[i * SF2C_EXEC_N_4_4B22 + 0] = (uint32_t)r[i].lo;
        gmap_tab[i * SF2C_EXEC_N_4_4B22 + 1] = (uint32_t)len;
        gmap_tab[i * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_2_D473] = (uint32_t)(off >> SF2C_EXEC_N_2_D473);
        off += (len + SF2C_EXEC_N_255_9556) & ~SF2C_EXEC_N_255ULL_DE76;
    }
    if (!m) return;
    if (getenv(SF2C_EXEC_S_LARECOMP_LOG_GFAST_6943)) {  // (debug) =<shader code address hex>: that shader's first tables
        static int nlog;
        if (st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr == strtoull(getenv(SF2C_EXEC_S_LARECOMP_LOG_GFAST_6943), NULL, SF2C_EXEC_N_16_B17E) && nlog++ < SF2C_EXEC_N_40_D59E) {
            LOG(SF2C_EXEC_S_GFAST_CS_LLX_D_RANGES_LLU_KB_26A0, (unsigned long long)st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr, m, (unsigned long long)(off >> SF2C_EXEC_N_10_4A44));
            for (int i = 0; i < m; i++) LOG(SF2C_EXEC_S_LLX_LLX_0B1D, (unsigned long long)(r[i].lo + HEAP_BASE), (unsigned long long)(r[i].hi - r[i].lo));
            LOG(SF2C_EXEC_S_N_D7A5);
        }
    }
    gmem_copy(cb, gfast_buf, cp, m, false);
    vk_full_barrier(cb);
    gmap_tab[GMAP_MAX * SF2C_EXEC_N_4_4B22] = (uint32_t)m;
    memcpy(gfast_cp, cp, (size_t)m * sizeof cp[0]);
    gfast_n = m;
}

// After a storing dispatch: its ranges back to the heap (before anything else reads them: draws, the CPU after the fence).
static void gfast_finish(VkCommandBuffer cb) {
    if (!gfast_n) return;
    VkBufferCopy back[GMAP_MAX];
    for (int i = 0; i < gfast_n; i++) back[i] = (VkBufferCopy){gfast_cp[i].dstOffset, gfast_cp[i].srcOffset, gfast_cp[i].size};
    vk_full_barrier(cb);
    gmem_copy(cb, gfast_buf, back, gfast_n, true);
    vk_full_barrier(cb);
    gfast_n = 0;
}

// ---- vertex / index cache
// Reading vertex / index data in place from the imported heap crosses PCIe for every vertex fetched on a discrete GPU
// (outdoors at 3x internal resolution on an RTX 3080 Ti: ~0.7-1.9 ms of GPU a frame; more on PCIe 3.0), and copying it
// per draw instead moves about as much onto the CPU. So each range bound for drawing gets a device-local copy, refreshed
// only where the CPU wrote since (the OS's page write tracking, plat_written_pages, asked at submission: vc_flush): one
// sequential GPU copy of the written pages in the submission's upload buffer (which completes before its draws) instead
// of fetches across the bus. Overlapping ranges are separate entries; the per-page generation keeps them all current.
// Pages the GPU writes itself (a compute dispatch's copy-back, gfast_finish) are never cached: draws over them read the
// heap in place as before, and so does everything when the OS gives no write tracking. LARECOMP_VK_VCACHE=0: off.
#define VC_PAGE SF2C_EXEC_VC_PAGE
#define VC_TAB SF2C_EXEC_N_8192_864A                   // entries (open addressing)
#define VC_MAX_RANGE (SF2C_EXEC_N_64ULL_DE1A << SF2C_EXEC_N_20_F5CA)    // larger bound ranges: read in place
#define VC_BUDGET (SF2C_EXEC_N_768ULL_F226 << SF2C_EXEC_N_20_F5CA)      // device-local bytes kept; the least recently used go first
typedef struct { uint64_t lo, hi, used_seq; uint32_t gen, epoch; VkBuffer buf; VmaAllocation mem; bool gpu; } VcEnt;
static VcEnt vc_tab[VC_TAB];
static uint32_t vc_pend[VC_TAB];  // entries the pending submission draws from (vc_flush brings them up to date)
static int vc_npend;
static uint32_t *vc_page_gen;  // per heap page: the generation of its last CPU write seen (vc_gen when observed)
static uint8_t *vc_page_gpu;   // per heap page: written by the GPU
static uint32_t vc_gen = 1, vc_epoch = 1;  // (epoch: bumped when pages turn GPU-written: entries check again)
static uint64_t vc_bytes;
#define VC_DEAD SF2C_EXEC_VC_DEAD
static struct { VkBuffer b; VmaAllocation m; uint64_t seq; } vc_dead[VC_DEAD];
static int vc_ndead;
static int vc_on = -1;
// (profiler: draws served / read in place, bytes refreshed, write tracking queries, written pages seen, query time)
uint64_t prof_vc_hits, prof_vc_bypass, prof_vc_copied, prof_vc_queries, prof_vc_scan_pages, prof_vc_scan_ns;

static void vc_free_dead(bool all) {
    uint64_t done = vk_completed_seq();
    int k = 0;
    for (int i = 0; i < vc_ndead; i++) {
        if (!all && vc_dead[i].seq > done) { vc_dead[k++] = vc_dead[i]; continue; }
        vmaDestroyBuffer(g_vk.vma, vc_dead[i].b, vc_dead[i].m);
    }
    vc_ndead = k;
}

static bool vc_drop(VcEnt *e) {  // (freed once the submissions that may read it are done)
    if (vc_ndead == VC_DEAD) { vk_wait_seq(vk_pending_seq() - 1); vc_free_dead(false); }
    if (vc_ndead == VC_DEAD) return false;  // (all read by the pending submission: keep it)
    vc_dead[vc_ndead].b = e->buf; vc_dead[vc_ndead].m = e->mem; vc_dead[vc_ndead++].seq = e->used_seq;
    vc_bytes -= e->hi - e->lo;
    memset(e, 0, sizeof *e);
    return true;
}

// The page tables (vc_page_gen / vc_page_gpu) exist whenever the OS gives write tracking: the vertex cache and the
// texture upload checks (vk_pages_changed) share them. Each write tracking query resets the OS bits, so every query
// stamps the pages it saw written with a new generation, and each user compares against the generation it last saw.
static int pt_state = -1;  // 1: tracking, 0: none
static bool pt_init(void) {
    if (pt_state >= 0) return pt_state;
    uint64_t pg = VC_PAGE, one;
    int n = plat_written_pages(HEAP_BASE, VC_PAGE, &one, 1, &pg);  // (is there write tracking?)
    pt_state = n >= 0 && pg == VC_PAGE;
    if (!pt_state) { LOG(SF2C_EXEC_S_VK_NO_PAGE_WRITE_TRACKING_D__E489, n, (unsigned long long)pg); return false; }
    vc_page_gen = calloc(HEAP_MAX / VC_PAGE, sizeof *vc_page_gen);
    vc_page_gpu = calloc(HEAP_MAX / VC_PAGE, 1);
    vc_page_gen[0] = ++vc_gen;  // (the probe took page 0's tracking)
    return true;
}

// Texture upload checks (textures.c): may guest memory [addr, addr + size) have changed since generation `since`?
// 0: no CPU write since, 1: written, -1: unknown (no tracking, outside the heap, or pages the GPU writes).
// *now: the generation to pass next time.
// (gpu_unknown: pages the GPU writes answer -1 - the upload checks; the storage write watch asks about the CPU alone)
static int pages_written(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now, bool gpu_unknown) {
    if (!size || addr < HEAP_BASE || addr - HEAP_BASE + size > HEAP_MAX || !pt_init()) return -1;
    uint64_t lo = (addr - HEAP_BASE) & ~(VC_PAGE - 1), hi = (addr - HEAP_BASE + size + VC_PAGE - 1) & ~(VC_PAGE - 1);
    static uint64_t pages[SF2C_EXEC_N_4096_8B92];
    uint64_t pg = VC_PAGE;
    int n = plat_written_pages(HEAP_BASE + lo, hi - lo, pages, SF2C_EXEC_N_4096_8B92, &pg);
    if (n < 0) return -1;
    uint32_t g = ++vc_gen;
    if (n >= SF2C_EXEC_N_4096_8B92) for (uint64_t p = lo / VC_PAGE; p < hi / VC_PAGE; p++) vc_page_gen[p] = g;
    else for (int k = 0; k < n; k++) vc_page_gen[(pages[k] - HEAP_BASE) / VC_PAGE] = g;
    *now = g;
    int changed = 0;
    for (uint64_t p = lo / VC_PAGE; p < hi / VC_PAGE; p++) {
        if (gpu_unknown && vc_page_gpu[p]) return -1;
        if (vc_page_gen[p] > since) changed = 1;
    }
    return changed;
}
int vk_pages_changed(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now) { return pages_written(addr, size, since, now, true); }
// The storage write watch (watch_check): CPU writes only; the GPU's own writes to the range (DMA) don't count
static int pages_cpu_written(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now) { return pages_written(addr, size, since, now, false); }

// Heap pages [lo, hi) (offsets) written by the GPU: never cached from now on.
static void vc_mark_gpu(uint64_t lo, uint64_t hi) {
    if (!pt_init()) return;
    for (uint64_t p = lo / VC_PAGE; p < (hi + VC_PAGE - 1) / VC_PAGE && p < HEAP_MAX / VC_PAGE; p++) vc_page_gpu[p] = 1;
    vc_epoch++;
}

// A storing shader (stage 0 / 1 / 5) may write any of its bound storage buffers.
static void vc_mark_sbufs(int stage) {
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) {
        uint64_t a = sbufs(stage)[i].addr, sz = sbufs(stage)[i].size;  // (overlap: the copy, where the shader wrote)
        if (a >= HEAP_BASE && sz && a - HEAP_BASE < HEAP_MAX) vc_mark_gpu(a - HEAP_BASE, a - HEAP_BASE + sz);
    }
}

static bool vcache_lookup(uint64_t addr, uint64_t size, VkBuffer *buf, VkDeviceSize *off) {
    if (vc_on < 0) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_VCACHE_1A22);
        vc_on = !e || atoi(e) != 0;
        // integrated GPU with a userptr heap (only with LARECOMP_WRITE_TRACKING=1: tracking is off there otherwise): the
        // heap already is the GPU's memory, no bus to save - the copies would only cost. LARECOMP_VK_VCACHE=1: on.
        if (vc_on && !e && gmem_userptr && g_vk.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            LOG(SF2C_EXEC_S_VK_VERTEX_CACHE_OFF_INTEGRAT_329B);
            vc_on = 0;
        }
        if (vc_on && !pt_init()) {
            LOG(SF2C_EXEC_S_VK_VERTEX_CACHE_OFF_NO_PAGE__2B13);
            vc_on = 0;
        }
    }
    if (!vc_on || (!gmem_buf && !gc_lazy) || size > VC_MAX_RANGE) return false;
    uint64_t lo = (addr - HEAP_BASE) & ~(VC_PAGE - 1), hi = (addr - HEAP_BASE + size + VC_PAGE - 1) & ~(VC_PAGE - 1);
    if (!gmem_covers(lo, hi - lo)) return false;
    uint64_t pend = vk_pending_seq();
    uint64_t h = (lo * SF2C_EXEC_N_0X9E3779B97F4A7C15ULL_8CC5) ^ (hi * SF2C_EXEC_N_0XC2B2AE3D27D4EB4FULL_ACE8);
    uint32_t slot = (uint32_t)(h ^ (h >> SF2C_EXEC_N_32_E29C)) & (VC_TAB - 1), free_slot = ~SF2C_EXEC_N_0U_86B4, old_slot = ~SF2C_EXEC_N_0U_86B4;
    VcEnt *e = NULL;
    for (int probe = 0; probe < SF2C_EXEC_N_16_B17E; probe++, slot = (slot + 1) & (VC_TAB - 1)) {
        VcEnt *c = &vc_tab[slot];
        if (c->buf && c->lo == lo && c->hi == hi) { e = c; break; }
        if (!c->buf && free_slot == ~SF2C_EXEC_N_0U_86B4) free_slot = slot;
        if (c->buf && c->used_seq < pend && (old_slot == ~SF2C_EXEC_N_0U_86B4 || c->used_seq < vc_tab[old_slot].used_seq)) old_slot = slot;
    }
    if (!e) {  // a new range: its own device-local buffer (gen 0: copied whole at the submission)
        if (vc_npend == VC_TAB) return false;
        if (vc_ndead) vc_free_dead(false);
        if (free_slot == ~SF2C_EXEC_N_0U_86B4) {
            if (old_slot == ~SF2C_EXEC_N_0U_86B4 || !vc_drop(&vc_tab[old_slot])) return false;
            free_slot = old_slot;
        }
        while (vc_bytes + (hi - lo) > VC_BUDGET) {  // over budget: the least recently used entry goes
            int lru = -1;
            for (int i = 0; i < VC_TAB; i++)
                if (vc_tab[i].buf && vc_tab[i].used_seq < pend && (lru < 0 || vc_tab[i].used_seq < vc_tab[lru].used_seq)) lru = i;
            if (lru < 0 || !vc_drop(&vc_tab[lru])) return false;
        }
        e = &vc_tab[free_slot];
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = hi - lo,
                                 .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &e->buf, &e->mem, NULL) != VK_SUCCESS) { memset(e, 0, sizeof *e); return false; }
        e->lo = lo; e->hi = hi;
        vc_bytes += hi - lo;
    }
    if (e->epoch != vc_epoch) {  // (pages turned GPU-written since: is this range one of them?)
        e->gpu = false;
        for (uint64_t p = lo / VC_PAGE; p < hi / VC_PAGE && !e->gpu; p++) e->gpu = vc_page_gpu[p];
        e->epoch = vc_epoch;
    }
    if (e->gpu) { prof_vc_bypass++; return false; }
    if (e->used_seq != pend) {  // first use in this submission: brought up to date by vc_flush before it runs
        e->used_seq = pend;
        vc_pend[vc_npend++] = (uint32_t)(e - vc_tab);
    }
    prof_vc_hits++;
    *buf = e->buf;
    *off = addr - HEAP_BASE - lo;
    return true;
}

static int vc_pend_cmp(const void *a, const void *b) {
    uint64_t x = vc_tab[*(const uint32_t *)a].lo, y = vc_tab[*(const uint32_t *)b].lo;
    return x < y ? -1 : x > y;
}

// At submission (vk_submit_extra, before the upload buffer closes): the pages the CPU wrote in the ranges this
// submission draws from - one write tracking query per run of nearby ranges (a query costs about as much as scanning
// ~64 pages: ranges closer than that share one) - and their copies into the cache, ahead of the draws.
void vc_flush(void) {
    if (!vc_npend) return;
    qsort(vc_pend, (size_t)vc_npend, sizeof vc_pend[0], vc_pend_cmp);
    uint32_t g = ++vc_gen;
    static uint64_t pages[SF2C_EXEC_N_4096_8B92];
    uint64_t t0 = SDL_GetTicksNS();
    for (int i = 0; i < vc_npend;) {
        uint64_t lo = vc_tab[vc_pend[i]].lo, hi = vc_tab[vc_pend[i]].hi;
        int j = i + 1;
        for (; j < vc_npend && vc_tab[vc_pend[j]].lo <= hi + SF2C_EXEC_N_64_A68B * VC_PAGE; j++)
            if (vc_tab[vc_pend[j]].hi > hi) hi = vc_tab[vc_pend[j]].hi;
        uint64_t pg = VC_PAGE;
        int n = plat_written_pages(HEAP_BASE + lo, hi - lo, pages, SF2C_EXEC_N_4096_8B92, &pg);
        prof_vc_queries++;
        if (n < 0 || n >= SF2C_EXEC_N_4096_8B92) for (uint64_t p = lo / VC_PAGE; p < hi / VC_PAGE; p++) vc_page_gen[p] = g;  // (all)
        else for (int k = 0; k < n; k++) vc_page_gen[(pages[k] - HEAP_BASE) / VC_PAGE] = g;
        if (n > 0) prof_vc_scan_pages += (uint64_t)n;
        i = j;
    }
    prof_vc_scan_ns += SDL_GetTicksNS() - t0;
    VkCommandBuffer cb = vk_upload_cmd();
    for (int i = 0; i < vc_npend; i++) {
        VcEnt *e = &vc_tab[vc_pend[i]];
        VkBufferCopy cp[SF2C_EXEC_N_64_A68B];
        int nc = 0;
        for (uint64_t p = e->lo / VC_PAGE; p < e->hi / VC_PAGE; p++) {
            if (e->gen && vc_page_gen[p] <= e->gen) continue;
            uint64_t o = p * VC_PAGE;
            if (nc && cp[nc - 1].srcOffset + cp[nc - 1].size == o) { cp[nc - 1].size += VC_PAGE; continue; }
            if (nc == SF2C_EXEC_N_64_A68B) { nc = 1; cp[0] = (VkBufferCopy){e->lo, 0, e->hi - e->lo}; break; }  // (scattered: the whole range)
            cp[nc++] = (VkBufferCopy){o, o - e->lo, VC_PAGE};
        }
        if (nc) {
            gmem_copy(cb, e->buf, cp, nc, false);
            for (int k = 0; k < nc; k++) prof_vc_copied += cp[k].size;
        }
        e->gen = g;
    }
    vc_npend = 0;
}

// Vertex / index data from the imported guest heap (renderer.m bind_guest reads g_guest_buffer the same way): the
// device-local cache above, else read in place - no per-draw copy, no index scan for the copy size. The game fences its
// CPU writes as on the Switch. Falls back to copies outside the heap or without the import; LARECOMP_VK_COPY_VERTICES=1
// forces copies.
static bool guest_direct(int kind, uint64_t addr, uint64_t size, VkBuffer *buf, VkDeviceSize *off) {  // kind: 0 vertex, 1 index (bind_note)
    static int copy = -1;
    if (copy < 0) copy = getenv(SF2C_EXEC_S_LARECOMP_VK_COPY_VERTICES_BDEF) != NULL;
    if (copy || addr < HEAP_BASE) return false;
    bind_note(kind, addr, size);
    if (gc_lazy) {
        uint64_t o = addr - HEAP_BASE;
        if (!size || o + size > HEAP_MAX) return false;
        int k = (int)(o >> gc_shift);
        if ((int)((o + size - 1) >> gc_shift) != k || !gmem_covers(o, size)) return false;  // (crossing chunks: copied)
        if (vcache_lookup(addr, size, buf, off)) return true;
        *buf = gc[k].buf;
        *off = o - ((uint64_t)k << gc_shift);
        return true;
    }
    if (addr + size > HEAP_BASE + gmem_size && kernel_heap_size() != gmem_tried) { gmem_tried = kernel_heap_size(); gmem_info(); }
    if (!gmem_buf || addr + size > HEAP_BASE + gmem_size) return false;
    if (vcache_lookup(addr, size, buf, off)) return true;
    *buf = gmem_buf;
    *off = addr - HEAP_BASE;
    return true;
}

static int g_cs_n;  // (compute program table below)

// In-place reboot (device.c backend_reset; GPU idle): drop what is keyed by guest addresses - programs by code
// address (the old Prog / Pipe objects are leaked, so pipeline keys holding their pointers can't collide), compute
// programs, shared copies - and the heap import: its pages are replaced (re-imported on first use).
void vk_exec_reset(void) {
    vk_guest_heap_preload_end();
    memset(prog_tab, 0, sizeof prog_tab);
    memset(pipe_tab, 0, sizeof pipe_tab);
    key_gen++;  // (the key memo: the tables' answers are gone)
    ds_gen++;   // (set reuse: the heap import below is destroyed at once)
    g_cs_n = 0;
    gcopy_gen++;
    if (gmem_buf) {
#ifdef __linux__
        bool driver_heap = gmem_dmabuf;
        heap_unmap_placed();
        // Drop the dma-buf CPU mapping before freeing its Vulkan allocation, too (not just placed maps).
        if (driver_heap && !plat_reset_guest(HEAP_BASE, HEAP_BASE + HEAP_MAX)) FATAL(SF2C_EXEC_S_CANNOT_RESTORE_THE_GUEST_HEA_BF54);
#endif
        vkDestroyBuffer(g_vk.dev, gmem_buf, NULL);
        vkFreeMemory(g_vk.dev, gmem_mem, NULL);
        gmem_buf = VK_NULL_HANDLE;
        gmem_mem = VK_NULL_HANDLE;
#ifdef __linux__
        gmem_dmabuf = false;  // (the next boot decides again)
#endif
    }
    gmem_size = gmem_tried = 0;
    for (int k = 0; k < gc_n; k++) {  // (lazy chunks: their pages are replaced too)
        if (gc[k].buf) { vkDestroyBuffer(g_vk.dev, gc[k].buf, NULL); vkFreeMemory(g_vk.dev, gc[k].mem, NULL); }
        memset(&gc[k], 0, sizeof gc[k]);
    }
    gc_imported = 0; gc_scanned = 0; gc_imported_bytes = 0; gc_gen++;  // (gc_remembered stays: the same heap layout next boot)
    gc_remembered_heap = 0;  // (imported again at the next boot's first chunk set)
    if (gc_miss) { memset(gc_miss, 0, sizeof(uint32_t) * GC_MAX); memset(gc_miss + GC_MAX, SF2C_EXEC_N_0XFF_7312, sizeof(uint32_t) * GC_MAX); }
    for (int i = 0; i < VC_TAB; i++)  // (the vertex cache: copies of the old pages)
        if (vc_tab[i].buf) vmaDestroyBuffer(g_vk.vma, vc_tab[i].buf, vc_tab[i].mem);
    memset(vc_tab, 0, sizeof vc_tab);
    vc_npend = 0;
    vc_free_dead(true);
    vc_bytes = 0;
    if (vc_page_gpu) memset(vc_page_gpu, 0, HEAP_MAX / VC_PAGE);
    vc_epoch++;
    snap_reset();  // (overlap: the storage copies' ring follows the heap, decided again once it is set up)
    st_defaults();
}

// Shadow (sampler*Shadow) slots need a depth view whose format can be compared (VUID-vkCmdDrawIndexed-None-06479):
// the colour dummy or a colour texture there is replaced by a 1x1 depth dummy at 1.0 (lit, textures.c). Each cause is
// logged once (LARECOMP_SHADOW_SLOT_LOG=1: every change of cause / handle, up to 200 lines), with the texture's
// address, size and format, so a run shows why the slot had nothing usable.
static void shadow_slot_check(int stage, const ShaderTexture *st_, int i, uint64_t h, uint32_t tic, const NvnTexEntry *e,
                              NvnTexture *t, VkImageView *view) {
    VkImageView dummy = vk_dummy_view(st_->type);
    if (*view && *view != dummy && t && vk_tex_depth_compare(t)) return;
    enum { HANDLE0, NO_ENTRY, FINALIZED, UNREGISTERED, MULTISAMPLE, FEEDBACK_ARRAY, VIEW_FAILED, TYPE_MISMATCH,
           DEPTH_NO_COMPARE, COLOUR, NCAUSE };
    static const char *names[NCAUSE] = {SF2C_EXEC_S_HANDLE_0_NOTHING_BOUND_A847, SF2C_EXEC_S_HANDLE_PAST_THE_TEXTURE_POOL_3BCE, SF2C_EXEC_S_TEXTURE_FINALIZED_813D,
                                        SF2C_EXEC_S_POOL_SLOT_NOT_REGISTERED_7D5C, SF2C_EXEC_S_MULTISAMPLE_TEXTURE_B9EE, SF2C_EXEC_S_FEEDBACK_SNAPSHOT_AS_ARRAY_80B7,
                                        SF2C_EXEC_S_VIEW_CREATION_FAILED_FBD3, SF2C_EXEC_S_TYPE_MISMATCH_70BF, SF2C_EXEC_S_DEPTH_FORMAT_WITHOUT_COMPARE_0C52,
                                        SF2C_EXEC_S_COLOUR_FORMAT_34ED};
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    int why;
    if (!h) why = HANDLE0;
    else if (!e) why = NO_ENTRY;
    else if (!t) why = e->dead_format ? FINALIZED : UNREGISTERED;
    else if (*view == dummy || !*view) {
        bool fb = false;
        for (int k = 0; k < nfeedback; k++) fb |= feedback_tex[k] == e->tex;
        why = b && b->samples > 1 ? MULTISAMPLE : fb && st_->type == TEXTYPE_2D_ARRAY ? FEEDBACK_ARRAY : !*view ? VIEW_FAILED : TYPE_MISMATCH;
    } else why = b && (b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? DEPTH_NO_COMPARE : COLOUR;
    static int verbose = -1, nverbose;
    static bool logged[NCAUSE];
    static int last_why = -1;
    static uint64_t last_h;
    if (verbose < 0) verbose = getenv(SF2C_EXEC_S_LARECOMP_SHADOW_SLOT_LOG_6907) != NULL;
    bool log = !logged[why] || (verbose && nverbose < SF2C_EXEC_N_200_27BA && (why != last_why || h != last_h));
    if (log) {
        logged[why] = true;
        if (verbose) nverbose++;
        last_why = why; last_h = h;
        if (t)
            LOG(SF2C_EXEC_S_VK_SHADOW_SLOT_T_D_STAGE_D_T_10AC
                SF2C_EXEC_S_TARGET_U_NVN_FMT_X_VK_FMT_D__0EE8,
                i, stage, st_->type, (unsigned long long)vk_frame_counter(), (unsigned long long)g_cur_fs_hash, names[why],
                (unsigned long long)h, tic, (unsigned long long)t->addr, t->width, t->height, t->depth, t->target, t->format,
                b ? b->format : 0, b ? b->aspect : 0, b ? b->samples : 0, verbose ? SF2C_EXEC_S_EMPTY_12AE : SF2C_EXEC_S_ONCE_PER_CAUSE_A949);
        else
            LOG(SF2C_EXEC_S_VK_SHADOW_SLOT_T_D_STAGE_D_T_4B2B
                SF2C_EXEC_S_1X1_DEPTH_DUMMY_INSTEAD_S_N_30E3, i, stage, st_->type, (unsigned long long)vk_frame_counter(),
                (unsigned long long)g_cur_fs_hash, names[why], (unsigned long long)h, tic, e && e->dead_format ? e->dead_format - 1 : 0,
                verbose ? SF2C_EXEC_S_EMPTY_12AE : SF2C_EXEC_S_ONCE_PER_CAUSE_A949);
    }
    *view = vk_dummy_depth_view(st_->type);
}

static void texture_info(int stage, ShaderInfo *si, int i, VkDescriptorImageInfo *out, float sc[SF2C_EXEC_N_2_D473], uint32_t wh[SF2C_EXEC_N_2_D473], float fr[SF2C_EXEC_N_2_D473]) {
    ShaderTexture *st_ = &si->tex[i];
    int slot = ((int)st_->handle_offset - SF2C_EXEC_N_0X20_A766) / SF2C_EXEC_N_8_2C62;  // 64-bit handles from offset 0x20
    uint64_t h = (slot >= 0 && slot < SF2C_EXEC_N_32_E29C) ? st.tex[stage][slot] : 0;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler smp = VK_NULL_HANDLE;
    uint32_t tic = h & SF2C_EXEC_N_0XFFFFF_9137, tsc = (uint32_t)(h >> SF2C_EXEC_N_20_F5CA) & SF2C_EXEC_N_0XFFF_1DF4;
    sc[0] = sc[1] = 1;
    wh[0] = wh[1] = 0;
    fr[0] = fr[1] = 1;
    NvnTexEntry *ent = NULL;  // (shadow slots: what the view came from)
    if (h && st.tpool && tic < (uint32_t)st.tpool->count) {
        NvnTexEntry *e = ent = &((NvnTexEntry *)st.tpool->entries)[tic];
        if (e->tex) {
            // Per-frame memo: ~2400 lookups per overworld frame recomputed the view (format / range math, cache
            // probe). The first lookup of a frame still runs vk_view (upload check); a view that may have been
            // destroyed (eviction, image re-created) bumps vk_view_gen. LARECOMP_VK_NO_TEX_MEMO=1 disables.
            extern uint32_t vk_view_gen;
            extern uint64_t vk_frame_counter(void);
            static int nomemo = -1;
            if (nomemo < 0) nomemo = getenv(SF2C_EXEC_S_LARECOMP_VK_NO_TEX_MEMO_FD78) != NULL;
            static struct { NvnTexture *tex; uint64_t vh, frame; uint32_t gen; uint8_t type, has_view; VkImageView view; } memo[SF2C_EXEC_N_1024_E39E];
            uint64_t vh = e->has_view ? hash_words(0, &e->view, sizeof e->view) : 0, frame = vk_frame_counter();
            uint32_t ms = (uint32_t)((((uintptr_t)e->tex >> SF2C_EXEC_N_4_4B22) ^ vh ^ (uint64_t)st_->type * SF2C_EXEC_N_0X9E3779B97F4A7C15ULL_8CC5) & SF2C_EXEC_N_1023_6629);
            if (!nomemo && memo[ms].tex == e->tex && memo[ms].vh == vh && memo[ms].frame == frame && memo[ms].gen == vk_view_gen &&
                memo[ms].type == st_->type && memo[ms].has_view == e->has_view) {
                view = memo[ms].view;
            } else {
                view = vk_view(naliases ? alias_read(e->tex) : e->tex, &e->view, e->has_view, false, st_->type);
                memo[ms].tex = e->tex; memo[ms].vh = vh; memo[ms].frame = frame; memo[ms].gen = vk_view_gen;
                memo[ms].type = (uint8_t)st_->type; memo[ms].has_view = e->has_view; memo[ms].view = view;
            }
            renderer_texture_scale(e->tex, &sc[0], &sc[1]);
            renderer_texture_fraction(e->tex, &fr[0], &fr[1]);
            wh[0] = e->tex->width; wh[1] = e->tex->height;
            for (int k = 0; k < nfeedback; k++)
                if (e->tex == feedback_tex[k] && (st_->type == TEXTYPE_2D || st_->type == TEXTYPE_2D_ARRAY)) {
                    view = feedback_view[k];
                    if (st_->type == TEXTYPE_2D_ARRAY) view = vk_dummy_view(TEXTYPE_2D_ARRAY);  // (2D snapshot as array: rare)
                }
        } else if (e->dead_format) {
            nvn_dead_slot_sampled(e, tic);
        }
    }
    if (h && st.spool && tsc < (uint32_t)st.spool->count) smp = vk_sampler(&((NvnSamplerDesc *)st.spool->entries)[tsc]);
    if (st_->shadow)
        shadow_slot_check(stage, st_, i, h, tic, ent, ent && ent->tex ? (naliases ? alias_read(ent->tex) : ent->tex) : NULL, &view);
    if (!view) view = vk_dummy_view(st_->type);
    if (!smp) smp = vk_default_sampler(true);
    *out = (VkDescriptorImageInfo){smp, view, VK_IMAGE_LAYOUT_GENERAL};
}

// Writes the descriptor set of one stage. gx..gz: compute group counts (driver constants).
// pre_ii: texture descriptors already resolved by the caller (draw: fragment textures, needed earlier for the
// viewport decision), or NULL.

// (set reuse) what a set holds, one entry per descriptor written, in stage_set's order (fixed per program)
typedef struct { uint32_t binding, type; uint64_t a, b, c; } SetEnt;
typedef struct SetCache { uint64_t seq; uint32_t view_gen, ds_gen; int n; VkDescriptorSet set; SetEnt e[MAX_BIND]; } SetCache;

// dyn / ndyn: the set's dynamic offsets are appended at dyn[*ndyn] (binding order, as vkCmdBindDescriptorSets takes
// them); NULL for programs without dynamic buffers (compute).
static VkDescriptorSet stage_set(Prog *p, VkDescriptorSetLayout layout, uint32_t set, int stage, ShaderInfo *si, const float *rscale_row0,
                                 uint32_t gx, uint32_t gy, uint32_t gz, const VkDescriptorImageInfo *pre_ii,
                                 uint32_t *dyn, uint32_t *ndyn) {
    VkWriteDescriptorSet w[MAX_BIND];
    VkDescriptorBufferInfo bi[MAX_BIND];
    VkDescriptorImageInfo ii[MAX_BIND];
    uint32_t dbind[MAX_BIND], doff[MAX_BIND];
    int n = 0, nd = 0;
    int sg = stage == 1 ? 1 : 0;
    bool lean = cbuf_lean();
    float rs[SF2C_EXEC_N_65_108C][SF2C_EXEC_N_4_4B22];  // (lean: only rows 0 and 1..ntex are set and read)
    if (!lean) memset(rs, 0, sizeof rs);
    rs[0][0] = rs[0][1] = 1; rs[0][SF2C_EXEC_N_2_D473] = rs[0][SF2C_EXEC_N_3_4E07] = 0;
    if (rscale_row0) memcpy(rs[0], rscale_row0, SF2C_EXEC_N_16_B17E);
    for (int i = 0; i < p->nbinds; i++) {
        VkBindingInfo *b = &p->binds[i];
        if (b->set != set) continue;
        w[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding,
                                      .descriptorCount = 1, .descriptorType = b->type};
        if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && b->binding >= SF2C_EXEC_N_32_E29C) {
            int ti = (int)b->binding - SF2C_EXEC_N_32_E29C;
            float sc[SF2C_EXEC_N_2_D473], fr[SF2C_EXEC_N_2_D473]; uint32_t wh[SF2C_EXEC_N_2_D473];
            if (ti < si->ntex && pre_ii && ti < SF2C_EXEC_N_64_A68B) {
                ii[n] = pre_ii[ti]; sc[0] = tex_sc[sg][ti][0]; sc[1] = tex_sc[sg][ti][1]; wh[0] = tex_wh[sg][ti][0]; wh[1] = tex_wh[sg][ti][1];
                fr[0] = tex_fr[sg][ti][0]; fr[1] = tex_fr[sg][ti][1];
            }
            else if (ti < si->ntex) texture_info(stage, si, ti, &ii[n], sc, wh, fr);
            else { ii[n] = (VkDescriptorImageInfo){vk_default_sampler(true), vk_dummy_view(TEXTYPE_2D), VK_IMAGE_LAYOUT_GENERAL}; sc[0] = sc[1] = 1; wh[0] = wh[1] = 0; fr[0] = fr[1] = 1; }
            if (ti < SF2C_EXEC_N_64_A68B) {
                tex_sc[sg][ti][0] = sc[0]; tex_sc[sg][ti][1] = sc[1];
                tex_wh[sg][ti][0] = wh[0]; tex_wh[sg][ti][1] = wh[1];
                tex_fr[sg][ti][0] = fr[0]; tex_fr[sg][ti][1] = fr[1];
            }
            w[n].pImageInfo = &ii[n];
        } else if (is_ubo(b->type) && b->binding < SF2C_EXEC_N_17_4523) {
            bi[n] = cbuf_info(stage, si, (int)b->binding, gx, gy, gz);
            w[n].pBufferInfo = &bi[n];
        } else if (b->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            bi[n] = b->binding == GFAST_BUFFER_INDEX ? gfast_info() : gmem_info();
            w[n].pBufferInfo = &bi[n];
        } else {
            continue;  // rscale: written below, once the textures (and their scales) are known
        }
        n++;
    }
    for (int i = 0; i < p->nbinds; i++) {
        VkBindingInfo *b = &p->binds[i];
        if (b->set != set || !is_ubo(b->type) || (b->binding != SF2C_EXEC_N_18_4EC9 && b->binding != GMAP_BUFFER_INDEX)) continue;
        w[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding,
                                      .descriptorCount = 1, .descriptorType = b->type};
        if (b->binding == GMAP_BUFFER_INDEX) {  // (compute) this dispatch's fast-copy table: gfast_prepare
            bi[n] = ubo_span(vk_transient_copy(gmap_tab, sizeof gmap_tab, SF2C_EXEC_N_256_51E8), sizeof gmap_tab);
        } else if (b->binding == SF2C_EXEC_N_18_4EC9) {
            for (int t = 0; t < si->ntex && t < SF2C_EXEC_N_64_A68B; t++) {  // scale x, y, content part x, y
                rs[1 + t][0] = tex_sc[sg][t][0]; rs[1 + t][1] = tex_sc[sg][t][1]; rs[1 + t][SF2C_EXEC_N_2_D473] = tex_fr[sg][t][0]; rs[1 + t][SF2C_EXEC_N_3_4E07] = tex_fr[sg][t][1];
            }
            if (lean) bi[n] = ubo_span(rscale_dedupe(sg, (const float (*)[SF2C_EXEC_N_4_4B22])rs, si->ntex < SF2C_EXEC_N_64_A68B ? si->ntex : SF2C_EXEC_N_64_A68B), sizeof rs);
            else bi[n] = ubo_span(copy_dedupe(sg ? DEDUPE_RSCALE_F : DEDUPE_RSCALE_V, rs, sizeof rs, SF2C_EXEC_N_256_51E8), sizeof rs);
        }
        w[n].pBufferInfo = &bi[n];
        n++;
    }
    // dynamic buffers: the offset moves to the bind (the descriptor keeps buffer and range)
    for (int i = 0; i < n; i++)
        if (w[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            int j = nd++;  // (insertion by binding: the order vkCmdBindDescriptorSets takes the offsets in)
            while (j > 0 && dbind[j - 1] > w[i].dstBinding) { dbind[j] = dbind[j - 1]; doff[j] = doff[j - 1]; j--; }
            dbind[j] = w[i].dstBinding;
            doff[j] = (uint32_t)bi[i].offset;
            bi[i].offset = 0;
        }
    if (nd) {
        if (!dyn || !ndyn) FATAL(SF2C_EXEC_S_STAGE_SET_DYNAMIC_BUFFERS_WI_32BF);
        memcpy(dyn + *ndyn, doff, (size_t)nd * sizeof doff[0]);
        *ndyn += (uint32_t)nd;
    }
    extern uint32_t vk_view_gen;
    SetCache *c = NULL;
    SetEnt e[MAX_BIND];
    if (p->reuse && set < SF2C_EXEC_N_2_D473) {  // the same contents as this program's last set of this index: bind that one again
        if (!p->scache[set]) p->scache[set] = calloc(1, sizeof *p->scache[set]);
        c = p->scache[set];
        memset(e, 0, (size_t)n * sizeof e[0]);  // (padding: compared with memcmp)
        for (int i = 0; i < n; i++) {
            e[i].binding = w[i].dstBinding;
            e[i].type = (uint32_t)w[i].descriptorType;
            if (w[i].pImageInfo) {
                e[i].a = (uint64_t)(uintptr_t)ii[i].sampler; e[i].b = (uint64_t)(uintptr_t)ii[i].imageView; e[i].c = (uint64_t)ii[i].imageLayout;
            } else {
                e[i].a = (uint64_t)(uintptr_t)bi[i].buffer; e[i].b = bi[i].offset; e[i].c = bi[i].range;
            }
        }
        if (c->set && c->seq == vk_pending_seq() && c->view_gen == vk_view_gen && c->ds_gen == ds_gen && c->n == n &&
            !memcmp(c->e, e, (size_t)n * sizeof e[0])) {
            prof_sets_reused++;
            return c->set;
        }
    }
    VkDescriptorSet ds = vk_alloc_set(layout);
    for (int i = 0; i < n; i++) w[i].dstSet = ds;
    if (n) rec_UpdateDescriptorSets((uint32_t)n, w);
    prof_sets_new++;
    if (c) {
        c->set = ds; c->seq = vk_pending_seq(); c->view_gen = vk_view_gen; c->ds_gen = ds_gen; c->n = n;
        memcpy(c->e, e, (size_t)n * sizeof e[0]);
    }
    return ds;
}

// ================================================================== compute
typedef struct { Prog p; ShaderInfo *si; VkPipeline pipe; uint32_t local[SF2C_EXEC_N_3_4E07]; uint64_t code; } CsProg;

static void build_compute(void *arg) {
    CsProg *cp = arg;
    uint64_t t0 = SDL_GetTicksNS();
    int bs = build_begin(SF2C_EXEC_S_PIPELINE_CS_366C, cp->code, 0);
    size_t n = 0;
    uint32_t *spv = vk_glsl_to_spirv(cp->si->msl, STAGE_COMPUTE, SF2C_EXEC_S_CS_DE6E, &n);
    if (!spv) { atomic_store(&cp->p.state, SF2C_EXEC_N_2_D473); build_end(bs); return; }
    cp->p.nbinds = vk_reflect(spv, n, cp->p.binds, MAX_BIND);
    VkShaderModule m = vk_module(spv, n);
    free(spv);
    if (!m) {
        LOG(SF2C_EXEC_S_COMPUTE_CS_LLX_SHADER_MODULE_70DB, (unsigned long long)cp->code);
        atomic_store(&cp->p.state, SF2C_EXEC_N_2_D473);
        build_end(bs);
        return;
    }
    cp->p.layout = make_layout(&cp->p, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0);
    VkSpecializationMapEntry me[SF2C_EXEC_N_3_4E07] = {{0, 0, SF2C_EXEC_N_4_4B22}, {1, SF2C_EXEC_N_4_4B22, SF2C_EXEC_N_4_4B22}, {SF2C_EXEC_N_2_D473, SF2C_EXEC_N_8_2C62, SF2C_EXEC_N_4_4B22}};
    VkSpecializationInfo spec = {SF2C_EXEC_N_3_4E07, me, SF2C_EXEC_N_12_6B51, cp->local};
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = SF2C_EXEC_S_MAIN_E4A9,
                                                .pSpecializationInfo = &spec},
                                      .layout = cp->p.layout};
    SLOW_FEEDBACK(ci);
    VkResult r = vkCreateComputePipelines(g_vk.dev, g_vk.pcache, 1, &ci, NULL, &cp->pipe);
    SLOW_FEEDBACK_HIT();
    atomic_fetch_add(&stat_compiles, 1);
    atomic_store(&cp->p.state, r == VK_SUCCESS ? 1 : SF2C_EXEC_N_2_D473);
    build_end(bs);
    if (r != VK_SUCCESS) LOG(SF2C_EXEC_S_COMPUTE_CS_LLX_PIPELINE_FAIL_1537, (unsigned long long)cp->code, r);
    slow_build(t0, SF2C_EXEC_S_PIPELINE_CS_366C, cp->code, 0);
}

static struct CsEnt { uint64_t addr; CsProg *cp; } g_cs_tab[SF2C_EXEC_N_256_51E8];  // compute programs by code address

static uint64_t cs_content_key(uint64_t code_hash, const uint32_t local[SF2C_EXEC_N_3_4E07]) {
    uint64_t h = code_hash;
    for (int i = 0; i < SF2C_EXEC_N_3_4E07; i++) { h ^= local[i]; h *= SF2C_EXEC_N_0X100000001B3ULL_3E10; }
    return h;
}

static bool translate_compute(CsProg *cp, const uint8_t *code, size_t max, uint64_t addr) {
    maxwell_gmem_base = HEAP_BASE;
    maxwell_gmem_size = HEAP_MAX;
    maxwell_gmem_chunk_shift = gmem_chunk_shift();
    ShaderKey k = {0};
    k.glsl = 1;
    pthread_mutex_lock(&translate_lock);
    cp->si = maxwell_translate(code, max, STAGE_COMPUTE, &k);
    pthread_mutex_unlock(&translate_lock);
    cp->p.vi = cp->si;
    if (!cp->si->msl) {
        LOG(SF2C_EXEC_S_COMPUTE_TRANSLATE_FAILED_S_C_04BB, cp->si->error, (unsigned long long)addr);
        atomic_store(&cp->p.state, SF2C_EXEC_N_2_D473);
        return false;
    }
    return true;
}

static CsProg *compute_program(NvnProgram *np) {
    NvnShaderStage *ss = &np->stages[SF2C_EXEC_N_5_EF2D];
    if (!ss->code_addr) return NULL;
    struct CsEnt *tab = g_cs_tab;
    int n = g_cs_n;
    for (int i = 0; i < n; i++)
        if (tab[i].addr == ss->code_addr) {
            shader_fp_check(&tab[i].cp->p.fp[0], ss, SF2C_EXEC_S_CS_DE6E);  // (keyed by code address: shader_prep.h)
            return tab[i].cp;
        }
    uint32_t local[SF2C_EXEC_N_3_4E07];
    for (int i = 0; i < SF2C_EXEC_N_3_4E07; i++) {
        local[i] = ss->control ? *(const uint32_t *)(ss->control + SF2C_EXEC_N_0X748_1509 + SF2C_EXEC_N_4_4B22 * i) : 1;
        if (!local[i]) local[i] = 1;
    }
    if (shader_rec_recording()) {
        ShaderRec rec = {.kind = SF2C_EXEC_CH_C_809F, .vs = shader_stage_hash(ss), .local = {local[0], local[1], local[SF2C_EXEC_N_2_D473]}};
        shader_rec_note(&rec);
    }
    uint64_t ch = shader_stage_hash(ss);
    CsProg *cp = ch && prep_cs_n ? PREP_GET(prep_cs, cs_content_key(ch, local)) : NULL;  // prepared before the game
    if (cp) {
        if (n < SF2C_EXEC_N_256_51E8) { tab[n].addr = ss->code_addr; tab[n].cp = cp; g_cs_n = n + 1; }
        shader_fp_check(&cp->p.fp[0], ss, SF2C_EXEC_S_CS_DE6E);
        return cp;
    }
    shader_first_visit(SF2C_EXEC_S_COMPUTE_9B08, ch, 0, cs_content_key(ch, local));
    cp = calloc(1, sizeof *cp);
    shader_fp_check(&cp->p.fp[0], ss, SF2C_EXEC_S_CS_DE6E);  // (takes the fingerprint)
    cp->code = ss->code_addr;  // (for the build logs)
    if (n < SF2C_EXEC_N_256_51E8) { tab[n].addr = ss->code_addr; tab[n].cp = cp; g_cs_n = n + 1; }
    memcpy(cp->local, local, sizeof local);
    if (!translate_compute(cp, (const uint8_t *)(uintptr_t)ss->code_addr, ss->const_offset ? ss->const_offset : SF2C_EXEC_N_0X10000_4939, ss->code_addr))
        return cp;
    save_glsl(cp->si->msl, ss->code_addr, SF2C_EXEC_S_CS_DE6E);
    run_job(build_compute, cp);
    return cp;
}

// ================================================================== shader preparation (shader_prep.h)
// Each record: translated here (one thread, the translator isn't re-entrant), compiled and built on the worker
// threads (run_job), kept in the prep_* tables for the game.
typedef struct { void (*fn)(void *); void *arg; } PrepJob;
static void prep_job_run(void *a) {
    PrepJob *j = a;
    j->fn(j->arg);
    free(j);
    prep_pending--;
}
static void prep_job(void (*fn)(void *), void *arg) {
    PrepJob *j = malloc(sizeof *j);
    j->fn = fn, j->arg = arg;
    prep_pending++;
    run_job(prep_job_run, j);
}

static _Atomic int prep_pipe_dups;  // records whose pipeline another record already made
bool renderer_prepare_begin(void) { return g_vk.dev != VK_NULL_HANDLE; }
int renderer_prepare_pending(void) { return prep_pending; }
static void gpl_probe(void);
static void gpl_probe_note(Pipe *pp);
void renderer_prepare_end(void) {
    vk_pipeline_cache_save();  // (otherwise only kept at exit)
    static bool logged;        // (the counts: once per process, the background preparation runs once)
    if (!logged) {
        logged = true;
        pthread_mutex_lock(&prep_lock);
        size_t n = prep_pipes_n;
        pthread_mutex_unlock(&prep_lock);
        LOG(SF2C_EXEC_S_VK_PREPARED_ZU_PIPELINES_D_R_526A, n, atomic_load(&prep_pipe_dups),
            g_vk.vids ? SF2C_EXEC_S_DYNAMIC_68ED : SF2C_EXEC_S_IN_EACH_PIPELINE_08AD);
        if (g_vk.gpl_probe) gpl_probe();
    }
}

typedef struct { Pipe *pp; ShaderRec r; } PrepPipe;
static void prep_pipeline(void *arg) {
    PrepPipe *a = arg;
    Prog *pr = a->pp->k.prog;
    // its program's compile was queued before (the queue is first in, first out): wait for it to finish
    while (atomic_load(&pr->state) == 0) { struct timespec ts = {0, SF2C_EXEC_N_1000000_6CCE}; nanosleep(&ts, NULL); }
    if (atomic_load(&pr->state) != 1) { atomic_store(&a->pp->state, SF2C_EXEC_N_2_D473); free(a); return; }
    const ShaderRec *r = &a->r;
    // the pass's attachment formats as begin_pass makes them for these targets
    VkFormat color[SF2C_EXEC_N_8_2C62] = {0}, depth = VK_FORMAT_UNDEFINED, stencil = VK_FORMAT_UNDEFINED;
    uint32_t ncolor = r->ncolor;
    for (uint32_t i = 0; i < ncolor && i < SF2C_EXEC_N_8_2C62; i++) {
        if (!r->color[i]) continue;
        VkFormat b = vk_image_format(r->color[i], NULL);
        color[i] = r->cview[i] && vk_format(r->color[i]) == b ? vk_format(r->cview[i]) : b;
    }
    while (ncolor && !r->color[ncolor - 1]) ncolor--;
    if (r->depth && renderer_format_is_depth(r->depth)) {
        VkImageAspectFlags asp;
        VkFormat b = vk_image_format(r->depth, &asp);
        if (asp & VK_IMAGE_ASPECT_DEPTH_BIT) depth = b;
        if (asp & VK_IMAGE_ASPECT_STENCIL_BIT) stencil = b;
    }
    const PipeIn in = {r->attrib, r->nattrib, r->stream, r->nstream, r->color, r->channel_mask, r->blend_on, r->blend,
                       r->a2c, r->topo, r->poly_fill};
    pipe_key_fill(&a->pp->k, pr, &in, color, ncolor, depth, stencil, r->samples ? r->samples : 1, NULL);
    uint64_t ph = hash_words(SF2C_EXEC_N_0XCBF29CE484222325ULL_7EE8, &a->pp->k, sizeof a->pp->k);
    if (!ph) ph = 1;
    // Another record makes the same pipeline (with dynamic vertex input: every record differing only in its vertex
    // layout): built once. Claimed before the build, so records on other worker threads at the same time see it too
    // (the game skips a draw whose prepared pipeline is still building or failed, as with one it builds itself).
    pthread_mutex_lock(&prep_lock);
    bool dup = prep_get(prep_pipes, prep_pipes_cap, ph) != NULL;
    if (!dup) prep_put(&prep_pipes, &prep_pipes_cap, &prep_pipes_n, ph, a->pp);
    pthread_mutex_unlock(&prep_lock);
    if (dup) { atomic_fetch_add(&prep_pipe_dups, 1); free(a->pp); free(a); return; }
    build_pipeline(a->pp);
    if (g_vk.gpl_probe) gpl_probe_note(a->pp);
    free(a);
}

bool renderer_prepare(const ShaderRec *r, const ShaderBlob *vsb, const ShaderBlob *fsb) {
    if (r->kind == SF2C_EXEC_CH_C_809F) {
        uint64_t ck = cs_content_key(r->vs, r->local);
        if (PREP_GET(prep_cs, ck)) return true;
        CsProg *cp = calloc(1, sizeof *cp);
        cp->code = r->vs;  // (prepared: its content hash, for the build logs)
        memcpy(cp->local, r->local, sizeof cp->local);
        if (!translate_compute(cp, vsb->code, vsb->const_offset ? vsb->const_offset : SF2C_EXEC_N_0X10000_4939, 0)) return false;
        PREP_PUT(prep_cs, ck, cp);
        prep_job(build_compute, cp);
        return true;
    }
    // the keys the draw path makes for this state (draw_)
    ShaderKey vkey = {0}, fkey = {0};
    vkey.glsl = fkey.glsl = 1;
    vkey.flip_y = fkey.flip_y = (r->flags & SR_FLIP_Y) != 0;
    vkey.depth_minus_one = (r->flags & SR_DEPTH_MINUS_ONE) != 0;
    for (int i = 0; i < r->nattrib; i++) vkey.attr_kind[i] = (uint8_t)attr_kind(r->attrib[i] & SF2C_EXEC_N_0XFF_7312);
    for (int i = 0; i < r->ncolor && i < SF2C_EXEC_N_8_2C62; i++) if (r->color[i]) fkey.rt_kind[i] = (uint8_t)rt_kind(r->color[i]);
    uint64_t ck = prog_content_key(r->vs, r->fs, &vkey, &fkey);
    Prog *p = PREP_GET(prep_progs, ck);
    if (!p) {
        p = calloc(1, sizeof *p);
        p->code[0] = (uintptr_t)vsb->code;
        p->code[1] = (uintptr_t)fsb->code;
        p->cmax[0] = vsb->const_offset ? vsb->const_offset : SF2C_EXEC_N_0X10000_4939;
        p->cmax[1] = fsb->const_offset ? fsb->const_offset : SF2C_EXEC_N_0X10000_4939;
        p->vkey = vkey;
        p->fkey = fkey;
        p->prep_vh = r->vs, p->prep_fh = r->fs;
        bool ok = translate_program_now(p);
        p->code[0] = p->code[1] = 0;  // (the game files' copies are gone after this call)
        PREP_PUT(prep_progs, ck, p);  // (failed ones too: the game then skips them as it would its own)
        if (!ok) return false;
        prep_job(compile_program, p);
    }
    if (atomic_load(&p->state) == SF2C_EXEC_N_2_D473) return false;
    PrepPipe *a = calloc(1, sizeof *a);
    a->pp = calloc(1, sizeof *a->pp);
    a->pp->k.prog = p;
    a->r = *r;
    prep_job(prep_pipeline, a);
    return true;
}

// ================================================================== graphics pipeline library probe (measurement only)
// LARECOMP_VK_GPL_PROBE=1 (or =<N>): after shader preparation, the first N prepared pipelines (20; in the order the
// list's records built them) are built again three ways and timed, on the preparing thread, one at a time:
//   full:      one vkCreateGraphicsPipelines, as the game builds them;
//   libraries: the four VK_EXT_graphics_pipeline_library parts (vertex input, pre-rasterization with the vertex shader,
//              fragment shader, fragment output) plus a fast link (no link-time optimization);
//   optimized: the same parts linked again with link-time optimization (what would replace a fast-linked pipeline).
// Every build is cold: no pipeline cache, and a specialization constant no shader reads (a new value per build and
// run) makes each one new to the driver's own caches. The results are destroyed; only [gpl] log lines remain.
// Nothing else uses the extension: the device enables it only with this variable set (device.c).
#ifdef VK_EXT_graphics_pipeline_library
#define GPL_PROBE_MAX SF2C_EXEC_GPL_PROBE_MAX
static Pipe *gpl_sample[GPL_PROBE_MAX];
static int gpl_nsample;
static void gpl_probe_note(Pipe *pp) {
    if (atomic_load(&pp->state) != 1) return;
    pthread_mutex_lock(&prep_lock);
    if (gpl_nsample < GPL_PROBE_MAX) gpl_sample[gpl_nsample++] = pp;
    pthread_mutex_unlock(&prep_lock);
}

static double gpl_ms_since(uint64_t t0) { return (double)(SDL_GetTicksNS() - t0) / SF2C_EXEC_N_1E6_8C96; }

// One pipeline: [0] full, [1] vertex input, [2] pre-rasterization, [3] fragment shader, [4] fragment output, [5] fast
// link, [6] optimized link (ms). false: a build failed (logged).
static bool gpl_probe_one(const PipeKey *k, uint32_t nonce, double t[SF2C_EXEC_N_7_7902]) {
    PipeCI c;
    pipe_ci_fill(&c, k);
    VkSpecializationMapEntry me = {SF2C_EXEC_N_0X7FFF_D189, 0, SF2C_EXEC_N_4_4B22};  // (no shader declares this id: it only makes the build unique)
    VkSpecializationInfo spec = {1, &me, SF2C_EXEC_N_4_4B22, &nonce};
    c.stages[0].pSpecializationInfo = c.stages[1].pSpecializationInfo = &spec;
    VkPipeline full = VK_NULL_HANDLE, lib[SF2C_EXEC_N_4_4B22] = {0}, fast = VK_NULL_HANDLE, opt = VK_NULL_HANDLE;
    const char *fail = NULL;
    VkResult r;
    uint64_t t0 = SDL_GetTicksNS();
    r = vkCreateGraphicsPipelines(g_vk.dev, VK_NULL_HANDLE, 1, &c.pi, NULL, &full);
    t[0] = gpl_ms_since(t0);
    if (r != VK_SUCCESS) { fail = SF2C_EXEC_S_FULL_1E59; goto out; }
    nonce ^= SF2C_EXEC_N_0X80000000U_AE05;  // (the libraries: new to the caches too, not the full pipeline's shaders again)
    static const VkGraphicsPipelineLibraryFlagsEXT part[SF2C_EXEC_N_4_4B22] = {
        VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
        VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT};
    static const char *part_name[SF2C_EXEC_N_4_4B22] = {SF2C_EXEC_S_VERTEX_INPUT_65B5, SF2C_EXEC_S_PRE_RASTERIZATION_E983, SF2C_EXEC_S_FRAGMENT_SHADER_CDAB, SF2C_EXEC_S_FRAGMENT_OUTPUT_FFA0};
    for (int i = 0; i < SF2C_EXEC_N_4_4B22; i++) {
        VkGraphicsPipelineLibraryCreateInfoEXT li = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
                                                     .pNext = &c.ri, .flags = part[i]};
        VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &li,
            .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
            .pDynamicState = &c.dy, .layout = c.pi.layout};
        switch (i) {
        case 0: pi.pVertexInputState = c.pi.pVertexInputState; pi.pInputAssemblyState = &c.ia; break;
        case 1: pi.stageCount = 1; pi.pStages = &c.stages[0]; pi.pViewportState = &c.vs; pi.pRasterizationState = &c.rs; break;
        case SF2C_EXEC_N_2_D473: pi.stageCount = 1; pi.pStages = &c.stages[1]; pi.pDepthStencilState = &c.ds; pi.pMultisampleState = &c.ms; break;
        case SF2C_EXEC_N_3_4E07: pi.pColorBlendState = &c.cb; pi.pMultisampleState = &c.ms; break;
        }
        t0 = SDL_GetTicksNS();
        r = vkCreateGraphicsPipelines(g_vk.dev, VK_NULL_HANDLE, 1, &pi, NULL, &lib[i]);
        t[1 + i] = gpl_ms_since(t0);
        if (r != VK_SUCCESS) { fail = part_name[i]; goto out; }
    }
    for (int o = 0; o < SF2C_EXEC_N_2_D473; o++) {
        VkPipelineLibraryCreateInfoKHR li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR, .libraryCount = SF2C_EXEC_N_4_4B22,
                                             .pLibraries = lib};
        VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &li,
            .flags = o ? VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT : 0, .layout = c.pi.layout};
        t0 = SDL_GetTicksNS();
        r = vkCreateGraphicsPipelines(g_vk.dev, VK_NULL_HANDLE, 1, &pi, NULL, o ? &opt : &fast);
        t[SF2C_EXEC_N_5_EF2D + o] = gpl_ms_since(t0);
        if (r != VK_SUCCESS) { fail = o ? SF2C_EXEC_S_OPTIMIZED_LINK_E79F : SF2C_EXEC_S_FAST_LINK_EA76; goto out; }
    }
out:
    if (fail) LOG(SF2C_EXEC_S_GPL_PROBE_S_BUILD_FAILED_D_N_AAAD, fail, r);
    VkPipeline all[SF2C_EXEC_N_7_7902] = {full, lib[0], lib[1], lib[SF2C_EXEC_N_2_D473], lib[SF2C_EXEC_N_3_4E07], fast, opt};
    for (int i = 0; i < SF2C_EXEC_N_7_7902; i++) if (all[i]) vkDestroyPipeline(g_vk.dev, all[i], NULL);
    return !fail;
}

static void gpl_probe(void) {
    const char *e = getenv(SF2C_EXEC_S_LARECOMP_VK_GPL_PROBE_6BED);
    int want = e && atoi(e) > 1 ? atoi(e) : SF2C_EXEC_N_20_F5CA;
    if (want > GPL_PROBE_MAX) want = GPL_PROBE_MAX;
    pthread_mutex_lock(&prep_lock);
    int n = gpl_nsample < want ? gpl_nsample : want;
    pthread_mutex_unlock(&prep_lock);
    if (!n) { LOG(SF2C_EXEC_S_GPL_PROBE_NO_PREPARED_PIPELI_C3A6); return; }
    LOG(SF2C_EXEC_S_GPL_PROBE_BUILDING_D_PREPARE_7B6E, n,
        g_vk.vids ? SF2C_EXEC_S_DYNAMIC_68ED : SF2C_EXEC_S_IN_EACH_PIPELINE_08AD);
    double sum[SF2C_EXEC_N_7_7902] = {0}, mx[SF2C_EXEC_N_7_7902] = {0};
    int ok = 0;
    uint32_t nonce = (uint32_t)SDL_GetTicksNS() & SF2C_EXEC_N_0X7FFFFFFFU_7A69;
    uint64_t t0 = SDL_GetTicksNS();
    for (int i = 0; i < n; i++) {
        const PipeKey *k = &gpl_sample[i]->k;
        double t[SF2C_EXEC_N_7_7902] = {0};
        if (!gpl_probe_one(k, nonce + (uint32_t)i, t)) continue;
        ok++;
        for (int j = 0; j < SF2C_EXEC_N_7_7902; j++) { sum[j] += t[j]; if (t[j] > mx[j]) mx[j] = t[j]; }
        LOG(SF2C_EXEC_S_GPL_2D_VS_FS_016LLX_016LLX_F_326B
            SF2C_EXEC_S_OPTIMIZED_LINK_1F_MS_N_919D, i, (unsigned long long)k->prog->prep_vh, (unsigned long long)k->prog->prep_fh,
            t[0], t[1], t[SF2C_EXEC_N_2_D473], t[SF2C_EXEC_N_3_4E07], t[SF2C_EXEC_N_4_4B22], t[SF2C_EXEC_N_5_EF2D], t[SF2C_EXEC_N_6_E7F6]);
    }
    if (!ok) { LOG(SF2C_EXEC_S_GPL_PROBE_EVERY_BUILD_FAILED_92EA); return; }
    double libs = sum[1] + sum[SF2C_EXEC_N_2_D473] + sum[SF2C_EXEC_N_3_4E07] + sum[SF2C_EXEC_N_4_4B22];
    LOG(SF2C_EXEC_S_GPL_PROBE_SUMMARY_D_PIPELINE_B2F8
        SF2C_EXEC_S_LIBRARIES_0F_MS_VERTEX_INPUT_B81F
        SF2C_EXEC_S_FAST_LINK_1F_MS_AVG_2F_MAX_2_9FE2,
        ok, gpl_ms_since(t0), sum[0], sum[0] / ok, mx[0], libs, sum[1], sum[SF2C_EXEC_N_2_D473], sum[SF2C_EXEC_N_3_4E07], sum[SF2C_EXEC_N_4_4B22], sum[SF2C_EXEC_N_5_EF2D], sum[SF2C_EXEC_N_5_EF2D] / ok, mx[SF2C_EXEC_N_5_EF2D],
        sum[SF2C_EXEC_N_6_E7F6], sum[SF2C_EXEC_N_6_E7F6] / ok, mx[SF2C_EXEC_N_6_E7F6]);
}
#else
static void gpl_probe_note(Pipe *pp) { (void)pp; }
static void gpl_probe(void) {}
#endif

static void dispatch_compute(uint32_t gx, uint32_t gy, uint32_t gz) {
    snap_reason = SF2C_EXEC_S_COMPUTE_9B08; snapshots_invalidate(NULL);
    if (!st.program || !gx || !gy || !gz) return;
    CsProg *cp = compute_program(st.program);
    if (!cp || atomic_load(&cp->p.state) != 1) { SKIP_CS(!cp ? SK_NO_STAGES : atomic_load(&cp->p.state) == SF2C_EXEC_N_2_D473 ? SK_CS_FAILED : SK_CS_BUILDING); return; }
    {   // LARECOMP_SKIP_CS=<code address hex> (debug): skip that compute shader's dispatches, to see what it does
        static uint64_t skip = ~SF2C_EXEC_N_0ULL_DF1B;
        if (skip == ~SF2C_EXEC_N_0ULL_DF1B) { const char *e = getenv(SF2C_EXEC_S_LARECOMP_SKIP_CS_863B); skip = e ? strtoull(e, NULL, SF2C_EXEC_N_16_B17E) : 0; }
        if (skip && st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr == skip) { SKIP_CS(SK_DEBUG_SKIP); return; }
    }
    if (cp->si->uses_gmem && getenv(SF2C_EXEC_S_LARECOMP_LOG_CS_SBUF_F2E3)) {  // (debug) the storage buffers bound to gmem dispatches
        static uint64_t seen_cs[SF2C_EXEC_N_16_B17E];
        static int seen_n[SF2C_EXEC_N_16_B17E];
        int k = 0;
        while (k < SF2C_EXEC_N_15_E629 && seen_cs[k] && seen_cs[k] != st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr) k++;
        seen_cs[k] = st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr;
        if (seen_n[k]++ < SF2C_EXEC_N_24_C235) {
            LOG(SF2C_EXEC_S_CS_LLX_UX_UX_U_0E54, (unsigned long long)st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr, gx, gy, gz);
            for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++)
                if (st.sbuf[SF2C_EXEC_N_5_EF2D][i].addr) LOG(SF2C_EXEC_S_SB_D_LLX_LLX_CCCE, i, (unsigned long long)st.sbuf[SF2C_EXEC_N_5_EF2D][i].addr, (unsigned long long)st.sbuf[SF2C_EXEC_N_5_EF2D][i].size);
            LOG(SF2C_EXEC_S_N_D7A5);
        }
    }
    snap_apply(SF2C_EXEC_N_5_EF2D, SF2C_EXEC_N_0XFFFF_F51F);  // (overlap) every bound slot: the probe / copy decision per range, before gfast and the sets read the table
    vk_end_pass();
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    gp_mark(cb, SF2C_EXEC_S_DISPATCH_UX_UX_U_LOCAL_UX_UX_8B9B, gx, gy, gz, cp->local[0], cp->local[1], cp->local[SF2C_EXEC_N_2_D473],
            (unsigned long long)st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr, cp->si->uses_gmem);
    dl_note(SF2C_EXEC_S_DISPATCH_UX_UX_U_CS_LLX_GMEM_8F83, gx, gy, gz, (unsigned long long)st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr, cp->si->uses_gmem);
    nfeedback = 0;
    if (ct_on()) {
        LOG(SF2C_EXEC_S_CT_DISPATCH_CS_LLX_UX_UX_U_T_9F03, (unsigned long long)st.program->stages[SF2C_EXEC_N_5_EF2D].code_addr, gx, gy, gz);
        for (int i = 0; i < cp->si->ntex; i++) {
            NvnTexture *t = shader_texture_object(SF2C_EXEC_N_5_EF2D, &cp->si->tex[i]);
            if (t) LOG(SF2C_EXEC_S_S_P_X_6B90, cp->si->tex[i].is_image ? SF2C_EXEC_S_I_53DE : SF2C_EXEC_S_EMPTY_12AE, (void *)t, t->format);
        }
        LOG(SF2C_EXEC_S_N_D7A5);
    }
    if (cp->si->uses_gmem_map) gfast_prepare(cb);
    VkDescriptorSet ds = stage_set(&cp->p, cp->p.set_layout[0], 0, SF2C_EXEC_N_5_EF2D, cp->si, NULL, gx, gy, gz, NULL, NULL, NULL);
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp->pipe);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp->p.layout, 0, 1, &ds, 0, NULL);
    if (gc_shift && cp->si->uses_gmem) gmem_chunk_bind(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp->p.layout, GMEM_CHUNK_SET(STAGE_COMPUTE), SF2C_EXEC_N_1U_B8E6 << STAGE_COMPUTE);
    rec_CmdDispatch(cb, gx, gy, gz);
    if (cp->si->uses_gmem_map && cp->si->uses_gmem_store) gfast_finish(cb);
    if (cp->si->uses_gmem_store) vc_mark_sbufs(SF2C_EXEC_N_5_EF2D);
    stat_dispatches++;
}

// ================================================================== draws
static void debug_draw(Prog *pr, CmdRec *r, bool indexed);

// Quads / fans / polygons as a 32-bit triangle index list.
static uint32_t *expand_indices(uint32_t mode, uint32_t count, const void *indices, int itype, uint32_t first, uint32_t *out_count) {
    uint32_t tris = mode == SF2C_EXEC_N_7_7902 ? count / SF2C_EXEC_N_4_4B22 * SF2C_EXEC_N_2_D473 : count >= SF2C_EXEC_N_3_4E07 ? count - SF2C_EXEC_N_2_D473 : 0;
    uint32_t *out = malloc(tris * SF2C_EXEC_N_3_4E07 * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_4_4B22);
    uint32_t n = 0;
#define IDX(i) (indices ? (itype == 0 ? ((const uint8_t *)indices)[i] : itype == 1 ? ((const uint16_t *)indices)[i] : ((const uint32_t *)indices)[i]) : first + (i))
    if (mode == SF2C_EXEC_N_7_7902) {
        for (uint32_t q = 0; q + SF2C_EXEC_N_3_4E07 < count; q += SF2C_EXEC_N_4_4B22) {
            out[n++] = IDX(q); out[n++] = IDX(q + 1); out[n++] = IDX(q + SF2C_EXEC_N_2_D473);
            out[n++] = IDX(q); out[n++] = IDX(q + SF2C_EXEC_N_2_D473); out[n++] = IDX(q + SF2C_EXEC_N_3_4E07);
        }
    } else {
        for (uint32_t i = 1; i + 1 < count; i++) { out[n++] = IDX(0); out[n++] = IDX(i); out[n++] = IDX(i + 1); }
    }
#undef IDX
    *out_count = n;
    return out;
}

static bool topology(uint32_t mode, VkPrimitiveTopology *t, uint8_t *cls) {
    switch (mode) {
    case 0: *t = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; *cls = 0; return true;
    case 1: *t = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; *cls = 1; return true;
    case SF2C_EXEC_N_3_4E07: *t = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; *cls = 1; return true;
    case SF2C_EXEC_N_4_4B22: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; *cls = SF2C_EXEC_N_2_D473; return true;
    case SF2C_EXEC_N_5_EF2D: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; *cls = SF2C_EXEC_N_2_D473; return true;
    default: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; *cls = SF2C_EXEC_N_2_D473; return false;
    }
}

// The draw's state as a variant record (shader_prep.h; as renderer.m: both backends record the same lines)
static void rec_from_state(ShaderRec *r, uint32_t mode) {
    memset(r, 0, sizeof *r);
    r->kind = SF2C_EXEC_CH_G_C429;
    r->vs = shader_stage_hash(&st.program->stages[0]);
    r->fs = shader_stage_hash(&st.program->stages[1]);
    r->flags = (nvn_window_origin() == 0 ? SR_FLIP_Y : 0) | (nvn_depth_mode() == 0 ? SR_DEPTH_MINUS_ONE : 0);
    r->nattrib = (uint8_t)(st.nattrib < SF2C_EXEC_N_16_B17E ? st.nattrib : SF2C_EXEC_N_16_B17E);
    memcpy(r->attrib, st.attrib, sizeof r->attrib[0] * r->nattrib);
    r->nstream = (uint8_t)(st.nstream < SF2C_EXEC_N_16_B17E ? st.nstream : SF2C_EXEC_N_16_B17E);
    memcpy(r->stream, st.stream, sizeof r->stream[0] * r->nstream);
    r->ncolor = (uint8_t)(st.rt.ncolor < SF2C_EXEC_N_8_2C62 ? st.rt.ncolor : SF2C_EXEC_N_8_2C62);
    r->samples = 1;
    for (int i = 0; i < r->ncolor; i++) {
        NvnTexture *t = st.rt.color[i];
        if (!t) continue;
        r->color[i] = t->format;
        if (st.rt.has_cview[i] && (st.rt.cview[i].setmask & SF2C_EXEC_N_4_4B22)) r->cview[i] = st.rt.cview[i].format;
        if (t->samples > 1) r->samples = t->samples;
        r->channel_mask |= st.channel_mask & (SF2C_EXEC_N_0XFU_6358 << (i * SF2C_EXEC_N_4_4B22));
        if (((st.color_state[0] >> i) & 1) && !rt_kind(t->format)) {
            r->blend_on |= (uint8_t)(SF2C_EXEC_N_1U_B8E6 << i);
            memcpy(r->blend[i], st.blend[i], SF2C_EXEC_N_8_2C62);
        }
    }
    if (st.rt.depth) {
        r->depth = st.rt.depth->format;
        if (!r->ncolor && st.rt.depth->samples > 1) r->samples = st.rt.depth->samples;
    }
    r->a2c = st.ms[SF2C_EXEC_N_0XC_D882] != 0;
    r->topo = shader_topo_class(mode);
    r->poly_fill = st.poly[SF2C_EXEC_N_2_D473] != 1;
}

// LARECOMP_PREP_CHECK=1 (diagnostics): a pipeline the game builds although its shaders were prepared - logs the
// fields its key differs in from the closest prepared pipeline of the same shaders (and the draw's variant record)
static bool prep_check_on(void) {
    static int v = -1;
    if (v < 0) v = getenv(SF2C_EXEC_S_LARECOMP_PREP_CHECK_8074) && atoi(getenv(SF2C_EXEC_S_LARECOMP_PREP_CHECK_8074));
    return v;
}

// Fields of two pipeline keys that differ: their number, and (out) "field: prepared / draw" for each
static int pk_diff(const PipeKey *a, const PipeKey *b, char *out, size_t n) {
    int d = 0;
    size_t o = 0;
    if (out && n) out[0] = 0;
#define PKD(cond, ...) do { if (cond) { d++; if (out && o < n) o += (size_t)snprintf(out + o, n - o, __VA_ARGS__); } } while (0)
    PKD(a->prog != b->prog, SF2C_EXEC_S_PROGRAM_P_P_READY_D_D_D7A1, (void *)a->prog, (void *)b->prog, a->prog ? (int)a->prog->state : -1,
        b->prog ? (int)b->prog->state : -1);
    PKD(a->ncolor != b->ncolor, SF2C_EXEC_S_NCOLOR_U_U_8D35, a->ncolor, b->ncolor);
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) PKD(a->color[i] != b->color[i], SF2C_EXEC_S_COLOR_D_D_D_8EE7, i, a->color[i], b->color[i]);
    PKD(a->depth != b->depth, SF2C_EXEC_S_DEPTH_D_D_63E7, a->depth, b->depth);
    PKD(a->stencil != b->stencil, SF2C_EXEC_S_STENCIL_D_D_28E7, a->stencil, b->stencil);
    PKD(a->samples != b->samples, SF2C_EXEC_S_SAMPLES_U_U_33E4, a->samples, b->samples);
    PKD(a->attr_mask != b->attr_mask, SF2C_EXEC_S_ATTR_MASK_X_X_C958, a->attr_mask, b->attr_mask);
    for (int i = 0; i < SF2C_EXEC_N_32_E29C; i++) {
        PKD(a->attr_fmt[i] != b->attr_fmt[i], SF2C_EXEC_S_ATTR_D_FMT_U_U_3438, i, a->attr_fmt[i], b->attr_fmt[i]);
        PKD(a->attr_off[i] != b->attr_off[i], SF2C_EXEC_S_ATTR_D_OFF_U_U_7853, i, a->attr_off[i], b->attr_off[i]);
        PKD(a->attr_bind[i] != b->attr_bind[i], SF2C_EXEC_S_ATTR_D_BIND_U_U_6D2D, i, a->attr_bind[i], b->attr_bind[i]);
    }
    PKD(a->used_streams != b->used_streams, SF2C_EXEC_S_STREAMS_X_X_B342, a->used_streams, b->used_streams);
    for (int s = 0; s < SF2C_EXEC_N_17_4523; s++) {
        PKD(a->stride[s] != b->stride[s], SF2C_EXEC_S_STRIDE_D_U_U_57AC, s, a->stride[s], b->stride[s]);
        PKD(a->divisor[s] != b->divisor[s], SF2C_EXEC_S_DIVISOR_D_U_U_4C70, s, a->divisor[s], b->divisor[s]);
    }
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++) {
        PKD(memcmp(a->blend[i], b->blend[i], SF2C_EXEC_N_8_2C62), SF2C_EXEC_S_BLEND_D_DIFFERS_9B98, i);
        PKD(a->write_mask[i] != b->write_mask[i], SF2C_EXEC_S_WRITE_MASK_D_X_X_CA69, i, a->write_mask[i], b->write_mask[i]);
    }
    PKD(a->blend_on != b->blend_on, SF2C_EXEC_S_BLEND_ON_X_X_7D88, a->blend_on, b->blend_on);
    PKD(a->a2c != b->a2c, SF2C_EXEC_S_A2C_U_U_804D, a->a2c, b->a2c);
    PKD(a->topo_class != b->topo_class, SF2C_EXEC_S_TOPO_U_U_FDE3, a->topo_class, b->topo_class);
    PKD(a->poly_fill != b->poly_fill, SF2C_EXEC_S_FILL_U_U_9E6D, a->poly_fill, b->poly_fill);
    if (!d && memcmp(a, b, sizeof *a)) {  // (no field differs: other bytes - padding)
        const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
        for (size_t i = 0; i < sizeof *a; i++) PKD(x[i] != y[i], SF2C_EXEC_S_BYTE_ZU_02X_02X_F589, i, x[i], y[i]);
    }
#undef PKD
    return d;
}

static void prep_check(const PipeKey *k, uint64_t vh, uint64_t fh, const ShaderRec *rec) {
    char line[SF2C_EXEC_N_2048_BFA0], diff[SF2C_EXEC_N_2048_BFA0] = SF2C_EXEC_S_EMPTY_12AE;
    shader_rec_format(rec, line, sizeof line);
    int nprep = 0, best_d = 1 << SF2C_EXEC_N_30_624B, progs = 0;
    pthread_mutex_lock(&prep_lock);
    for (size_t i = 0; i < prep_progs_cap; i++) {
        Prog *q = prep_progs[i].obj;
        if (prep_progs[i].key && q && q->prep_vh == vh && q->prep_fh == fh) progs++;
    }
    const Pipe *best = NULL;
    for (size_t i = 0; i < prep_pipes_cap; i++) {
        const Pipe *p = prep_pipes[i].obj;
        if (!prep_pipes[i].key || !p || p->k.prog->prep_vh != vh || p->k.prog->prep_fh != fh) continue;
        nprep++;
        int d = pk_diff(&p->k, k, NULL, 0);
        if (d < best_d) best_d = d, best = p;
    }
    if (best) pk_diff(&best->k, k, diff, sizeof diff);
    pthread_mutex_unlock(&prep_lock);
    LOG(SF2C_EXEC_S_PREPCHECK_016LLX_016LLX_PROG_8C6D
        SF2C_EXEC_S_DIFFERS_PREPARED_DRAW_IN_S_N_5E4D, (unsigned long long)vh, (unsigned long long)fh,
        k->prog->prep_vh ? SF2C_EXEC_S_PREPARED_DE6B : SF2C_EXEC_S_NOT_PREPARED_6629, progs, nprep, best ? diff : SF2C_EXEC_S_NONE_F2E3, line);
}

static void draw_(CmdRec *r, bool indexed);
static void draw(CmdRec *r, bool indexed) {
    PROF_T0();
    draw_(r, indexed);
    PROF_ADD(prof_draw);
}
static uint64_t prof_tp_;
static void draw_(CmdRec *r, bool indexed) {
    if (trace_render) LOG(SF2C_EXEC_S_DRAW_MODE_LLU_ITYPE_LLU_COUN_2CC9,
        (unsigned long long)r->x[0], (unsigned long long)r->x[1],
        (unsigned long long)r->x[SF2C_EXEC_N_2_D473], (unsigned long long)r->x[SF2C_EXEC_N_3_4E07], indexed);
    if (prof) prof_tp_ = SDL_GetTicksNS();
    if (!st.program) { SKIP(SK_NO_PROGRAM); return; }
    ShaderKey vkey = {0}, fkey = {0};
    vkey.glsl = fkey.glsl = 1;
    vkey.flip_y = fkey.flip_y = nvn_window_origin() == 0;
    vkey.depth_minus_one = nvn_depth_mode() == 0;
    for (int i = 0; i < st.nattrib; i++) vkey.attr_kind[i] = (uint8_t)attr_kind(st.attrib[i] & SF2C_EXEC_N_0XFF_7312);
    for (int i = 0; i < st.rt.ncolor; i++) if (st.rt.color[i]) fkey.rt_kind[i] = (uint8_t)rt_kind(st.rt.color[i]->format);
    Prog *pr = get_program(st.program, &vkey, &fkey);
    if (!pr || atomic_load(&pr->state) != 1) { SKIP(!pr ? SK_NO_STAGES : atomic_load(&pr->state) == SF2C_EXEC_N_2_D473 ? SK_PROG_FAILED : SK_PROG_BUILDING); return; }
    if (trace_render) LOG(SF2C_EXEC_S_DRAW_STEP_1_PR_P_VS_LLX_FS_L_7174, pr, (unsigned long long)pr->code[0], (unsigned long long)pr->code[1]);
    snap_apply(0, pr->vi->ssbo_read);  // (overlap) the storage slots the shaders read: a copy where the CPU rewrites the range
    snap_apply(1, pr->fi->ssbo_read);
    {   // Fragment-shader identity for debug skip lists and diagnostics.
        static uint64_t last_fs, last_hash;
        uint64_t fs = st.program->stages[1].code_addr;
        if (fs != last_fs) {
            last_fs = fs;
            last_hash = fs ? hmix(SF2C_EXEC_N_1469598103934665603ULL_7B90, (const void *)(uintptr_t)(fs + SF2C_EXEC_N_0X30_8BAD), SF2C_EXEC_N_0X200_3790) : 0;
        }
        static uint64_t skip[SF2C_EXEC_N_16_B17E];
        static int nskip = -1;
        if (nskip < 0) {
            nskip = 0;
            for (const char *p = getenv(SF2C_EXEC_S_LARECOMP_SKIP_FSHASH_2C3B); p && *p && nskip < SF2C_EXEC_N_16_B17E;) {
                skip[nskip++] = strtoull(p, (char **)&p, SF2C_EXEC_N_16_B17E);
                while (*p == SF2C_EXEC_CH_EMPTY_EE1B) p++;
            }
        }
        for (int i = 0; i < nskip; i++) if (last_hash == skip[i]) { SKIP(SK_DEBUG_SKIP); return; }
        if (g_debug_frame && getenv(SF2C_EXEC_S_LARECOMP_LOG_FSHASH_1DF5)) LOG(SF2C_EXEC_S_FSHASH_FS_LLX_HASH_016LLX_N_392C, (unsigned long long)fs, (unsigned long long)last_hash);
        g_cur_fs_hash = last_hash;

    }
    if (g_debug_frame) debug_draw(pr, r, indexed);
    if (ct_on()) ct_draw(pr, SF2C_EXEC_S_DRAW_D939);
    alias_draw_check();
    resolve_feedback(pr);
    snap_reason = SF2C_EXEC_S_DRAW_DEPTHWRITE_63A2;
    if (st.rt.depth && st.ds[0] && st.ds[1]) snapshots_invalidate(st.rt.depth);
    snap_reason = SF2C_EXEC_S_DRAW_COLORWRITE_5FFC;
    for (int i = 0; i < st.rt.ncolor; i++)
        if (st.rt.color[i] && ((st.channel_mask >> (i * SF2C_EXEC_N_4_4B22)) & SF2C_EXEC_N_0XF_7419)) snapshots_invalidate(st.rt.color[i]);
    begin_pass();
    VkCommandBuffer cb = vk_cmd();

    uint32_t mode = (uint32_t)r->x[0];
    VkPrimitiveTopology topo;
    uint8_t cls;
    bool native = topology(mode, &topo, &cls);
    if (!native && mode != SF2C_EXEC_N_6_E7F6 && mode != SF2C_EXEC_N_7_7902 && mode != SF2C_EXEC_N_9_1958) { SKIP(SK_TOPOLOGY); return; }

    uint64_t prof_tk_ = prof ? SDL_GetTicksNS() : 0;
    if (prof) prof_pre += prof_tk_ - prof_tp_;
    // ---- pipeline
    uint32_t rt_fmt[SF2C_EXEC_N_8_2C62] = {0};
    for (int i = 0; i < st.rt.ncolor && i < SF2C_EXEC_N_8_2C62; i++) rt_fmt[i] = st.rt.color[i] ? st.rt.color[i]->format : 0;
    const PipeIn in = {st.attrib, st.nattrib, (const uint32_t (*)[SF2C_EXEC_N_2_D473])st.stream, st.nstream, rt_fmt, st.channel_mask,
                       st.color_state[0], (const uint8_t (*)[SF2C_EXEC_N_8_2C62])st.blend, st.ms[SF2C_EXEC_N_0XC_D882] != 0, cls, st.poly[SF2C_EXEC_N_2_D473] != 1};
    bool memo_on = key_memo_on();
    if (memo_on && !pr->pmemo) pr->pmemo = calloc(1, sizeof *pr->pmemo);
    struct PipeMemo *pm = memo_on ? pr->pmemo : NULL;
    int nat = st.nattrib < 0 ? 0 : st.nattrib > SF2C_EXEC_N_16_B17E ? SF2C_EXEC_N_16_B17E : st.nattrib, nst = st.nstream < 0 ? 0 : st.nstream > SF2C_EXEC_N_16_B17E ? SF2C_EXEC_N_16_B17E : st.nstream;
    uint32_t npc = pass_ncolor < SF2C_EXEC_N_8_2C62 ? pass_ncolor : SF2C_EXEC_N_8_2C62;
    prof_key_draws++;
    PipeKey k;
    VtxLayout vl;  // the draw's vertex layout (on a memo hit: the memo's, made from the same inputs)
    Pipe *pp;
    if (pm && pm->pp && pm->gen == key_gen && pm->ncolor == pass_ncolor && pm->samples == pass_samples && pm->depth == pass_depth &&
        pm->stencil == pass_stencil && pm->nattrib == st.nattrib && pm->nstream == st.nstream && pm->channel_mask == in.channel_mask &&
        pm->blend_on == in.blend_on && pm->a2c == in.a2c && pm->cls == in.topo_class && pm->poly_fill == in.poly_fill &&
        !memcmp(pm->color, pass_color, npc * sizeof pass_color[0]) && !memcmp(pm->rt_fmt, rt_fmt, sizeof rt_fmt) &&
        !memcmp(pm->attrib, st.attrib, (size_t)nat * SF2C_EXEC_N_4_4B22) && !memcmp(pm->stream, st.stream, (size_t)nst * SF2C_EXEC_N_8_2C62) &&
        !memcmp(pm->blend, st.blend, sizeof pm->blend)) {
        prof_pipe_memo++;
        pp = pm->pp;
        vl = pm->vl;  // (the key leaves the layout out with dynamic vertex input: kept with the memo)
    } else {
        pipe_key_fill(&k, pr, &in, pass_color, pass_ncolor, pass_depth, pass_stencil, pass_samples, &vl);
        uint64_t ph = hash_words(SF2C_EXEC_N_0XCBF29CE484222325ULL_7EE8, &k, sizeof k);
        if (!ph) ph = 1;
        uint32_t pslot = (uint32_t)(ph ^ (ph >> SF2C_EXEC_N_29_3513)) & (PIPE_TAB - 1);
        while (pipe_tab[pslot].key && pipe_tab[pslot].key != ph) pslot = (pslot + 1) & (PIPE_TAB - 1);
        pp = pipe_tab[pslot].pipe;
        if (!pp) {
            if (shader_rec_recording()) {
                ShaderRec rec;
                rec_from_state(&rec, mode);
                shader_rec_note(&rec);
            }
            pp = prep_pipes_n ? PREP_GET(prep_pipes, ph) : NULL;  // prepared before the game
            if (pp && memcmp(&pp->k, &k, sizeof k)) pp = NULL;
            if (!pp) {
                uint64_t vh = shader_stage_hash(&st.program->stages[0]), fh = shader_stage_hash(&st.program->stages[1]);
                shader_first_visit(SF2C_EXEC_S_PIPELINE_F742, vh, fh, ph);
                if (prep_check_on()) {
                    ShaderRec rec;
                    rec_from_state(&rec, mode);
                    prep_check(&k, vh, fh, &rec);
                }
                pp = calloc(1, sizeof *pp);
                pp->k = k;
                run_job(build_pipeline, pp);
            }
            pipe_tab[pslot].key = ph;
            pipe_tab[pslot].pipe = pp;
        }
        if (pm) {
            pm->gen = key_gen; pm->ncolor = pass_ncolor; pm->samples = pass_samples; pm->depth = pass_depth; pm->stencil = pass_stencil;
            pm->nattrib = st.nattrib; pm->nstream = st.nstream; pm->channel_mask = in.channel_mask; pm->blend_on = in.blend_on;
            pm->a2c = in.a2c; pm->cls = in.topo_class; pm->poly_fill = in.poly_fill;
            memcpy(pm->color, pass_color, sizeof pm->color); memcpy(pm->rt_fmt, rt_fmt, sizeof pm->rt_fmt);
            memcpy(pm->attrib, st.attrib, sizeof pm->attrib); memcpy(pm->stream, st.stream, sizeof pm->stream);
            memcpy(pm->blend, st.blend, sizeof pm->blend);
            pm->pp = pp;
            pm->vl = vl;
        }
    }
    {   // a pipeline still building: the draw is skipped (as on Metal), unless the recorder waits for it at the bind
        // (LARECOMP_VK_RECORDER_WAIT_PIPES=1, record.c: pipe_resolve); one that failed is always skipped
        int ps = atomic_load(&pp->state);
        if (ps == SF2C_EXEC_N_2_D473) { SKIP(SK_PIPE_FAILED); return; }
        if (ps != 1 && !rec_wait_pipes()) { SKIP(SK_PIPE_BUILDING); return; }
    }
    if (trace_render) LOG(SF2C_EXEC_S_DRAW_STEP_3_PIPELINE_READY_P_1E2B, atomic_load(&pp->state), pp);
    ss_enabled();
    if (ss_need(SS_PIPE, ss.pipe == pp)) { ss.pipe = pp; rec_CmdBindPipelineObj(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pp); }
    if (g_vk.vids && ss_need(SS_VI, !memcmp(&ss.vi, &vl, sizeof vl))) { ss.vi = vl; set_vertex_input(cb, &vl); }
    uint64_t prof_td_ = prof ? SDL_GetTicksNS() : 0;
    if (prof) prof_key += prof_td_ - prof_tk_;

    // ---- depth / stencil (dynamic)
    static int nodt = -1;
    if (nodt < 0) nodt = getenv(SF2C_EXEC_S_LARECOMP_NO_DEPTH_TEST_509A) != NULL;
    bool dt = st.ds[0] && !nodt && pass_depth;
    if (ss_need(SS_DT, ss.dt == (VkBool32)dt)) { ss.dt = dt; rec_CmdSetDepthTestEnable(cb, dt); }
    VkBool32 dw = dt && st.ds[1];
    if (ss_need(SS_DW, ss.dw == dw)) { ss.dw = dw; rec_CmdSetDepthWriteEnable(cb, dw); }
    VkCompareOp dop = dt ? compare_fn(st.ds[SF2C_EXEC_N_2_D473]) : VK_COMPARE_OP_ALWAYS;
    // EQUAL against a depth pre-pass written by a different vertex shader (foliage, trees) needs both shaders to land
    // on identical depths. They do since the translator stopped using unpackHalf2x16 (NVIDIA ran the half math after
    // it in fp16, differently per shader: black slashes in foliage). LARECOMP_VK_EQUAL_TOL=1 brings back the old
    // workaround: LESS_OR_EQUAL with a few ulps of bias towards the camera, which also let walls paint over the
    // plants stuck flat on them (beach) - so it is off.
    static int tol = -1;
    if (tol < 0) tol = getenv(SF2C_EXEC_S_LARECOMP_VK_EQUAL_TOL_3D17) != NULL;
    bool equal_tol = dop == VK_COMPARE_OP_EQUAL && tol;
    if (equal_tol) dop = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (ss_need(SS_DOP, ss.dop == dop)) { ss.dop = dop; rec_CmdSetDepthCompareOp(cb, dop); }
    bool sten = st.ds[SF2C_EXEC_N_3_4E07] && pass_stencil;
    if (ss_need(SS_STEN, ss.sten == (VkBool32)sten)) { ss.sten = sten; rec_CmdSetStencilTestEnable(cb, sten); }
    {   // Initialize all declared dynamic stencil state, including draws with stencil testing disabled.
        uint32_t ops = st.ds[SF2C_EXEC_N_5_EF2D] | st.ds[SF2C_EXEC_N_6_E7F6] << SF2C_EXEC_N_8_2C62 | st.ds[SF2C_EXEC_N_7_7902] << SF2C_EXEC_N_16_B17E;
        for (int f = 0; f < SF2C_EXEC_N_2_D473; f++) {
            uint32_t o = f ? ops >> SF2C_EXEC_N_12_6B51 : ops;
            VkStencilFaceFlags face = f ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;
            uint32_t v[SF2C_EXEC_N_7_7902] = {stencil_op(o & SF2C_EXEC_N_0XF_7419), stencil_op((o >> SF2C_EXEC_N_8_2C62) & SF2C_EXEC_N_0XF_7419), stencil_op((o >> SF2C_EXEC_N_4_4B22) & SF2C_EXEC_N_0XF_7419),
                             compare_fn(f ? st.ds[SF2C_EXEC_N_4_4B22] >> SF2C_EXEC_N_4_4B22 : st.ds[SF2C_EXEC_N_4_4B22] & SF2C_EXEC_N_0XF_7419), st.stencil_vmask[f] & SF2C_EXEC_N_0XFF_7312, st.stencil_mask[f] & SF2C_EXEC_N_0XFF_7312,
                             st.stencil_ref[f]};
            if (!ss_need(f ? SS_SFACE1 : SS_SFACE0, !memcmp(v, ss.sface[f], sizeof v))) continue;
            memcpy(ss.sface[f], v, sizeof v);
            rec_CmdSetStencilOp(cb, face, (VkStencilOp)v[0], (VkStencilOp)v[1], (VkStencilOp)v[SF2C_EXEC_N_2_D473], (VkCompareOp)v[SF2C_EXEC_N_3_4E07]);
            rec_CmdSetStencilCompareMask(cb, face, v[SF2C_EXEC_N_4_4B22]);
            rec_CmdSetStencilWriteMask(cb, face, v[SF2C_EXEC_N_5_EF2D]);
            rec_CmdSetStencilReference(cb, face, v[SF2C_EXEC_N_6_E7F6]);
        }
    }
    if (ss_need(SS_BC, !memcmp(ss.bc, st.blend_color, sizeof ss.bc))) { memcpy(ss.bc, st.blend_color, sizeof ss.bc); rec_CmdSetBlendConstants(cb, st.blend_color); }

    // ---- rasterizer
    uint8_t cull = st.poly[0];
    VkCullModeFlags cm = cull == 1 ? VK_CULL_MODE_FRONT_BIT : cull == SF2C_EXEC_N_2_D473 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    if (ss_need(SS_CULL, ss.cull == cm)) { ss.cull = cm; rec_CmdSetCullMode(cb, cm); }
    bool ccw = st.poly[1] == 1;
    static int flipw = -1;  // LARECOMP_VK_FLIP_WINDING: invert the front-face mapping (A/B against Metal)
    if (flipw < 0) flipw = getenv(SF2C_EXEC_S_LARECOMP_VK_FLIP_WINDING_6897) != NULL;
    bool front_ccw = (ccw != vkey.flip_y) != (flipw != 0);
    VkFrontFace ff = front_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    if (ss_need(SS_FF, ss.ff == ff)) { ss.ff = ff; rec_CmdSetFrontFace(cb, ff); }
    VkPrimitiveTopology tp = native ? topo : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (ss_need(SS_TOPO, ss.topo == tp)) { ss.topo = tp; rec_CmdSetPrimitiveTopology(cb, tp); }
    VkBool32 pre = st.prim_restart && native && (mode == SF2C_EXEC_N_3_4E07 || mode == SF2C_EXEC_N_5_EF2D);
    if (ss_need(SS_PRE, ss.pre == pre)) { ss.pre = pre; rec_CmdSetPrimitiveRestartEnable(cb, pre); }
    {
        float f[SF2C_EXEC_N_3_4E07] = {0, 0, 0};
        static int off = -1;
        if (off < 0) off = getenv(SF2C_EXEC_S_LARECOMP_NO_DEPTH_BIAS_7DA7) != NULL;
        bool on = (st.poly[SF2C_EXEC_N_3_4E07] & SF2C_EXEC_N_4_4B22) && !off;
        if (on) memcpy(f, st.poly_offset, sizeof f);
        if (equal_tol) { f[1] -= SF2C_EXEC_N_8_2C62; on = true; }  // (see the EQUAL comment above: a few depth ulps nearer)
        if (ss_need(SS_DBE, ss.dbe == (VkBool32)on)) { ss.dbe = on; rec_CmdSetDepthBiasEnable(cb, on); }
        if (ss_need(SS_DB, !memcmp(ss.db, f, sizeof f))) { memcpy(ss.db, f, sizeof f); rec_CmdSetDepthBias(cb, f[1], f[SF2C_EXEC_N_2_D473], f[0]); }
    }

    // ---- textures first (their scales feed the widescreen decision and the rscale table), then the viewport
    float *vp = st.viewport[0];
    float zn = st.depth_range[0][0], zf = st.depth_range[0][1];
    vp_sx = pass_sx; vp_sy = pass_sy; vp_ox = 0; vp_oy = 0;
    // fragment textures are bound before deciding the viewport (tex_sc / tex_wh), rscale depends on the decision:
    // build the vertex set now and the fragment set after the decision (stage_set fills tex_sc for stage 1).
    if (prof) prof_dyn += SDL_GetTicksNS() - prof_td_;
    PROF_T0();
    uint32_t dyn[SF2C_EXEC_N_2_D473 * MAX_BIND], ndyn = 0;  // (set reuse: the sets' dynamic offsets, vertex then fragment)
    VkDescriptorSet vset = stage_set(pr, pr->set_layout[0], 0, 0, pr->vi, NULL, 0, 0, 0, NULL, dyn, &ndyn);
    PROF_ADD(prof_sets);
    VkDescriptorImageInfo fii[SF2C_EXEC_N_64_A68B];
    uint64_t prof_tx_ = prof ? SDL_GetTicksNS() : 0;
    for (int i = 0; i < pr->fi->ntex && i < SF2C_EXEC_N_64_A68B; i++) {
        float sc[SF2C_EXEC_N_2_D473], fr[SF2C_EXEC_N_2_D473]; uint32_t wh[SF2C_EXEC_N_2_D473];
        texture_info(1, pr->fi, i, &fii[i], sc, wh, fr);
        tex_sc[1][i][0] = sc[0]; tex_sc[1][i][1] = sc[1]; tex_wh[1][i][0] = wh[0]; tex_wh[1][i][1] = wh[1];
        tex_fr[1][i][0] = fr[0]; tex_fr[1][i][1] = fr[1];

    }
    if (prof) prof_tex += SDL_GetTicksNS() - prof_tx_;
    if (fabsf(pass_sx - pass_sy) > SF2C_EXEC_N_0_01F_C479 * pass_sy && st.rt.ncolor && renderer_texture_presented(st.rt.color[0])) {
        // Draws into the swapchain image (see renderer.m): composites fill the window, UI goes in a centred 16:9 box.
        bool reads_scene = false;
        for (int i = 0; i < pr->fi->ntex && i < SF2C_EXEC_N_64_A68B; i++)
            if ((tex_sc[1][i][0] != 1 || tex_sc[1][i][1] != 1) && tex_wh[1][i][0] == st.rt.color[0]->width &&
                tex_wh[1][i][1] == st.rt.color[0]->height)
                reads_scene = true;
        bool fullscreen_quad = r->x[SF2C_EXEC_N_2_D473] <= SF2C_EXEC_N_6_E7F6 && st.viewport[0][0] == 0 && st.viewport[0][1] == 0 &&
                               st.viewport[0][SF2C_EXEC_N_2_D473] >= st.rt.color[0]->width && st.viewport[0][SF2C_EXEC_N_3_4E07] >= st.rt.color[0]->height;
        bool fullscreen_fill = pr->fi->ntex == 0 && fullscreen_quad;
        if (fullscreen_quad && !reads_scene && pr->fi->ntex == 1 && (tex_sc[1][0][0] != 1 || tex_sc[1][0][1] != 1)) reads_scene = true;
        static uint64_t wide_frame = ~SF2C_EXEC_N_0ULL_DF1B;
        extern uint64_t renderer_exec_frame(void);  // (the frame being translated)
        if (reads_scene) wide_frame = renderer_exec_frame();
        bool fade_fill = false;
        if (fullscreen_fill && st.ubuf[1][0].addr && st.ubuf[1][0].size >= SF2C_EXEC_N_12_6B51) {
            const float *c = (const float *)(uintptr_t)st.ubuf[1][0].addr;
            bool black = c[0] < SF2C_EXEC_N_0_01F_C479 && c[1] < SF2C_EXEC_N_0_01F_C479 && c[SF2C_EXEC_N_2_D473] < SF2C_EXEC_N_0_01F_C479, white = c[0] > SF2C_EXEC_N_0_99F_00F3 && c[1] > SF2C_EXEC_N_0_99F_00F3 && c[SF2C_EXEC_N_2_D473] > SF2C_EXEC_N_0_99F_00F3;
            fade_fill = black || white;
        }
        bool composite = reads_scene || fade_fill || (fullscreen_fill && wide_frame == renderer_exec_frame());
        if (!composite) wide_frame = ~SF2C_EXEC_N_0ULL_DF1B;
        if (!composite) {
            int pw, ph;
            pass_size(&pw, &ph);
            float u = fminf(pass_sx, pass_sy);
            vp_sx = vp_sy = u;
            vp_ox = (pw - st.rt.color[0]->width * u) * SF2C_EXEC_N_0_5F_2DB0;
            vp_oy = (ph - st.rt.color[0]->height * u) * SF2C_EXEC_N_0_5F_2DB0;
        }
    }
    {
        float vx = vp[0] * vp_sx + vp_ox, vw = vp[SF2C_EXEC_N_2_D473] * vp_sx;
        set_viewport(vx, vp[1] * vp_sy + vp_oy, vw, vp[SF2C_EXEC_N_3_4E07] * vp_sy, zn, zf);
        if (g_debug_frame) {
            VkRect2D sr = scissor_rect();
            LOG(SF2C_EXEC_S_DRAW_VP_2F_2F_2F_2F_FS_HASH__47CB,
                vx, vp[1] * vp_sy + vp_oy, vw, vp[SF2C_EXEC_N_3_4E07] * vp_sy, (unsigned long long)g_cur_fs_hash, sr.offset.x, sr.offset.y,
                sr.extent.width, sr.extent.height, st.scissor[0][0], st.scissor[0][1], st.scissor[0][SF2C_EXEC_N_2_D473], st.scissor[0][SF2C_EXEC_N_3_4E07], pass_w, pass_h,
                vp_sx, vp_sy);
        }
    }
    apply_scissor();
    float rs0[SF2C_EXEC_N_4_4B22] = {vp_sx, vp_sy, vp_ox, vp_oy};
    uint64_t prof_t1_ = prof ? SDL_GetTicksNS() : 0;
    VkDescriptorSet fset = stage_set(pr, pr->set_layout[1], 1, 1, pr->fi, rs0, 0, 0, 0, fii, dyn, &ndyn);
    if (prof) prof_sets += SDL_GetTicksNS() - prof_t1_;
    VkDescriptorSet sets[SF2C_EXEC_N_2_D473] = {vset, fset};
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pr->layout, 0, SF2C_EXEC_N_2_D473, sets, ndyn, ndyn ? dyn : NULL);
    if (gc_shift && (pr->vi->uses_gmem || pr->fi->uses_gmem))
        gmem_chunk_bind(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pr->layout, GMEM_CHUNK_SET(STAGE_FRAGMENT),
                        (pr->vi->uses_gmem ? SF2C_EXEC_N_1U_B8E6 << STAGE_VERTEX : 0) | (pr->fi->uses_gmem ? SF2C_EXEC_N_1U_B8E6 << STAGE_FRAGMENT : 0));
    if (pr->vi->uses_gmem_store) vc_mark_sbufs(0);
    if (pr->fi->uses_gmem_store) vc_mark_sbufs(1);

    uint64_t prof_t2_ = prof ? SDL_GetTicksNS() : 0;
    // ---- indices (copied; the largest index bounds the vertex data copied below)
    uint32_t first = 0, count = (uint32_t)r->x[SF2C_EXEC_N_2_D473], inst, base_inst, max_index = 0;
    int32_t base_vertex = 0;
    VkSpan ib = {0};
    VkIndexType itype_vk = VK_INDEX_TYPE_UINT32;
    uint32_t icount = 0;
    bool use_index = indexed || !native;
    const void *scan_src = NULL;  // deferred index scan (direct index buffer)
    uint32_t scan_isz = 0, scan_restart = 0;
    if (!indexed) {
        first = (uint32_t)r->x[1]; base_inst = (uint32_t)r->x[SF2C_EXEC_N_3_4E07]; inst = (uint32_t)r->x[SF2C_EXEC_N_4_4B22] ? (uint32_t)r->x[SF2C_EXEC_N_4_4B22] : 1;
        max_index = first + count;
        if (!native) {
            uint32_t *e = expand_indices(mode, count, NULL, 0, first, &icount);
            ib = vk_transient_copy(e, (uint64_t)icount * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_4_4B22, SF2C_EXEC_N_4_4B22);
            free(e);
        }
    } else {
        uint32_t itype = (uint32_t)r->x[1];
        uint64_t iaddr = r->x[SF2C_EXEC_N_3_4E07];
        base_vertex = (int32_t)r->x[SF2C_EXEC_N_4_4B22];
        base_inst = (uint32_t)r->x[SF2C_EXEC_N_5_EF2D];
        inst = (uint32_t)r->x[SF2C_EXEC_N_6_E7F6] ? (uint32_t)r->x[SF2C_EXEC_N_6_E7F6] : 1;
        const void *src = (const void *)(uintptr_t)iaddr;
        uint32_t restart = st.prim_restart ? st.restart_index : SF2C_EXEC_N_0XFFFFFFFFU_9601;
        if (native && itype != 0) {
            uint32_t isz = itype == 1 ? SF2C_EXEC_N_2_D473 : SF2C_EXEC_N_4_4B22;
            itype_vk = itype == 1 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            icount = count;
            if (iaddr % isz == 0 && guest_direct(1, iaddr, (uint64_t)count * isz, &ib.buf, &ib.off)) {
                scan_src = src; scan_isz = isz; scan_restart = restart;  // max_index only if a stream gets copied
            } else {
                ib = vk_transient_copy(src, (uint64_t)count * isz + SF2C_EXEC_N_4_4B22, SF2C_EXEC_N_4_4B22);
                if (isz == SF2C_EXEC_N_2_D473) { const uint16_t *s = src; for (uint32_t i = 0; i < count; i++) if (s[i] > max_index && s[i] != (uint16_t)restart) max_index = s[i]; }
                else { const uint32_t *s = src; for (uint32_t i = 0; i < count; i++) if (s[i] > max_index && s[i] != restart) max_index = s[i]; }
            }
        } else if (native) {  // 8-bit indices: widen to 16
            ib = vk_transient((uint64_t)count * SF2C_EXEC_N_2_D473 + SF2C_EXEC_N_4_4B22, SF2C_EXEC_N_4_4B22);
            uint16_t *w = ib.ptr;
            const uint8_t *s = src;
            for (uint32_t i = 0; i < count; i++) { w[i] = s[i] == SF2C_EXEC_N_0XFF_7312 && st.prim_restart ? SF2C_EXEC_N_0XFFFF_F51F : s[i]; if (s[i] > max_index && w[i] != SF2C_EXEC_N_0XFFFF_F51F) max_index = s[i]; }
            itype_vk = VK_INDEX_TYPE_UINT16;
            icount = count;
        } else {
            uint32_t *e = expand_indices(mode, count, src, (int)itype, 0, &icount);
            for (uint32_t i = 0; i < icount; i++) if (e[i] > max_index) max_index = e[i];
            ib = vk_transient_copy(e, (uint64_t)icount * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_4_4B22, SF2C_EXEC_N_4_4B22);
            free(e);
        }
        if (!scan_src) {
            max_index += 1;
            if (base_vertex > 0) max_index += (uint32_t)base_vertex;
        }
    }
    if (use_index && ss_need(SS_IB, ss.ib == ib.buf && ss.io == ib.off && ss.it == itype_vk)) {
        ss.ib = ib.buf; ss.io = ib.off; ss.it = itype_vk;
        rec_CmdBindIndexBuffer(cb, ib.buf, ib.off, itype_vk);
    }

    // ---- vertex streams: the range this draw can read, copied once per submission
    for (int s = 0; s < SF2C_EXEC_N_17_4523; s++) {
        if (!(vl.used_streams & (SF2C_EXEC_N_1U_B8E6 << s))) continue;
        VkSpan sp;
        if (s == ZERO_BINDING) {
            static const float zero[SF2C_EXEC_N_4_4B22] = {0, 0, 0, 1};
            sp = vk_transient_copy(zero, SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
        } else {
            Range v = st.vbuf[s];
            VkBuffer dbuf;
            VkDeviceSize doff;
            if (v.addr && v.size && guest_direct(0, v.addr, v.size, &dbuf, &doff)) {
                if (ss_on && (ss.vb_known >> s & 1) && ss.vb[s] == dbuf && ss.vo[s] == doff) continue;
                ss.vb[s] = dbuf; ss.vo[s] = doff; ss.vb_known |= SF2C_EXEC_N_1U_B8E6 << s;
                rec_CmdBindVertexBuffers(cb, (uint32_t)s, 1, &dbuf, &doff);
                continue;
            }
            if (scan_src) {  // copying after all: the index scan bounds the copy
                if (scan_isz == SF2C_EXEC_N_2_D473) { const uint16_t *q = scan_src; for (uint32_t i = 0; i < icount; i++) if (q[i] > max_index && q[i] != (uint16_t)scan_restart) max_index = q[i]; }
                else { const uint32_t *q = scan_src; for (uint32_t i = 0; i < icount; i++) if (q[i] > max_index && q[i] != scan_restart) max_index = q[i]; }
                max_index += 1;
                if (base_vertex > 0) max_index += (uint32_t)base_vertex;
                scan_src = NULL;
            }
            uint64_t stride = vl.stride[s], need;
            if (!stride) need = SF2C_EXEC_N_256_51E8;
            else if (vl.divisor[s]) need = ((uint64_t)base_inst + inst + vl.divisor[s] - 1) / vl.divisor[s] * stride + SF2C_EXEC_N_256_51E8;
            else need = (uint64_t)max_index * stride + SF2C_EXEC_N_256_51E8;
            if (v.size && need > v.size) need = v.size;
            if (!v.addr) { static const uint8_t zero[SF2C_EXEC_N_256_51E8]; sp = vk_transient_copy(zero, SF2C_EXEC_N_256_51E8, SF2C_EXEC_N_16_B17E); }
            else sp = guest_copy(v.addr, need ? need : SF2C_EXEC_N_16_B17E);
        }
        VkDeviceSize off = sp.off;
        if (ss_on && (ss.vb_known >> s & 1) && ss.vb[s] == sp.buf && ss.vo[s] == off) continue;
        ss.vb[s] = sp.buf; ss.vo[s] = off; ss.vb_known |= SF2C_EXEC_N_1U_B8E6 << s;
        rec_CmdBindVertexBuffers(cb, (uint32_t)s, 1, &sp.buf, &off);
    }

    if (prof) prof_vtx += SDL_GetTicksNS() - prof_t2_;
    // ---- draw
    if (trace_render) LOG(SF2C_EXEC_S_DRAW_STEP_7_REC_CMDDRAW_USE__2299, use_index, count, icount);
    if (!use_index) rec_CmdDraw(cb, count, inst, first, base_inst);
    else rec_CmdDrawIndexed(cb, icount, inst, 0, base_vertex, base_inst);
    if (trace_render) LOG(SF2C_EXEC_S_DRAW_STEP_8_REC_CMDDRAW_DONE_D1D3);
    stat_draws++;
    cum_draws++;
    if (g_gpuprof_frame && gp_n) {
        if (!gp_draws[gp_n - 1]) {  // first draw of the row: its fragment shader hash
            size_t l = strlen(gp_label[gp_n - 1]);
            snprintf(gp_label[gp_n - 1] + l, sizeof gp_label[0] - l, SF2C_EXEC_S_FIRST_FS_016LLX_A8F1, (unsigned long long)g_cur_fs_hash);
        }
        gp_draws[gp_n - 1]++;
    }
}

static void debug_draw(Prog *pr, CmdRec *r, bool indexed) {
    LOG(SF2C_EXEC_S_DRAW_S_INST_LLU_MODE_LLU_COU_6B33, indexed ? SF2C_EXEC_S_E_8C86 : SF2C_EXEC_S_A_AC8D,
        (unsigned long long)(indexed ? r->x[SF2C_EXEC_N_6_E7F6] : r->x[SF2C_EXEC_N_4_4B22]), (unsigned long long)r->x[0], (unsigned long long)r->x[SF2C_EXEC_N_2_D473],
        (unsigned long long)st.program->stages[0].code_addr, (unsigned long long)st.program->stages[1].code_addr,
        pr->vi->cbuf_mask, pr->fi->cbuf_mask, st.rt.ncolor);
    for (int i = 0; i < st.rt.ncolor; i++) if (st.rt.color[i]) LOG(SF2C_EXEC_S_UX_U_F_X_38C2, st.rt.color[i]->width, st.rt.color[i]->height, st.rt.color[i]->format);
    if (st.rt.depth) LOG(SF2C_EXEC_S_D_X_P_2400, st.rt.depth->format, (void *)st.rt.depth);
    LOG(SF2C_EXEC_S_VP_0F_0F_0F_0F_ATTRS_X_BLEND_DF54, st.viewport[0][0], st.viewport[0][1],
        st.viewport[0][SF2C_EXEC_N_2_D473], st.viewport[0][SF2C_EXEC_N_3_4E07], pr->vi->attr_mask, st.color_state[0], st.ds[0], st.ds[1], st.ds[SF2C_EXEC_N_2_D473], st.poly[0], st.poly[1],
        st.channel_mask);
    if (getenv(SF2C_EXEC_S_LARECOMP_DEBUG_DATA_5C9F)) {  // vertex streams 0-1 and fragment cbufs: the first floats
        for (int s = 0; s < SF2C_EXEC_N_2_D473; s++) {
            if (!st.vbuf[s].addr) continue;
            const float *v = (const float *)(uintptr_t)st.vbuf[s].addr;
            LOG(SF2C_EXEC_S_DRAW_VBUF_D_LLX_SIZE_LLU_9FD6, s, (unsigned long long)st.vbuf[s].addr, (unsigned long long)st.vbuf[s].size);
            for (int k = 0; k < SF2C_EXEC_N_16_B17E; k++) LOG(SF2C_EXEC_S_4G_9D1E, v[k]);
            LOG(SF2C_EXEC_S_N_D7A5);
        }
        for (int c = 0; c < SF2C_EXEC_N_18_4EC9; c++) {
            if (!((pr->fi->cbuf_mask >> c) & 1) || !st.ubuf[1][c].addr) continue;
            const float *u = (const float *)(uintptr_t)st.ubuf[1][c].addr;
            LOG(SF2C_EXEC_S_DRAW_FCB_D_LLX_SIZE_U_7B3A, c, (unsigned long long)st.ubuf[1][c].addr, (unsigned)st.ubuf[1][c].size);
            for (int k = 0; k < SF2C_EXEC_N_96_7B1A && k * SF2C_EXEC_N_4_4B22 < (int)st.ubuf[1][c].size; k++) LOG(SF2C_EXEC_S_6G_3B7A, u[k]);
            LOG(SF2C_EXEC_S_N_D7A5);
        }
    }
    for (int i = 0; i < pr->fi->ntex && i < SF2C_EXEC_N_64_A68B; i++) {  // fragment textures: guest size / format, allocation, scale
        NvnTexture *t = shader_texture_object(1, &pr->fi->tex[i]);
        const TexBackend *b = t ? (const TexBackend *)t->backend : NULL;
        if (!t) { LOG(SF2C_EXEC_S_DRAW_TEX_D_NONE_N_CDED, i); continue; }
        LOG(SF2C_EXEC_S_DRAW_TEX_D_P_UX_U_F_X_LV_U_U_034D, i, (void *)t, t->width, t->height, t->format,
            b ? b->levels : 0, b ? b->width : 0, b ? b->height : 0, b ? b->sx : 0, b ? b->sy : 0, b && b->scalable ? SF2C_EXEC_S_SCALABLE_F126 : SF2C_EXEC_S_EMPTY_12AE);
    }
}

// ================================================================== texture copies
typedef struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[SF2C_EXEC_N_6_E7F6]; } TexRefRec;

static void copy_region_dims(NvnTexture *t, const TexRefRec *ref, const FormatInfo **fi, int32_t rg[SF2C_EXEC_N_6_E7F6], int *level,
                             uint64_t *row, uint64_t *img) {
    *fi = nvn_format_info(t->format);
    memcpy(rg, ref->region, sizeof ref->region);
    if (!rg[SF2C_EXEC_N_3_4E07]) rg[SF2C_EXEC_N_3_4E07] = (int32_t)t->width;
    if (!rg[SF2C_EXEC_N_4_4B22]) rg[SF2C_EXEC_N_4_4B22] = (int32_t)t->height;
    if (!rg[SF2C_EXEC_N_5_EF2D]) rg[SF2C_EXEC_N_5_EF2D] = 1;
    *level = ref->has_view && (ref->view.setmask & 1) ? ref->view.base_level : 0;
    uint32_t wb = ((uint32_t)rg[SF2C_EXEC_N_3_4E07] + (*fi)->bw - 1) / (*fi)->bw, hb = ((uint32_t)rg[SF2C_EXEC_N_4_4B22] + (*fi)->bh - 1) / (*fi)->bh;
    *row = st.copy_row_stride ? st.copy_row_stride : (uint64_t)wb * (*fi)->bpb;
    *img = st.copy_image_stride ? st.copy_image_stride : *row * hb;
}

// Without an imported heap, draws read staging snapshots, not guest memory.
// A CPU copy can then proceed while those snapshots are rendered. A mapped heap
// still needs the wait: GPU loads/stores can access either range directly.
// Without overlap, timestamp resolution can also write guest memory on a wait.
static void copy_buffer_to_buffer(uint64_t src, uint64_t dst, uint64_t size) {
    if (gmem_buf || gc_lazy || !overlap_on()) {
        wait_why = SF2C_EXEC_S_BUFFER_BUFFER_COPY_7A78;
        renderer_flush();
    }
    memmove((void *)(uintptr_t)dst, (void *)(uintptr_t)src, size);
    // Later draws in this submission must copy the updated guest bytes again.
    guest_copy_flush();
}

static void copy_buffer_to_texture(uint64_t src, const TexRefRec *dst) {
    snap_reason = SF2C_EXEC_S_BUF2TEX_E11A; snapshots_invalidate(NULL);
    NvnTexture *t = (NvnTexture *)(uintptr_t)dst->tex;
    if (!t || !src) return;
    alias_before_write(t, 0, SF2C_EXEC_S_UPLOAD_CA46);
    vk_end_pass();
    const FormatInfo *fi;
    int32_t rg[SF2C_EXEC_N_6_E7F6];
    int level;
    uint64_t row, img;
    copy_region_dims(t, dst, &fi, rg, &level, &row, &img);
    if (!fi || !fi->bpb) return;
    TexBackend *b = vk_tex(t, false);  // prior CPU contents uploaded first
    renderer_mark_gpu_owned(t);
    if (b->aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) return;
    if (b->astc_bw) {  // (blocks would need the CPU decode: not seen in the game yet)
        static int nlog;
        if (nlog++ < SF2C_EXEC_N_5_EF2D) LOG(SF2C_EXEC_S_VK_BUFFER_ASTC_TEXTURE_COPY__E157, t->width, t->height);
        return;
    }
    bool array = b->layers > 1 && b->depth == 1;
    uint32_t hb = ((uint32_t)rg[SF2C_EXEC_N_4_4B22] + fi->bh - 1) / fi->bh, wb = ((uint32_t)rg[SF2C_EXEC_N_3_4E07] + fi->bw - 1) / fi->bw;
    uint32_t out_bpb = fi->bpb == SF2C_EXEC_N_3_4E07 ? SF2C_EXEC_N_4_4B22 : fi->bpb;
    uint32_t nz = (uint32_t)rg[SF2C_EXEC_N_5_EF2D];
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    uint64_t out_row = (uint64_t)wb * out_bpb;
    VkSpan s = vk_transient(out_row * hb * nz, SF2C_EXEC_N_16_B17E);
    for (uint32_t z = 0; z < nz; z++)
        for (uint32_t y = 0; y < hb; y++) {
            const uint8_t *sr = (const uint8_t *)(uintptr_t)(src + (uint64_t)z * img + (uint64_t)y * row);
            uint8_t *dr = (uint8_t *)s.ptr + ((uint64_t)z * hb + y) * out_row;
            if (out_bpb == fi->bpb) memcpy(dr, sr, out_row);
            else for (uint32_t x = 0; x < wb; x++) { memcpy(dr + x * SF2C_EXEC_N_4_4B22, sr + x * SF2C_EXEC_N_3_4E07, SF2C_EXEC_N_3_4E07); dr[x * SF2C_EXEC_N_4_4B22 + SF2C_EXEC_N_3_4E07] = SF2C_EXEC_N_255_9556; }
        }
    VkBufferImageCopy c = {.bufferOffset = s.off, .bufferRowLength = wb * fi->bw, .bufferImageHeight = hb * fi->bh,
                           .imageSubresource = {b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : b->aspect,
                                                (uint32_t)level, array ? (uint32_t)rg[SF2C_EXEC_N_2_D473] : 0, array ? nz : 1},
                           .imageOffset = {rg[0], rg[1], array ? 0 : rg[SF2C_EXEC_N_2_D473]},
                           .imageExtent = {(uint32_t)rg[SF2C_EXEC_N_3_4E07], (uint32_t)rg[SF2C_EXEC_N_4_4B22], array ? 1 : nz}};
    rec_CmdCopyBufferToImage(cb, s.buf, b->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
}

static void copy_texture_to_buffer(const TexRefRec *src, uint64_t dst) {
    NvnTexture *t = (NvnTexture *)(uintptr_t)src->tex;
    if (!t || !dst || dst < HEAP_BASE) return;
    t = alias_read(t);
    vk_end_pass();
    const FormatInfo *fi;
    int32_t rg[SF2C_EXEC_N_6_E7F6];
    int level;
    uint64_t row, img;
    copy_region_dims(t, src, &fi, rg, &level, &row, &img);
    if (!fi || !fi->bpb || fi->bpb == SF2C_EXEC_N_3_4E07 || row % fi->bpb) return;
    TexBackend *b = vk_tex(t, false);
    float tsx, tsy;
    renderer_texture_scale(t, &tsx, &tsy);
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    VkImage srcimg = b->image;
    VmaAllocation small_mem = NULL;
    if ((tsx != 1 || tsy != 1) && level == 0 && !renderer_format_is_depth(t->format)) {
        // the CPU expects guest-sized texels: filter the scaled allocation down first
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = b->format,
                                .extent = {t->width, t->height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                                .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &srcimg, &small_mem, NULL));
        vk_image_init_layout(cb, srcimg, VK_IMAGE_ASPECT_COLOR_BIT);
        VkImageBlit bl = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {(int32_t)lroundf(t->width * tsx), (int32_t)lroundf(t->height * tsy), 1}},  // (the contents)
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {(int32_t)t->width, (int32_t)t->height, 1}}};
        rec_CmdBlitImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, srcimg, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
        vk_full_barrier(cb);
    }
    bool array = b->layers > 1 && b->depth == 1;
    uint32_t nz = (uint32_t)rg[SF2C_EXEC_N_5_EF2D];
    uint64_t total = img * nz;
    VkSpan out = vk_readback(total);
    VkBufferImageCopy c = {.bufferOffset = out.off, .bufferRowLength = (uint32_t)(row / fi->bpb) * fi->bw,
                           .bufferImageHeight = (uint32_t)(img / row) * fi->bh,
                           .imageSubresource = {b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : b->aspect,
                                                (uint32_t)level, array ? (uint32_t)rg[SF2C_EXEC_N_2_D473] : 0, array ? nz : 1},
                           .imageOffset = {rg[0], rg[1], array ? 0 : rg[SF2C_EXEC_N_2_D473]},
                           .imageExtent = {(uint32_t)rg[SF2C_EXEC_N_3_4E07], (uint32_t)rg[SF2C_EXEC_N_4_4B22], array ? 1 : nz}};
    rec_CmdCopyImageToBuffer(cb, srcimg, VK_IMAGE_LAYOUT_GENERAL, out.buf, 1, &c);
    wait_why = SF2C_EXEC_S_TEXTURE_BUFFER_READBACK_B68D;
    renderer_flush();  // the CPU may read the result right after the fence
    memcpy((void *)(uintptr_t)dst, out.ptr, total);
    if (small_mem) vmaDestroyImage(g_vk.vma, srcimg, small_mem);
}

// Scaled depth copies (the game's depth pyramid) are blits, but a depth format needn't support being a blit destination:
// Intel's drivers don't for D32_SFLOAT_S8_UINT (validation on a UHD 770; a suspect in the UHD 630's crash after a
// depth pass, issues #8, #46). There the depth goes the long way round with transfers only: to a buffer, into an
// unsigned-integer colour image with the same bits per texel, blitted (nearest: bits kept) into a second one at the
// new size, back to the buffer, and into the depth image. The regions are copy_texture's, already cut to the
// contents' extent (Dynamic resolution's sub-rect, issue #191's content_extent) and kept inside their levels.
// The scratch buffer and images are sized by the source and destination *levels* (not the regions, which follow the
// render scale), so a changing scale doesn't regrow them; outgrown ones are destroyed once the submissions that may
// use them are done. All of it (and depth_blit_ok's table) belongs to the translating thread: the render thread, or
// the game's GPU thread with LARECOMP_RENDER_THREAD=0 - copy_texture runs nowhere else.
static bool depth_blit_ok(VkFormat f) {
    static VkFormat known[SF2C_EXEC_N_8_2C62];
    static bool ok[SF2C_EXEC_N_8_2C62];
    static int n;
    for (int i = 0; i < n; i++) if (known[i] == f) return ok[i];
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &fp);
    bool r = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    if (n < SF2C_EXEC_N_8_2C62) { known[n] = f; ok[n++] = r; }
    if (!r) LOG(SF2C_EXEC_S_VK_DEPTH_FORMAT_D_CAN_T_BE_B_328A, f);
    return r;
}

typedef struct { VkImage img; VmaAllocation mem; uint32_t w, h; } ScratchImage;

static bool scratch_image(ScratchImage *si, uint32_t w, uint32_t h, VkFormat fmt) {
    if (si->img && si->w >= w && si->h >= h) return true;
    if (si->w > w) w = si->w;
    if (si->h > h) h = si->h;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage img;
    VmaAllocation mem;
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &img, &mem, NULL) != VK_SUCCESS) return false;
    if (si->img) vk_defer_image(si->img, si->mem);  // (no views: never sampled or rendered to)
    vk_image_init_layout(vk_upload_cmd(), img, VK_IMAGE_ASPECT_COLOR_BIT);
    *si = (ScratchImage){img, mem, w, h};
    return true;
}

static void depth_blit_via_color(VkCommandBuffer cb, TexBackend *s, TexBackend *d, uint32_t slv, uint32_t sly, int sx, int sy,
                                 int sw, int sh, uint32_t dlv, uint32_t dly, int dx, int dy, int dw, int dh) {
    if (s->samples != 1 || d->samples != 1) return;  // (buffer copies and blits are single-sample only, as before)
    // bits per texel of the depth aspect in a buffer: 4 (D32 / X8_D24 variants) or 2 (D16)
    uint32_t bpp = s->format == VK_FORMAT_D16_UNORM || s->format == VK_FORMAT_D16_UNORM_S8_UINT ? SF2C_EXEC_N_2_D473 : SF2C_EXEC_N_4_4B22;
    VkFormat cf = bpp == SF2C_EXEC_N_2_D473 ? VK_FORMAT_R16_UINT : VK_FORMAT_R32_UINT;
    static VkBuffer buf;
    static VmaAllocation buf_mem;
    static VkDeviceSize buf_size;
    static struct { VkBuffer buf; VmaAllocation mem; uint64_t seq; } old[SF2C_EXEC_N_8_2C62];  // outgrown buffers, until their work is done
    static ScratchImage a[SF2C_EXEC_N_2_D473], b[SF2C_EXEC_N_2_D473];  // per colour format (R16 / R32)
    uint64_t done = vk_completed_seq();
    for (int i = 0; i < SF2C_EXEC_N_8_2C62; i++)
        if (old[i].buf && old[i].seq <= done) { vmaDestroyBuffer(g_vk.vma, old[i].buf, old[i].mem); old[i].buf = VK_NULL_HANDLE; }
    uint32_t slw = level_extent(s->width, slv), slh = level_extent(s->height, slv);
    uint32_t dlw = level_extent(d->width, dlv), dlh = level_extent(d->height, dlv);
    VkDeviceSize need = (VkDeviceSize)(slw * slh > dlw * dlh ? slw * slh : dlw * dlh) * bpp;
    if (need > buf_size) {
        if (need < buf_size * SF2C_EXEC_N_2_D473) need = buf_size * SF2C_EXEC_N_2_D473;
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = need,
                                 .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VkBuffer nb;
        VmaAllocation nm;
        if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &nb, &nm, NULL) != VK_SUCCESS) return;
        if (buf) {
            int i = 0;
            while (i < SF2C_EXEC_N_8_2C62 && old[i].buf) i++;
            if (i < SF2C_EXEC_N_8_2C62) old[i].buf = buf, old[i].mem = buf_mem, old[i].seq = vk_pending_seq();
            // (else kept, never freed: 8 outgrown in flight at once, with each at least twice the last - not seen)
        }
        buf = nb; buf_mem = nm; buf_size = need;
    }
    int k = bpp == SF2C_EXEC_N_2_D473 ? 0 : 1;
    if (!scratch_image(&a[k], slw, slh, cf) || !scratch_image(&b[k], dlw, dlh, cf)) return;
    VkBufferImageCopy c = {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, slv, sly, 1}, .imageOffset = {sx, sy, 0},
                           .imageExtent = {(uint32_t)sw, (uint32_t)sh, 1}};
    rec_CmdCopyImageToBuffer(cb, s->image, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &c);
    vk_full_barrier(cb);
    c.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageOffset = (VkOffset3D){0, 0, 0};
    rec_CmdCopyBufferToImage(cb, buf, a[k].img, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    vk_full_barrier(cb);
    VkImageBlit bl = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {sw, sh, 1}},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {dw, dh, 1}}};
    rec_CmdBlitImage(cb, a[k].img, VK_IMAGE_LAYOUT_GENERAL, b[k].img, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_NEAREST);
    vk_full_barrier(cb);
    c.imageExtent = (VkExtent3D){(uint32_t)dw, (uint32_t)dh, 1};
    rec_CmdCopyImageToBuffer(cb, b[k].img, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &c);
    vk_full_barrier(cb);
    c.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_DEPTH_BIT, dlv, dly, 1};
    c.imageOffset = (VkOffset3D){dx, dy, 0};
    rec_CmdCopyBufferToImage(cb, buf, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
}

static void copy_texture(const TexRefRec *src, const TexRefRec *dst, uint64_t flags) {
    snap_reason = SF2C_EXEC_S_TEX2TEX_432F; snapshots_invalidate(NULL);
    NvnTexture *st_ = (NvnTexture *)(uintptr_t)src->tex, *dt = (NvnTexture *)(uintptr_t)dst->tex;
    if (!st_ || !dt) return;
    vk_end_pass();
    if (dl_on == SF2C_EXEC_N_2_D473) renderer_flush();  // (LARECOMP_VK_DEVLOST_TRACE=2: each copy its own submission, waited on)
    gp_mark(vk_cmd(), SF2C_EXEC_S_COPY_TEXTURE_UX_U_UX_U_34B4, st_->width, st_->height, dt->width, dt->height);
    dl_note(SF2C_EXEC_S_COPY_TEXTURE_UX_U_F_X_UX_U_F_C1A7, st_->width, st_->height, st_->format, dt->width, dt->height,
            dt->format, (void *)st_, (void *)dt);
    TexBackend *s = vk_tex(st_, false), *d = vk_tex(dt, true);
    renderer_mark_gpu_owned(dt);
    if (renderer_texture_presented(dt) && st_->width == dt->width && st_->height == dt->height) renderer_texture_mark_presented(st_);
    renderer_texture_first_write(dt);
    const int32_t *sr = src->region, *dr = dst->region;
    float ssx, ssy, dsx, dsy;
    renderer_texture_scale(st_, &ssx, &ssy);
    renderer_texture_scale(dt, &dsx, &dsy);
    int sx0 = (int)lroundf(sr[0] * ssx), sy0 = (int)lroundf(sr[1] * ssy);
    int dx0 = (int)lroundf(dr[0] * dsx), dy0 = (int)lroundf(dr[1] * dsy);
    int sw = sr[SF2C_EXEC_N_3_4E07] ? (int)lroundf(sr[SF2C_EXEC_N_3_4E07] * ssx) : (int)lroundf(st_->width * ssx), sh = sr[SF2C_EXEC_N_4_4B22] ? (int)lroundf(sr[SF2C_EXEC_N_4_4B22] * ssy) : (int)lroundf(st_->height * ssy);
    int dw = dr[SF2C_EXEC_N_3_4E07] ? (int)lroundf(dr[SF2C_EXEC_N_3_4E07] * dsx) : (int)lroundf(dt->width * dsx), dh = dr[SF2C_EXEC_N_4_4B22] ? (int)lroundf(dr[SF2C_EXEC_N_4_4B22] * dsy) : (int)lroundf(dt->height * dsy);
    uint32_t slv = src->has_view && (src->view.setmask & 1) ? src->view.base_level : 0;
    uint32_t dlv = dst->has_view && (dst->view.setmask & 1) ? dst->view.base_level : 0;
    uint32_t sly = src->has_view && (src->view.setmask & SF2C_EXEC_N_2_D473) ? src->view.min_layer : 0;
    uint32_t dly = dst->has_view && (dst->view.setmask & SF2C_EXEC_N_2_D473) ? dst->view.min_layer : 0;
    if (slv >= s->levels || dlv >= d->levels || sly >= s->layers || dly >= d->layers) return;
    {   // whole-image regions are the contents' extent in the level, and every region stays inside its level: scaled
        // allocations (internal resolution below 1x) round guest regions past the edge, and out-of-bounds copies /
        // blits are undefined in Vulkan (garbage that changed every frame on NVIDIA: flicker in the depth pyramid's
        // users). Dynamic resolution draws into the top-left part of the allocation: the rest holds older frames, and
        // a whole-image copy of it into a texture of another scale (the save slot's screenshot, 192x108) took those
        // along (the frame small in a corner, stale frames around it)
        int slw = (int)level_extent(s->width, slv), slh = (int)level_extent(s->height, slv);
        int dlw = (int)level_extent(d->width, dlv), dlh = (int)level_extent(d->height, dlv);
        int scw = content_extent(slw, s->scalable, s->sx, s->ax), sch = content_extent(slh, s->scalable, s->sy, s->ay);
        int dcw = content_extent(dlw, d->scalable, d->sx, d->ax), dch = content_extent(dlh, d->scalable, d->sy, d->ay);
        bool same_part = slw == dlw && slh == dlh && scw == dcw && sch == dch;  // (contents in the same part of equal images)
        if (!sr[SF2C_EXEC_N_3_4E07]) sw = scw - sx0;
        if (!sr[SF2C_EXEC_N_4_4B22]) sh = sch - sy0;
        if (!dr[SF2C_EXEC_N_3_4E07]) dw = dcw - dx0;
        if (!dr[SF2C_EXEC_N_4_4B22]) dh = dch - dy0;
        if (same_part && sx0 == 0 && sy0 == 0 && dx0 == 0 && dy0 == 0 && sw == scw && sh == sch && dw == dcw && dh == dch)
            sw = dw = slw, sh = dh = slh;  // (the same part of images of one size: the whole image, which may alias)
        if (sx0 + sw > slw) sw = slw - sx0;
        if (sy0 + sh > slh) sh = slh - sy0;
        if (dx0 + dw > dlw) dw = dlw - dx0;
        if (dy0 + dh > dlh) dh = dlh - dy0;
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    }
    if (g_debug_frame || ct_on())
        LOG(SF2C_EXEC_S_COPYTEX_P_UX_U_F_X_LV_U_D_D__7DC4,
            (void *)st_, st_->width, st_->height, st_->format, slv, sr[0], sr[1], sr[SF2C_EXEC_N_3_4E07], sr[SF2C_EXEC_N_4_4B22], s->width, s->height, (void *)dt, dt->width,
            dt->height, dt->format, dlv, dr[0], dr[1], dr[SF2C_EXEC_N_3_4E07], dr[SF2C_EXEC_N_4_4B22], d->width, d->height, sx0, sy0, sw, sh, dx0, dy0, dw, dh,
            (unsigned long long)flags);
    if (copy_opt() && sx0 == 0 && sy0 == 0 && dx0 == 0 && dy0 == 0 && sw == (int)s->width && sh == (int)s->height && dw == (int)d->width &&
        dh == (int)d->height && slv == 0 && dlv == 0 && sly == 0 && dly == 0 && alias_make(st_, dt, s, d)) {
        if (ct_on()) LOG(SF2C_EXEC_S_CT_ALIAS_MADE_P_P_N_04E7, (void *)st_, (void *)dt);
        return;
    }
    alias_before_write(dt, 0, SF2C_EXEC_S_COPY_DST_792B);
    if (naliases) s = alias_read(st_)->backend;
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    VkImageAspectFlags sa = s->aspect, da = d->aspect;
    if (s->samples > 1 && d->samples == 1 && sw == dw && sh == dh && s->format == d->format && !(sa & VK_IMAGE_ASPECT_DEPTH_BIT)) {
        VkImageResolve rv = {.srcSubresource = {sa, slv, sly, 1}, .srcOffset = {sx0, sy0, 0}, .dstSubresource = {da, dlv, dly, 1},
                             .dstOffset = {dx0, dy0, 0}, .extent = {(uint32_t)sw, (uint32_t)sh, 1}};
        rec_CmdResolveImage(cb, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rv);
        return;
    }
    if (s->samples != d->samples) return;
    if (sw == dw && sh == dh && s->format == d->format) {
        VkImageCopy c = {.srcSubresource = {sa, slv, sly, 1}, .srcOffset = {sx0, sy0, 0}, .dstSubresource = {da, dlv, dly, 1},
                         .dstOffset = {dx0, dy0, 0}, .extent = {(uint32_t)sw, (uint32_t)sh, 1}};
        rec_CmdCopyImage(cb, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        return;
    }
    // scaled / converting copy: the game builds its depth pyramid this way (nearest), colour copies filter if asked
    bool depth = (sa | da) & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    if (depth && s->format != d->format) return;
    static int via_color = -1;  // LARECOMP_VK_DEPTH_BLIT_VIA_COLOR=1: always (tests on GPUs that can blit depth)
    if (via_color < 0) via_color = getenv(SF2C_EXEC_S_LARECOMP_VK_DEPTH_BLIT_VIA_C_E867) ? atoi(getenv(SF2C_EXEC_S_LARECOMP_VK_DEPTH_BLIT_VIA_C_E867)) : 0;
    if (depth && (sa & da & VK_IMAGE_ASPECT_DEPTH_BIT) && (via_color || !depth_blit_ok(d->format) || !depth_blit_ok(s->format))) {
        dl_note(SF2C_EXEC_S_DEPTH_BLIT_VIA_COLOUR_P_LV_U_C335, (void *)s->image, slv, sx0, sy0, sw, sh,
                (void *)d->image, dlv, dx0, dy0, dw, dh);
        depth_blit_via_color(cb, s, d, slv, sly, sx0, sy0, sw, sh, dlv, dly, dx0, dy0, dw, dh);
        return;
    }
    VkImageBlit bl = {.srcSubresource = {depth ? (sa & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : sa) : sa, slv, sly, 1},
                      .srcOffsets = {{sx0, sy0, 0}, {sx0 + sw, sy0 + sh, 1}},
                      .dstSubresource = {depth ? (da & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : da) : da, dlv, dly, 1},
                      .dstOffsets = {{dx0, dy0, 0}, {dx0 + dw, dy0 + dh, 1}}};
    bool linear = (flags & 1) && !depth && !rt_kind(st_->format);
    dl_note(SF2C_EXEC_S_BLIT_P_LV_U_U_UX_U_D_D_DX_D__3907, (void *)s->image, slv, s->levels, s->width,
            s->height, sx0, sy0, sw, sh, (void *)d->image, dlv, d->levels, d->width, d->height, dx0, dy0, dw, dh, linear ? SF2C_EXEC_S_LINEAR_4DD1 : SF2C_EXEC_S_NEAREST_65DD);
    rec_CmdBlitImage(cb, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
}

// ================================================================== present helpers (device.c)
static VkPipelineLayout present_layout;
static VkDescriptorSetLayout present_set_layout;

// ---- decoded video into its texture (textures.c renderer_upload_nv12 / upload_pending, render thread): the NV12
// planes go into two images (R8, R8G8: reused while the size stays), and a fullscreen draw converts them into the
// texture through a UNORM view (the bytes as the RGBA upload wrote them: no sRGB encoding). Recorded into the upload
// command buffer, ahead of the frame that samples the texture.
static bool nv12_plane(TexBackend *p, VkFormat f, uint32_t w, uint32_t h, VkCommandBuffer cb) {
    if (p->image) vk_views_forget(p);  // (a new size: the old image is left to the driver's teardown - one per movie size)
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = f,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    *p = (TexBackend){0};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &p->image, &p->mem, NULL) != VK_SUCCESS) { p->image = VK_NULL_HANDLE; return false; }
    vk_image_init_layout(cb, p->image, VK_IMAGE_ASPECT_COLOR_BIT);
    p->format = f; p->width = w; p->height = h; p->depth = 1; p->levels = p->layers = p->samples = 1;
    p->aspect = VK_IMAGE_ASPECT_COLOR_BIT; p->base_type = VK_IMAGE_VIEW_TYPE_2D; p->gpu_owned = true;
    return true;
}

void vk_convert_nv12(TexBackend *b, const void *nv12, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    static TexBackend py, puv;
    static VkDescriptorSetLayout dsl;
    static VkPipelineLayout layout;
    static VkPipeline pipes[SF2C_EXEC_N_2_D473];  // RGBA8, BGRA8 targets
    VkCommandBuffer cb = vk_upload_cmd();
    bool bgra = b->format == VK_FORMAT_B8G8R8A8_UNORM || b->format == VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat raw = bgra ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    if (!layout) {
        VkDescriptorSetLayoutBinding bd[SF2C_EXEC_N_2_D473] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                                              {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
        VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = SF2C_EXEC_N_2_D473, .pBindings = bd};
        VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &li, NULL, &dsl));
        VkPipelineLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &dsl};
        VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &ci, NULL, &layout));
        LOG(SF2C_EXEC_S_MOVIE_VIDEO_FRAMES_CONVERTED_1AB6);
    }
    if (!pipes[bgra]) {
        VkPipelineShaderStageCreateInfo stages[SF2C_EXEC_N_2_D473] = {
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vk_builtin_shader(SF2C_EXEC_S_NV12_VERT_BCD1), .pName = SF2C_EXEC_S_MAIN_E4A9},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = vk_builtin_shader(SF2C_EXEC_S_NV12_FRAG_482D), .pName = SF2C_EXEC_S_MAIN_E4A9}};
        VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                     .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineViewportStateCreateInfo vs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1,
                                                .scissorCount = 1};
        VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1};
        VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                   .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = SF2C_EXEC_N_0XF_7419};
        VkPipelineColorBlendStateCreateInfo cbs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1,
                                                   .pAttachments = &cba};
        VkDynamicState dyn[SF2C_EXEC_N_3_4E07] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        uint32_t ndyn = SF2C_EXEC_N_2_D473;
        fullscreen_vertex_dyn(dyn, &ndyn, SF2C_EXEC_N_3_4E07);
        VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = ndyn,
                                               .pDynamicStates = dyn};
        VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
                                            .pColorAttachmentFormats = &raw};
        VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri, .stageCount = SF2C_EXEC_N_2_D473,
                                           .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
                                           .pViewportState = &vs, .pRasterizationState = &rs, .pMultisampleState = &ms,
                                           .pDepthStencilState = &ds, .pColorBlendState = &cbs, .pDynamicState = &dy, .layout = layout};
        if (vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &pi, NULL, &pipes[bgra]) != VK_SUCCESS) {
            LOG(SF2C_EXEC_S_MOVIE_NV12_PIPELINE_FAILED_N_3D76);
            pipes[bgra] = VK_NULL_HANDLE;
            return;
        }
    }
    if (py.width != w || py.height != h || !py.image)
        if (!nv12_plane(&py, VK_FORMAT_R8_UNORM, w, h, cb) || !nv12_plane(&puv, VK_FORMAT_R8G8_UNORM, w / SF2C_EXEC_N_2_D473, h / SF2C_EXEC_N_2_D473, cb)) return;
    VkComponentMapping ident = {0};  // (VK_COMPONENT_SWIZZLE_IDENTITY)
    VkImageView vy = vk_image_view(&py, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8_UNORM, 0, 1, 0, 1, ident, VK_IMAGE_ASPECT_COLOR_BIT);
    VkImageView vuv = vk_image_view(&puv, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8_UNORM, 0, 1, 0, 1, ident, VK_IMAGE_ASPECT_COLOR_BIT);
    VkImageView vt = vk_image_view(b, VK_IMAGE_VIEW_TYPE_2D, raw, 0, 1, 0, 1, ident, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!vy || !vuv || !vt) return;
    VkSpan s = vk_transient_copy(nv12, (uint64_t)uv_off + (uint64_t)stride * (h / SF2C_EXEC_N_2_D473), SF2C_EXEC_N_16_B17E);
    VkBufferImageCopy c[SF2C_EXEC_N_2_D473] = {
        {.bufferOffset = s.off, .bufferRowLength = stride, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
         .imageExtent = {w, h, 1}},
        {.bufferOffset = s.off + uv_off, .bufferRowLength = stride / SF2C_EXEC_N_2_D473, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
         .imageExtent = {w / SF2C_EXEC_N_2_D473, h / SF2C_EXEC_N_2_D473, 1}}};
    vk_full_barrier(cb);
    rec_CmdCopyBufferToImage(cb, s.buf, py.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c[0]);
    rec_CmdCopyBufferToImage(cb, s.buf, puv.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c[1]);
    vk_full_barrier(cb);
    VkDescriptorSet set = vk_alloc_set(dsl);
    VkDescriptorImageInfo ii[SF2C_EXEC_N_2_D473] = {{vk_default_sampler(true), vy, VK_IMAGE_LAYOUT_GENERAL},
                                   {vk_default_sampler(true), vuv, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[SF2C_EXEC_N_2_D473] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii[0]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii[1]}};
    rec_UpdateDescriptorSets(SF2C_EXEC_N_2_D473, wr);
    uint32_t rw = w < b->width ? w : b->width, rh = h < b->height ? h : b->height;
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vt,
                                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {rw, rh}}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    rec_CmdBeginRendering(cb, &ri);
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[bgra]);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    VkViewport vp = {0, 0, (float)rw, (float)rh, 0, 1};
    VkRect2D sc = {{0, 0}, {rw, rh}};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    fullscreen_vertex_bind(cb);
    rec_CmdDraw(cb, SF2C_EXEC_N_3_4E07, 1, 0, 0);
    rec_CmdEndRendering(cb);
    vk_full_barrier(cb);
}

static VkPipeline present_pipeline(VkFormat fmt) {
    static struct { VkFormat f; VkPipeline p; } cache[SF2C_EXEC_N_4_4B22];
    for (int i = 0; i < SF2C_EXEC_N_4_4B22; i++) if (cache[i].p && cache[i].f == fmt) return cache[i].p;
    if (!present_layout) {
        VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &b};
        VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &li, NULL, &present_set_layout));
        // (vertex: flip + crop, 32 bytes; fragment: the screen filter, 16 more)
        VkPushConstantRange r = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_48_9801};
        VkPipelineLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                         .pSetLayouts = &present_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &r};
        VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &ci, NULL, &present_layout));
    }
    VkPipelineShaderStageCreateInfo stages[SF2C_EXEC_N_2_D473] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vk_builtin_shader(SF2C_EXEC_S_PRESENT_VERT_0EDD), .pName = SF2C_EXEC_S_MAIN_E4A9},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = vk_builtin_shader(SF2C_EXEC_S_PRESENT_FRAG_EEE9), .pName = SF2C_EXEC_S_MAIN_E4A9}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = SF2C_EXEC_N_0XF_7419};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1,
                                              .pAttachments = &cba};
    VkDynamicState dyn[SF2C_EXEC_N_3_4E07] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    uint32_t ndyn = SF2C_EXEC_N_2_D473;
    fullscreen_vertex_dyn(dyn, &ndyn, SF2C_EXEC_N_3_4E07);
    VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = ndyn, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
                                        .pColorAttachmentFormats = &fmt};
    VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri, .stageCount = SF2C_EXEC_N_2_D473,
                                       .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vs,
                                       .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds,
                                       .pColorBlendState = &cb, .pDynamicState = &dy, .layout = present_layout};
    VkPipeline p;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &pi, NULL, &p));
    for (int i = 0; i < SF2C_EXEC_N_4_4B22; i++) if (!cache[i].p) { cache[i].f = fmt; cache[i].p = p; break; }
    return p;
}

// Scanout shows the bytes the game wrote: sRGB images are read through a UNORM view (no decode), like metal.m.
static VkImageView raw_view(TexBackend *b) {
    VkFormat f = b->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM
               : b->format == VK_FORMAT_B8G8R8A8_SRGB ? VK_FORMAT_B8G8R8A8_UNORM : b->format;
    static const VkComponentMapping id = {0};
    return vk_image_view(b, VK_IMAGE_VIEW_TYPE_2D, f, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ---- post passes (Video tab "Anti-aliasing" / "Upscaler"): SMAA 1x at the game image's size, then AMD FSR 1 (EASU
// upscale to the image's size in the window, RCAS sharpen) when it is smaller than its place in the window, as
// metal.m does with MetalFX. Each pass: a fullscreen triangle into an RGBA8 image (gamma-encoded, like the input);
// up to three inputs (bindings 0-2), sampled linear / clamp.
static VkPipelineLayout post_layout;
static VkDescriptorSetLayout post_set_layout;

// kind: 0 a plain post pass; 1 SMAA edges also writing stencil 1 where they draw, 2 SMAA weights only where the
// stencil is 1 (g_vk.smaa_stencil; dsfmt: the stencil format)
static VkPipeline post_pipeline_kind(const char *fs, int kind, VkFormat dsfmt) {
    static struct { const char *fs; int kind; VkFormat f; VkPipeline p; } cache[SF2C_EXEC_N_16_B17E];
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) if (cache[i].p && cache[i].fs == fs && cache[i].kind == kind && cache[i].f == dsfmt) return cache[i].p;
    if (!post_layout) {
        VkDescriptorSetLayoutBinding b[SF2C_EXEC_N_3_4E07];
        for (uint32_t i = 0; i < SF2C_EXEC_N_3_4E07; i++)
            b[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = SF2C_EXEC_N_3_4E07, .pBindings = b};
        VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &li, NULL, &post_set_layout));
        VkPushConstantRange r = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_32_E29C};
        VkPipelineLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                         .pSetLayouts = &post_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &r};
        VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &ci, NULL, &post_layout));
    }
    VkPipelineShaderStageCreateInfo stages[SF2C_EXEC_N_2_D473] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vk_builtin_shader(SF2C_EXEC_S_POST_VERT_500E), .pName = SF2C_EXEC_S_MAIN_E4A9},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = vk_builtin_shader(fs), .pName = SF2C_EXEC_S_MAIN_E4A9}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = SF2C_EXEC_N_0XF_7419};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1,
                                              .pAttachments = &cba};
    VkDynamicState dyn[SF2C_EXEC_N_3_4E07] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    uint32_t ndyn = SF2C_EXEC_N_2_D473;
    fullscreen_vertex_dyn(dyn, &ndyn, SF2C_EXEC_N_3_4E07);
    VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = ndyn, .pDynamicStates = dyn};
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
                                        .pColorAttachmentFormats = &fmt};
    VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (kind == 1 || kind == SF2C_EXEC_N_2_D473) {
        ri.stencilAttachmentFormat = dsfmt;
        VkStencilOpState so = {.failOp = VK_STENCIL_OP_KEEP, .passOp = kind == 1 ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP,
                               .depthFailOp = VK_STENCIL_OP_KEEP, .compareOp = kind == 1 ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL,
                               .compareMask = SF2C_EXEC_N_0XFF_7312, .writeMask = kind == 1 ? SF2C_EXEC_N_0XFF_7312 : 0, .reference = 1};
        ds.stencilTestEnable = VK_TRUE;
        ds.front = ds.back = so;
    }
    VkGraphicsPipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri, .stageCount = SF2C_EXEC_N_2_D473,
                                       .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vs,
                                       .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds,
                                       .pColorBlendState = &cb, .pDynamicState = &dy, .layout = post_layout};
    VkPipeline p;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &pi, NULL, &p));
    for (int i = 0; i < SF2C_EXEC_N_16_B17E; i++) if (!cache[i].p) { cache[i].fs = fs; cache[i].kind = kind; cache[i].f = dsfmt; cache[i].p = p; break; }
    return p;
}

// Post image `i` at w x h (RGBA8, kept until the size changes). 0, 1: SMAA edges / weights, 2: SMAA output,
// 3, 4: FSR EASU / RCAS.
static TexBackend *post_image(int i, uint32_t w, uint32_t h) {
    static TexBackend img[SF2C_EXEC_N_5_EF2D];
    TexBackend *d = &img[i];
    if (d->image && (d->width != w || d->height != h)) {
        vk_views_forget(d);
        vk_defer_image(d->image, d->mem);
        d->image = VK_NULL_HANDLE;
        d->mem = VK_NULL_HANDLE;
    }
    if (!d->image) {
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &d->image, &d->mem, NULL));
        d->format = VK_FORMAT_R8G8B8A8_UNORM; d->width = w; d->height = h; d->depth = 1; d->layers = 1; d->levels = 1;
        d->samples = 1; d->aspect = VK_IMAGE_ASPECT_COLOR_BIT; d->base_type = VK_IMAGE_VIEW_TYPE_2D;
        vk_image_init_layout(vk_cmd(), d->image, d->aspect);
    }
    return d;
}

static VkImageView post_view(TexBackend *d) {
    static const VkComponentMapping id = {0};
    return vk_image_view(d, VK_IMAGE_VIEW_TYPE_2D, d->format, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
}

// SMAA's lookup textures (runtime/nvn/smaa_data.c): 0 = area (160 x 560 R8G8), 1 = search (64 x 16 R8), uploaded once.
static VkImageView smaa_lut(int i) {
    static TexBackend t[SF2C_EXEC_N_2_D473];
    TexBackend *d = &t[i];
    if (!d->image) {
        extern const unsigned char *smaa_area_tex(void);
        extern const unsigned char *smaa_search_tex(void);
        uint32_t w = i ? SF2C_EXEC_N_64_A68B : SF2C_EXEC_N_160_A512, h = i ? SF2C_EXEC_N_16_B17E : SF2C_EXEC_N_560_6BCA, bpp = i ? 1 : SF2C_EXEC_N_2_D473;
        VkFormat f = i ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = f,
                                .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                                .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &d->image, &d->mem, NULL));
        d->format = f; d->width = w; d->height = h; d->depth = 1; d->layers = 1; d->levels = 1;
        d->samples = 1; d->aspect = VK_IMAGE_ASPECT_COLOR_BIT; d->base_type = VK_IMAGE_VIEW_TYPE_2D;
        VkCommandBuffer cb = vk_cmd();
        vk_image_init_layout(cb, d->image, d->aspect);
        VkSpan s = vk_transient_copy(i ? smaa_search_tex() : smaa_area_tex(), (uint64_t)w * h * bpp, SF2C_EXEC_N_16_B17E);
        VkBufferImageCopy c = {.bufferOffset = s.off, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
        rec_CmdCopyBufferToImage(cb, s.buf, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    }
    return post_view(d);
}

// One post pass (outside the window image's rendering): srcs (bindings 0..n-1) -> dst, fragment shader `fs`.
// clear: start from zero (SMAA's edge pass only writes edge pixels; its weights / output read them).
// post_area_w / _h (> 0): only that top-left part of dst (Dynamic resolution: the contents' part)
static uint32_t post_area_w, post_area_h;
// (g_vk.smaa_stencil) a stencil image at w x h for SMAA's edge mask (kept until the size changes; NULL: none usable)
static TexBackend *smaa_stencil_image(VkCommandBuffer cb, uint32_t w, uint32_t h) {
    static TexBackend img;
    static bool unusable;
    TexBackend *d = &img;
    if (unusable) return NULL;
    if (d->image && (d->width != w || d->height != h)) {
        vk_views_forget(d);
        vk_defer_image(d->image, d->mem);
        d->image = VK_NULL_HANDLE;
        d->mem = VK_NULL_HANDLE;
    }
    if (!d->image) {
        static const VkFormat cand[SF2C_EXEC_N_3_4E07] = {VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
        VkFormat f = VK_FORMAT_UNDEFINED;
        for (int i = 0; i < SF2C_EXEC_N_3_4E07 && !f; i++) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(g_vk.phys, cand[i], &fp);
            if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) f = cand[i];
        }
        if (!f) { unusable = true; LOG(SF2C_EXEC_S_VK_SMAA_STENCIL_NO_STENCIL_F_2F53); return NULL; }
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = f,
                                .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                                .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateImage(g_vk.vma, &ci, &ai, &d->image, &d->mem, NULL) != VK_SUCCESS) { d->image = VK_NULL_HANDLE; return NULL; }
        d->format = f; d->width = w; d->height = h; d->depth = 1; d->layers = 1; d->levels = 1; d->samples = 1;
        d->aspect = f == VK_FORMAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        d->base_type = VK_IMAGE_VIEW_TYPE_2D;
        vk_image_init_layout(cb, d->image, d->aspect);
        LOG(SF2C_EXEC_S_VK_SMAA_STENCIL_IMAGE_UX_U_F_818B, w, h, f);
    }
    return d;
}

static void post_pass_ex(VkCommandBuffer cb, const char *fs, const VkImageView *srcs, int n, TexBackend *dst, const float pc[SF2C_EXEC_N_8_2C62],
                         bool clear, TexBackend *sten, int kind);
static void post_pass(VkCommandBuffer cb, const char *fs, const VkImageView *srcs, int n, TexBackend *dst, const float pc[SF2C_EXEC_N_8_2C62],
                      bool clear) {
    post_pass_ex(cb, fs, srcs, n, dst, pc, clear, NULL, 0);
}
// sten / kind (g_vk.smaa_stencil): kind 1 clears sten and writes 1 where the pass draws, kind 2 draws only there
static void post_pass_ex(VkCommandBuffer cb, const char *fs, const VkImageView *srcs, int n, TexBackend *dst, const float pc[SF2C_EXEC_N_8_2C62],
                         bool clear, TexBackend *sten, int kind) {
    if (!sten) kind = 0;
    uint32_t aw = post_area_w && post_area_w < dst->width ? post_area_w : dst->width;
    uint32_t ah = post_area_h && post_area_h < dst->height ? post_area_h : dst->height;
    // (before the rendering begins: created inside it, the NVIDIA driver crashed)
    VkPipeline pipe = post_pipeline_kind(fs, kind, sten ? sten->format : VK_FORMAT_UNDEFINED);
    vk_full_barrier(cb);
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = post_view(dst),
                                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                    .loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {aw, ah}},
                          .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &ca};
    VkRenderingAttachmentInfo sa = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (kind) {
        static const VkComponentMapping id = {0};
        sa.imageView = vk_image_view(sten, VK_IMAGE_VIEW_TYPE_2D, sten->format, 0, 1, 0, 1, id, sten->aspect);
        sa.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        sa.loadOp = kind == 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        sa.storeOp = kind == 1 ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ri.pStencilAttachment = &sa;
    }
    rec_CmdBeginRendering(cb, &ri);
    VkDescriptorSet ds = vk_alloc_set(post_set_layout);
    VkDescriptorImageInfo ii[SF2C_EXEC_N_3_4E07];
    VkWriteDescriptorSet w[SF2C_EXEC_N_3_4E07];
    for (int i = 0; i < n; i++) {
        ii[i] = (VkDescriptorImageInfo){vk_default_sampler(true), srcs[i], VK_IMAGE_LAYOUT_GENERAL};
        w[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .dstBinding = (uint32_t)i,
                                      .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .pImageInfo = &ii[i]};
    }
    rec_UpdateDescriptorSets((uint32_t)n, w);
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, post_layout, 0, 1, &ds, 0, NULL);
    VkViewport vp = {0, 0, (float)dst->width, (float)dst->height, 0, 1};
    VkRect2D sc = {{0, 0}, {aw, ah}};  // (the viewport stays the whole image: gl_FragCoord is the pixel)
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    rec_CmdPushConstants(cb, post_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_32_E29C, pc);
    fullscreen_vertex_bind(cb);
    rec_CmdDraw(cb, SF2C_EXEC_N_3_4E07, 1, 0, 0);
    rec_CmdEndRendering(cb);
}

// The view the present samples: the game image, or its anti-aliased / upscaled version. ow x oh: the whole image's
// size in the window (its crop is drawn into the viewport).
static VkImageView post_process(VkCommandBuffer cb, TexBackend *b, float fx, float fy, double ow_d, double oh_d, bool *upscaled) {
    VkImageView v = raw_view(b);
    bool smaa = g_settings.antialias == AA_SMAA;
    uint32_t ow = (uint32_t)lround(ow_d), oh = (uint32_t)lround(oh_d);
    if (ow > SF2C_EXEC_N_16384_CA90) ow = SF2C_EXEC_N_16384_CA90;
    if (oh > SF2C_EXEC_N_16384_CA90) oh = SF2C_EXEC_N_16384_CA90;
    uint32_t cw = (uint32_t)lroundf(b->width * fx), ch = (uint32_t)lroundf(b->height * fy);  // (the contents' size)
    bool fsr = g_settings.upscaler == UPSCALE_FSR1 && (ow > cw + 1 || oh > ch + 1);  // (an upscale only)
    *upscaled = fsr;
    if (!smaa && !fsr) return v;
    extern void vk_frame_suspend(void);
    extern void vk_frame_resume(void);
    vk_frame_suspend();
    if (smaa) {  // SMAA 1x: luma edges -> blending weights -> neighbourhood blending
        float w = (float)b->width, h = (float)b->height;
        float m[SF2C_EXEC_N_8_2C62] = {SF2C_EXEC_N_1_0F_CA71 / w, SF2C_EXEC_N_1_0F_CA71 / h, w, h};
        TexBackend *edges = post_image(0, b->width, b->height), *weights = post_image(1, b->width, b->height);
        TexBackend *out = post_image(SF2C_EXEC_N_2_D473, b->width, b->height);
        // Dynamic resolution: only the contents' part, and 64 pixels past it (the edge searches reach 2 x 16 pixels, so
        // the contents' pixels never read past it) - the window step's cost then scales with the factor like the game's
        post_area_w = cw + SF2C_EXEC_N_64_A68B, post_area_h = ch + SF2C_EXEC_N_64_A68B;
        // (g_vk.smaa_stencil, integrated GPUs) the weights only where the edge pass found edges (its shader discards
        // the others), as SMAA's reference integration does with a stencil
        TexBackend *sten = g_vk.smaa_stencil ? smaa_stencil_image(cb, b->width, b->height) : NULL;
        VkImageView in[SF2C_EXEC_N_3_4E07] = {v};
        post_pass_ex(cb, SF2C_EXEC_S_SMAA_EDGE_FRAG_9084, in, 1, edges, m, true, sten, 1);
        VkImageView wi[SF2C_EXEC_N_3_4E07] = {post_view(edges), smaa_lut(0), smaa_lut(1)};
        post_pass_ex(cb, SF2C_EXEC_S_SMAA_WEIGHT_FRAG_BA04, wi, SF2C_EXEC_N_3_4E07, weights, m, true, sten, SF2C_EXEC_N_2_D473);
        VkImageView bi[SF2C_EXEC_N_3_4E07] = {v, post_view(weights)};
        post_pass(cb, SF2C_EXEC_S_SMAA_BLEND_FRAG_A526, bi, SF2C_EXEC_N_2_D473, out, m, false);
        post_area_w = post_area_h = 0;
        v = post_view(out);
    }
    if (fsr) {
        float iw = (float)cw, ih = (float)ch;  // (EASU fetches texels of the contents: a larger texture needs no uv scale)
        TexBackend *e = post_image(SF2C_EXEC_N_3_4E07, ow, oh), *r = post_image(SF2C_EXEC_N_4_4B22, ow, oh);
        float pe[SF2C_EXEC_N_8_2C62] = {iw / ow, ih / oh, SF2C_EXEC_N_0_5F_2DB0 * iw / ow - SF2C_EXEC_N_0_5F_2DB0, SF2C_EXEC_N_0_5F_2DB0 * ih / oh - SF2C_EXEC_N_0_5F_2DB0, iw, ih};
        VkImageView ei[SF2C_EXEC_N_3_4E07] = {v};
        post_pass(cb, SF2C_EXEC_S_EASU_FRAG_E8DD, ei, 1, e, pe, false);
        float pr[SF2C_EXEC_N_8_2C62] = {exp2f(-SF2C_EXEC_N_0_2F_ED8E), 0, 0, 0, (float)ow, (float)oh};  // (sharpness 0.2 stops: AMD's default)
        VkImageView ri_[SF2C_EXEC_N_3_4E07] = {post_view(e)};
        post_pass(cb, SF2C_EXEC_S_RCAS_FRAG_2A91, ri_, 1, r, pr, false);
        v = post_view(r);
    }
    vk_full_barrier(cb);
    vk_frame_resume();
    {
        static uint32_t piw, pih, pow_, poh;
        static int psm = -1, pfs = -1;
        if (b->width != piw || b->height != pih || ow != pow_ || oh != poh || smaa != psm || fsr != pfs)
            LOG(SF2C_EXEC_S_POST_UX_U_S_S_UX_U_N_B1B2, b->width, b->height, smaa ? SF2C_EXEC_S_SMAA_03DF : SF2C_EXEC_S_EMPTY_12AE, fsr ? SF2C_EXEC_S_FSR1_51D3 : SF2C_EXEC_S_EMPTY_12AE, ow, oh);
        piw = b->width; pih = b->height; pow_ = ow; poh = oh; psm = smaa; pfs = fsr;
    }
    return v;
}

// Inside the window image's rendering (device.c vk_frame_begin): the game image into (vx, vy, vw, vh).
void vk_present_draw(VkCommandBuffer cb, TexBackend *b, float vx, float vy, float vw, float vh, const float pv[SF2C_EXEC_N_8_2C62]) {
    if (!b || !b->image || (b->aspect & ~VK_IMAGE_ASPECT_COLOR_BIT)) return;
    // Dynamic: the image occupies the top-left part of the texture; FSR maps that part to its whole output, otherwise
    // the present samples the part
    float fx = b->scalable && b->ax > 0 ? b->sx / b->ax : 1, fy = b->scalable && b->ay > 0 ? b->sy / b->ay : 1;
    bool upscaled = false;
    VkImageView src = post_process(cb, b, fx, fy, pv[SF2C_EXEC_N_6_E7F6] > 0 ? vw / pv[SF2C_EXEC_N_6_E7F6] : vw, pv[SF2C_EXEC_N_7_7902] > 0 ? vh / pv[SF2C_EXEC_N_7_7902] : vh, &upscaled);
    float pvc[SF2C_EXEC_N_8_2C62];
    memcpy(pvc, pv, sizeof pvc);
    if (!upscaled) { pvc[SF2C_EXEC_N_4_4B22] *= fx; pvc[SF2C_EXEC_N_5_EF2D] *= fy; pvc[SF2C_EXEC_N_6_E7F6] *= fx; pvc[SF2C_EXEC_N_7_7902] *= fy; }
    pv = pvc;
    VkPipeline p = present_pipeline(g_vk.sc_format);
    VkDescriptorSet ds = vk_alloc_set(present_set_layout);
    VkDescriptorImageInfo ii = {vk_default_sampler(true), src, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .dstBinding = 0, .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii};
    rec_UpdateDescriptorSets(1, &w);
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, present_layout, 0, 1, &ds, 0, NULL);
    VkViewport vp = {vx, vy, vw, vh, 0, 1};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    float pc[SF2C_EXEC_N_12_6B51];
    memcpy(pc, pv, SF2C_EXEC_N_32_E29C);
    screen_filter_params(pc + SF2C_EXEC_N_8_2C62);
    rec_CmdPushConstants(cb, present_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_48_9801, pc);
    fullscreen_vertex_bind(cb);
    rec_CmdDraw(cb, SF2C_EXEC_N_3_4E07, 1, 0, 0);
    VkViewport full = {0, 0, (float)g_vk.sc_extent.width, (float)g_vk.sc_extent.height, 0, 1};
    rec_CmdSetViewport(cb, 0, 1, &full);
}

// Screenshot of a game texture (presented crop applied to build/frame_* shots), via the present shader into RGBA8.
// Only the part its contents occupy (Dynamic resolution: the top-left part; the rest holds older frames).
void renderer_save_texture_tex(TexBackend *b, const char *path) {
    if (!b || !b->image || (b->aspect & ~VK_IMAGE_ASPECT_COLOR_BIT)) return;
    extern const float *vk_shot_crop(void);
    float crop[SF2C_EXEC_N_4_4B22] = {0, 0, 1, 1};
    if (strstr(path, SF2C_EXEC_S_FRAME_EE06)) memcpy(crop, vk_shot_crop(), sizeof crop);
    float fx = (float)content_extent((int)b->width, b->scalable, b->sx, b->ax) / (float)b->width;
    float fy = (float)content_extent((int)b->height, b->scalable, b->sy, b->ay) / (float)b->height;
    crop[0] *= fx; crop[1] *= fy; crop[SF2C_EXEC_N_2_D473] *= fx; crop[SF2C_EXEC_N_3_4E07] *= fy;
    uint32_t w = (uint32_t)(b->width * crop[SF2C_EXEC_N_2_D473]), h = (uint32_t)(b->height * crop[SF2C_EXEC_N_3_4E07]);
    if (!w || !h) return;
    vk_end_pass();
    VkImage img;
    VmaAllocation mem;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &img, &mem, NULL));
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    vk_image_init_layout(cb, img, VK_IMAGE_ASPECT_COLOR_BIT);
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img, .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = VK_FORMAT_R8G8B8A8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView tv;
    VK_CHECK(vkCreateImageView(g_vk.dev, &vci, NULL, &tv));
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = tv, .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {w, h}}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    rec_CmdBeginRendering(cb, &ri);
    VkPipeline p = present_pipeline(VK_FORMAT_R8G8B8A8_UNORM);
    VkDescriptorSet ds = vk_alloc_set(present_set_layout);
    VkDescriptorImageInfo ii = {vk_default_sampler(true), raw_view(b), VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .dstBinding = 0, .descriptorCount = 1,
                               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii};
    rec_UpdateDescriptorSets(1, &wr);
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, present_layout, 0, 1, &ds, 0, NULL);
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1};
    VkRect2D sc = {{0, 0}, {w, h}};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    float pv[SF2C_EXEC_N_12_6B51] = {renderer_origin_lower_left() && !strstr(path, SF2C_EXEC_S_WINDOW_D4D5) ? SF2C_EXEC_N_1_0F_CA71 : SF2C_EXEC_N_0_0F_6895, 0, 0, 0, crop[0], crop[1], crop[SF2C_EXEC_N_2_D473], crop[SF2C_EXEC_N_3_4E07]};
    if (strstr(path, SF2C_EXEC_S_FRAME_EE06)) screen_filter_params(pv + SF2C_EXEC_N_8_2C62);  // (shots of the game image: as shown)
    rec_CmdPushConstants(cb, present_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, SF2C_EXEC_N_48_9801, pv);
    fullscreen_vertex_bind(cb);
    rec_CmdDraw(cb, SF2C_EXEC_N_3_4E07, 1, 0, 0);
    rec_CmdEndRendering(cb);
    vk_full_barrier(cb);
    VkSpan out = vk_readback((uint64_t)w * h * SF2C_EXEC_N_4_4B22);
    VkBufferImageCopy c = {.bufferOffset = out.off, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
    rec_CmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_GENERAL, out.buf, 1, &c);
    renderer_flush();
    vk_save_png(path, out.ptr, w, h);
    vkDestroyImageView(g_vk.dev, tv, NULL);
    vmaDestroyImage(g_vk.vma, img, mem);
}

static const char *op_names[CMD_COUNT] = {
    [CMD_END] = SF2C_EXEC_S_CMD_END_4780,
    [CMD_JUMP] = SF2C_EXEC_S_CMD_JUMP_2C42,
    [CMD_CALL] = SF2C_EXEC_S_CMD_CALL_4954,
    [CMD_BIND_PROGRAM] = SF2C_EXEC_S_CMD_BIND_PROGRAM_0B68,
    [CMD_BIND_BLEND] = SF2C_EXEC_S_CMD_BIND_BLEND_06AE,
    [CMD_BIND_CHANNEL_MASK] = SF2C_EXEC_S_CMD_BIND_CHANNEL_MASK_1DD4,
    [CMD_BIND_COLOR] = SF2C_EXEC_S_CMD_BIND_COLOR_7E0D,
    [CMD_BIND_DEPTH_STENCIL] = SF2C_EXEC_S_CMD_BIND_DEPTH_STENCIL_0F9B,
    [CMD_BIND_MULTISAMPLE] = SF2C_EXEC_S_CMD_BIND_MULTISAMPLE_083D,
    [CMD_BIND_POLYGON] = SF2C_EXEC_S_CMD_BIND_POLYGON_A655,
    [CMD_BIND_VERTEX_ATTRIB] = SF2C_EXEC_S_CMD_BIND_VERTEX_ATTRIB_F4C5,
    [CMD_BIND_VERTEX_STREAM] = SF2C_EXEC_S_CMD_BIND_VERTEX_STREAM_E9EC,
    [CMD_BIND_VERTEX_BUFFERS] = SF2C_EXEC_S_CMD_BIND_VERTEX_BUFFERS_D786,
    [CMD_BIND_UNIFORM_BUFFERS] = SF2C_EXEC_S_CMD_BIND_UNIFORM_BUFFERS_0008,
    [CMD_BIND_STORAGE_BUFFERS] = SF2C_EXEC_S_CMD_BIND_STORAGE_BUFFERS_6941,
    [CMD_BIND_TEXTURES] = SF2C_EXEC_S_CMD_BIND_TEXTURES_5AA1,
    [CMD_BIND_IMAGES] = SF2C_EXEC_S_CMD_BIND_IMAGES_1CC1,
    [CMD_SET_TEXTURE_POOL] = SF2C_EXEC_S_CMD_SET_TEXTURE_POOL_FA9B,
    [CMD_SET_SAMPLER_POOL] = SF2C_EXEC_S_CMD_SET_SAMPLER_POOL_C29C,
    [CMD_SET_RENDER_TARGETS] = SF2C_EXEC_S_CMD_SET_RENDER_TARGETS_78F7,
    [CMD_SET_VIEWPORTS] = SF2C_EXEC_S_CMD_SET_VIEWPORTS_283D,
    [CMD_SET_SCISSORS] = SF2C_EXEC_S_CMD_SET_SCISSORS_7D99,
    [CMD_SET_DEPTH_RANGES] = SF2C_EXEC_S_CMD_SET_DEPTH_RANGES_EB2F,
    [CMD_SET_BLEND_COLOR] = SF2C_EXEC_S_CMD_SET_BLEND_COLOR_919A,
    [CMD_SET_STENCIL] = SF2C_EXEC_S_CMD_SET_STENCIL_33C1,
    [CMD_SET_PRIMITIVE_RESTART] = SF2C_EXEC_S_CMD_SET_PRIMITIVE_RESTART_6B6A,
    [CMD_SET_ALPHA_REF] = SF2C_EXEC_S_CMD_SET_ALPHA_REF_4708,
    [CMD_SET_DEPTH_BOUNDS] = SF2C_EXEC_S_CMD_SET_DEPTH_BOUNDS_6042,
    [CMD_SET_POLYGON_OFFSET] = SF2C_EXEC_S_CMD_SET_POLYGON_OFFSET_DBFC,
    [CMD_SET_LINE_WIDTH] = SF2C_EXEC_S_CMD_SET_LINE_WIDTH_1548,
    [CMD_SET_POINT_SIZE] = SF2C_EXEC_S_CMD_SET_POINT_SIZE_C45A,
    [CMD_SET_SAMPLE_MASK] = SF2C_EXEC_S_CMD_SET_SAMPLE_MASK_8628,
    [CMD_SET_RENDER_ENABLE] = SF2C_EXEC_S_CMD_SET_RENDER_ENABLE_1E67,
    [CMD_CLEAR_COLOR] = SF2C_EXEC_S_CMD_CLEAR_COLOR_843B,
    [CMD_CLEAR_DEPTH_STENCIL] = SF2C_EXEC_S_CMD_CLEAR_DEPTH_STENCIL_8CAE,
    [CMD_CLEAR_TEXTURE] = SF2C_EXEC_S_CMD_CLEAR_TEXTURE_BB18,
    [CMD_CLEAR_BUFFER] = SF2C_EXEC_S_CMD_CLEAR_BUFFER_8AC5,
    [CMD_DRAW_ARRAYS] = SF2C_EXEC_S_CMD_DRAW_ARRAYS_7B2B,
    [CMD_DRAW_ELEMENTS] = SF2C_EXEC_S_CMD_DRAW_ELEMENTS_A3A1,
    [CMD_DRAW_ARRAYS_INDIRECT] = SF2C_EXEC_S_CMD_DRAW_ARRAYS_INDIRECT_5D98,
    [CMD_DRAW_ELEMENTS_INDIRECT] = SF2C_EXEC_S_CMD_DRAW_ELEMENTS_INDIRECT_B333,
    [CMD_DISPATCH] = SF2C_EXEC_S_CMD_DISPATCH_8DB6,
    [CMD_DISPATCH_INDIRECT] = SF2C_EXEC_S_CMD_DISPATCH_INDIRECT_6E8D,
    [CMD_COPY_BUFFER_TO_BUFFER] = SF2C_EXEC_S_CMD_COPY_BUFFER_TO_BUFFER_D5D1,
    [CMD_COPY_BUFFER_TO_TEXTURE] = SF2C_EXEC_S_CMD_COPY_BUFFER_TO_TEXTURE_BC29,
    [CMD_COPY_TEXTURE_TO_BUFFER] = SF2C_EXEC_S_CMD_COPY_TEXTURE_TO_BUFFER_6219,
    [CMD_COPY_TEXTURE_TO_TEXTURE] = SF2C_EXEC_S_CMD_COPY_TEXTURE_TO_TEXTURE_C729,
    [CMD_DOWNSAMPLE] = SF2C_EXEC_S_CMD_DOWNSAMPLE_FF51,
    [CMD_UPDATE_UNIFORM_BUFFER] = SF2C_EXEC_S_CMD_UPDATE_UNIFORM_BUFFER_9480,
    [CMD_FENCE_SYNC] = SF2C_EXEC_S_CMD_FENCE_SYNC_D213,
    [CMD_WAIT_SYNC] = SF2C_EXEC_S_CMD_WAIT_SYNC_5E7A,
    [CMD_BARRIER] = SF2C_EXEC_S_CMD_BARRIER_46C9,
    [CMD_SIGNAL_EVENT] = SF2C_EXEC_S_CMD_SIGNAL_EVENT_8DA5,
    [CMD_WAIT_EVENT] = SF2C_EXEC_S_CMD_WAIT_EVENT_A9C4,
    [CMD_REPORT_COUNTER] = SF2C_EXEC_S_CMD_REPORT_COUNTER_1671,
    [CMD_DISCARD_COLOR] = SF2C_EXEC_S_CMD_DISCARD_COLOR_A053,
    [CMD_DISCARD_DEPTH_STENCIL] = SF2C_EXEC_S_CMD_DISCARD_DEPTH_STENCIL_3D2C,
    [CMD_SET_COPY_STRIDES] = SF2C_EXEC_S_CMD_SET_COPY_STRIDES_162A,
    [CMD_DRAW_TEXTURE] = SF2C_EXEC_S_CMD_DRAW_TEXTURE_C8EB,
    [CMD_RESOLVE_DEPTH] = SF2C_EXEC_S_CMD_RESOLVE_DEPTH_E102,
};

// ================================================================== executor (renderer.m exec_list)
static void exec_list(uint64_t handle, int depth) {
    uint64_t p = handle;
    // Early stall note (the later system hang dump fires only when presenting stopped 30 s, by which time a driver
    // block may already be unrecoverable): if this exec stands still for LARECOMP_EXEC_STALL=<ms> (0 = every call,
    // 5 s default), print the stack and the command it sits on, once per exec.
    uint64_t stall_ns = UINT64_MAX;
    { static long stall_ms = -1;
      if (stall_ms < 0) {
          const char *e = getenv(SF2C_EXEC_S_LARECOMP_EXEC_STALL_F300);
          stall_ms = e ? atol(e) : depth == 0 ? SF2C_EXEC_N_5000_0F8E : 0;  // (nested execs of one frame are one logical call)
      }
      stall_ns = depth == 0 ? (uint64_t)(stall_ms > 0 ? stall_ms : UINT64_MAX / SF2C_EXEC_N_2_D473) * SF2C_EXEC_N_1000000ULL_6C52 : UINT64_MAX;
    }
    uint64_t t0_ = stall_ns == UINT64_MAX ? 0 : plat_now_ns();
    int stalled_op = -1;
    for (int guard = 0; guard < SF2C_EXEC_N_10000000_FA95; guard++) {
        CmdRec *r = (CmdRec *)(uintptr_t)p;
        void *x = r->extra;
        if (stall_ns != UINT64_MAX) {
            uint64_t el = plat_now_ns() - t0_;
            if (el > stall_ns && stalled_op != r->op) {
                stalled_op = r->op;
                LOG(SF2C_EXEC_S_EXEC_STALL_AFTER_LLU_MS_IN_E_F591,
                    (unsigned long long)(el / SF2C_EXEC_N_1000000ULL_6C52), (unsigned long long)handle,
                    r->op, (r->op < CMD_COUNT && op_names[r->op]) ? op_names[r->op] : SF2C_EXEC_S_UNKNOWN_0F15,
                    (unsigned long long)p);
                plat_print_backtrace();
            }
        }
        if (r->op < CMD_COUNT) op_counts[r->op]++;
        static int log_exec = -1;
        if (log_exec < 0) log_exec = getenv(SF2C_EXEC_S_LARECOMP_LOG_EXEC_695F) ? 1 : 0;
        if (log_exec) {
            const char *name = (r->op < CMD_COUNT && op_names[r->op]) ? op_names[r->op] : SF2C_EXEC_S_UNKNOWN_0F15;
            LOG(SF2C_EXEC_S_EXEC_LIST_P_LLX_OP_U_S_SIZE__C7A1, (unsigned long long)p, r->op, name, r->size);
        }
        if (r->size == 0 && r->op != CMD_END) {
            LOG(SF2C_EXEC_S_EXEC_LIST_ERROR_R_SIZE_0_AT__6004, (unsigned long long)p, r->op);
            break;
        }
        switch (r->op) {
        case CMD_END: return;
        case CMD_JUMP: p = r->x[0]; continue;
        case CMD_CALL: {
            const uint64_t *h = x;
            for (uint64_t i = 0; i < r->x[0] && depth < SF2C_EXEC_N_8_2C62; i++) exec_list(h[i], depth + 1);
            break;
        }
        case CMD_BIND_PROGRAM: {
            NvnProgram *p = (NvnProgram *)(uintptr_t)r->x[0];
            uint32_t mask = (uint32_t)r->x[1];
            if (!p) {
                st.program = NULL;
                st.combined_program.stage_mask = 0;
            } else if (!mask || (mask & SF2C_EXEC_N_3_4E07) == SF2C_EXEC_N_3_4E07 || (p->stage_mask & SF2C_EXEC_N_3_4E07) == SF2C_EXEC_N_3_4E07) {
                st.combined_program = *p;
                st.program = &st.combined_program;
            } else {
                for (int s = 0; s < SF2C_EXEC_N_6_E7F6; s++) {
                    if (mask & (1 << s)) {
                        st.combined_program.stages[s] = p->stages[s];
                        st.combined_program.stage_mask |= (1 << s);
                    }
                }
                st.program = &st.combined_program;
            }
            break;
        }
        case CMD_BIND_BLEND: { uint8_t *b = x; memcpy(st.blend[b[0] & SF2C_EXEC_N_7_7902], b, SF2C_EXEC_N_8_2C62); break; }
        case CMD_BIND_CHANNEL_MASK: memcpy(&st.channel_mask, x, SF2C_EXEC_N_4_4B22); break;
        case CMD_BIND_COLOR: memcpy(st.color_state, x, SF2C_EXEC_N_4_4B22); break;
        case CMD_BIND_DEPTH_STENCIL: memcpy(st.ds, x, SF2C_EXEC_N_8_2C62); break;
        case CMD_BIND_MULTISAMPLE: memcpy(st.ms, x, SF2C_EXEC_N_0X18_D47C); break;
        case CMD_BIND_POLYGON: memcpy(st.poly, x, SF2C_EXEC_N_4_4B22); break;
        case CMD_BIND_VERTEX_ATTRIB:
            st.nattrib = (int)r->x[0] > SF2C_EXEC_N_16_B17E ? SF2C_EXEC_N_16_B17E : (int)r->x[0];
            memcpy(st.attrib, x, st.nattrib * SF2C_EXEC_N_4_4B22);
            break;
        case CMD_BIND_VERTEX_STREAM:
            st.nstream = (int)r->x[0] > SF2C_EXEC_N_16_B17E ? SF2C_EXEC_N_16_B17E : (int)r->x[0];
            memcpy(st.stream, x, st.nstream * SF2C_EXEC_N_8_2C62);
            break;
        case CMD_BIND_VERTEX_BUFFERS:
            for (uint64_t i = 0; i < r->x[1] && r->x[0] + i < SF2C_EXEC_N_16_B17E; i++) memcpy(&st.vbuf[r->x[0] + i], (uint8_t *)x + i * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
            break;
        case CMD_BIND_UNIFORM_BUFFERS:
            for (uint64_t i = 0; i < r->x[SF2C_EXEC_N_2_D473] && r->x[1] + i < SF2C_EXEC_N_18_4EC9 && r->x[0] < SF2C_EXEC_N_6_E7F6; i++)
                memcpy(&st.ubuf[r->x[0]][r->x[1] + i], (uint8_t *)x + i * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
            break;
        case CMD_BIND_STORAGE_BUFFERS:
            for (uint64_t i = 0; i < r->x[SF2C_EXEC_N_2_D473] && r->x[1] + i < SF2C_EXEC_N_16_B17E && r->x[0] < SF2C_EXEC_N_6_E7F6; i++)
                memcpy(&st.sbuf[r->x[0]][r->x[1] + i], (uint8_t *)x + i * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
            break;
        case CMD_BIND_TEXTURES:
            for (uint64_t i = 0; i < r->x[SF2C_EXEC_N_2_D473] && r->x[1] + i < SF2C_EXEC_N_32_E29C && r->x[0] < SF2C_EXEC_N_6_E7F6; i++)
                memcpy(&st.tex[r->x[0]][r->x[1] + i], (uint8_t *)x + i * SF2C_EXEC_N_8_2C62, SF2C_EXEC_N_8_2C62);
            break;
        case CMD_BIND_IMAGES:
            for (uint64_t i = 0; i < r->x[SF2C_EXEC_N_2_D473] && r->x[1] + i < SF2C_EXEC_N_8_2C62 && r->x[0] < SF2C_EXEC_N_6_E7F6; i++)
                memcpy(&st.img[r->x[0]][r->x[1] + i], (uint8_t *)x + i * SF2C_EXEC_N_8_2C62, SF2C_EXEC_N_8_2C62);
            break;
        case CMD_SET_TEXTURE_POOL: st.tpool = (NvnDescPool *)(uintptr_t)r->x[0]; break;
        case CMD_SET_SAMPLER_POOL: st.spool = (NvnDescPool *)(uintptr_t)r->x[0]; break;
        case CMD_SET_RENDER_TARGETS: {
            const struct { uint64_t tex[SF2C_EXEC_N_8_2C62]; NvnTextureView views[SF2C_EXEC_N_8_2C62]; uint8_t has_view[SF2C_EXEC_N_8_2C62]; uint64_t depth;
                           NvnTextureView dview; uint8_t has_dview; } *rt = x;
            RtState prev = st.rt;  // the same targets again: keep the pass (a restart = full barrier + reload)
            memset(&st.rt, 0, sizeof st.rt);
            st.rt.ncolor = (int)r->x[0] > SF2C_EXEC_N_8_2C62 ? SF2C_EXEC_N_8_2C62 : (int)r->x[0];
            for (int i = 0; i < st.rt.ncolor; i++) {
                st.rt.color[i] = (NvnTexture *)(uintptr_t)rt->tex[i];
                st.rt.cview[i] = rt->views[i];
                st.rt.has_cview[i] = rt->has_view[i];
            }
            st.rt.depth = (NvnTexture *)(uintptr_t)rt->depth;
            st.rt.dview = rt->dview;
            st.rt.has_dview = rt->has_dview;
            static int always = -1;
            if (always < 0) always = getenv(SF2C_EXEC_S_LARECOMP_VK_PASS_RESTART_632A) != NULL;
            if (always || memcmp(&prev, &st.rt, sizeof prev)) st.rt_dirty = true;
            break;
        }
        case CMD_SET_VIEWPORTS:
            for (uint64_t i = 0; i < r->x[1] && r->x[0] + i < SF2C_EXEC_N_16_B17E; i++) memcpy(st.viewport[r->x[0] + i], (uint8_t *)x + i * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
            break;
        case CMD_SET_SCISSORS:
            for (uint64_t i = 0; i < r->x[1] && r->x[0] + i < SF2C_EXEC_N_16_B17E; i++) memcpy(st.scissor[r->x[0] + i], (uint8_t *)x + i * SF2C_EXEC_N_16_B17E, SF2C_EXEC_N_16_B17E);
            break;
        case CMD_SET_DEPTH_RANGES:
            for (uint64_t i = 0; i < r->x[1] && r->x[0] + i < SF2C_EXEC_N_16_B17E; i++) memcpy(st.depth_range[r->x[0] + i], (uint8_t *)x + i * SF2C_EXEC_N_8_2C62, SF2C_EXEC_N_8_2C62);
            break;
        case CMD_SET_BLEND_COLOR: memcpy(st.blend_color, x, SF2C_EXEC_N_16_B17E); break;
        case CMD_SET_STENCIL: {
            int face = (int)r->x[0];
            for (int f = 0; f < SF2C_EXEC_N_2_D473; f++) {
                if (!(face & (1 << f))) continue;
                if (r->x[SF2C_EXEC_N_6_E7F6] == 0) st.stencil_ref[f] = (uint32_t)r->x[1];
                else if (r->x[SF2C_EXEC_N_6_E7F6] == 1) st.stencil_mask[f] = (uint32_t)r->x[1];
                else st.stencil_vmask[f] = (uint32_t)r->x[1];
            }
            break;
        }
        case CMD_SET_POLYGON_OFFSET: memcpy(st.poly_offset, r->f, sizeof st.poly_offset); break;
        case CMD_SET_PRIMITIVE_RESTART: st.prim_restart = r->x[0] != 0; st.restart_index = (uint32_t)r->x[1]; break;
        case CMD_SET_COPY_STRIDES: st.copy_row_stride = r->x[0]; st.copy_image_stride = r->x[1]; break;
        case CMD_CLEAR_COLOR: {
            snap_reason = SF2C_EXEC_S_CLEAR_COLOR_289C; snapshots_invalidate(NULL);
            int index = (int)r->x[0];
            if (getenv(SF2C_EXEC_S_LARECOMP_LOG_CLEARS_EE39) && index < st.rt.ncolor && st.rt.color[index])
                LOG(SF2C_EXEC_S_CCLEAR_FRAME_LLU_IDX_D_UX_U__575C, (unsigned long long)renderer_present_count(), index,
                    st.rt.color[index]->width, st.rt.color[index]->height, st.rt.color[index]->format, (uint32_t)r->x[SF2C_EXEC_N_2_D473]);
            if (ct_on() && index < st.rt.ncolor && st.rt.color[index])
                LOG(SF2C_EXEC_S_CT_CLEAR_COLOR_P_MASK_X_SC_D_D43F, (void *)st.rt.color[index], (uint32_t)r->x[SF2C_EXEC_N_2_D473], st.scissor[0][0], st.scissor[0][1],
                    st.scissor[0][SF2C_EXEC_N_2_D473], st.scissor[0][SF2C_EXEC_N_3_4E07]);
            if (index < st.rt.ncolor && st.rt.color[index]) alias_before_write(st.rt.color[index], 0, SF2C_EXEC_S_CLEAR_0CD2);
            if (index < st.rt.ncolor && st.rt.color[index]) clear(index, (int)r->x[SF2C_EXEC_N_5_EF2D], x, (uint32_t)r->x[SF2C_EXEC_N_2_D473], false, 0, false, 0, 0);
            break;
        }
        case CMD_CLEAR_TEXTURE: {
            // ClearTexture(cmd, texture, view, region, color, mask): a render-target clear of the texture alone (renderer.m)
            const struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[SF2C_EXEC_N_6_E7F6]; float color[SF2C_EXEC_N_4_4B22]; } *ct = x;
            NvnTexture *t = (NvnTexture *)(uintptr_t)ct->tex;
            if (!t) break;
            snap_reason = SF2C_EXEC_S_CLEAR_TEXTURE_1718; snapshots_invalidate(NULL);
            alias_before_write(t, 0, SF2C_EXEC_S_CLEAR_0CD2);
            RtState saved = st.rt;
            int32_t sc[SF2C_EXEC_N_4_4B22];
            memcpy(sc, st.scissor[0], sizeof sc);
            vk_end_pass();
            memset(&st.rt, 0, sizeof st.rt);
            st.rt.ncolor = 1;
            st.rt.color[0] = t;
            if (ct->has_view) { st.rt.cview[0] = ct->view; st.rt.has_cview[0] = 1; }
            st.rt_dirty = true;
            const int32_t *rg = ct->region;
            if (rg[SF2C_EXEC_N_3_4E07] > 0 && rg[SF2C_EXEC_N_4_4B22] > 0) { st.scissor[0][0] = rg[0]; st.scissor[0][1] = rg[1]; st.scissor[0][SF2C_EXEC_N_2_D473] = rg[SF2C_EXEC_N_3_4E07]; st.scissor[0][SF2C_EXEC_N_3_4E07] = rg[SF2C_EXEC_N_4_4B22]; }
            else { st.scissor[0][0] = st.scissor[0][1] = 0; st.scissor[0][SF2C_EXEC_N_2_D473] = st.scissor[0][SF2C_EXEC_N_3_4E07] = SF2C_EXEC_N_16384_CA90; }
            clear(0, (int)r->x[SF2C_EXEC_N_6_E7F6], ct->color, (uint32_t)r->x[SF2C_EXEC_N_4_4B22] ? (uint32_t)r->x[SF2C_EXEC_N_4_4B22] : SF2C_EXEC_N_0XF_7419, false, 0, false, 0, 0);
            vk_end_pass();
            st.rt = saved;
            memcpy(st.scissor[0], sc, sizeof sc);
            st.rt_dirty = true;
            break;
        }
        case CMD_CLEAR_DEPTH_STENCIL:
            snap_reason = SF2C_EXEC_S_CLEAR_DEPTH_90D6; snapshots_invalidate(NULL);
            if (ct_on() && st.rt.depth)
                LOG(SF2C_EXEC_S_CT_CLEAR_DEPTH_P_D_D_S_D_SC__99C3, (void *)st.rt.depth, r->x[0] != 0, r->x[SF2C_EXEC_N_2_D473] != 0, st.scissor[0][0],
                    st.scissor[0][1], st.scissor[0][SF2C_EXEC_N_2_D473], st.scissor[0][SF2C_EXEC_N_3_4E07]);
            if (st.rt.depth && naliases) {  // (copy aliases: which aspects of the depth target this clear replaces whole)
                NvnTexture *d = st.rt.depth;
                bool whole = st.scissor[0][0] <= 0 && st.scissor[0][1] <= 0 && (int64_t)st.scissor[0][0] + st.scissor[0][SF2C_EXEC_N_2_D473] >= (int64_t)d->width &&
                             (int64_t)st.scissor[0][1] + st.scissor[0][SF2C_EXEC_N_3_4E07] >= (int64_t)d->height &&
                             !(st.rt.has_dview && (((st.rt.dview.setmask & 1) && st.rt.dview.base_level) ||
                                                   ((st.rt.dview.setmask & SF2C_EXEC_N_2_D473) && st.rt.dview.min_layer)));
                for (int i = 0; i < st.rt.ncolor; i++)  // (the clear covers the smallest attachment only)
                    if (st.rt.color[i] && (st.rt.color[i]->width < d->width || st.rt.color[i]->height < d->height)) whole = false;
                VkImageAspectFlags full = 0;
                if (whole && r->x[0] != 0) full |= VK_IMAGE_ASPECT_DEPTH_BIT;
                if (whole && renderer_format_has_stencil(d->format) && (r->x[SF2C_EXEC_N_2_D473] & SF2C_EXEC_N_0XFF_7312) == SF2C_EXEC_N_0XFF_7312) full |= VK_IMAGE_ASPECT_STENCIL_BIT;
                if (!renderer_format_has_stencil(d->format)) full |= VK_IMAGE_ASPECT_STENCIL_BIT;
                if (ct_on())
                    LOG(SF2C_EXEC_S_CT_ALIAS_CLEAR_P_FULL_X_WHOL_F5DF, (void *)d, full, whole,
                        (unsigned long long)r->x[0], (unsigned long long)r->x[1], (unsigned long long)r->x[SF2C_EXEC_N_2_D473], st.rt.has_dview,
                        st.rt.dview.setmask, st.rt.ncolor);
                alias_before_write(d, full, SF2C_EXEC_S_CLEAR_0CD2);
            }
            if (st.rt.depth) {
                bool has_stencil = renderer_format_has_stencil(st.rt.depth->format);
                clear(-1, 0, NULL, 0, r->x[0] != 0, r->f[0], has_stencil && r->x[SF2C_EXEC_N_2_D473] != 0, (uint32_t)r->x[1], (uint32_t)r->x[SF2C_EXEC_N_2_D473]);
            }
            break;
        case CMD_DISPATCH: dispatch_compute((uint32_t)r->x[0], (uint32_t)r->x[1], (uint32_t)r->x[SF2C_EXEC_N_2_D473]); break;
        case CMD_DRAW_ARRAYS: draw(r, false); break;
        case CMD_DRAW_ELEMENTS: draw(r, true); break;
        case CMD_UPDATE_UNIFORM_BUFFER:
            memcpy((void *)(uintptr_t)(r->x[0] + r->x[SF2C_EXEC_N_2_D473]), x, r->x[SF2C_EXEC_N_3_4E07]);
            guest_copy_flush();  // (cbuf copies shared within the submission are stale now)
            break;
        case CMD_FENCE_SYNC: {
            static int fence_flush = -1;
            if (fence_flush < 0) fence_flush = getenv(SF2C_EXEC_S_LARECOMP_FENCE_FLUSH_8086) ? 1 : 0;
            if (fence_flush) renderer_flush();
            extern void nvn_sync_set_seq(uint64_t guest, uint64_t seq);
            if (r->x[1]) nvn_sync_set_seq(r->x[1], renderer_fence_seq());
            break;
        }
        case CMD_REPORT_COUNTER: {
            // The game times its GPU work with these (two per frame) and drops to 30 fps when that looks slow: the
            // host clock now, replaced by the GPU's time at this point once the submission is done (cq_resolve).
            uint64_t *out = (uint64_t *)(uintptr_t)r->x[1];
            uint32_t type = (uint32_t)r->x[0];
            static uint32_t seen;
            if (type < SF2C_EXEC_N_32_E29C && !(seen & (SF2C_EXEC_N_1U_B8E6 << type))) { seen |= SF2C_EXEC_N_1U_B8E6 << type; LOG(SF2C_EXEC_S_NVN_REPORTCOUNTER_TYPE_U_N_590C, type); }
            int mode = vk_counter_mode();
            static uint64_t frame_seen = ~SF2C_EXEC_N_0ULL_DF1B, frame_ts;
            extern uint64_t vk_frame_counter(void);
            if (mode == 1 && vk_frame_counter() != frame_seen) { frame_seen = vk_frame_counter(); frame_ts = ns_to_gpu_ticks(plat_now_ns()); }
            if (!out) break;
            out[0] = 0;
            out[1] = mode == 1 ? frame_ts : mode == SF2C_EXEC_N_3_4E07 ? SDL_GetTicksNS() : ns_to_gpu_ticks(plat_now_ns());  // (3 legacy: ns, the old bug)
            static uint64_t *prev_out;  // (frame pacing: the pair's start)
            // (overlap) the game reads this pair before its GPU work is done: the end counter reports the latest
            // completed pair's GPU span after this pair's start (the frame before, as a frame-late measurement)
            if (mode == SF2C_EXEC_N_2_D473 && overlap_on() && prev_out && out - prev_out == SF2C_EXEC_N_2_D473) out[1] = prev_out[1] + atomic_load(&g_last_gpu_frame_ticks);
            prev_out = out;
            {   // (test) LARECOMP_TEST_COUNTER_DELAY_MS=<ms>: stall the host after a frame's first counter - a slow CPU
                static double delay = -1;
                static uint64_t delayed_frame = ~SF2C_EXEC_N_0ULL_DF1B;
                if (delay < 0) delay = getenv(SF2C_EXEC_S_LARECOMP_TEST_COUNTER_DELAY__C285) ? atof(getenv(SF2C_EXEC_S_LARECOMP_TEST_COUNTER_DELAY__C285)) : 0;
                if (delay > 0 && vk_frame_counter() != delayed_frame) {
                    delayed_frame = vk_frame_counter();
                    SDL_DelayPrecise((Uint64)(delay * SF2C_EXEC_N_1E6_8C96));
                }
            }
            if (mode == SF2C_EXEC_N_2_D473 && type == 0) {  // (type 0: timestamp)
                VkCommandBuffer cb = vk_cmd();  // (first: may begin the buffer, which resets the slot's queries)
                CounterQ *q = &cq[cur_sub];
                pthread_mutex_lock(&cq_lock);
                if (q->pool && q->n < CQ_MAX) {
                    rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, q->pool, q->n);
                    q->out[q->n++] = out;
                }
                pthread_mutex_unlock(&cq_lock);
            }
            break;
        }
        case CMD_COPY_TEXTURE_TO_TEXTURE: { const TexRefRec *t = x; copy_texture(&t[0], &t[1], r->x[SF2C_EXEC_N_6_E7F6]); break; }
        case CMD_DOWNSAMPLE: { const TexRefRec *t = x; copy_texture(&t[0], &t[1], 1); break; }
        case CMD_COPY_BUFFER_TO_TEXTURE: copy_buffer_to_texture(r->x[0], x); break;
        case CMD_COPY_TEXTURE_TO_BUFFER: copy_texture_to_buffer(x, r->x[SF2C_EXEC_N_3_4E07]); break;
        case CMD_COPY_BUFFER_TO_BUFFER: copy_buffer_to_buffer(r->x[0], r->x[1], r->x[SF2C_EXEC_N_2_D473]); break;
        default: break;
        }
        p += r->size;
    }
}

// The exec-stall watchdog: if an exec runs longer than LARECOMP_EXEC_STALL=<ms> (default 2000) without finishing,
// capture the executing thread's stack with a signal (a block inside one command is invisible to exec_list's own
// loop check above). One line of frames per stall, every stall_ms until the exec finishes.
static volatile int wd_done = 1;  // (plain int: the handler handshake, single writer pairs)
static volatile int wd_target_tid;  // (the executing thread's kernel TID: pthread_kill's EINVAL'd; tgkill is direct)
static volatile long wd_interval_ms;

static void *exec_wd_run(void *arg) {
    (void)arg;
    volatile int reported = 0;
    for (;;) {
        for (int i = 0; i < wd_interval_ms && !wd_done; i++) SDL_Delay(1);
        if (wd_done) return NULL;
        if (reported < SF2C_EXEC_N_5_EF2D) {
            int tid = wd_target_tid;
            bool alive = plat_host_thread_alive(tid);
            fprintf(stderr, SF2C_EXEC_S_EXEC_WATCHDOG_EXEC_IS_D_MS_I_B4A8,
                    reported * wd_interval_ms + wd_interval_ms, tid,
                    alive ? SF2C_EXEC_S_EXISTS_C852 : SF2C_EXEC_S_GONE_OR_INACCESSIBLE_0BE0);
            if (alive)
                plat_bt_other_thread_tid(tid, reported ? SF2C_EXEC_S_EXEC_WATCHDOG_STILL_STUCK_5520 : SF2C_EXEC_S_EXEC_WATCHDOG_EBD9);
        }
        reported++;
    }
}

void backend_execute(uint64_t handle) {
    if (trace_render) LOG(SF2C_EXEC_S_EXEC_BACKEND_EXECUTE_HANDLE__CA79, (unsigned long long)handle);
    if (!wd_interval_ms) {
        const char *e = getenv(SF2C_EXEC_S_LARECOMP_EXEC_STALL_F300);
        wd_interval_ms = e ? atol(e) : SF2C_EXEC_N_2000_81A8;
    }
    pthread_t wd;
    if (wd_interval_ms > 0) {
        wd_target_tid = plat_host_thread_id();
        wd_done = 0;
        if (pthread_create(&wd, NULL, exec_wd_run, NULL) == 0) pthread_detach(wd);
        else wd_done = 1;
    }
    PROF_T0();
    exec_list(handle, 0);
    if (trace_render) LOG(SF2C_EXEC_S_EXEC_BACKEND_EXECUTE_EXEC_LI_1E2E);
    wd_done = 1;
    PROF_ADD(prof_exec);
    if (prof) cum_exec += SDL_GetTicksNS() - prof_t0_;
    static int sync_submit = -1;
    if (sync_submit < 0) sync_submit = getenv(SF2C_EXEC_S_LARECOMP_SYNC_SUBMIT_C965) ? 1 : 0;
    if (sync_submit) renderer_flush();
    else renderer_submit();
    if (trace_render) LOG(SF2C_EXEC_S_EXEC_BACKEND_EXECUTE_RENDERE_941D);
}


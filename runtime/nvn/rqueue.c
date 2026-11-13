#include "../constants/nvn.h"
// The render thread: see rqueue.h.
//
// The game's GPU thread records command lists and submits them; translating them into Metal / Vulkan work took
// milliseconds of that thread's frame. Now nvnQueueSubmitCommands queues the list and returns, and this thread
// translates and submits it. Everything the translation reads (command memory, constant and vertex buffers,
// textures, descriptor pools) is memory the Switch GPU would read while executing the list, which the game keeps
// intact until the fence after it signals: a fence here means "translated and submitted" (rq_wait_item) and then
// "executed" (the backend's own wait on the submission that item ended with), so the game's memory discipline
// holds as it did. The present, the texture destruction (recorded lists hold host NvnTexture pointers) and movie
// frame uploads are queued too, so they keep their order against the lists.
//
// Back-pressure: a present waits until the previous present is done (the thread then always holds about a frame
// of work; latency stays one frame), and the queue has a fixed size. Waits by guest threads give up while the
// kernel stops them for a reboot (g_rt_unwind), as kernel waits do.
//
// Hang dump: part of runtime/kernel.c's "[hang]" watch (SF2RECOMP_HANG_DUMP=<seconds>, default 30, 0 = off; this side
// armed at the game's GPU device setup, render thread on or off): when no frame was presented twice that long after
// the device setup, presenting stops for that long, or a wait noted with rq_watch() lasts that long, the dump adds
// the queue (counts, the last items, the one running and for how long) and what each noted thread waits on to the
// guest threads' kernel waits - so a hang explains itself.
#include "rqueue.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../platform.h"
#include "../rt.h"

__thread uint64_t g_rq_wait_ns;
__thread uint64_t g_rq_lock_ns;
__thread uint64_t g_present_ns, g_hooks_ns;  // (the backends' backend_present)
extern __thread uint64_t g_blocked_ns;  // kernel.c: idle time of the presenting thread (frame pacing)

enum { IT_EXEC, IT_PRESENT, IT_DESTROY, IT_UPLOAD, IT_FLUSH, IT_FINALIZE, IT_COUNT };
static const char *const kind_name[] = {SF2C_RQUEUE_S_EXEC_37C9, SF2C_RQUEUE_S_PRESENT_9433, SF2C_RQUEUE_S_DESTROY_0482, SF2C_RQUEUE_S_UPLOAD_CA46, SF2C_RQUEUE_S_FLUSH_D030, SF2C_RQUEUE_S_FINALIZE_3693};
typedef struct {
    int kind;
    uint64_t handle;  // EXEC
    NvnCommands *commands;  // EXEC: owns captured textures until translation completes
    NvnTexture *tex;  // PRESENT / DESTROY / UPLOAD
    uint64_t frame;   // PRESENT
    bool advance;     // PRESENT: a new frame (false: the last one again, backend_represent)
    int up_kind;      // UPLOAD
    void *data;
    uint32_t w, h, stride, uv_off;
} Item;

#define RQ_CAP SF2C_RQUEUE_N_1024_E39E  // items queued at most (power of two)
#define RQ_HIST SF2C_RQUEUE_RQ_HIST
static Item items[RQ_CAP];
static uint64_t gpu_seq_of[RQ_HIST];  // per item number: the backend submission it ended with
static uint8_t kind_of[RQ_HIST];      // (hang dump) per item number: its kind
static uint64_t enq_ns[RQ_HIST], done_ns[RQ_HIST];  // (SF2RECOMP_LOG_PACE fence stats) per item: queued, translated
static uint64_t q_enq, q_done, q_cur;  // items queued / finished / in progress (0: none)
static int presents_pending;
static uint64_t last_present_q;  // (SF2RECOMP_LOG_PACE fence stats) the latest present's item
extern __thread uint64_t rq_fence_q;
uint64_t rq_trace_t0;  // (debug: SF2RECOMP_RQ_TRACE's time base)
static uint64_t exec_frame;  // (the render thread) the frame whose lists it runs now: see rq_frame
static _Atomic uint64_t presents_done, cur_since;  // (hang dump) presents the thread finished; when q_cur started
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_work = PTHREAD_COND_INITIALIZER, cv_done = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static _Atomic(int) render_tid;  // (host kernel TID, for the hang dump's cross-thread backtrace)
static _Atomic bool started;
static _Atomic int on = -1;
static _Atomic bool stopping, parked;  // (quitting: rq_stop) the thread is to stop / has stopped between items

static void *run(void *arg);
static _Atomic uint64_t render_cpu_ns;
uint64_t rq_render_cpu_ns(void) { return atomic_load_explicit(&render_cpu_ns, memory_order_relaxed); }

// Started once, by whichever thread asks first: the game's threads get here concurrently at boot (the GPU thread's
// first submit or fence, a loader thread's texture finalize, the movie thread's frame), and two callers each starting
// a render thread had two threads running the same items at the same time.
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void start(void) {
    extern uint64_t kernel_current_thread_id(void);
    uint64_t asker = kernel_current_thread_id();
    if (getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_START_DELAY_MS_CB6C)) usleep((unsigned)atoi(getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_START_DELAY_MS_CB6C)) * SF2C_RQUEUE_N_1000U_4C8A);  // (test: the race)
    const char *e = getenv(SF2C_RQUEUE_S_SF2RECOMP_RENDER_THREAD_5CA6);
    int want = !e || atoi(e) != 0;
    if (want) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, SF2C_RQUEUE_N_16_B17E << SF2C_RQUEUE_N_20_F5CA);
        rq_watch(SF2C_RQUEUE_S_STARTING_THE_RENDER_THREAD_5F28, 0);
        int r = pthread_create(&thread, &attr, run, NULL);
        rq_watch(NULL, 0);
        if (r != 0) {
            LOG(SF2C_RQUEUE_S_RQ_CANNOT_START_THE_RENDER_T_2D2D);
            want = 0;
        } else {
            atomic_store(&started, true);
        }
        pthread_attr_destroy(&attr);
    }
    LOG(SF2C_RQUEUE_S_RQ_RENDER_THREAD_S_FIRST_ASK_A5F6, want ? SF2C_RQUEUE_S_ON_990C : SF2C_RQUEUE_S_OFF_9F3B, (unsigned long long)asker);
    atomic_store(&on, want);
    rq_hang_watch_start();
}

extern uint64_t kernel_current_thread_id(void);
bool rq_on(void) {
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (__builtin_expect(v < 0, 0)) {
        static _Atomic int askers;
        int n = atomic_fetch_add(&askers, 1);
        pthread_once(&once, start);
        if (n == 1)
            LOG(SF2C_RQUEUE_S_RQ_GUEST_THREAD_LLU_ASKED_TO_00E3,
                (unsigned long long)kernel_current_thread_id());
        v = atomic_load(&on);
    }
    return v > 0;
}

// ---- what threads wait on (the hang dump's view)
#define NWATCH SF2C_RQUEUE_NWATCH
typedef struct {
    _Atomic(const char *) what;  // NULL: not waiting
    _Atomic uint64_t since, arg;
    char name[SF2C_RQUEUE_N_24_C235];
} Watch;
static Watch watches[NWATCH];
static _Atomic int nwatch;
static __thread Watch *my_watch;
static __thread bool my_watch_full, is_render;

static Watch *watch_self(void) {
    if (my_watch || my_watch_full) return my_watch;
    int i = atomic_fetch_add(&nwatch, 1);
    if (i >= NWATCH) { my_watch_full = true; return NULL; }
    Watch *w = &watches[i];
    extern uint64_t kernel_current_thread_id(void);
    uint64_t kt = kernel_current_thread_id();
    if (is_render) snprintf(w->name, sizeof w->name, SF2C_RQUEUE_S_RENDER_THREAD_3762);
    else if (kt) snprintf(w->name, sizeof w->name, SF2C_RQUEUE_S_GUEST_THREAD_LLU_5DDC, (unsigned long long)kt);
    else snprintf(w->name, sizeof w->name, SF2C_RQUEUE_S_HOST_THREAD_D_36A2, i);
    my_watch = w;
    return w;
}

static _Atomic(const char *) r_longest_what;  // the render thread's longest watched wait since the last ask (slowframe.c)
static _Atomic uint64_t r_longest_ns;
uint64_t rq_render_longest(const char **what) {
    *what = atomic_exchange(&r_longest_what, NULL);
    return atomic_exchange(&r_longest_ns, 0);
}
void rq_watch(const char *what, uint64_t arg) {
    Watch *w = watch_self();
    if (!w) return;
    if (!what && is_render) {  // (a section ends: the longest one is what a slow frame reports)
        const char *was = atomic_load_explicit(&w->what, memory_order_relaxed);
        uint64_t dur = plat_now_ns() - atomic_load_explicit(&w->since, memory_order_relaxed);
        if (was && dur > atomic_load_explicit(&r_longest_ns, memory_order_relaxed)) {
            atomic_store_explicit(&r_longest_ns, dur, memory_order_relaxed);
            atomic_store_explicit(&r_longest_what, was, memory_order_relaxed);
        }
    }
    if (what) {
        atomic_store_explicit(&w->arg, arg, memory_order_relaxed);
        atomic_store_explicit(&w->since, plat_now_ns(), memory_order_relaxed);
    }
    atomic_store_explicit(&w->what, what, memory_order_release);
}

// ---- the queue

// Wait on cv while !ready(arg); guest: give up while the kernel stops the threads. Caller holds the lock. A wait of
// 2 s and more is logged with the queue's state (a stuck render thread shows up instead of a silent hang).
static void wait_until(pthread_cond_t *cv, bool (*ready)(uint64_t), uint64_t arg, bool guest, const char *what) {
    uint64_t t0 = 0;
    bool warned = false;
    bool noted = what && !ready(arg);
    if (noted) rq_watch(what, arg);
    while (!ready(arg)) {
        if (guest && g_rt_unwind) break;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += SF2C_RQUEUE_N_50000000_C546;
        if (ts.tv_nsec >= SF2C_RQUEUE_N_1000000000_52A5) { ts.tv_sec++; ts.tv_nsec -= SF2C_RQUEUE_N_1000000000_52A5; }
        pthread_cond_timedwait(cv, &lock, &ts);
        if (!what) continue;
        uint64_t now = plat_now_ns();
        if (!t0) t0 = now;
        else if (!warned && now - t0 > SF2C_RQUEUE_N_2000000000ULL_C95E) {
            warned = true;
            static int n;
            if (n++ < SF2C_RQUEUE_N_20_F5CA)
                LOG(SF2C_RQUEUE_S_RQ_S_WAITING_LLU_MS_QUEUED_L_D79E, what,
                    (unsigned long long)((now - t0) / SF2C_RQUEUE_N_1000000_6CCE), (unsigned long long)q_enq, (unsigned long long)q_done,
                    (unsigned long long)q_cur, presents_pending);
        }
    }
    if (noted) rq_watch(NULL, 0);
}

static bool has_room(uint64_t a) { (void)a; return q_enq - q_done < RQ_CAP; }
static bool no_present(uint64_t a) { (void)a; return presents_pending == 0; }
static bool all_done(uint64_t a) { (void)a; return q_done == q_enq; }
// (each waiter its own item: with one shared target, a waiter for a later item let one for an earlier item go on
// before its list was translated, and the other way round)
static bool item_done(uint64_t q) { return q_done >= q; }
static bool has_work(uint64_t a) { (void)a; return q_done < q_enq || atomic_load(&stopping); }

static void lock_timed(void) {  // (SF2RECOMP_LOG_PACE: the game thread's time waiting for this lock)
    if (pthread_mutex_trylock(&lock) == 0) return;
    uint64_t t0 = plat_now_ns();
    pthread_mutex_lock(&lock);
    g_rq_lock_ns += plat_now_ns() - t0;
}

// Queue an item. A list's fences (prescan) get its number in the same step, under the lock: no other thread's item
// (a texture destruction, a movie frame, a present) can take the number they were given.
static uint64_t enqueue(const Item *it, const uint64_t *fences, int nfences) {
    lock_timed();
    uint64_t t0 = 0;
    if (!has_room(0)) { t0 = plat_now_ns(); wait_until(&cv_done, has_room, 0, false, SF2C_RQUEUE_S_QUEUE_FULL_66DE); }
    if (it->kind == IT_PRESENT) {
        if (!t0) t0 = plat_now_ns();
        wait_until(&cv_done, no_present, 0, true, SF2C_RQUEUE_S_PRESENT_THE_PREVIOUS_ONE_TO__8311);
        presents_pending++;
        last_present_q = q_enq + 1;
    }
    if (t0) g_blocked_ns += plat_now_ns() - t0;  // (the pacer: as nextDrawable's wait was)
    uint64_t q = ++q_enq;
    items[q % RQ_CAP] = *it;
    kind_of[q % RQ_HIST] = (uint8_t)it->kind;
    enq_ns[q % RQ_HIST] = plat_now_ns();
    if (nfences) {
        extern void nvn_sync_set_seq(uint64_t guest, uint64_t seq);
        for (int i = 0; i < nfences; i++) nvn_sync_set_seq(fences[i], q);
    }
    pthread_cond_signal(&cv_work);
    pthread_mutex_unlock(&lock);
    return q;
}

static void *run(void *arg) {
    (void)arg;
    plat_thread_guest(SF2C_RQUEUE_N_44_71EE);  // (the game's GPU thread's class: performance cores on a Mac)
    plat_thread_perf_core(PLAT_PERF_RENDER);
    is_render = true;
    atomic_store(&render_tid, plat_host_thread_id());
    rq_watch(NULL, 0);  // (registers this thread for the hang dump)
    for (;;) {
        pthread_mutex_lock(&lock);
        wait_until(&cv_work, has_work, 0, false, NULL);
        if (atomic_load(&stopping)) {  // quitting (rq_stop): no more GPU work from here on, nothing queued runs
            atomic_store(&parked, true);
            pthread_cond_broadcast(&cv_done);
            for (;;) pthread_cond_wait(&cv_work, &lock);
        }
        uint64_t q = q_done + 1;
        Item it = items[q % RQ_CAP];
        q_cur = q;
        atomic_store_explicit(&cur_since, plat_now_ns(), memory_order_relaxed);
        pthread_mutex_unlock(&lock);
        static long stall_at = -1;  // (test) SF2RECOMP_RQ_STALL_AT=<item>: the thread stops there for 60 s (hang dump test)
        if (stall_at < 0) stall_at = getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_STALL_AT_54FA) ? atol(getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_STALL_AT_54FA)) : 0;
        if (stall_at && q == (uint64_t)stall_at) { rq_watch(SF2C_RQUEUE_S_SF2RECOMP_RQ_STALL_AT_TEST_755E, q); sleep(SF2C_RQUEUE_N_60_39FA); rq_watch(NULL, 0); }
        static int stats = -1;  // (SF2RECOMP_LOG_PACE) the thread's wall vs CPU time per item kind
        if (stats < 0) stats = getenv(SF2C_RQUEUE_S_SF2RECOMP_LOG_PACE_ED01) != NULL;
        uint64_t c0 = stats ? plat_thread_cpu_ns() : 0, w0 = stats ? plat_now_ns() : 0;
        void *pool = backend_pool_push();
        static _Atomic(int) rq_tid;  // (diagnostics: the running thread's kernel TID, for cross-thread backtraces)
        atomic_store(&rq_tid, plat_host_thread_id());
        static int trace = -1;
        if (trace < 0) trace = getenv(SF2C_RQUEUE_S_SF2_TRACE_RENDER_5E88) != NULL;
        if (trace) LOG(SF2C_RQUEUE_S_RQ_ITEM_LLU_KIND_D_S_STARTIN_C229,
                       (unsigned long long)q, it.kind, kind_name[it.kind], (unsigned long long)it.handle);
        switch (it.kind) {
        case IT_EXEC: backend_execute(it.handle); nvn_commands_release(it.commands); break;
        case IT_PRESENT: backend_present_now(it.tex, it.frame, it.advance); break;
        case IT_DESTROY: { extern void nvn_texture_destroy(NvnTexture *t); nvn_texture_destroy(it.tex); break; }
        case IT_UPLOAD: backend_upload_now(it.tex, it.up_kind, it.data, it.w, it.h, it.stride, it.uv_off); break;
        case IT_FLUSH: { extern void renderer_flush(void); renderer_flush(); break; }
        case IT_FINALIZE: nvn_texture_finalize(it.tex); break;
        }
        if (trace) LOG(SF2C_RQUEUE_S_RQ_ITEM_LLU_KIND_D_S_FINISHE_98D3, (unsigned long long)q, it.kind, kind_name[it.kind]);
        atomic_store(&rq_tid, 0);  // (clearly not running while waiting: alive probes then see the idle loop)
        backend_pool_pop(pool);
        uint64_t gs = renderer_issued_seq(), c1 = plat_thread_cpu_ns();  // (its last submission; renderer_wait_seq waits for it)
        atomic_store_explicit(&render_cpu_ns, c1, memory_order_relaxed);  // (SF2RECOMP_LOG_PACE: pace.c)
        if (stats) {
            static uint64_t n[IT_COUNT], wall[IT_COUNT], cpu[IT_COUNT], items;
            int k = it.kind;
            n[k]++, wall[k] += plat_now_ns() - w0, cpu[k] += c1 - c0;
            if (++items % SF2C_RQUEUE_N_1200_1519 == 0) {
                for (int i = 0; i < IT_COUNT; i++)
                    if (n[i]) LOG(SF2C_RQUEUE_S_RQ_RENDER_THREAD_S_ITEMS_LLU_8D77, kind_name[i],
                                  (unsigned long long)n[i], wall[i] / SF2C_RQUEUE_N_1E6_8C96 / n[i], cpu[i] / SF2C_RQUEUE_N_1E6_8C96 / n[i]);
                memset(n, 0, sizeof n), memset(wall, 0, sizeof wall), memset(cpu, 0, sizeof cpu);
            }
        }
        {   // (debug) SF2RECOMP_RQ_TRACE=<frame>: that frame's and the next 2 frames' items, times in ms from its first
            static long tr_at = -1;
            static uint64_t tr_t0;
            if (tr_at < 0) tr_at = getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_TRACE_50B2) ? atol(getenv(SF2C_RQUEUE_S_SF2RECOMP_RQ_TRACE_50B2)) : 0;
            if (tr_at && exec_frame >= (uint64_t)tr_at && exec_frame < (uint64_t)tr_at + SF2C_RQUEUE_N_3_4E07) {
                uint64_t now = plat_now_ns(), e = enq_ns[q % RQ_HIST];
                if (!tr_t0) tr_t0 = e / SF2C_RQUEUE_N_1000000000ULL_E860 * SF2C_RQUEUE_N_1000000000ULL_E860;
                LOG(SF2C_RQUEUE_S_RQ_TRACE_FRAME_LLU_ITEM_LLU__F184,
                    (unsigned long long)exec_frame, (unsigned long long)q, kind_name[it.kind], (e - tr_t0) / SF2C_RQUEUE_N_1E6_8C96,
                    (atomic_load(&cur_since) - tr_t0) / SF2C_RQUEUE_N_1E6_8C96, (now - tr_t0) / SF2C_RQUEUE_N_1E6_8C96, (unsigned long long)gs);
                rq_trace_t0 = tr_t0;
            }
        }
        pthread_mutex_lock(&lock);
        gpu_seq_of[q % RQ_HIST] = gs;
        done_ns[q % RQ_HIST] = plat_now_ns();
        if (it.kind == IT_PRESENT) {
            presents_pending--;
            atomic_fetch_add(&presents_done, 1);
            exec_frame = it.frame + (it.advance ? 1 : 0);
        }
        q_cur = 0;
        q_done = q;
        pthread_cond_broadcast(&cv_done);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

// ---- items

// The sync objects a list's fences signal (CMD_FENCE_SYNC records, also in the lists it calls): numbered when it's
// queued, as the game may wait on one before the thread reaches the list.
typedef struct { uint64_t v[SF2C_RQUEUE_N_32_E29C], *p; int n, cap; } Fences;
static void fences_add(Fences *f, uint64_t guest) {
    if (f->n == f->cap) {
        int nc = f->cap * SF2C_RQUEUE_N_2_D473;
        uint64_t *np = malloc(sizeof *np * (size_t)nc);
        memcpy(np, f->p, sizeof *np * (size_t)f->n);
        if (f->p != f->v) free(f->p);
        f->p = np, f->cap = nc;
    }
    f->p[f->n++] = guest;
}
static void prescan(uint64_t handle, Fences *f, int depth) {
    uint64_t p = handle;
    for (int guard = 0; guard < SF2C_RQUEUE_N_10000000_FA95; guard++) {
        const CmdRec *r = (const CmdRec *)(uintptr_t)p;
        switch (r->op) {
        case CMD_END: return;
        case CMD_JUMP: p = r->x[0]; continue;
        case CMD_CALL: {
            const uint64_t *h = (const uint64_t *)r->extra;
            for (uint64_t i = 0; i < r->x[0] && depth < SF2C_RQUEUE_N_8_2C62; i++) prescan(h[i], f, depth + 1);
            break;
        }
        case CMD_FENCE_SYNC:
            if (r->x[1]) fences_add(f, r->x[1]);
            break;
        default: break;
        }
        p += r->size;
    }
}

uint64_t rq_submit(uint64_t handle) {
    NvnCommands *commands = nvn_commands_acquire(handle);
    Fences f = {.n = 0, .cap = SF2C_RQUEUE_N_32_E29C};
    f.p = f.v;
    prescan(handle, &f, 0);
    Item it = {.kind = IT_EXEC, .handle = handle, .commands = commands};
    uint64_t q = enqueue(&it, f.p, f.n);
    if (f.p != f.v) free(f.p);
    return q;
}

void rq_present(NvnTexture *t, uint64_t frame, bool advance) {
    Item it = {.kind = IT_PRESENT, .tex = t, .frame = frame, .advance = advance};
    enqueue(&it, NULL, 0);
}

bool rq_try_present(NvnTexture *t, uint64_t frame) {
    lock_timed();
    if (atomic_load(&stopping) || q_enq - q_done >= RQ_CAP || presents_pending) {
        pthread_mutex_unlock(&lock);
        return false;
    }
    uint64_t q = ++q_enq;
    items[q % RQ_CAP] = (Item){.kind = IT_PRESENT, .tex = t, .frame = frame, .advance = false};
    kind_of[q % RQ_HIST] = IT_PRESENT;
    enq_ns[q % RQ_HIST] = plat_now_ns();
    presents_pending = 1;
    last_present_q = q;
    pthread_cond_signal(&cv_work);
    pthread_mutex_unlock(&lock);
    return true;
}

void rq_destroy_texture(NvnTexture *t) {
    Item it = {.kind = IT_DESTROY, .tex = t};
    enqueue(&it, NULL, 0);
}

void rq_finalize_texture(NvnTexture *t) {
    Item it = {.kind = IT_FINALIZE, .tex = t};
    enqueue(&it, NULL, 0);
}

void rq_upload(NvnTexture *t, int kind, void *data, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    Item it = {.kind = IT_UPLOAD, .tex = t, .up_kind = kind, .data = data, .w = w, .h = h, .stride = stride, .uv_off = uv_off};
    enqueue(&it, NULL, 0);
}

// ---- waiting

uint64_t rq_wait_item(uint64_t q) {
    uint64_t t0 = plat_now_ns(), gs;
    lock_timed();
    if (q > q_enq) q = q_enq;
    wait_until(&cv_done, item_done, q, true, SF2C_RQUEUE_S_FENCE_ITS_LIST_NOT_TRANSLATE_3910);
    gs = q_done >= q ? gpu_seq_of[q % RQ_HIST] : renderer_issued_seq();  // (gave up: whatever is queued)
    pthread_mutex_unlock(&lock);
    g_rq_wait_ns += plat_now_ns() - t0;
    return gs;
}

// (SF2RECOMP_LOG_PACE) a fence's item: how many items were queued after it, and whether it's in the frame being
// recorded (after the latest present) or an earlier one.
bool rq_fence_info(uint64_t q, uint64_t *behind, bool *untranslated) {
    lock_timed();
    rq_fence_q = q;
    *behind = q_enq > q ? q_enq - q : 0;
    *untranslated = q_done < q;
    bool cur = q > last_present_q;
    pthread_mutex_unlock(&lock);
    return cur;
}

// (SF2RECOMP_LOG_PACE) after rq_fence_info + the waits: the item's translation time (queued to done) and how long
// before the wait began it was done (its GPU head start; 0 when the wait had to wait for it)
__thread uint64_t rq_fence_q;
void rq_fence_times(uint64_t wait_start, uint64_t *translate_ns, uint64_t *head_ns) {
    uint64_t q = rq_fence_q;
    lock_timed();
    uint64_t e = enq_ns[q % RQ_HIST], d = done_ns[q % RQ_HIST];
    pthread_mutex_unlock(&lock);
    *translate_ns = d > e ? d - e : 0;
    *head_ns = wait_start > d ? wait_start - d : 0;
}

uint64_t rq_fence_seq(void) {
    lock_timed();
    uint64_t q = q_enq;
    pthread_mutex_unlock(&lock);
    return q;
}

uint64_t rq_frame(void) { return exec_frame; }

uint64_t rq_current_item(void) {
    if (!atomic_load(&started) || !pthread_equal(pthread_self(), thread)) return 0;
    return q_cur;
}

void rq_drain(void) {
    if (atomic_load(&on) <= 0 || !atomic_load(&started)) return;
    if (rq_current_item()) return;  // (the thread itself: a flush from inside an item)
    uint64_t t0 = plat_now_ns();
    lock_timed();
    wait_until(&cv_done, all_done, 0, false, SF2C_RQUEUE_S_DRAIN_EVERY_QUEUED_ITEM_D0FE);
    pthread_mutex_unlock(&lock);
    g_rq_wait_ns += plat_now_ns() - t0;
}

// Quitting: the thread finishes the item it runs and stops there (what is still queued never runs), so nothing
// presents or submits while the process ends - exit()'s handlers and the libraries' destructors (Vulkan layers such
// as the Steam overlay, #40) tear down under it otherwise. Waits up to 3 s for a thread stuck in the driver.
bool rq_stopping(void) { return atomic_load_explicit(&stopping, memory_order_relaxed); }
void rq_stop(void) {
    atomic_store(&stopping, true);
    if (atomic_load(&on) <= 0 || !atomic_load(&started) || pthread_equal(pthread_self(), thread)) return;
    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&cv_work);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += SF2C_RQUEUE_N_3_4E07;
    while (!atomic_load(&parked))
        if (pthread_cond_timedwait(&cv_done, &lock, &ts) != 0 && !atomic_load(&parked)) {
            LOG(SF2C_RQUEUE_S_RQ_QUIT_THE_RENDER_THREAD_DI_2246, (unsigned long long)q_cur);
            break;
        }
    pthread_mutex_unlock(&lock);
}

void rq_flush(void) {
    Item it = {.kind = IT_FLUSH};
    enqueue(&it, NULL, 0);
    rq_drain();
}

// ---- the hang dump's renderer side (runtime/kernel.c's hang watch: one "[hang]" facility, SF2RECOMP_HANG_DUMP)

static void hang_state(const char *pfx) {
    uint64_t now = plat_now_ns();
    extern uint64_t renderer_present_count(void);
    bool locked = false;
    for (int i = 0; i < SF2C_RQUEUE_N_100_AD57 && !(locked = pthread_mutex_trylock(&lock) == 0); i++) usleep(SF2C_RQUEUE_N_1000_4051);
    uint64_t enq = q_enq, done = q_done, cur = q_cur, cs = atomic_load(&cur_since);
    int pp = presents_pending;
    if (locked) pthread_mutex_unlock(&lock);
    LOG(SF2C_RQUEUE_S_S_RENDER_THREAD_S_QUEUED_LLU_62C9
        SF2C_RQUEUE_S_PRESENTS_DONE_LLU_GAME_FRAME_E5A1, pfx,
        atomic_load(&on) > 0 ? SF2C_RQUEUE_S_ON_990C : SF2C_RQUEUE_S_OFF_9F3B, (unsigned long long)enq, (unsigned long long)done, (unsigned long long)cur,
        cur ? SF2C_RQUEUE_S_EMPTY_5210 : SF2C_RQUEUE_S_EMPTY_12AE, cur ? kind_name[kind_of[cur % RQ_HIST] % IT_COUNT] : SF2C_RQUEUE_S_EMPTY_12AE,
        cur && cs < now ? (unsigned long long)((now - cs) / SF2C_RQUEUE_N_1000000_6CCE) : SF2C_RQUEUE_N_0ULL_DF1B, pp,
        (unsigned long long)atomic_load(&presents_done), (unsigned long long)renderer_present_count(),
        (unsigned long long)renderer_submit_seq(), locked ? SF2C_RQUEUE_S_EMPTY_12AE : SF2C_RQUEUE_S_QUEUE_LOCK_HELD_100_MS_READ__9ED8);
    if (enq) {
        char b[SF2C_RQUEUE_N_600_284B];
        int n = 0;
        uint64_t from = enq > SF2C_RQUEUE_N_24_C235 ? enq - SF2C_RQUEUE_N_23_535F : 1;
        for (uint64_t q = from; q <= enq && n < (int)sizeof b - SF2C_RQUEUE_N_32_E29C; q++)
            n += snprintf(b + n, sizeof b - (size_t)n, SF2C_RQUEUE_S_LLU_S_550C, (unsigned long long)q, kind_name[kind_of[q % RQ_HIST] % IT_COUNT]);
        LOG(SF2C_RQUEUE_S_S_RENDER_QUEUE_LAST_ITEMS_S__9ADC, pfx, b);
    }
    int nw = atomic_load(&nwatch);
    if (nw > NWATCH) nw = NWATCH;
    for (int i = 0; i < nw; i++) {
        const char *w = atomic_load(&watches[i].what);
        uint64_t since = atomic_load(&watches[i].since);
        if (w) LOG(SF2C_RQUEUE_S_S_S_WAITING_ON_S_LLU_FOR_LLU_B09B, pfx, watches[i].name, w,
                   (unsigned long long)atomic_load(&watches[i].arg),
                   (unsigned long long)(since < now ? (now - since) / SF2C_RQUEUE_N_1000000_6CCE : 0));
        else LOG(SF2C_RQUEUE_S_S_S_NO_NOTED_WAIT_N_D46C, pfx, watches[i].name);
    }
    // The render thread's actual host stack: a stuck exec shows up here even with no noted kernel wait.
    int host_tid = atomic_load(&render_tid);
    if (host_tid) plat_bt_other_thread_tid(host_tid, pfx);  // ("[hang] host stack (n frames)" or the failure reason)
}

static _Atomic uint64_t hang_t_start;  // the GPU device setup (rq_hang_watch_start)
static bool hang_check(uint64_t limit, char *why, size_t nwhy) {
    static uint64_t last_frames, frames_at;
    static bool reported_first, reported_stall;
    static uint64_t reported_since[NWATCH];
    uint64_t t_start = atomic_load(&hang_t_start);
    if (!t_start) return false;
    uint64_t now = plat_now_ns();
    if (!frames_at) frames_at = now;
    extern uint64_t renderer_present_count(void);
    uint64_t f = renderer_present_count() + atomic_load(&presents_done);
    if (f != last_frames || g_rt_unwind) last_frames = f, frames_at = now, reported_stall = false;
    bool hit = false;  // (one reason per check: the dump shows every wait anyway)
    if (!f && !reported_first && now - t_start > SF2C_RQUEUE_N_2_D473 * limit) {  // (the first frame: twice the time, loading)
        reported_first = hit = true;
        snprintf(why, nwhy, SF2C_RQUEUE_S_NO_FRAME_PRESENTED_SINCE_THE_D5E4);
    } else if (f && !reported_stall && now - frames_at > limit) {
        reported_stall = hit = true;
        snprintf(why, nwhy, SF2C_RQUEUE_S_PRESENTING_STOPPED_A003);
    }
    int nw = atomic_load(&nwatch);
    if (nw > NWATCH) nw = NWATCH;
    now = plat_now_ns();  // (after the loads below can't see a newer start: since <= now)
    for (int i = 0; i < nw; i++) {
        const char *w = atomic_load(&watches[i].what);
        uint64_t since = atomic_load(&watches[i].since);
        // (a wait that started after `now` was read, or a slot rewritten between the two loads: not stale)
        if (w && since < now && now - since > limit && reported_since[i] != since) {
            reported_since[i] = since;
            if (hit) continue;
            hit = true;
            snprintf(why, nwhy, SF2C_RQUEUE_S_S_ONE_WAIT_FOR_LLU_S_S_EB81, watches[i].name,
                     (unsigned long long)((now - since) / SF2C_RQUEUE_N_1000000000ULL_E860), w);
        }
    }
    return hit;
}

static pthread_once_t wd_once = PTHREAD_ONCE_INIT;
static void watchdog_start_once(void) {
    atomic_store(&hang_t_start, plat_now_ns());
    extern void kernel_hang_hooks(bool (*)(uint64_t, char *, size_t), void (*)(const char *));
    kernel_hang_hooks(hang_check, hang_state);
}
void rq_hang_watch_start(void) { pthread_once(&wd_once, watchdog_start_once); }

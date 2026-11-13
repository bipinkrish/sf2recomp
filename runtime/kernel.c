#include "constants/core.h"
// A small Horizon kernel: handles, threads, synchronization, memory, and the SVC dispatcher.
#define _GNU_SOURCE 1  // REG_RIP/REG_RSP/REG_RBP in ucontext, Dl_info: the host-stack spy
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <unistd.h>

#include "hle.h"
#include "kernel.h"
#ifdef _WIN32
#include "platform/windows_threads.h"
#endif

// ------------------------------------------------------------------ results
#undef R_OK
#define R_OK 0
#define R_TIMEOUT SF2C_KERNEL_R_TIMEOUT
#define R_CANCELLED SF2C_KERNEL_R_CANCELLED
#define R_INVALID_HANDLE SF2C_KERNEL_R_INVALID_HANDLE
#define R_INVALID_STATE SF2C_KERNEL_R_INVALID_STATE
#define R_INVALID_ADDRESS SF2C_KERNEL_R_INVALID_ADDRESS
#define R_NOT_FOUND SF2C_KERNEL_R_NOT_FOUND
#define R_INVALID_ENUM SF2C_KERNEL_R_INVALID_ENUM
#define R_OUT_OF_RESOURCE SF2C_KERNEL_R_OUT_OF_RESOURCE
#define PSEUDO_THREAD SF2C_KERNEL_PSEUDO_THREAD
#define PSEUDO_PROCESS SF2C_KERNEL_PSEUDO_PROCESS
#define WAITERS_BIT SF2C_KERNEL_WAITERS_BIT
pthread_mutex_t g_klock = PTHREAD_MUTEX_INITIALIZER;
static __thread KThread *t_cur;

// ------------------------------------------------------------------ handle table
#define MAX_HANDLES SF2C_KERNEL_MAX_HANDLES
static KObject *handles[MAX_HANDLES];

static uint32_t handle_alloc(KObject *o) {
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (!handles[i]) {
            handles[i] = o;
            o->refs++;
            return SF2C_KERNEL_N_0X100_BE6E + i;
        }
    }
    FATAL(SF2C_KERNEL_S_OUT_OF_HANDLES_N_81F8);
}

KObject *handle_get(uint32_t h) {
    if (h == PSEUDO_THREAD) return &t_cur->obj;
    if (h < SF2C_KERNEL_N_0X101_83A4 || h >= SF2C_KERNEL_N_0X100_BE6E + MAX_HANDLES) return NULL;
    return handles[h - SF2C_KERNEL_N_0X100_BE6E];
}

uint32_t kernel_new_handle(KObject *o) {
    pthread_mutex_lock(&g_klock);
    uint32_t h = handle_alloc(o);
    pthread_mutex_unlock(&g_klock);
    return h;
}

static void handle_close(uint32_t h) {
    KObject *o = handle_get(h);
    if (!o || h == PSEUDO_THREAD) return;
    handles[h - SF2C_KERNEL_N_0X100_BE6E] = NULL;
    o->refs--;
}

// Threads blocked in WaitSynchronization; object signals wake only these (caller holds g_klock).
static KThread *g_syncwaiters;

static void wake(KThread *t) { pthread_cond_signal(&t->cond); }

void kernel_signal(KObject *o) {
    // caller holds g_klock
    o->signaled = true;
    for (KThread *t = g_syncwaiters; t; t = t->snext) wake(t);
}

// ------------------------------------------------------------------ time

uint64_t rt_ticks(void) {
    // 19.2 MHz system tick
    uint64_t ns = plat_now_ns();
    static double scale = -1;
    if (scale < 0) scale = getenv(SF2C_KERNEL_S_LARECOMP_TICK_SCALE_2FD4) ? atof(getenv(SF2C_KERNEL_S_LARECOMP_TICK_SCALE_2FD4)) : 1.0;
    if (scale != 1.0) ns = (uint64_t)(ns * scale);
    return (uint64_t)((__uint128_t)ns * SF2C_KERNEL_N_192_EB3B / SF2C_KERNEL_N_10000_39E5);
}

static void deadline(struct timespec *ts, int64_t timeout_ns) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ns / SF2C_KERNEL_N_1000000000_52A5;
    ts->tv_nsec += timeout_ns % SF2C_KERNEL_N_1000000000_52A5;
    if (ts->tv_nsec >= SF2C_KERNEL_N_1000000000_52A5) { ts->tv_sec++; ts->tv_nsec -= SF2C_KERNEL_N_1000000000_52A5; }
}

// Sleep on the current thread's condition. Returns false on timeout. Caller holds g_klock.
static pthread_cond_t g_nothread_cond = PTHREAD_COND_INITIALIZER;
// Time this thread spent blocked in the kernel (waits, sleeps): idle, not work. Frame pacing (nvn/pace.c)
// takes the presenting thread's: a frame's busy time is its period minus this.
__thread uint64_t g_blocked_ns;

static bool kwait(int64_t timeout_ns, const struct timespec *ts) {
    pthread_cond_t *cv = t_cur ? &t_cur->cond : &g_nothread_cond;
    uint64_t t0 = plat_now_ns();
    bool r = true;
    if (timeout_ns < 0) pthread_cond_wait(cv, &g_klock);
    else r = pthread_cond_timedwait(cv, &g_klock, ts) == 0;
    g_blocked_ns += plat_now_ns() - t0;
    return r;
}

// ------------------------------------------------------------------ memory map
typedef struct { uint64_t addr, size; uint32_t type, perm; } Region;
static Region regions[SF2C_KERNEL_N_1024_E39E];
static int nregions;
static uint64_t heap_size;
static uint64_t tls_next = TLS_BASE;
uint64_t kernel_heap_size(void) { return heap_size; }  // bytes from HEAP_BASE set by SetHeapSize
// Writable guest data regions (heap, mapped/physical memory, module .data): for debug/runtime memory scans.
int kernel_data_regions(uint64_t *addr, uint64_t *size, int max) {
    int n = 0;
    for (int i = 0; i < nregions && n < max; i++)
        if ((regions[i].perm & SF2C_KERNEL_N_2_D473) && regions[i].type != SF2C_KERNEL_N_0XB_CCDE && regions[i].type != SF2C_KERNEL_N_0XC_D882) {
            addr[n] = regions[i].addr; size[n] = regions[i].size; n++;
        }
    return n;
}

static void region_add(uint64_t addr, uint64_t size, uint32_t type, uint32_t perm) {
    if (nregions == SF2C_KERNEL_N_1024_E39E) FATAL(SF2C_KERNEL_S_TOO_MANY_REGIONS_N_01E9);
    plat_commit(addr, size);  // back it before the OS writes there (Windows: file reads into the heap)
    int i = nregions++;
    while (i > 0 && regions[i - 1].addr > addr) { regions[i] = regions[i - 1]; i--; }
    regions[i] = (Region){addr, size, type, perm};
}

static void region_remove(uint64_t addr) {
    for (int i = 0; i < nregions; i++) {
        if (regions[i].addr == addr) {
            memmove(&regions[i], &regions[i + 1], (nregions - i - 1) * sizeof(Region));
            nregions--;
            return;
        }
    }
}

static void query_memory(uint64_t addr, uint64_t out) {
    uint64_t prev_end = 0;
    uint64_t info[SF2C_KERNEL_N_5_EF2D] = {0};  // addr, size, type|attr<<32, perm|ipc<<32, device|pad<<32
    for (int i = 0; i < nregions; i++) {
        Region *r = &regions[i];
        if (addr >= r->addr && addr < r->addr + r->size) {
            info[0] = r->addr;
            info[1] = r->size;
            info[SF2C_KERNEL_N_2_D473] = r->type;
            info[SF2C_KERNEL_N_3_4E07] = r->perm;
            memcpy((void *)out, info, SF2C_KERNEL_N_0X28_EAB9);
            return;
        }
        if (addr < r->addr) {
            info[0] = prev_end;
            info[1] = r->addr - prev_end;
            memcpy((void *)out, info, SF2C_KERNEL_N_0X28_EAB9);
            return;
        }
        prev_end = r->addr + r->size;
    }
    info[0] = prev_end;
    info[1] = (SF2C_KERNEL_N_1ULL_622E << SF2C_KERNEL_N_39_0B91) - prev_end;
    memcpy((void *)out, info, SF2C_KERNEL_N_0X28_EAB9);
}

// ------------------------------------------------------------------ threads
static uint64_t next_thread_id = SF2C_KERNEL_N_0X50_4171;

// ------------------------------------------------------------------ in-place reboot
// Recompiled guest code can't be killed from outside, so a reboot (runtime/main.c) sets g_rt_unwind, wakes every
// waiting thread, and each guest thread longjmps back to thread_main at its next safe point: rt_svc (entry and
// exit), an HLE call (runtime/dispatch.c), or a kernel wait (which returns early while g_rt_unwind is set). The
// frames dropped are recompiled code (its state lives in guest memory, which is wiped next) and HLE functions;
// none of those hold a host lock across a call into the guest, so nothing stays locked. svcExitThread ends a
// thread the same way.
volatile int g_rt_unwind;
static KThread *g_threads;  // every thread of this boot (g_klock)

void rt_unwind_point(void) {
    if (__builtin_expect(g_rt_unwind, 0) && t_cur) longjmp(t_cur->unwind, 1);
}

static void *thread_main(void *arg) {
    KThread *t = arg;
    t_cur = t;
    plat_thread_guest(t->prio);
    if (t->is_main) plat_thread_perf_core(PLAT_PERF_GAME);  // (the game's frame runs here)
    crash_handler_install();
    rt_set_fpcr(0);
    rt_guest_eh_reset();
    if (!setjmp(t->unwind)) guest_call(&t->cpu, t->entry);
    rt_guest_eh_reset();  // a reboot/ExitThread longjmp can discard translated EH frames
    // Finish diagnostic: how a guest thread exits (return value + last guest frames). The boot's
    // silent nnMain return (~29 s: scene built, Vulkan up, zero presents, render queue saw one
    // texture-destroy) has no other trace; this names the last code main ran and what it gave back.
    if (t->is_main) {
        LOG(SF2C_KERNEL_S_EXIT_MAIN_GUEST_THREAD_LLU_E_9D56,
            (unsigned long long)t->id, (unsigned long long)t->entry, (unsigned long long)t->cpu.x[0],
            (unsigned long long)t->cpu.pc, (unsigned long long)t->cpu.x[SF2C_KERNEL_N_30_624B]);
        // Full state snapshot at the death instant: the post-mortem dumps only ever run after the
        // system drained dry (the ktrace-quiet trigger fires long after, with everything parked);
        // THIS one still holds the pre-death waiters, event states, queue keys and the ring.
        kernel_dump_threads(SF2C_KERNEL_S_EXITDUMP_09C4);
        uint64_t fp = t->cpu.x[SF2C_KERNEL_N_29_3513];
        for (int k = 0; k < SF2C_KERNEL_N_10_4A44 && fp >= STACK_BASE && fp < STACK_BASE + STACK_SIZE && !(fp & SF2C_KERNEL_N_15_E629); k++) {
            uint64_t up = 0, lr = 0;
            if (!rt_probe_u64(fp, &up) || !rt_probe_u64(fp + SF2C_KERNEL_N_8_2C62, &lr)) break;
            uint64_t off = 0;
            const char *s = loader_symbol_at(lr, &off);
            LOG(SF2C_KERNEL_S_EXIT_FRAME_D_LR_LLX_S_0X_LLX_DBB7, k, (unsigned long long)lr, s ? s : SF2C_KERNEL_S_EMPTY_15C5,
                (unsigned long long)off);
            if (up <= fp) break;
            fp = up;
        }
    }
    pthread_mutex_lock(&g_klock);
    t->finished = true;
    kernel_signal(&t->obj);
    pthread_mutex_unlock(&g_klock);
    t_cur = NULL;
    return NULL;
}

static KThread *thread_new(uint64_t entry, uint64_t arg, uint64_t stack_top, int prio, int core) {
    KThread *t = calloc(1, sizeof *t);
    pthread_cond_init(&t->cond, NULL);
    t->obj.type = KOBJ_THREAD;
    t->entry = entry;
    t->prio = prio;
    t->core = core < 0 ? 0 : core;
    t->cpu.x[0] = arg;
    t->cpu.sp = stack_top & ~SF2C_KERNEL_N_15ULL_40A4;
    t->cpu.excl_addr = ~SF2C_KERNEL_N_0ULL_DF1B;
    t->cpu.thread = t;
    pthread_mutex_lock(&g_klock);  // (two guest threads may create threads at once: one id and TLS slot each)
    t->id = next_thread_id++;
    t->tlr = tls_next;
    tls_next += SF2C_KERNEL_N_0X200_3790;
    t->cpu.tpidrro = t->tlr;
    t->anext = g_threads;
    g_threads = t;
    pthread_mutex_unlock(&g_klock);
    return t;
}

static void thread_start(KThread *t) {
#ifdef _WIN32
    t->host = guest_thread_create(thread_main, t, SF2C_KERNEL_N_256U_DE07 << SF2C_KERNEL_N_20_F5CA);
    if (!t->host) FATAL(SF2C_KERNEL_S_COULD_NOT_CREATE_A_GAME_THRE_AA69);
    t->started = true;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SF2C_KERNEL_N_256ULL_3175 << SF2C_KERNEL_N_20_F5CA);  // guest recursion runs on the host stack
    t->started = true;
    pthread_create(&t->host, &attr, thread_main, t);
    pthread_attr_destroy(&attr);
#endif
}

KThread *kernel_create_main_thread(uint64_t entry, uint64_t stack_size, int prio) {
    uint64_t stack = STACK_BASE + STACK_SIZE - ((stack_size + SF2C_KERNEL_N_0XFFFF_F51F) & ~SF2C_KERNEL_N_0XFFFFULL_2BD3);
    region_add(stack, stack_size, SF2C_KERNEL_N_0XB_CCDE, SF2C_KERNEL_N_3_4E07);
    KThread *t = thread_new(entry, 0, stack + stack_size, prio, 0);
    t->is_main = true;
    t->handle = kernel_new_handle(&t->obj);
    return t;
}

static KThread *g_main_thread;  // (g_klock) for hang_watch
static void *hang_watch(void *arg);

void kernel_run_main(KThread *t, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3) {
    t->cpu.x[0] = x0; t->cpu.x[1] = x1; t->cpu.x[SF2C_KERNEL_N_2_D473] = x2; t->cpu.x[SF2C_KERNEL_N_3_4E07] = x3;
    thread_start(t);  // the host main thread goes on to run the window event loop
    pthread_mutex_lock(&g_klock);
    g_main_thread = t;
    pthread_mutex_unlock(&g_klock);
    static bool watching;
    const char *e = getenv(SF2C_KERNEL_S_LARECOMP_HANG_DUMP_B06A);
    uint64_t s = e ? strtoull(e, NULL, SF2C_KERNEL_N_10_4A44) : SF2C_KERNEL_N_30_624B;
    if (!watching && s) {
        watching = true;
        pthread_t w;
        if (!pthread_create(&w, NULL, hang_watch, (void *)(uintptr_t)s)) pthread_detach(w);
    }
}

uint32_t kernel_current_thread_handle(void) { return t_cur ? t_cur->handle : 0; }
Cpu *kernel_current_cpu(void) { return t_cur ? &t_cur->cpu : NULL; }
KThread *kernel_current_thread(void) { return t_cur; }
uint64_t kernel_current_thread_id(void) { return t_cur ? t_cur->id : 0; }

// ------------------------------------------------------------------ mutex / condvar (Horizon semantics)
// Mutex word: owner handle | WAITERS_BIT. Waiting threads sleep on the global cond and are handed ownership.

static uint32_t *u32p(uint64_t a) { return (uint32_t *)(uintptr_t)a; }

// Try to take mutex `m` for `tag`, or mark it contended. Returns true if acquired. Uses CAS so a
// concurrent user-space unlock (owner storing 0) is never overwritten.
static bool mutex_acquire_or_mark(uint64_t m, uint32_t tag) {
    for (;;) {
        uint32_t v = __atomic_load_n(u32p(m), __ATOMIC_SEQ_CST);
        if (v == 0) {
            if (__atomic_compare_exchange_n(u32p(m), &v, tag, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return true;
        } else {
            if ((v & WAITERS_BIT) ||
                __atomic_compare_exchange_n(u32p(m), &v, v | WAITERS_BIT, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
                return false;
        }
    }
}

// Pick a thread waiting on mutex `addr` and give it ownership. Caller holds g_klock.
static void mutex_release(uint64_t addr) {
    KThread *next = NULL;
    int count = 0;
    for (KThread *t = g_waitlist; t; t = t->wnext) {
        if (t->wait_kind == WAIT_MUTEX && t->wait_addr == addr) {
            if (!next || t->prio < next->prio) next = t;
            count++;
        }
    }
    if (!next) {
        __atomic_store_n(u32p(addr), 0, __ATOMIC_SEQ_CST);
        return;
    }
    __atomic_store_n(u32p(addr), next->handle | (count > 1 ? WAITERS_BIT : 0), __ATOMIC_SEQ_CST);
    next->wait_kind = WAIT_NONE;
    next->wait_result = R_OK;
    wake(next);
}

KThread *g_waitlist;

// At the end: the wakers take the most urgent waiter and, among equals, the first found - the longest waiting, as
// Horizon (first in, first out within a priority). Added at the head, the latest waiter won every tie.
static void waitlist_add(KThread *t) {
    KThread **p = &g_waitlist;
    while (*p) p = &(*p)->wnext;
    t->wnext = NULL;
    *p = t;
}
static void waitlist_remove(KThread *t) {
    for (KThread **p = &g_waitlist; *p; p = &(*p)->wnext)
        if (*p == t) { *p = t->wnext; return; }
}

// Block until wait_kind is cleared by a waker, timeout, or a reboot. Caller holds g_klock.
// CancelSynchronization doesn't end these waits (mutex, condition variable, address arbiter), as on Horizon: its flag
// is for the thread's next WaitSynchronization only. Ending them used the flag up there (a cancel meant to wake a
// thread out of a WaitSynchronization could be lost), and the SDK's untimed condition variable wait treats any
// result but success as fatal.
static uint32_t block_current(int64_t timeout_ns) {
    KThread *t = t_cur;
    struct timespec ts;
    if (timeout_ns >= 0) deadline(&ts, timeout_ns);
    t->wait_since = plat_now_ns();
    waitlist_add(t);
    while (t->wait_kind != WAIT_NONE) {
        if (g_rt_unwind) {  // reboot: give up the wait (the thread unwinds when its svc returns)
            t->wait_kind = WAIT_NONE;
            t->wait_result = R_CANCELLED;
            break;
        }
        if (!kwait(timeout_ns, &ts)) {
            if (t->wait_kind == WAIT_NONE) break;  // (woken just as the time ran out: the waker's result stands)
            t->wait_kind = WAIT_NONE;
            t->wait_result = R_TIMEOUT;
            break;
        }
    }
    waitlist_remove(t);
    return t->wait_result;
}

// (diagnostics) the last KTRACE_N condition variable (untimed waits) / mutex kernel calls, lock-free, for the [hang] dump: what each
// thread did with which key / mutex word, and the words before and after. LARECOMP_KTRACE=0 turns it off.
enum { KT_WAIT, KT_WAIT_END, KT_SIGNAL, KT_LOCK, KT_LOCK_END, KT_UNLOCK };
#define KTRACE_N SF2C_KERNEL_KTRACE_N
typedef struct { uint64_t ns, tid, addr, addr2, pc, lr; uint32_t w0, w1, w2, arg, kind; } KTrace;
static KTrace ktrace[KTRACE_N];
static uint64_t ktrace_n;  // (__atomic builtins)
static int ktrace_on = -1;
static void kt(uint32_t kind, uint64_t addr, uint64_t addr2, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t arg) {
    if (ktrace_on < 0) ktrace_on = !getenv(SF2C_KERNEL_S_LARECOMP_KTRACE_A502) || atoi(getenv(SF2C_KERNEL_S_LARECOMP_KTRACE_A502)) != 0;
    if (!ktrace_on) return;
    Cpu *c = kernel_current_cpu();
    KTrace *e = &ktrace[__atomic_fetch_add(&ktrace_n, 1, __ATOMIC_RELAXED) % KTRACE_N];
    *e = (KTrace){plat_now_ns(), t_cur ? t_cur->id : 0, addr, addr2, c ? c->pc : 0, c ? c->x[SF2C_KERNEL_N_30_624B] : 0,
                  w0, w1, w2, arg, kind};
}
static uint32_t rdw(uint64_t a) { return a ? __atomic_load_n(u32p(a), __ATOMIC_SEQ_CST) : 0; }
// Keys used with timed waits (periodic pollers, e.g. a 1 ms timer thread) would fill the ring within a second: their
// waits and signals are left out (g_klock).
static uint64_t kt_timed[SF2C_KERNEL_N_16_B17E];
static bool kt_timed_key(uint64_t key, bool add) {
    for (int i = 0; i < SF2C_KERNEL_N_16_B17E; i++) {
        if (kt_timed[i] == key) return true;
        if (!kt_timed[i] && add) { kt_timed[i] = key; return true; }
    }
    return false;
}

static uint32_t svc_arbitrate_lock(uint32_t owner, uint64_t addr, uint32_t tag) {
    pthread_mutex_lock(&g_klock);
    uint32_t v = __atomic_load_n(u32p(addr), __ATOMIC_SEQ_CST);
    kt(KT_LOCK, addr, 0, v, 0, owner, tag);
    if (v != (owner | WAITERS_BIT)) {
        pthread_mutex_unlock(&g_klock);
        return R_OK;
    }
    KThread *t = t_cur;
    t->wait_kind = WAIT_MUTEX;
    t->wait_addr = addr;
    t->wait_tag = tag;
    uint32_t r = block_current(-1);
    kt(KT_LOCK_END, addr, 0, rdw(addr), 0, 0, r);
    pthread_mutex_unlock(&g_klock);
    return r == R_CANCELLED ? R_OK : r;
}

static uint32_t svc_arbitrate_unlock(uint64_t addr) {
    pthread_mutex_lock(&g_klock);
    uint32_t before = rdw(addr);
    mutex_release(addr);
    kt(KT_UNLOCK, addr, 0, before, rdw(addr), 0, 0);
    pthread_mutex_unlock(&g_klock);
    return R_OK;
}

static uint32_t svc_wait_process_wide_key(uint64_t mutex, uint64_t key, uint32_t tag, int64_t timeout) {
    static long late_us = -1;  // (test) LARECOMP_DEBUG_CV_LATE_WAIT=<us>: every condition variable wait starts that late
    if (late_us < 0) late_us = getenv(SF2C_KERNEL_S_LARECOMP_DEBUG_CV_LATE_WAIT_BD4A) ? atol(getenv(SF2C_KERNEL_S_LARECOMP_DEBUG_CV_LATE_WAIT_BD4A)) : 0;
    if (late_us > 0) usleep((useconds_t)late_us);  // (forces notify-before-wait races in the game's code: hle/delegate_null.c)
    pthread_mutex_lock(&g_klock);
    KThread *t = t_cur;
    uint32_t mbefore = rdw(mutex), kbefore = rdw(key);
    // Publish before releasing ownership: a guest notifier can take the mutex
    // entirely in user space and skip SignalProcessWideKey when this key is 0.
    // g_klock alone cannot protect that fast path. Once the key is set, a
    // notifier enters the kernel and waits until registration below completes.
    __atomic_store_n(u32p(key), 1, __ATOMIC_SEQ_CST);
    mutex_release(mutex);
    bool traced = !kt_timed_key(key, timeout >= 0);
    if (traced) kt(KT_WAIT, key, mutex, kbefore, mbefore, rdw(mutex), SF2C_KERNEL_N_0XFFFFFFFFU_7BAF);
    t->wait_kind = WAIT_CONDVAR;
    t->wait_addr = key;
    t->wait_mutex = mutex;
    t->wait_tag = tag;
    // Timed out / cancelled: returns without the mutex, as Horizon does - nn::os takes it again itself (TimedWait on
    // 0xEA01 calls the critical section's Enter). Taking it here as well left the thread holding it when Enter ran:
    // Enter saw its own handle in the lock word and ArbitrateLock waited on itself forever (issue #19's thread dump: a
    // controller thread's timer-event wait).
    uint32_t r = block_current(timeout);
    if (traced) kt(KT_WAIT_END, key, mutex, rdw(key), rdw(mutex), 0, r);
    pthread_mutex_unlock(&g_klock);
    return r;
}

static void svc_signal_process_wide_key(uint64_t key, int32_t count) {
    pthread_mutex_lock(&g_klock);
    uint32_t kbefore = rdw(key);
    int woken = 0;
    bool any_left = false;
    for (;;) {
        if (count > 0 && woken >= count) break;
        KThread *best = NULL;
        for (KThread *t = g_waitlist; t; t = t->wnext)
            if (t->wait_kind == WAIT_CONDVAR && t->wait_addr == key && (!best || t->prio < best->prio)) best = t;
        if (!best) break;
        woken++;
        // the woken thread must reacquire its mutex
        uint64_t m = best->wait_mutex;
        best->wait_result = R_OK;
        if (mutex_acquire_or_mark(m, best->wait_tag)) {
            best->wait_kind = WAIT_NONE;
            wake(best);
        } else {
            best->wait_kind = WAIT_MUTEX;
            best->wait_addr = m;
        }
    }
    for (KThread *t = g_waitlist; t; t = t->wnext)
        if (t->wait_kind == WAIT_CONDVAR && t->wait_addr == key) any_left = true;
    if (!any_left) __atomic_store_n(u32p(key), 0, __ATOMIC_SEQ_CST);
    if (!kt_timed_key(key, false)) kt(KT_SIGNAL, key, 0, kbefore, rdw(key), (uint32_t)woken, (uint32_t)count);
    pthread_mutex_unlock(&g_klock);
}

// ------------------------------------------------------------------ address arbiter
enum { ARB_WAIT_LESS = 0, ARB_DEC_WAIT_LESS = 1, ARB_WAIT_EQ = SF2C_KERNEL_N_2_D473 };
enum { ARB_SIGNAL = 0, ARB_SIGNAL_INC_EQ = 1, ARB_SIGNAL_MOD_EQ = SF2C_KERNEL_N_2_D473 };

static uint32_t svc_wait_for_address(uint64_t addr, uint32_t type, int32_t value, int64_t timeout) {
    pthread_mutex_lock(&g_klock);
    int32_t cur = (int32_t)__atomic_load_n(u32p(addr), __ATOMIC_SEQ_CST);
    bool wait;
    if (type == ARB_WAIT_LESS) wait = cur < value;
    else if (type == ARB_DEC_WAIT_LESS) {
        // Atomically, as Horizon: the game changes these words in user space (exclusive load / store, no kernel
        // lock), and a plain store of the value read before overwrote such a change - a lost signal.
        uint32_t v = (uint32_t)cur;
        while ((wait = (int32_t)v < value) &&
               !__atomic_compare_exchange_n(u32p(addr), &v, v - 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {}
    } else if (type == ARB_WAIT_EQ) wait = cur == value;
    else { pthread_mutex_unlock(&g_klock); return R_INVALID_ENUM; }
    if (!wait) { pthread_mutex_unlock(&g_klock); return R_INVALID_STATE; }
    if (timeout == 0) { pthread_mutex_unlock(&g_klock); return R_TIMEOUT; }
    t_cur->wait_kind = WAIT_ARBITER;
    t_cur->wait_addr = addr;
    uint32_t r = block_current(timeout);
    pthread_mutex_unlock(&g_klock);
    return r;
}

static int arbiter_wake(uint64_t addr, int32_t count) {  // (the most urgent waiters first, as Horizon)
    int woken = 0;
    while (count <= 0 || woken < count) {
        KThread *best = NULL;
        for (KThread *t = g_waitlist; t; t = t->wnext)
            if (t->wait_kind == WAIT_ARBITER && t->wait_addr == addr && (!best || t->prio < best->prio)) best = t;
        if (!best) break;
        best->wait_kind = WAIT_NONE;
        best->wait_result = R_OK;
        wake(best);
        woken++;
    }
    return woken;
}

// The word at addr from `expected` to `desired` if it still holds `expected` (user space may change it any time).
static bool arbiter_update(uint64_t addr, int32_t expected, int32_t desired) {
    uint32_t e = (uint32_t)expected;
    return __atomic_compare_exchange_n(u32p(addr), &e, (uint32_t)desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static uint32_t svc_signal_to_address(uint64_t addr, uint32_t type, int32_t value, int32_t count) {
    pthread_mutex_lock(&g_klock);
    uint32_t r = R_OK;
    if (type == ARB_SIGNAL) {
        arbiter_wake(addr, count);
    } else if (type == ARB_SIGNAL_INC_EQ) {
        if (!arbiter_update(addr, value, value + 1)) r = R_INVALID_STATE;
        else arbiter_wake(addr, count);
    } else if (type == ARB_SIGNAL_MOD_EQ) {
        int waiting = 0;
        for (KThread *t = g_waitlist; t; t = t->wnext)
            if (t->wait_kind == WAIT_ARBITER && t->wait_addr == addr) waiting++;
        int32_t nv;
        if (count <= 0) nv = waiting > 0 ? value - SF2C_KERNEL_N_2_D473 : value + 1;
        else nv = waiting == 0 ? value + 1 : (waiting <= count ? value - 1 : value);
        if (!arbiter_update(addr, value, nv)) r = R_INVALID_STATE;
        else arbiter_wake(addr, count);
    } else {
        r = R_INVALID_ENUM;
    }
    pthread_mutex_unlock(&g_klock);
    return r;
}

// ------------------------------------------------------------------ wait synchronization

static uint32_t svc_wait_synchronization(uint64_t handles_ptr, int32_t count, int64_t timeout, int32_t *index) {
    pthread_mutex_lock(&g_klock);
    struct timespec ts;
    if (timeout > 0) deadline(&ts, timeout);
    KThread *t = t_cur;
    uint32_t *hs = (uint32_t *)(uintptr_t)handles_ptr;
    uint32_t r;
    if (ENV(SF2C_KERNEL_S_LARECOMP_VSYNC_BT_9A1D)) {  // (debug) who waits for the display's vsync event: the game's frame loop
        extern uint32_t vi_vsync_handle(void);
        static int shown;
        for (int i = 0; i < count && shown < SF2C_KERNEL_N_2_D473; i++)
            if (hs[i] && hs[i] == vi_vsync_handle()) {
                shown++;
                LOG(SF2C_KERNEL_S_VSYNC_BT_WAIT_ON_VSYNC_D_HAN_880D, count, (long long)timeout,
                    (unsigned long long)(t ? t->cpu.x[SF2C_KERNEL_N_30_624B] : 0));
                plat_print_backtrace();
            }
    }
    for (;;) {
        for (int i = 0; i < count; i++) {
            KObject *o = handle_get(hs[i]);
            if (!o) { r = R_INVALID_HANDLE; goto out; }
            if (o->signaled) { *index = i; r = R_OK; goto out; }
        }
        if (timeout == 0) { r = R_TIMEOUT; goto out; }  // (before the cancel: a poll leaves it for the next real wait)
        if (t->cancel) { t->cancel = false; r = R_CANCELLED; goto out; }
        if (g_rt_unwind) { r = R_CANCELLED; goto out; }
        t->in_sync_wait = true;
        t->wait_since = plat_now_ns();
        t->sync_handles = handles_ptr, t->sync_count = count;
        t->snext = g_syncwaiters;
        g_syncwaiters = t;
        bool ok = kwait(timeout < 0 ? -1 : timeout, &ts);
        for (KThread **p = &g_syncwaiters; *p; p = &(*p)->snext)
            if (*p == t) { *p = t->snext; break; }
        t->in_sync_wait = false;
        if (!ok) { r = R_TIMEOUT; goto out; }
    }
out:
    pthread_mutex_unlock(&g_klock);
    return r;
}

// ------------------------------------------------------------------ debug output
static void debug_string(uint64_t p, uint64_t len) {
    fprintf(stderr, SF2C_KERNEL_S_GUEST_S_E008, (int)len, (const char *)(uintptr_t)p);
    if (len && ((const char *)(uintptr_t)p)[len - 1] != SF2C_KERNEL_CH_N_AD65) fputc(SF2C_KERNEL_CH_N_AD65, stderr);
}

// ------------------------------------------------------------------ SVC dispatch
static int trace_svc = -1;

static void svc_impl(Cpu *c, uint32_t imm, uint64_t pc);

static _Atomic uint64_t svc_wait_ns[SF2C_KERNEL_N_32_E29C][SF2C_KERNEL_N_0X80_D41C], svc_calls[SF2C_KERNEL_N_32_E29C][SF2C_KERNEL_N_0X80_D41C];
static int prof_svc = -1;

static void svc_entry(Cpu *c, uint32_t imm, uint64_t pc);
void rt_svc(Cpu *c, uint32_t imm, uint64_t pc) {
    rt_unwind_point();
    svc_entry(c, imm, pc);
    rt_unwind_point();
}

static void svc_entry(Cpu *c, uint32_t imm, uint64_t pc) {
    if (prof_svc < 0) prof_svc = ENV(SF2C_KERNEL_S_LARECOMP_PROF_SVC_6FEB) != NULL;
    if (prof_svc && imm < SF2C_KERNEL_N_0X80_D41C) {
        uint64_t a0 = c->x[0], a1 = c->x[1], a2 = c->x[SF2C_KERNEL_N_2_D473], a3 = c->x[SF2C_KERNEL_N_3_4E07], lr = c->x[SF2C_KERNEL_N_30_624B];
        uint32_t h0 = imm == SF2C_KERNEL_N_0X18_D47C && a2 ? *(uint32_t *)(uintptr_t)a1 : 0;
        uint64_t t0 = plat_now_ns();
        svc_impl(c, imm, pc);
        uint64_t dt = plat_now_ns() - t0;
        static _Atomic int nslow;
        if (((t_cur && t_cur->id == SF2C_KERNEL_N_80_4844 && (imm == SF2C_KERNEL_N_0X18_D47C || imm == SF2C_KERNEL_N_0X34_806E)) || (dt > SF2C_KERNEL_N_10000000_FA95 && imm != SF2C_KERNEL_N_0X0B_7C21 && imm != SF2C_KERNEL_N_0X1C_7BDE)) && nslow < SF2C_KERNEL_N_20000_876C) {
            nslow++;
            LOG(SF2C_KERNEL_S_SLOW_T_LLU_SVC_02X_LLUMS_A_L_6B68, (unsigned long long)(t_cur ? t_cur->id : 0), imm,
                (unsigned long long)(dt / SF2C_KERNEL_N_1000000_6CCE), (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2, (unsigned long long)a3, h0, (unsigned long long)lr);
        }
        unsigned tid = t_cur ? (unsigned)(t_cur->id & SF2C_KERNEL_N_31_EB1E) : SF2C_KERNEL_N_31_EB1E;
        svc_wait_ns[tid][imm] += dt;
        svc_calls[tid][imm]++;
        static _Atomic uint64_t last;
        uint64_t now = plat_now_ns();
        if (now - last > SF2C_KERNEL_N_2000000000ULL_C95E) {
            last = now;
            for (int t = 0; t < SF2C_KERNEL_N_32_E29C; t++) {
                int any = 0;
                for (int i = 0; i < SF2C_KERNEL_N_0X80_D41C; i++)
                    if (svc_calls[t][i]) {
                        if (!any) { LOG(SF2C_KERNEL_S_SVCPROF_T_D_EB5C, t); any = 1; }
                        LOG(SF2C_KERNEL_S_02X_LLU_LLUMS_4D1D, i, (unsigned long long)svc_calls[t][i], (unsigned long long)(svc_wait_ns[t][i] / SF2C_KERNEL_N_1000000_6CCE));
                        svc_calls[t][i] = 0; svc_wait_ns[t][i] = 0;
                    }
                if (any) LOG(SF2C_KERNEL_S_N_D7A5);
            }
        }
        return;
    }
    if (trace_svc < 0) trace_svc = getenv(SF2C_KERNEL_S_LARECOMP_TRACE_SVC_4E28) != NULL;
    if (!trace_svc) return svc_impl(c, imm, pc);
    uint64_t in[SF2C_KERNEL_N_4_4B22] = {c->x[0], c->x[1], c->x[SF2C_KERNEL_N_2_D473], c->x[SF2C_KERNEL_N_3_4E07]};
    svc_impl(c, imm, pc);
    LOG(SF2C_KERNEL_S_SVC_02X_LLX_LLX_LLX_LLX_LLX__80CD, imm, in[0], in[1], in[SF2C_KERNEL_N_2_D473], in[SF2C_KERNEL_N_3_4E07], c->x[0],
        c->x[1], c->x[SF2C_KERNEL_N_2_D473], pc);
}

static void svc_impl(Cpu *c, uint32_t imm, uint64_t pc) {
    uint64_t *x = c->x;
    switch (imm) {
    case SF2C_KERNEL_N_0X01_B51C: {  // SetHeapSize
        uint64_t size = x[1];
        uint64_t capacity = backend_guest_heap_capacity();
        LOG(SF2C_KERNEL_S_KERNEL_SETHEAPSIZE_SIZE_LLX__91EC, size, capacity);
        if (size > capacity) { x[0] = R_OUT_OF_RESOURCE; break; }
        uint64_t previous_size = heap_size;
        heap_size = size;
        if (size && !previous_size) {
            // Select/map the renderer's heap before any guest allocations exist.
            // This avoids importing live anonymous memory through AMD's HMM path.
            backend_guest_heap_prepare();
        }
        region_remove(HEAP_BASE);
        if (size) region_add(HEAP_BASE, size, SF2C_KERNEL_N_5_EF2D, SF2C_KERNEL_N_3_4E07);
        x[0] = R_OK;
        x[1] = HEAP_BASE;
        break;
    }
    case SF2C_KERNEL_N_0X02_E6BD: case SF2C_KERNEL_N_0X03_9D1B: x[0] = R_OK; break;  // SetMemoryPermission / SetMemoryAttribute
    case SF2C_KERNEL_N_0X04_B03A:  // MapMemory(dst, src, size): src is inaccessible while mapped, so copying is equivalent
        memcpy((void *)x[0], (void *)x[1], x[SF2C_KERNEL_N_2_D473]);
        region_add(x[0], x[SF2C_KERNEL_N_2_D473], SF2C_KERNEL_N_0XB_CCDE, SF2C_KERNEL_N_3_4E07);
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X05_4ED3:  // UnmapMemory(dst, src, size): copy back
        memcpy((void *)x[1], (void *)x[0], x[SF2C_KERNEL_N_2_D473]);
        region_remove(x[0]);
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X06_9AE3:  // QueryMemory(out_info, _, addr)
        query_memory(x[SF2C_KERNEL_N_2_D473], x[0]);
        x[0] = R_OK;
        x[1] = 0;
        break;
    case SF2C_KERNEL_N_0X07_A022: LOG(SF2C_KERNEL_S_EXITPROCESS_N_C20C); exit(0);
    case SF2C_KERNEL_N_0X08_1EC8: {  // CreateThread(_, entry, arg, stack_top, prio, core)
        KThread *t = thread_new(x[1], x[SF2C_KERNEL_N_2_D473], x[SF2C_KERNEL_N_3_4E07], (int)x[SF2C_KERNEL_N_4_4B22], (int)(int32_t)x[SF2C_KERNEL_N_5_EF2D]);
        t->handle = kernel_new_handle(&t->obj);
        LOG(SF2C_KERNEL_S_KERNEL_CREATETHREAD_ENTRY_LL_2B07, x[1], x[SF2C_KERNEL_N_3_4E07], (int)x[SF2C_KERNEL_N_4_4B22], t->handle);
        x[0] = R_OK;
        x[1] = t->handle;
        break;
    }
    case SF2C_KERNEL_N_0X09_59B7: {  // StartThread
        KObject *o = handle_get((uint32_t)x[0]);
        if (!o || o->type != KOBJ_THREAD) { x[0] = R_INVALID_HANDLE; break; }
        thread_start((KThread *)o);
        x[0] = R_OK;
        break;
    }
    case SF2C_KERNEL_N_0X0A_AF2D: longjmp(t_cur->unwind, 1);  // ExitThread: back to thread_main, which signals the thread object
    case SF2C_KERNEL_N_0X0B_C106: {  // SleepThread (in slices of at most 50 ms: a reboot doesn't wait out a long sleep)
        int64_t ns = (int64_t)x[0];
        if (ns <= 0) { sched_yield(); break; }
        for (uint64_t end = plat_now_ns() + (uint64_t)ns, now; !g_rt_unwind && (now = plat_now_ns()) < end;) {
            uint64_t d = end - now < SF2C_KERNEL_N_50000000_C546 ? end - now : SF2C_KERNEL_N_50000000_C546;
            struct timespec ts = {(time_t)(d / SF2C_KERNEL_N_1000000000_52A5), (long)(d % SF2C_KERNEL_N_1000000000_52A5)};
            nanosleep(&ts, NULL);
            g_blocked_ns += plat_now_ns() - now;
        }
        break;
    }
    case SF2C_KERNEL_N_0X0C_49AA: {  // GetThreadPriority(_, handle)
        KObject *o = handle_get((uint32_t)x[1]);
        x[0] = o ? R_OK : R_INVALID_HANDLE;
        x[1] = o ? (uint64_t)((KThread *)o)->prio : 0;
        break;
    }
    case SF2C_KERNEL_N_0X0D_106A: {  // SetThreadPriority
        KObject *o = handle_get((uint32_t)x[0]);
        if (o) ((KThread *)o)->prio = (int)x[1];
        x[0] = o ? R_OK : R_INVALID_HANDLE;
        break;
    }
    case SF2C_KERNEL_N_0X0E_D851: {  // GetThreadCoreMask(_, _, handle)
        KObject *o = handle_get((uint32_t)x[SF2C_KERNEL_N_2_D473]);
        x[0] = o ? R_OK : R_INVALID_HANDLE;
        x[1] = o ? (uint64_t)((KThread *)o)->core : 0;
        x[SF2C_KERNEL_N_2_D473] = SF2C_KERNEL_N_0XF_7419;
        break;
    }
    case SF2C_KERNEL_N_0X0F_1F1D: x[0] = R_OK; break;  // SetThreadCoreMask
    case SF2C_KERNEL_N_0X10_BF9C: x[0] = t_cur->core; break;  // GetCurrentProcessorNumber
    case SF2C_KERNEL_N_0X11_A3D6: case SF2C_KERNEL_N_0X12_9AE3: case SF2C_KERNEL_N_0X17_A99E: {  // SignalEvent / ClearEvent / ResetSignal
        pthread_mutex_lock(&g_klock);
        KObject *o = handle_get((uint32_t)x[0]);
        if (!o) x[0] = R_INVALID_HANDLE;
        else {
            KObject *ev = o->type == KOBJ_EVENT ? o : NULL;
            if (!ev) x[0] = R_INVALID_HANDLE;
            else {
                if (imm == SF2C_KERNEL_N_0X11_A3D6) kernel_signal(ev);
                else if (imm == SF2C_KERNEL_N_0X17_A99E && !ev->signaled) { x[0] = R_INVALID_STATE; pthread_mutex_unlock(&g_klock); break; }
                else ev->signaled = false;
                x[0] = R_OK;
            }
        }
        pthread_mutex_unlock(&g_klock);
        break;
    }
    case SF2C_KERNEL_N_0X13_3ABB: {  // MapSharedMemory(handle, addr, size, perm)
        KObject *o = handle_get((uint32_t)x[0]);
        if (!o || o->type != KOBJ_SHMEM) { x[0] = R_INVALID_HANDLE; break; }
        KSharedMem *s = (KSharedMem *)o;
        memset((void *)x[1], 0, x[SF2C_KERNEL_N_2_D473]);
        s->guest_addr = x[1];  // HLE services write straight into the guest's mapping
        region_add(x[1], x[SF2C_KERNEL_N_2_D473], SF2C_KERNEL_N_6_E7F6, SF2C_KERNEL_N_3_4E07);
        if (s->on_map) s->on_map(s);
        x[0] = R_OK;
        break;
    }
    case SF2C_KERNEL_N_0X14_F942: region_remove(x[1]); x[0] = R_OK; break;  // UnmapSharedMemory
    case SF2C_KERNEL_N_0X15_5FB5: {  // CreateTransferMemory(_, addr, size, perm)
        KTransferMem *t = calloc(1, sizeof *t);
        t->obj.type = KOBJ_TMEM;
        t->addr = x[1];
        t->size = x[SF2C_KERNEL_N_2_D473];
        x[0] = R_OK;
        x[1] = kernel_new_handle(&t->obj);
        break;
    }
    case SF2C_KERNEL_N_0X16_D7D4:  // CloseHandle
        pthread_mutex_lock(&g_klock);
        handle_close((uint32_t)x[0]);
        pthread_mutex_unlock(&g_klock);
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X18_D47C: {  // WaitSynchronization(_, handles, count, timeout)
        int32_t idx = -1;
        x[0] = svc_wait_synchronization(x[1], (int32_t)x[SF2C_KERNEL_N_2_D473], (int64_t)x[SF2C_KERNEL_N_3_4E07], &idx);
        x[1] = (uint32_t)idx;
        break;
    }
    case SF2C_KERNEL_N_0X19_6E00: {  // CancelSynchronization
        pthread_mutex_lock(&g_klock);
        KObject *o = handle_get((uint32_t)x[0]);
        if (o && o->type == KOBJ_THREAD) {
            ((KThread *)o)->cancel = true;
            wake((KThread *)o);
            x[0] = R_OK;
        } else x[0] = R_INVALID_HANDLE;
        pthread_mutex_unlock(&g_klock);
        break;
    }
    case SF2C_KERNEL_N_0X1A_7631: x[0] = svc_arbitrate_lock((uint32_t)x[0], x[1], (uint32_t)x[SF2C_KERNEL_N_2_D473]); break;
    case SF2C_KERNEL_N_0X1B_E0FA: x[0] = svc_arbitrate_unlock(x[0]); break;
    case SF2C_KERNEL_N_0X1C_E409: x[0] = svc_wait_process_wide_key(x[0], x[1], (uint32_t)x[SF2C_KERNEL_N_2_D473], (int64_t)x[SF2C_KERNEL_N_3_4E07]); break;
    case SF2C_KERNEL_N_0X1D_A216: svc_signal_process_wide_key(x[0], (int32_t)x[1]); x[0] = R_OK; break;
    case SF2C_KERNEL_N_0X1E_43F5: x[0] = rt_ticks(); break;
    case SF2C_KERNEL_N_0X1F_A571: {  // ConnectToNamedPort(_, name)
        uint32_t h = ipc_connect_named_port((const char *)(uintptr_t)x[1]);
        x[0] = h ? R_OK : R_NOT_FOUND;
        x[1] = h;
        break;
    }
    case SF2C_KERNEL_N_0X21_A102: x[0] = ipc_handle_request(c, (uint32_t)x[0]); break;  // SendSyncRequest
    case SF2C_KERNEL_N_0X22_9F8A: {  // SendSyncRequestWithUserBuffer(buf, size, handle)
        uint64_t saved = c->tpidrro;
        c->tpidrro = x[0];
        x[0] = ipc_handle_request(c, (uint32_t)x[SF2C_KERNEL_N_2_D473]);
        c->tpidrro = saved;
        break;
    }
    case SF2C_KERNEL_N_0X24_EB21: x[0] = R_OK; x[1] = SF2C_KERNEL_N_0X81_C29D; break;  // GetProcessId
    case SF2C_KERNEL_N_0X25_2611: {  // GetThreadId(_, handle)
        KObject *o = handle_get((uint32_t)x[1]);
        x[0] = o ? R_OK : R_INVALID_HANDLE;
        x[1] = o ? ((KThread *)o)->id : 0;
        break;
    }
    case SF2C_KERNEL_N_0X26_50D1:  // Break
        if (x[0] & SF2C_KERNEL_N_0X80000000U_AE05) { LOG(SF2C_KERNEL_S_KERNEL_BREAK_NOTIFICATION_LL_6778, x[0]); x[0] = R_OK; break; }
        FATAL(SF2C_KERNEL_S_SVCBREAK_REASON_LLX_INFO_LLX_C1F2, x[0], x[1], x[SF2C_KERNEL_N_2_D473], pc);
    case SF2C_KERNEL_N_0X27_D7EC: debug_string(x[0], x[1]); x[0] = R_OK; break;
    case SF2C_KERNEL_N_0X29_F941: {  // GetInfo(_, id0, handle, id1)
        uint32_t id0 = (uint32_t)x[1];
        uint64_t v = 0;
        uint32_t r = R_OK;
        switch (id0) {
        case 0: v = SF2C_KERNEL_N_0XF_7419; break;
        case 1: v = SF2C_KERNEL_N_0XFFFFFFFFFFFFFFFFULL_D876; break;
        case SF2C_KERNEL_N_2_D473: v = ALIAS_BASE; break;
        case SF2C_KERNEL_N_3_4E07: v = ALIAS_SIZE; break;
        case SF2C_KERNEL_N_4_4B22: v = HEAP_BASE; break;
        case SF2C_KERNEL_N_5_EF2D: v = HEAP_MAX; break;  // virtual address range
        case SF2C_KERNEL_N_6_E7F6: case SF2C_KERNEL_N_21_6F4B:
            v = backend_guest_heap_capacity();
            if (v > SF2C_KERNEL_N_0XCD500000ULL_1FE5) v = SF2C_KERNEL_N_0XCD500000ULL_1FE5;
            break;
        case SF2C_KERNEL_N_7_7902: case SF2C_KERNEL_N_22_785F: v = heap_size + SF2C_KERNEL_N_0X4000000_329B; break;
        case SF2C_KERNEL_N_8_2C62: v = 0; break;
        case SF2C_KERNEL_N_9_1958: v = 0; break;
        case SF2C_KERNEL_N_11_4FC8: v = ((uint64_t)arc4random() << SF2C_KERNEL_N_32_E29C) | arc4random(); break;
        case SF2C_KERNEL_N_12_6B51: v = GUEST_LO; break;
        case SF2C_KERNEL_N_13_3FDB: v = GUEST_HI - GUEST_LO; break;
        case SF2C_KERNEL_N_14_8527: v = STACK_BASE; break;
        case SF2C_KERNEL_N_15_E629: v = STACK_SIZE; break;
        case SF2C_KERNEL_N_16_B17E: case SF2C_KERNEL_N_17_4523: v = 0; break;
        case SF2C_KERNEL_N_18_4EC9: v = SF2C_KERNEL_N_0X01006BB00C6F0000ULL_15CA; break;
        case SF2C_KERNEL_N_20_F5CA: v = 0; break;
        case SF2C_KERNEL_N_24_C235: {  // FreeThreadCount: 96 - live guest threads. The SDK's own accounting
                    // (unity workers' spawn gate: a result below 1 blocks thread creation;
                    // reference: nxnative dm/host/hle_svc.c thr_limit_used)
            uint32_t live = 0;
            pthread_mutex_lock(&g_klock);
            for (KThread *t = g_threads; t; t = t->anext)
                if (!t->finished) live++;
            pthread_mutex_unlock(&g_klock);
            v = live >= SF2C_KERNEL_N_96_7B1A ? 0 : SF2C_KERNEL_N_96_7B1A - live;
            break;
        }
        default: r = R_INVALID_ENUM; LOG(SF2C_KERNEL_S_KERNEL_GETINFO_UNKNOWN_ID_U__552E, id0, x[SF2C_KERNEL_N_3_4E07]);
        }
        x[0] = r;
        x[1] = v;
        break;
    }
    case SF2C_KERNEL_N_0X2C_8A63:  // MapPhysicalMemory(addr, size)
        memset((void *)x[0], 0, x[1]);
        region_add(x[0], x[1], SF2C_KERNEL_N_0X7_ECB4, SF2C_KERNEL_N_3_4E07);
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X2D_A7B5:  // UnmapPhysicalMemory
        region_remove(x[0]);
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X32_8B2B:  // SetThreadActivity(handle, activity)
        x[0] = R_OK;
        break;
    case SF2C_KERNEL_N_0X33_8FFB: {  // GetThreadContext3(out_context, thread_handle, flags)
        KObject *o = handle_get((uint32_t)x[1]);
        if (!o || o->type != KOBJ_THREAD) {
            x[0] = R_INVALID_HANDLE;
            break;
        }
        KThread *t = (KThread *)o;
        uint64_t out = x[0];
        if (out) {
            for (int i = 0; i < SF2C_KERNEL_N_31_EB1E; i++) {
                *(uint64_t *)(uintptr_t)(out + i * SF2C_KERNEL_N_8_2C62) = t->cpu.x[i];
            }
            *(uint64_t *)(uintptr_t)(out + SF2C_KERNEL_N_31_EB1E * SF2C_KERNEL_N_8_2C62) = t->cpu.sp;
            *(uint64_t *)(uintptr_t)(out + SF2C_KERNEL_N_32_E29C * SF2C_KERNEL_N_8_2C62) = t->cpu.pc;
            *(uint32_t *)(uintptr_t)(out + SF2C_KERNEL_N_33_C6F3 * SF2C_KERNEL_N_8_2C62) = 0;
            memcpy((void *)(uintptr_t)(out + SF2C_KERNEL_N_0X110_8C83), t->cpu.v, sizeof(t->cpu.v));
            *(uint64_t *)(uintptr_t)(out + SF2C_KERNEL_N_0X318_7E03) = t->cpu.tpidrro;
        }
        x[0] = R_OK;
        break;
    }
    case SF2C_KERNEL_N_0X34_806E: x[0] = svc_wait_for_address(x[0], (uint32_t)x[1], (int32_t)x[SF2C_KERNEL_N_2_D473], (int64_t)x[SF2C_KERNEL_N_3_4E07]); break;
    case SF2C_KERNEL_N_0X35_B555: x[0] = svc_signal_to_address(x[0], (uint32_t)x[1], (int32_t)x[SF2C_KERNEL_N_2_D473], (int32_t)x[SF2C_KERNEL_N_3_4E07]); break;
    case SF2C_KERNEL_N_0X45_17A0: {  // CreateEvent -> w1 writable, w2 readable
        KObject *ev = calloc(1, sizeof *ev);
        ev->type = KOBJ_EVENT;
        x[0] = R_OK;
        x[1] = kernel_new_handle(ev);
        x[SF2C_KERNEL_N_2_D473] = kernel_new_handle(ev);
        if (ENV(SF2C_KERNEL_S_LARECOMP_PROF_SVC_6FEB)) LOG(SF2C_KERNEL_S_EVT_GUEST_W_LLX_R_LLX_LR_LLX_57D0, (unsigned long long)x[1], (unsigned long long)x[SF2C_KERNEL_N_2_D473], (unsigned long long)x[SF2C_KERNEL_N_30_624B]);
        break;
    }
    default:
        FATAL(SF2C_KERNEL_S_UNIMPLEMENTED_SVC_0X_X_AT_LL_FAF2, imm, pc, x[0], x[1], x[SF2C_KERNEL_N_2_D473]);
    }
}

// Reboot, step 1: stop every guest thread (see rt_unwind_point) and join it. `idle` keeps the window responsive.
void kernel_stop_threads(void (*idle)(void)) {
    g_rt_unwind = 1;
    uint64_t t0 = plat_now_ns(), logged = t0;
    for (;;) {
        int live = 0;
        pthread_mutex_lock(&g_klock);
        for (KThread *t = g_threads; t; t = t->anext)
            if (t->started && !t->finished) { live++; wake(t); }
        pthread_cond_broadcast(&g_nothread_cond);
        pthread_mutex_unlock(&g_klock);
        if (!live) break;
        uint64_t now = plat_now_ns();
        if (now - logged > SF2C_KERNEL_N_3000000000ULL_16F2) {  // (a thread that never reaches a safe point: who and where)
            logged = now;
            LOG(SF2C_KERNEL_S_REBOOT_STILL_WAITING_FOR_D_G_6E07, live, (unsigned long long)((now - t0) / SF2C_KERNEL_N_1000000_6CCE));
            for (KThread *t = g_threads; t; t = t->anext)
                if (t->started && !t->finished)
                    LOG(SF2C_KERNEL_S_REBOOT_THREAD_LLU_ENTRY_LLX__6491, (unsigned long long)t->id,
                        (unsigned long long)t->entry, (unsigned long long)t->cpu.pc, (unsigned long long)t->cpu.x[SF2C_KERNEL_N_30_624B],
                        t->wait_kind, t->in_sync_wait);
        }
        if (idle) idle();
        usleep(SF2C_KERNEL_N_2000_81A8);
    }
    int n = 0;
    for (KThread *t = g_threads; t; t = t->anext)
        if (t->started) {
#ifdef _WIN32
            if (!guest_thread_join(t->host)) FATAL(SF2C_KERNEL_S_COULD_NOT_JOIN_A_GAME_THREAD_598A);
#else
            pthread_join(t->host, NULL);
#endif
            n++;
        }
    LOG(SF2C_KERNEL_S_REBOOT_D_GUEST_THREADS_STOPP_A056, n, (unsigned long long)((plat_now_ns() - t0) / SF2C_KERNEL_N_1000000_6CCE));
}

// (poor-box) host-stack spy, sampled at hang-dump time: the SIGUSR2 handler runs on the target
// thread itself and copies ONLY its register context (rip/rsp/rbp from ucontext: the only shape
// that is genuinely async-signal-safe - the first v1 used backtrace() and none of the 24 live
// threads ever reached spy_done, i.e. the frozen process was holding the global malloc/dl lock
// the unwinder needs). The dump thread then symbolizes: dladdr our own base for module offsets,
// and walks the rbp frame chain (when frame pointers exist) via rt_probe_u64.
#ifndef _WIN32
#include <dlfcn.h>
#include <signal.h>

static volatile sig_atomic_t spy_armed;

static void spy_handler(int sig, siginfo_t *si, void *uctx) {
    (void)sig;
    (void)si;
    ucontext_t *uc = uctx;
    if (!uc) return;
    KThread *t = t_cur;  // thread-local: the guest thread this host thread runs
    if (spy_armed && t && !t->spy_done) {
#if defined(__APPLE__) && defined(__aarch64__)
        t->spy_rip = uc->uc_mcontext->__ss.__pc;
        t->spy_rsp = uc->uc_mcontext->__ss.__sp;
        t->spy_rbp = uc->uc_mcontext->__ss.__fp;
#elif defined(__APPLE__) && defined(__x86_64__)
        t->spy_rip = uc->uc_mcontext->__ss.__rip;
        t->spy_rsp = uc->uc_mcontext->__ss.__rsp;
        t->spy_rbp = uc->uc_mcontext->__ss.__rbp;
#elif defined(__aarch64__)
        t->spy_rip = uc->uc_mcontext.pc;
        t->spy_rsp = uc->uc_mcontext.sp;
        t->spy_rbp = uc->uc_mcontext.regs[SF2C_KERNEL_N_29_3513];
#elif defined(__x86_64__)
        t->spy_rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
        t->spy_rsp = (uint64_t)uc->uc_mcontext.gregs[REG_RSP];
        t->spy_rbp = (uint64_t)uc->uc_mcontext.gregs[REG_RBP];
#else
#error Unsupported host register context
#endif
        t->spy_done = 1;
    }
}

static void spy_install(void) {
    static volatile sig_atomic_t once;
    if (__atomic_exchange_n(&once, 1, __ATOMIC_RELAXED)) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = spy_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR2, &sa, NULL) == 0) {
        __atomic_store_n(&spy_armed, 1, __ATOMIC_SEQ_CST);
        Dl_info di;
        if (dladdr((void *)(uintptr_t)&spy_install, &di))
            LOG(SF2C_KERNEL_S_SPY_MODULE_BASE_LLX_S_N_8879, (unsigned long long)(uintptr_t)di.dli_fbase,
                di.dli_fname ? di.dli_fname : SF2C_KERNEL_S_EMPTY_15C5);
    }
}
#else
static void spy_install(void) {}
#endif

// One sample: ask, wait up to ~60 ms, print rip/rsp/rbp and walk the rbp frame chain (reading
// another thread's live stack is racy but the stack pages are mapped; rt_probe_u64 stops faults).
static void spy_sample(KThread *t, const char *prefix) {
#ifdef _WIN32
    if (t->started && !t->finished)
        plat_bt_other_thread_tid((int)GetThreadId((HANDLE)t->host), prefix);
#else
    if (!t->started || t->finished) return;
    t->spy_done = 0;
    if (pthread_kill(t->host, SIGUSR2) != 0) {
        LOG(SF2C_KERNEL_S_S_SPY_T_LLU_HOST_PTHREAD_DEA_ABE4, prefix, (unsigned long long)t->id);
        return;
    }
    for (int i = 0; i < SF2C_KERNEL_N_60_39FA && !t->spy_done; i++) usleep(SF2C_KERNEL_N_1000_4051);
    if (!t->spy_done) {
        LOG(SF2C_KERNEL_S_S_SPY_T_LLU_UNRESPONSIVE_60__4E8A, prefix,
            (unsigned long long)t->id);
        return;
    }
    static uint64_t base;  // our own module base (stable per boot: dladdr once in the dump thread)
    if (!base) {
        Dl_info di;
        base = dladdr((void *)(uintptr_t)&spy_install, &di) ? (uint64_t)(uintptr_t)di.dli_fbase : 0;
    }
    LOG(SF2C_KERNEL_S_S_SPY_T_LLU_RIP_LLX_LLX_RSP__E2EC, prefix, (unsigned long long)t->id,
        (unsigned long long)t->spy_rip,
        base && t->spy_rip > base && t->spy_rip < base + SF2C_KERNEL_N_0X2000000ULL_5DDE ? (unsigned long long)(t->spy_rip - base) : SF2C_KERNEL_N_0ULL_DF1B,
        (unsigned long long)t->spy_rsp, (unsigned long long)t->spy_rbp);
    {   // which module (and nearest dynamic symbol) the frozen rip sits in - names libc futex parks,
        // our own lifted loops, or a third-party lib with zero extra tooling
        Dl_info di = {0};
        if (dladdr((void *)(uintptr_t)t->spy_rip, &di))
            LOG(SF2C_KERNEL_S_S_SPY_T_LLU_RIP_IN_S_0X_LLX__EA37, prefix, (unsigned long long)t->id,
                di.dli_fname ? di.dli_fname : SF2C_KERNEL_S_EMPTY_15C5,
                (unsigned long long)((uint64_t)t->spy_rip - (uint64_t)(uintptr_t)di.dli_fbase),
                di.dli_sname ? di.dli_sname : SF2C_KERNEL_S_EMPTY_0933, di.dli_saddr ? (unsigned long long)((uint64_t)t->spy_rip - (uint64_t)(uintptr_t)di.dli_saddr) : SF2C_KERNEL_N_0ULL_DF1B);
    }
    uint64_t rbp = t->spy_rbp;
    for (int i = 0; i < SF2C_KERNEL_N_24_C235; i++) {
        uint64_t ret = 0, up = 0;
        if (!rt_probe_u64(rbp + SF2C_KERNEL_N_8_2C62, &ret)) {
            LOG(SF2C_KERNEL_S_S_SPY_T_LLU_FRAME_D_RBP_CHAI_A705, prefix,
                (unsigned long long)t->id, i, (unsigned long long)rbp);
            break;
        }
        Dl_info di = {0};
        const char *in = SF2C_KERNEL_S_EMPTY_15C5;
        int64_t off = -1;
        if (dladdr((void *)(uintptr_t)ret, &di)) {
            in = di.dli_fname ? di.dli_fname : SF2C_KERNEL_S_EMPTY_15C5;
            off = (int64_t)((uint64_t)ret - (uint64_t)(uintptr_t)di.dli_fbase);
        }
        LOG(SF2C_KERNEL_S_S_SPY_T_LLU_FRAME_D_RET_LLX__B2BE, prefix, (unsigned long long)t->id, i,
            (unsigned long long)ret, in[0] ? SF2C_KERNEL_S_IN_0F80 : SF2C_KERNEL_S_EMPTY_12AE, in[0] ? in : SF2C_KERNEL_S_EMPTY_12AE, off > 0 ? SF2C_KERNEL_S_0X_67BF : SF2C_KERNEL_S_EMPTY_12AE,
            off > 0 ? (unsigned long long)off : SF2C_KERNEL_N_0ULL_DF1B);
        if (!rt_probe_u64(rbp, &up) || up <= rbp || (up & SF2C_KERNEL_N_7_7902)) break;
        rbp = up;
    }
#endif
}

// (diagnostics) every guest thread and what it waits on, with the guest frame chain (x29 / x30 pairs on the guest
// stack: the game function that waits). Doesn't wait for g_klock: a hang may hold it (then says so).
void kernel_dump_threads(const char *prefix) {
    bool locked = false;
    for (int i = 0; i < SF2C_KERNEL_N_100_AD57 && !(locked = pthread_mutex_trylock(&g_klock) == 0); i++) usleep(SF2C_KERNEL_N_1000_4051);
    if (!locked) LOG(SF2C_KERNEL_S_S_G_KLOCK_HELD_FOR_100_MS_RE_F713, prefix);
    static const char *const kinds[] = {SF2C_KERNEL_S_EMPTY_0933, SF2C_KERNEL_S_MUTEX_58C4, SF2C_KERNEL_S_CONDVAR_BB44, SF2C_KERNEL_S_ARBITER_E55E};
    extern const char *hle_name_at(uint64_t pc);
    uint64_t now = plat_now_ns();
    // The load-machine's own gates (nnMain's phase==2 spin and the engine exit flag both read these;
    // GetLoadPhase 0x700166e400 is literally ldr w0,[0x7003bb7a54]):
    uint32_t loadphase = 0, loader_gate = 0;
    rt_probe_u32(SF2C_KERNEL_N_0X7003BB7A54ULL_222D, &loadphase);
    rt_probe_u32(SF2C_KERNEL_N_0X7003BB7A4CULL_F5EA, &loader_gate);          // the PlayerLoop guard byte next to it
    LOG(SF2C_KERNEL_S_S_LOAD_MACHINE_LOADPHASE_U_S_B643, prefix, loadphase,
        loadphase == SF2C_KERNEL_N_2_D473 ? SF2C_KERNEL_S_LOADING_NNMAIN_SPINS_NO_PLAY_C2F3 :
        loadphase == 0 ? SF2C_KERNEL_S_CLEARED_PLAYERLOOP_RUNS_FRAM_E499 : SF2C_KERNEL_S_OTHER_D448, loader_gate);
    for (KThread *t = g_threads; t; t = t->anext) {
        bool waiting = t->wait_kind != WAIT_NONE || t->in_sync_wait;
        const char *hle = hle_name_at(t->cpu.pc);  // (pc: the last call through dispatch; an HLE one: its name)
        if (!t->started || t->finished)
            LOG(SF2C_KERNEL_S_S_THREAD_LLU_H_X_S_S_S_SKIP__04E0, prefix, (unsigned long long)t->id,
                t->handle, hle ? SF2C_KERNEL_S_EMPTY_2C8B : SF2C_KERNEL_S_EMPTY_12AE, hle ? hle : SF2C_KERNEL_S_EMPTY_12AE, hle ? SF2C_KERNEL_S_EMPTY_C921 : SF2C_KERNEL_S_EMPTY_12AE, t->started, t->finished);
        else
        LOG(SF2C_KERNEL_S_S_THREAD_LLU_H_X_PRIO_D_CORE_E419,
            prefix, (unsigned long long)t->id, t->handle, t->prio, t->core, (unsigned long long)t->entry,
            (unsigned long long)t->cpu.pc, hle ? SF2C_KERNEL_S_EMPTY_AB96 : SF2C_KERNEL_S_EMPTY_12AE, hle ? hle : SF2C_KERNEL_S_EMPTY_12AE, hle ? SF2C_KERNEL_S_EMPTY_C921 : SF2C_KERNEL_S_EMPTY_12AE, (unsigned long long)t->cpu.x[SF2C_KERNEL_N_30_624B],
            (unsigned)t->wait_kind < SF2C_KERNEL_N_4_4B22 ? kinds[t->wait_kind] : SF2C_KERNEL_S_EMPTY_15C5, (unsigned long long)t->wait_addr,
            (unsigned long long)t->wait_mutex, t->wait_tag,
            t->in_sync_wait ? SF2C_KERNEL_S_IN_WAITSYNCHRONIZATION_654C : SF2C_KERNEL_S_EMPTY_12AE, t->cancel ? SF2C_KERNEL_S_CANCEL_PENDING_C87D : SF2C_KERNEL_S_EMPTY_12AE,
            waiting ? (unsigned long long)((now - t->wait_since) / SF2C_KERNEL_N_1000000_6CCE) : SF2C_KERNEL_N_0ULL_DF1B);
        if (t->wait_kind == WAIT_CONDVAR || t->wait_kind == WAIT_MUTEX) {
            uint64_t m = t->wait_kind == WAIT_CONDVAR ? t->wait_mutex : t->wait_addr;
            LOG(SF2C_KERNEL_S_S_MUTEX_WORD_X_S_N_A5D9, prefix, *(volatile uint32_t *)(uintptr_t)m,
                t->wait_kind == WAIT_CONDVAR ? SF2C_KERNEL_S_EMPTY_12AE : SF2C_KERNEL_S_WAITING_TO_BE_HANDED_IT_4D2A);
        }
        for (int i = 0; t->in_sync_wait && i < t->sync_count && i < SF2C_KERNEL_N_8_2C62; i++) {
            uint32_t h = ((const uint32_t *)(uintptr_t)t->sync_handles)[i];
            KObject *o = h > SF2C_KERNEL_N_0X100_BE6E && h - SF2C_KERNEL_N_0X100_BE6E < MAX_HANDLES ? handles[h - SF2C_KERNEL_N_0X100_BE6E] : NULL;
            LOG(SF2C_KERNEL_S_S_HANDLE_X_TYPE_D_S_N_E6F4, prefix, h, o ? (int)o->type : 0, o && o->signaled ? SF2C_KERNEL_S_SIGNALED_9C12 : SF2C_KERNEL_S_EMPTY_12AE);
        }
        uint64_t fp = t->cpu.x[SF2C_KERNEL_N_29_3513];  // guest frames: [fp] = caller's fp, [fp + 8] = return address
        for (int i = 0; i < SF2C_KERNEL_N_12_6B51 && fp >= STACK_BASE && fp < STACK_BASE + STACK_SIZE && !(fp & SF2C_KERNEL_N_15_E629); i++) {
            LOG(SF2C_KERNEL_S_S_FRAME_D_LR_LLX_N_942D, prefix, i, (unsigned long long)*(uint64_t *)(uintptr_t)(fp + SF2C_KERNEL_N_8_2C62));
            uint64_t up = *(uint64_t *)(uintptr_t)fp;
            if (up <= fp) break;
            fp = up;
        }
    }
    {   // host-stack spy pass: main first (the freeze is wherever main is), then the rest
        spy_install();
        if (g_main_thread) spy_sample(g_main_thread, prefix);
        for (KThread *t = g_threads; t; t = t->anext)
            if (t != g_main_thread) spy_sample(t, prefix);
    }
    {   // the kernel trace: oldest first
        static const char *const kn[] = {SF2C_KERNEL_S_CV_WAIT_539D, SF2C_KERNEL_S_CV_WAIT_END_815A, SF2C_KERNEL_S_CV_SIGNAL_BA59, SF2C_KERNEL_S_ARB_LOCK_F915, SF2C_KERNEL_S_ARB_LOCK_END_55E5, SF2C_KERNEL_S_ARB_UNLOCK_8C47};
        uint64_t n = __atomic_load_n(&ktrace_n, __ATOMIC_RELAXED), first = n > KTRACE_N ? n - KTRACE_N : 0;
        LOG(SF2C_KERNEL_S_S_KERNEL_TRACE_LAST_LLU_OF_L_0689, prefix,
            (unsigned long long)(n - first), (unsigned long long)n);
        for (uint64_t i = first; i < n; i++) {
            const KTrace *e = &ktrace[i % KTRACE_N];
            long long ago = (long long)(now - e->ns) / SF2C_KERNEL_N_1000000_6CCE;
            switch (e->kind) {
            case KT_WAIT: LOG(SF2C_KERNEL_S_S_LLD_T_LLU_CV_WAIT_KEY_LLX__648D, prefix, ago,
                              (unsigned long long)e->tid, (unsigned long long)e->addr, e->w0, (unsigned long long)e->addr2,
                              e->w1, e->w2, (unsigned long long)e->pc, (unsigned long long)e->lr); break;
            case KT_WAIT_END: LOG(SF2C_KERNEL_S_S_LLD_T_LLU_CV_WAIT_END_KEY__FCED, prefix, ago,
                                  (unsigned long long)e->tid, (unsigned long long)e->addr, e->w0, (unsigned long long)e->addr2,
                                  e->w1, e->arg); break;
            case KT_SIGNAL: LOG(SF2C_KERNEL_S_S_LLD_T_LLU_CV_SIGNAL_KEY_LL_3D47, prefix, ago,
                                (unsigned long long)e->tid, (unsigned long long)e->addr, e->w0, e->w1, (int)e->arg, e->w2,
                                (unsigned long long)e->pc, (unsigned long long)e->lr); break;
            default: LOG(SF2C_KERNEL_S_S_LLD_T_LLU_S_MUTEX_LLX_X_X__2126, prefix, ago, (unsigned long long)e->tid,
                         kn[e->kind < SF2C_KERNEL_N_6_E7F6 ? e->kind : 0], (unsigned long long)e->addr, e->w0, e->w1, e->w2, e->arg);
            }
        }
    }
    if (locked) pthread_mutex_unlock(&g_klock);
}

// The hang dump, one facility for the whole runtime. LARECOMP_HANG_DUMP=<s> (default 30, 0 off), checked every
// second from the main guest thread's start:
// - the main guest thread in one kernel wait that long (startup hangs on Windows with few cores leave no other trace)
// - the renderer's own checks (runtime/nvn/rqueue.c, kernel_hang_hooks): no frame presented twice that long after the
//   GPU device setup, presenting stopped that long, or one noted host wait (GPU fence, queue, nextDrawable /
//   vkAcquire...) that long
// -> "[hang]" lines: why, the renderer's state (render thread queue, what each noted thread waits on), every guest
// thread with its kernel wait and guest frames, and the kernel trace. At most 6 dumps per run.
static bool (*hang_check_fn)(uint64_t limit_ns, char *why, size_t n);
static void (*hang_state_fn)(const char *prefix);
void kernel_hang_hooks(bool (*check)(uint64_t limit_ns, char *why, size_t n), void (*state)(const char *prefix)) {
    __atomic_store_n(&hang_state_fn, state, __ATOMIC_RELEASE);
    __atomic_store_n(&hang_check_fn, check, __ATOMIC_RELEASE);
}
static void *hang_watch(void *arg) {
    uint64_t s = (uint64_t)(uintptr_t)arg, dumped_since = 0;
    for (int dumps = 0; dumps < SF2C_KERNEL_N_6_E7F6;) {
        sleep(1);
        // Live phase tick (independent of hang detection): the load-machine's gates, once per
        // second, on change or every 5 s — cheap proof of where the boot sits.
        static uint32_t last_phase = SF2C_KERNEL_N_0XFFFFFFFF_8755, last_gate = SF2C_KERNEL_N_0XFFFFFFFF_8755;
        static int tick;
        uint32_t loadphase = 0, loader_gate = 0;
        rt_probe_u32(SF2C_KERNEL_N_0X7003BB7A54ULL_222D, &loadphase);
        rt_probe_u32(SF2C_KERNEL_N_0X7003BB7A4CULL_F5EA, &loader_gate);
        if (loadphase != last_phase || loader_gate != last_gate || ++tick >= SF2C_KERNEL_N_5_EF2D) {
            tick = 0;
            LOG(SF2C_KERNEL_S_PHASE_LOADPHASE_U_PLAYERLOOP_DA5E, loadphase, loader_gate);
            last_phase = loadphase;
            last_gate = loader_gate;
        }
        char why[SF2C_KERNEL_N_256_51E8];
        int n = 0;
        pthread_mutex_lock(&g_klock);
        KThread *m = g_main_thread;  // (the current boot's: kernel_reset clears it)
        uint64_t since = m ? m->wait_since : 0, now = plat_now_ns();
        bool stuck = m && m->started && !m->finished && (m->wait_kind != WAIT_NONE || m->in_sync_wait) && since < now &&
                     now - since >= s * SF2C_KERNEL_N_1000000000ULL_E860 && since != dumped_since;
        pthread_mutex_unlock(&g_klock);
        if (stuck) {
            n = snprintf(why, sizeof why, SF2C_KERNEL_S_MAIN_GUEST_THREAD_IN_ONE_KER_4C73, (unsigned long long)s);
            dumped_since = since;
        }
        // kernel-trace-quiet trigger: the in-wait trigger above cannot fire when main spins
        // free-running (a guest loop, or stuck in one HLE call) making no syscalls - the whole
        // ktrace ring freezing is exactly that signature. Dump while the ring stays still for
        // `s` seconds (the freeze's own shape also answers once the spy rows land).
        if (!n) {
            static uint64_t last_n;
            static int quiet = -1;  // (-1: primed on the first tick, before any recording)
            uint64_t kn = __atomic_load_n(&ktrace_n, __ATOMIC_RELAXED);
            if (kn != last_n) {
                last_n = kn;
                quiet = 0;
            } else if (++quiet == s) {
                quiet = 0;
                n = snprintf(why, sizeof why, SF2C_KERNEL_S_KERNEL_TRACE_QUIET_FOR_LLU_S_946D,
                             (unsigned long long)s);
            }
        }
        bool (*check)(uint64_t, char *, size_t) = __atomic_load_n(&hang_check_fn, __ATOMIC_ACQUIRE);  // (every second: it keeps its own "reported" state)
        if (check && check(s * SF2C_KERNEL_N_1000000000ULL_E860, why + n + (n ? SF2C_KERNEL_N_2_D473 : 0), sizeof why - (size_t)n - SF2C_KERNEL_N_2_D473)) {
            if (n) why[n] = SF2C_KERNEL_CH_EMPTY_CF57, why[n + 1] = SF2C_KERNEL_CH_EMPTY_730D;
            n = 1;
        }
        if (!n) continue;
        LOG(SF2C_KERNEL_S_HANG_S_N_314F, why);
        void (*state)(const char *) = __atomic_load_n(&hang_state_fn, __ATOMIC_ACQUIRE);
        if (state) state(SF2C_KERNEL_S_HANG_A2CC);
        kernel_dump_threads(SF2C_KERNEL_S_HANG_A2CC);
        LOG(SF2C_KERNEL_S_HANG_N_72A0);
        dumps++;
    }
    return NULL;
}

// Reboot, step 2 (threads stopped): a fresh kernel. Kernel objects are leaked rather than freed: host statics
// can still point at some (vi.c's vsync event, signalled by its host timer thread).
void kernel_reset(void) {
    pthread_mutex_lock(&g_klock);
    for (KThread *t = g_threads, *next; t; t = next) {
        next = t->anext;
        pthread_cond_destroy(&t->cond);
        free(t);
    }
    g_threads = NULL;
    g_main_thread = NULL;
    g_waitlist = NULL;
    g_syncwaiters = NULL;
    memset(handles, 0, sizeof handles);
    nregions = 0;
    heap_size = 0;
    tls_next = TLS_BASE;
    next_thread_id = SF2C_KERNEL_N_0X50_4171;
    g_rt_unwind = 0;
    pthread_mutex_unlock(&g_klock);
}

void kernel_init(void) {
    // Reserve the heap, alias, stack, and TLS regions.
    region_add(TLS_BASE, TLS_SIZE, SF2C_KERNEL_N_0XC_D882, SF2C_KERNEL_N_3_4E07);
    for (int i = 0; i < g_nmodules; i++) {
        Module *m = &g_modules[i];
        region_add(m->base + m->text_off, m->ro_off - m->text_off, SF2C_KERNEL_N_3_4E07, SF2C_KERNEL_N_5_EF2D);
        region_add(m->base + m->ro_off, m->data_off - m->ro_off, SF2C_KERNEL_N_3_4E07, 1);
        region_add(m->base + m->data_off, m->size - m->data_off, SF2C_KERNEL_N_4_4B22, SF2C_KERNEL_N_3_4E07);
    }
}

KEvent *kernel_create_event_pair(uint32_t *w, uint32_t *r) {
    KObject *ev = calloc(1, sizeof *ev);
    ev->type = KOBJ_EVENT;
    *w = kernel_new_handle(ev);
    *r = kernel_new_handle(ev);
    // Kernel-side accounting: the guest-frame lr names the SDK construct/into-function, so a later
    // WaitSynchronization on this handle resolves to its creation site (e.g. t104 waiting on h12c).
    Cpu *c = kernel_current_cpu();
    LOG(SF2C_KERNEL_S_EVT_PAIR_W_X_R_X_GUEST_PC_LL_43FE, *w, *r,
        (unsigned long long)(c ? c->pc : 0), (unsigned long long)(c ? c->x[SF2C_KERNEL_N_30_624B] : 0),
        (unsigned long long)(t_cur ? t_cur->id : 0));
    return ev;
}

KSharedMem *kernel_create_shared_memory(uint64_t size, void (*on_map)(KSharedMem *), uint32_t *handle) {
    KSharedMem *s = calloc(1, sizeof *s);
    s->obj.type = KOBJ_SHMEM;
    s->size = size;
    s->on_map = on_map;
    *handle = kernel_new_handle(&s->obj);
    return s;
}

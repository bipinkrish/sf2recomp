#include "constants/core.h"
// Guest address -> recompiled function lookup, and host (HLE) function registry.
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gamelib.h"
#include "hle.h"
#include "kernel.h"
#include "network.h"

typedef struct { const uint64_t *addrs; const GuestFn *fns; uint64_t count, lo, hi; } FnTable;
static FnTable tables[SF2C_DISPATCH_N_2_D473];

// (probe helpers live further down; the dispatch-miss fatal path reports the failing object's class)
static bool in_guest(uint64_t a);
static bool probe_u64(uint64_t a, uint64_t *v);
static bool probe_cstr(uint64_t a, char *out, size_t n);
static void probe_managed_object(uint64_t obj, const char *what, int r, uint64_t off);

// HLE stubs live at fake addresses HLE_BASE + 16*i
#define MAX_HLE SF2C_DISPATCH_MAX_HLE
typedef struct { const char *name; HostFn fn; } HleSlot;
static HleSlot hle_slots[MAX_HLE];
static int hle_count;

// Host implementations registered by name (runtime/hle/*.c fill this via HLE_FUNC)
typedef struct { const char *name; HostFn fn; } HleImpl;
#ifdef __APPLE__
extern const HleImpl __start_hle_impls[] __asm__(SF2C_DISPATCH_S_SECTION_START_DATA_HLE_IMPLS_FFA0);
extern const HleImpl __stop_hle_impls[] __asm__(SF2C_DISPATCH_S_SECTION_END_DATA_HLE_IMPLS_DA79);
#elif defined(__linux__)
extern const HleImpl __start_hle_impls[], __stop_hle_impls[];  // (ELF: defined by the linker)
#else
// PE/COFF: the linker sorts .hle$a < .hle$m (entries, hle.h) < .hle$z by the text after '$'.
__attribute__((section(SF2C_DISPATCH_S_HLE_A_BC8C), used)) static const HleImpl __start_hle_impls[1];
__attribute__((section(SF2C_DISPATCH_S_HLE_Z_14B5), used)) static const HleImpl __stop_hle_impls[1];
#endif

HostFn hle_lookup_impl(const char *name) {
    if (network_hle_blocked(name)) return NULL;
    const char *skip = getenv(SF2C_DISPATCH_S_SF2RECOMP_HLE_SKIP_8648);  // debugging: disable HLE for names containing this
    if (skip && strstr(name, skip)) return NULL;
    const HleImpl *start = __start_hle_impls, *stop = __stop_hle_impls;
    __asm__(SF2C_DISPATCH_S_EMPTY_12AE : SF2C_DISPATCH_S_R_E649(start), SF2C_DISPATCH_S_R_E649(stop));  // separate objects: keep the compiler from reasoning about them
    for (const HleImpl *p = start; p < stop; p++)
        if (p->name && !strcmp(p->name, name)) return p->fn;
    return NULL;
}

static void hle_unimplemented(Cpu *c);

uint64_t hle_register(const char *name, HostFn fn) {
    for (int i = 0; i < hle_count; i++)
        if (!strcmp(hle_slots[i].name, name)) return HLE_BASE + SF2C_DISPATCH_N_16ULL_001B * i;
    if (hle_count == MAX_HLE) FATAL(SF2C_DISPATCH_S_TOO_MANY_HLE_STUBS_N_B360);
    hle_slots[hle_count].name = strdup(name);
    hle_slots[hle_count].fn = fn;
    return HLE_BASE + SF2C_DISPATCH_N_16ULL_001B * hle_count++;
}

// (diagnostics) the host function behind an HLE stub address, NULL if pc isn't one
const char *hle_name_at(uint64_t pc) {
    if (pc < HLE_BASE || pc >= HLE_BASE + HLE_SIZE) return NULL;
    int i = (int)((pc - HLE_BASE) / SF2C_DISPATCH_N_16_B17E);
    return i < hle_count ? hle_slots[i].name : NULL;
}

static __thread uint64_t unimpl_pc;
static void hle_unimplemented(Cpu *c) {
    int i = (int)((unimpl_pc - HLE_BASE) / SF2C_DISPATCH_N_16_B17E);
    FATAL(SF2C_DISPATCH_S_UNIMPLEMENTED_HOST_FUNCTION__7D2E,
          hle_slots[i].name, (unsigned long long)c->x[SF2C_DISPATCH_N_30_624B]);
}

uint64_t guest_sym(const char *name) {
    // small cache: callers pass string literals, so pointer identity is a good key
    static struct { const char *name; uint64_t addr; } cache[SF2C_DISPATCH_N_64_A68B];
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    for (int i = 0; i < SF2C_DISPATCH_N_64_A68B && cache[i].name; i++)
        if (cache[i].name == name) { uint64_t a = cache[i].addr; pthread_mutex_unlock(&m); return a; }
    pthread_mutex_unlock(&m);
    uint64_t a = loader_find_symbol(name);
    if (!a) FATAL(SF2C_DISPATCH_S_GUEST_SYMBOL_S_NOT_FOUND_N_BD46, name);
    pthread_mutex_lock(&m);
    for (int i = 0; i < SF2C_DISPATCH_N_64_A68B; i++) if (!cache[i].name) { cache[i].name = name; cache[i].addr = a; break; }
    pthread_mutex_unlock(&m);
    return a;
}

static uint64_t hle_next = HLE_HEAP;
RT_ON_RESET(hle_heap_reset) { hle_next = HLE_HEAP; }  // (guest memory, wiped by the reboot)

void *hle_alloc(uint64_t size) {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    uint64_t a = hle_next;
    hle_next += (size + SF2C_DISPATCH_N_63_DA4E) & ~SF2C_DISPATCH_N_63ULL_F414;
    if (hle_next > HLE_HEAP + HLE_HEAP_SIZE) FATAL(SF2C_DISPATCH_S_HLE_HEAP_EXHAUSTED_N_2DDC);
    pthread_mutex_unlock(&m);
    plat_commit(a, size);
    memset((void *)a, 0, size);
    return (void *)a;
}

void dispatch_init(void) {  // (after gamelib_load)
    const GameLibTables *g = gamelib_tables();
    uint64_t nm = *g->main_count, ns = *g->sdk_count;
    tables[0] = (FnTable){g->main_addrs, g->main_fns, nm, g->main_addrs[0], g->main_addrs[nm - 1]};
    tables[1] = (FnTable){g->sdk_addrs, g->sdk_fns, ns, g->sdk_addrs[0], g->sdk_addrs[ns - 1]};
}

GuestFn dispatch_lookup(uint64_t addr) {
    for (int t = 0; t < SF2C_DISPATCH_N_2_D473; t++) {
        FnTable *ft = &tables[t];
        if (addr < ft->lo || addr > ft->hi) continue;
        uint64_t lo = 0, hi = ft->count;
        while (lo < hi) {
            uint64_t mid = (lo + hi) / SF2C_DISPATCH_N_2_D473;
            if (ft->addrs[mid] < addr) lo = mid + 1;
            else hi = mid;
        }
        if (lo < ft->count && ft->addrs[lo] == addr) return ft->fns[lo];
    }
    return NULL;
}

// Small direct-mapped cache in front of the binary search
#define DCACHE SF2C_DISPATCH_DCACHE
static __thread struct { uint64_t addr; GuestFn fn; } dcache[DCACHE];

__attribute__((noinline)) static GuestFn dispatch_slow(Cpu *c) {
    uint64_t pc = c->pc;
    if (pc >= HLE_BASE && pc < HLE_BASE + HLE_SIZE) {
        rt_unwind_point();  // (HLE calls aren't cached below, so every one passes here: a reboot's safe point)
        int i = (int)((pc - HLE_BASE) / SF2C_DISPATCH_N_16_B17E);
        if (i < hle_count) {
            if (hle_slots[i].fn) return hle_slots[i].fn;
            unimpl_pc = pc;
            return hle_unimplemented;
        }
    }
    GuestFn f = dispatch_lookup(pc);
    if (!f) {
        uint64_t off;
        const char *s = loader_symbol_at(c->x[SF2C_DISPATCH_N_30_624B], &off);
        if (!pc) probe_managed_object(c->x[0], SF2C_DISPATCH_S_NULL_CALL_64A9, 0, 0);
        FATAL(SF2C_DISPATCH_S_NO_FUNCTION_AT_LLX_LR_LLX_S__A40F, pc, c->x[SF2C_DISPATCH_N_30_624B], s ? s : SF2C_DISPATCH_S_EMPTY_15C5, off);
    }
    unsigned h = (unsigned)(pc >> SF2C_DISPATCH_N_2_D473) & (DCACHE - 1);
    dcache[h].addr = pc;
    dcache[h].fn = f;
    return f;
}
__attribute__((noinline)) static void dispatch_miss(Cpu *c) {
    GuestFn f = dispatch_slow(c);
    __attribute__((musttail)) return f(c);
}

// Always tail-calls, so chains of indirect jumps don't grow the host stack.
void rt_dispatch(Cpu *c) {
    unsigned h = (unsigned)(c->pc >> SF2C_DISPATCH_N_2_D473) & (DCACHE - 1);
    if (__builtin_expect(dcache[h].fn && dcache[h].addr == c->pc, 1)) {
        GuestFn f = dcache[h].fn;
        __attribute__((musttail)) return f(c);
    }
    __attribute__((musttail)) return dispatch_miss(c);
}

void guest_call(Cpu *c, uint64_t addr) {
    uint64_t lr = c->x[SF2C_DISPATCH_N_30_624B];
    c->x[SF2C_DISPATCH_N_30_624B] = 0;
    c->pc = addr;
    rt_dispatch(c);
    c->x[SF2C_DISPATCH_N_30_624B] = lr;
}

// Host pc -> recompiled guest function containing it ("f_<addr>"), for hosts without symbol lookup.
static int cmp_host(const void *a, const void *b) {
    uint64_t x = ((const uint64_t *)a)[0], y = ((const uint64_t *)b)[0];
    return x < y ? -1 : x > y;
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) {
    static uint64_t (*byhost)[SF2C_DISPATCH_N_2_D473];  // {host address, guest address}, sorted by host address
    static uint64_t n;
    static __thread char name[SF2C_DISPATCH_N_32_E29C];
    if (!byhost) {
        uint64_t total = tables[0].count + tables[1].count, k = 0;
        uint64_t (*v)[SF2C_DISPATCH_N_2_D473] = malloc(total * sizeof *v);
        if (!v) return NULL;
        for (int t = 0; t < SF2C_DISPATCH_N_2_D473; t++)
            for (uint64_t i = 0; i < tables[t].count; i++) {
                v[k][0] = (uint64_t)(uintptr_t)tables[t].fns[i];
                v[k++][1] = tables[t].addrs[i];
            }
        qsort(v, k, sizeof *v, cmp_host);
        n = k;
        byhost = v;
    }
    uint64_t lo = 0, hi = n;  // last entry with host address <= pc
    while (lo < hi) {
        uint64_t mid = (lo + hi) / SF2C_DISPATCH_N_2_D473;
        if (byhost[mid][0] <= pc) lo = mid + 1;
        else hi = mid;
    }
    if (!lo || pc - byhost[lo - 1][0] > SF2C_DISPATCH_N_0X100000_0A2A) return NULL;
    snprintf(name, sizeof name, SF2C_DISPATCH_S_F_LLX_982B, (unsigned long long)byhost[lo - 1][1]);
    if (offset) *offset = pc - byhost[lo - 1][0];
    return name;
}

static const char *host_sym(uint64_t pc, uint64_t *offset) {
    // (game functions first: they live in lasgame.dll, which the OS lookup can only name as a whole)
    const char *s = rt_host_fn_name(pc, offset);
    return s ? s : plat_symbol(pc, offset);
}

// Safe diagnostic reads: unmapped guest addresses fail instead of causing
// another fault inside the crash handler. __cxa_throw decodes the known wrapper.
static bool probe_read(uint64_t a, void *buf, size_t n) {
    return plat_probe_read(a, buf, n);
}
static bool probe_u32(uint64_t a, uint32_t *v) { return probe_read(a, v, SF2C_DISPATCH_N_4_4B22); }
static bool probe_u64(uint64_t a, uint64_t *v) { return probe_read(a, v, SF2C_DISPATCH_N_8_2C62); }

bool rt_probe_u32(uint64_t a, uint32_t *v) { return probe_u32(a, v); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { return probe_u64(a, v); }  // (hle/ use: pre-call reads)
static bool in_guest(uint64_t a) { return a >= GUEST_LO && a < GUEST_HI; }

static bool probe_cstr(uint64_t a, char *out, size_t n) {
    for (size_t o = 0; o < n - 1; o++) {
        uint8_t ch;
        if (!probe_read(a + o, &ch, 1)) return false;
        if (!ch) { out[o] = 0; return o > 0; }
        out[o] = (ch >= SF2C_DISPATCH_N_0X20_A766 && ch < SF2C_DISPATCH_N_0X7F_0AFB) ? (char)ch : SF2C_DISPATCH_CH_EMPTY_5DE5;
    }
    return false;
}

// (a managed string: klass, monitor, int length at +0x10, UTF-16 chars at +0x14)
static bool probe_utf16(uint64_t a, char *out, size_t n) {
    uint32_t len;
    if (!probe_u32(a + SF2C_DISPATCH_N_0X10_BF9C, &len) || !len || len > SF2C_DISPATCH_N_0X20000_422F) return false;
    size_t o = 0;
    for (uint32_t i = 0; i < len && o < n - 1; i++) {
        uint16_t ch;
        if (!probe_read(a + SF2C_DISPATCH_N_0X14_F942 + SF2C_DISPATCH_N_2_D473 * (uint64_t)i, &ch, SF2C_DISPATCH_N_2_D473) || !ch) break;
        if (ch >= SF2C_DISPATCH_N_0X20_A766 && ch < SF2C_DISPATCH_N_0X7F_0AFB) out[o++] = (char)ch;
        else if (ch == SF2C_DISPATCH_CH_R_4859 || ch == SF2C_DISPATCH_CH_N_AD65) out[o++] = SF2C_DISPATCH_CH_EMPTY_730D;
        else { out[o++] = SF2C_DISPATCH_CH_EMPTY_5DE5; if (i < o) break; }
    }
    out[o] = 0;
    return o > 0;
}

// A managed (IL2CPP) object: klass, name/namespaze strings, then System.Exception's string fields.
static void probe_managed_object(uint64_t obj, const char *what, int r, uint64_t off) {
    if (!in_guest(obj)) return;
    uint64_t klass;
    if (!probe_u64(obj, &klass) || !in_guest(klass)) return;
    char name[SF2C_DISPATCH_N_96_7B1A], ns[SF2C_DISPATCH_N_96_7B1A];
    uint64_t kname, kns;
    bool have_name = probe_u64(klass + SF2C_DISPATCH_N_0X10_BF9C, &kname) && kname >= SF2C_DISPATCH_N_0X10_BF9C && probe_cstr(kname, name, sizeof name);
    bool have_ns = probe_u64(klass + SF2C_DISPATCH_N_0X18_D47C, &kns) && kns >= SF2C_DISPATCH_N_0X10_BF9C && probe_cstr(kns, ns, sizeof ns);
    if (!have_name && !have_ns) return;
    LOG(SF2C_DISPATCH_S_X_02D_S_LLX_OBJ_CLASS_S_S_S__C268, r, what, (unsigned long long)off,
        have_ns ? ns : SF2C_DISPATCH_S_EMPTY_12AE, have_ns && have_name ? SF2C_DISPATCH_S_EMPTY_7299 : SF2C_DISPATCH_S_EMPTY_12AE, have_name ? name : SF2C_DISPATCH_S_EMPTY_12AE);
    for (uint64_t f = SF2C_DISPATCH_N_0X10_BF9C; f <= SF2C_DISPATCH_N_0X58_08FB; f += SF2C_DISPATCH_N_8_2C62) {  // (System.Exception fields: className, message, stackTrace, ...)
        uint64_t p;
        if (!probe_u64(obj + f, &p) || !in_guest(p)) continue;
        char msg[SF2C_DISPATCH_N_192_EB3B];
        if (probe_utf16(p, msg, sizeof msg) && strlen(msg) > SF2C_DISPATCH_N_2_D473)
            LOG(SF2C_DISPATCH_S_X_02D_S_LLX_FIELD_0X_LLX_S_N_0076, r, what, (unsigned long long)off, (unsigned long long)f, msg);
    }
}

void rt_fatal_trace(void) {
    fprintf(stderr, SF2C_DISPATCH_S_HOST_BACKTRACE_GUEST_FUNCTIO_7967);
    plat_print_backtrace();
    Cpu *c = kernel_current_cpu();
    if (c) {
        uint64_t off = 0;
        const char *sym = loader_symbol_at(c->x[SF2C_DISPATCH_N_30_624B], &off);
        fprintf(stderr, SF2C_DISPATCH_S_GUEST_SP_LLX_LR_LLX_S_0X_LLX_33BB, c->sp, c->x[SF2C_DISPATCH_N_30_624B], sym ? sym : SF2C_DISPATCH_S_UNKNOWN_EB8B, (unsigned long long)off);
        for (int i = 0; i < SF2C_DISPATCH_N_31_EB1E; ++i)
            fprintf(stderr, SF2C_DISPATCH_S_X_2D_016LLX_S_FF3D, i, (unsigned long long)c->x[i],
                    i % SF2C_DISPATCH_N_4_4B22 == SF2C_DISPATCH_N_3_4E07 || i == SF2C_DISPATCH_N_30_624B ? SF2C_DISPATCH_S_N_D7A5 : SF2C_DISPATCH_S_EMPTY_12AE);
    }
    plat_fatal_notice();
    // (debug) SF2RECOMP_HWBREAK=1: park this thread forever instead of aborting, so a `gdb -p`
    // can inspect the crash's exact host state (registers, heap structs) before the core goes away
    if (getenv(SF2C_DISPATCH_S_SF2RECOMP_HWBREAK_8AFC)) {
        fprintf(stderr, SF2C_DISPATCH_S_HWBREAK_STOPPED_PID_D_FROZEN_C10E, (int)getpid(), (int)getpid());
        for (;;) sleep(SF2C_DISPATCH_N_3600_8B34);
    }
    // our crash handler also catches SIGABRT: without this abort() re-entered it forever (a 5 GB log)
    signal(SIGABRT, SIG_DFL);
    abort();
}

// Throws with a matching generated LSDA catch resume at that guest landing pad. If no
// typed/catch-all handler is active, keep the detailed fatal diagnostic below.
HLE_FUNC(SF2C_DISPATCH_S_CXA_THROW_DFEA, hle_cxa_throw) {
    if (rt_guest_raise(c->x[0], c->x[1])) return;
    LOG(SF2C_DISPATCH_S_THROW_GUEST_C_EXCEPTION_OBJ__ADA0,
        (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[SF2C_DISPATCH_N_2_D473]);
    uint64_t name_ptr = 0;
    char name[SF2C_DISPATCH_N_96_7B1A] = {0};
    if (in_guest(c->x[1]) && probe_u64(c->x[1] + SF2C_DISPATCH_N_8_2C62, &name_ptr) && probe_cstr(name_ptr, name, sizeof name))
        LOG(SF2C_DISPATCH_S_THROW_C_TYPE_S_N_1953, name);
    uint64_t managed = 0;
    if (probe_u64(c->x[0], &managed) && in_guest(managed))
        probe_managed_object(managed, SF2C_DISPATCH_S_EXCEPTION_C22C, 0, 0);
    rt_fatal_trace();
}

void rt_udf(Cpu *c, uint64_t pc) {
    (void)c;
    FATAL(SF2C_DISPATCH_S_UNDEFINED_INSTRUCTION_TRAP_A_D0DA, pc);
}

// SDK trap stubs retain their original failure semantics.
void rt_stub(Cpu *c, uint64_t pc) {
    FATAL(SF2C_DISPATCH_S_GUEST_SDK_TRAP_AT_LLX_LR_LLX_15E1, pc, c->x[SF2C_DISPATCH_N_30_624B]);
}

void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) {
    (void)c;
    FATAL(SF2C_DISPATCH_S_BRK_U_AT_LLX_N_CCDD, imm, pc);
}

// ---- crash reporting

// Debug data watchpoint: the page is protected; accesses in [lo, hi) are attributed to the host
// function (f_<guest addr>) doing them, then the page is reopened until the next re-arm.
uint64_t g_watch_lo, g_watch_hi;
uint64_t g_fault_lr;  // (the platform's handler: the faulting thread's link register, for the watch log)
static uint64_t watch_page;
static void *watch_thread(void *a) {
    (void)a;
    int r = plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_NONE);
    fprintf(stderr, SF2C_DISPATCH_S_WATCH_PAGE_LLX_PROTECT_D_N_58AB, (unsigned long long)watch_page, r);
    extern volatile int g_watch_stop;
    while (!g_watch_stop) { plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_NONE); usleep(SF2C_DISPATCH_N_100_AD57); }
    plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_RW);
    fprintf(stderr, SF2C_DISPATCH_S_WATCH_PAGE_LLX_RELEASED_N_5CD7, (unsigned long long)watch_page);
    return NULL;
}
volatile int g_watch_stop;  // (set by the watcher's client: the watch ends, the page opens for good)
const char *rt_host_sym(uint64_t pc, uint64_t *off) { return host_sym(pc, off); }  // (the renderer's fault log)
void watch_arm(void) {
    if (!g_watch_lo) {  // (debug) SF2RECOMP_WATCH_ADDR=<lo>,<hi> (hex): who touches [lo, hi) - one page watched
        const char *e = getenv(SF2C_DISPATCH_S_SF2RECOMP_WATCH_ADDR_5B12);
        if (e) { g_watch_lo = strtoull(e, (char **)&e, SF2C_DISPATCH_N_16_B17E); if (*e == SF2C_DISPATCH_CH_EMPTY_EE1B) g_watch_hi = strtoull(e + 1, NULL, SF2C_DISPATCH_N_16_B17E); }
    }
    if (!g_watch_lo || watch_page) return;
    watch_page = g_watch_lo & ~(PLAT_PAGE - 1);
    pthread_t t;
    pthread_create(&t, NULL, watch_thread, NULL);
}

// The renderer's write watch on storage buffers (the Vulkan backend's renderer_watch_fault, exec.c; the Metal one's,
// renderer.m): registered by the backend when it arms pages, called first for every fault on any thread. A plain
// pointer, not a weak symbol: an undefined weak function reference doesn't link on Mach-O.
static bool (*g_watch_hook)(uint64_t fa);
void rt_fault_watch_hook(bool (*hook)(uint64_t fa)) { g_watch_hook = hook; }
bool rt_fault_watch(uint64_t fa, uint64_t pc) {
    if (g_watch_hook && g_watch_hook(fa)) return true;
    bool watcher = watch_page && fa >= watch_page && fa < watch_page + PLAT_PAGE;
    if (!watcher) return false;
    {
        uint64_t lr = g_fault_lr;
        extern uint64_t renderer_present_count(void);
        static uint64_t last_frame, last_pc;
        uint64_t fr = renderer_present_count();
        if (fr != last_frame || pc != last_pc) {  // (each (frame, pc) once; the dedup table below covers repeats)
            last_frame = fr, last_pc = pc;
            uint64_t lo = 0;
            const char *ls = host_sym(lr, &lo);
            // generated guest code first (f_<addr> names cover our own .text, where host_sym only sees dynsym)
            const char *pfn = rt_host_fn_name(pc, NULL);
            char ps[SF2C_DISPATCH_N_64_A68B] = SF2C_DISPATCH_S_EMPTY_15C5;
            if (pfn) snprintf(ps, sizeof ps, SF2C_DISPATCH_S_S_0030, pfn);
            else if (host_sym(pc, NULL)) snprintf(ps, sizeof ps, SF2C_DISPATCH_S_S_0030, host_sym(pc, NULL));
            fprintf(stderr, SF2C_DISPATCH_S_WATCH_FA_LLX_PC_LLX_S_LR_LLX_5973, (unsigned long long)fa,
                    (unsigned long long)pc, ps, (unsigned long long)lr, ls ? ls : SF2C_DISPATCH_S_EMPTY_15C5, (unsigned long long)lo);
            // (debug) identify the mapping the writer pc lives in when neither library covers it
            if (!pfn && !host_sym(pc, NULL)) {
                FILE *m = fopen(SF2C_DISPATCH_S_PROC_SELF_MAPS_D46C, SF2C_DISPATCH_S_R_2659);
                if (m) for (char l[SF2C_DISPATCH_N_512_94F8]; fgets(l, sizeof l, m);) {
                    uint64_t mlo = 0, mhi = 0;
                    if (sscanf(l, SF2C_DISPATCH_S_LX_LX_EE31, (unsigned long *)&mlo, (unsigned long *)&mhi) != SF2C_DISPATCH_N_2_D473) continue;
                    if (pc >= mlo && pc < mhi) { fprintf(stderr, SF2C_DISPATCH_S_WATCH_WRITER_MAP_S_C932, l); break; }
                }
                if (m) fclose(m);
            }
        }
        static char seen[SF2C_DISPATCH_N_64_A68B][SF2C_DISPATCH_N_48_9801];
        static int nseen;
        const char *nm = host_sym(pc, NULL);
        if (!nm) nm = SF2C_DISPATCH_S_EMPTY_15C5;
        char key[SF2C_DISPATCH_N_48_9801];  // (each function + offset once)
        snprintf(key, sizeof key, SF2C_DISPATCH_S_36S_LLX_D2D9, nm, (unsigned long long)fa);
        bool found = false;
        for (int i = 0; i < nseen; i++) found |= !strcmp(seen[i], key);
        if (!found && nseen < SF2C_DISPATCH_N_64_A68B) {
            snprintf(seen[nseen++], SF2C_DISPATCH_N_48_9801, SF2C_DISPATCH_S_S_0030, key);
            fprintf(stderr, SF2C_DISPATCH_S_WATCH_LLX_BY_S_N_B56C, fa - g_watch_lo, nm);
        }
    }
    plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_RW);
    return true;
}

void rt_fault_report(const char *what, uint64_t fa, uint64_t pc, uint64_t lr) {
    rt_crash_fault(what, fa, pc);
    uint64_t po = 0, lo = 0;
    const char *ps = host_sym(pc, &po), *ls = host_sym(lr, &lo);
    fprintf(stderr, SF2C_DISPATCH_S_N_S_ACCESSING_LLX_N_HOST_PC__A55E, what,
            (unsigned long long)fa, (unsigned long long)pc, ps ? ps : SF2C_DISPATCH_S_EMPTY_15C5, (unsigned long long)po,
            (unsigned long long)lr, ls ? ls : SF2C_DISPATCH_S_EMPTY_15C5);
    rt_fatal_trace();
}

void crash_handler_install(void) { plat_crash_handler_install(); }

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

typedef struct { const uint64_t *addrs; const GuestFn *fns; uint64_t count, lo, hi; } FnTable;
static FnTable tables[2];

// (probe helpers live further down; the dispatch-miss fatal path reports the failing object's class)
static bool in_guest(uint64_t a);
static bool probe_u64(uint64_t a, uint64_t *v);
static bool probe_cstr(uint64_t a, char *out, size_t n);
static void probe_managed_object(uint64_t obj, const char *what, int r, uint64_t off);

// HLE stubs live at fake addresses HLE_BASE + 16*i
#define MAX_HLE 4096
typedef struct { const char *name; HostFn fn; } HleSlot;
static HleSlot hle_slots[MAX_HLE];
static int hle_count;

// Host implementations registered by name (runtime/hle/*.c fill this via HLE_FUNC)
typedef struct { const char *name; HostFn fn; } HleImpl;
#ifdef __APPLE__
extern const HleImpl __start_hle_impls[] __asm__("section$start$__DATA$__hle_impls");
extern const HleImpl __stop_hle_impls[] __asm__("section$end$__DATA$__hle_impls");
#elif defined(__linux__)
extern const HleImpl __start_hle_impls[], __stop_hle_impls[];  // (ELF: defined by the linker)
#else
// PE/COFF: the linker sorts .hle$a < .hle$m (entries, hle.h) < .hle$z by the text after '$'.
__attribute__((section(".hle$a"), used)) static const HleImpl __start_hle_impls[1];
__attribute__((section(".hle$z"), used)) static const HleImpl __stop_hle_impls[1];
#endif

HostFn hle_lookup_impl(const char *name) {
    const char *skip = getenv("LARECOMP_HLE_SKIP");  // debugging: disable HLE for names containing this
    if (skip && strstr(name, skip)) return NULL;
    const HleImpl *start = __start_hle_impls, *stop = __stop_hle_impls;
    __asm__("" : "+r"(start), "+r"(stop));  // separate objects: keep the compiler from reasoning about them
    for (const HleImpl *p = start; p < stop; p++)
        if (p->name && !strcmp(p->name, name)) return p->fn;
    return NULL;
}

static void hle_unimplemented(Cpu *c);

uint64_t hle_register(const char *name, HostFn fn) {
    for (int i = 0; i < hle_count; i++)
        if (!strcmp(hle_slots[i].name, name)) return HLE_BASE + 16ull * i;
    if (hle_count == MAX_HLE) FATAL("too many HLE stubs\n");
    hle_slots[hle_count].name = strdup(name);
    hle_slots[hle_count].fn = fn;
    return HLE_BASE + 16ull * hle_count++;
}

// (diagnostics) the host function behind an HLE stub address, NULL if pc isn't one
const char *hle_name_at(uint64_t pc) {
    if (pc < HLE_BASE || pc >= HLE_BASE + HLE_SIZE) return NULL;
    int i = (int)((pc - HLE_BASE) / 16);
    return i < hle_count ? hle_slots[i].name : NULL;
}

static __thread uint64_t unimpl_pc;
static void hle_unimplemented(Cpu *c) {
    int i = (int)((unimpl_pc - HLE_BASE) / 16);
    FATAL("unimplemented host function %s (guest lr=%llx)\n",
          hle_slots[i].name, (unsigned long long)c->x[30]);
}

uint64_t guest_sym(const char *name) {
    // small cache: callers pass string literals, so pointer identity is a good key
    static struct { const char *name; uint64_t addr; } cache[64];
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    for (int i = 0; i < 64 && cache[i].name; i++)
        if (cache[i].name == name) { uint64_t a = cache[i].addr; pthread_mutex_unlock(&m); return a; }
    pthread_mutex_unlock(&m);
    uint64_t a = loader_find_symbol(name);
    if (!a) FATAL("guest symbol %s not found\n", name);
    pthread_mutex_lock(&m);
    for (int i = 0; i < 64; i++) if (!cache[i].name) { cache[i].name = name; cache[i].addr = a; break; }
    pthread_mutex_unlock(&m);
    return a;
}

static uint64_t hle_next = HLE_HEAP;
RT_ON_RESET(hle_heap_reset) { hle_next = HLE_HEAP; }  // (guest memory, wiped by the reboot)

void *hle_alloc(uint64_t size) {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    uint64_t a = hle_next;
    hle_next += (size + 63) & ~63ull;
    if (hle_next > HLE_HEAP + HLE_HEAP_SIZE) FATAL("HLE heap exhausted\n");
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
    for (int t = 0; t < 2; t++) {
        FnTable *ft = &tables[t];
        if (addr < ft->lo || addr > ft->hi) continue;
        uint64_t lo = 0, hi = ft->count;
        while (lo < hi) {
            uint64_t mid = (lo + hi) / 2;
            if (ft->addrs[mid] < addr) lo = mid + 1;
            else hi = mid;
        }
        if (lo < ft->count && ft->addrs[lo] == addr) return ft->fns[lo];
    }
    return NULL;
}

// Small direct-mapped cache in front of the binary search
#define DCACHE 4096
static __thread struct { uint64_t addr; GuestFn fn; } dcache[DCACHE];

__attribute__((noinline)) static GuestFn dispatch_slow(Cpu *c) {
    uint64_t pc = c->pc;
    if (pc >= HLE_BASE && pc < HLE_BASE + HLE_SIZE) {
        rt_unwind_point();  // (HLE calls aren't cached below, so every one passes here: a reboot's safe point)
        int i = (int)((pc - HLE_BASE) / 16);
        if (i < hle_count) {
            if (hle_slots[i].fn) return hle_slots[i].fn;
            unimpl_pc = pc;
            return hle_unimplemented;
        }
    }
    GuestFn f = dispatch_lookup(pc);
    if (!f) {
        uint64_t off;
        const char *s = loader_symbol_at(c->x[30], &off);
        if (!pc) probe_managed_object(c->x[0], "null-call", 0, 0);
        FATAL("no function at %llx (lr=%llx %s+%llx)\n", pc, c->x[30], s ? s : "?", off);
    }
    unsigned h = (unsigned)(pc >> 2) & (DCACHE - 1);
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
    unsigned h = (unsigned)(c->pc >> 2) & (DCACHE - 1);
    if (__builtin_expect(dcache[h].fn && dcache[h].addr == c->pc, 1)) {
        GuestFn f = dcache[h].fn;
        __attribute__((musttail)) return f(c);
    }
    __attribute__((musttail)) return dispatch_miss(c);
}

void guest_call(Cpu *c, uint64_t addr) {
    uint64_t lr = c->x[30];
    c->x[30] = 0;
    c->pc = addr;
    rt_dispatch(c);
    c->x[30] = lr;
}

// Host pc -> recompiled guest function containing it ("f_<addr>"), for hosts without symbol lookup.
static int cmp_host(const void *a, const void *b) {
    uint64_t x = ((const uint64_t *)a)[0], y = ((const uint64_t *)b)[0];
    return x < y ? -1 : x > y;
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) {
    static uint64_t (*byhost)[2];  // {host address, guest address}, sorted by host address
    static uint64_t n;
    static __thread char name[32];
    if (!byhost) {
        uint64_t total = tables[0].count + tables[1].count, k = 0;
        uint64_t (*v)[2] = malloc(total * sizeof *v);
        if (!v) return NULL;
        for (int t = 0; t < 2; t++)
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
        uint64_t mid = (lo + hi) / 2;
        if (byhost[mid][0] <= pc) lo = mid + 1;
        else hi = mid;
    }
    if (!lo || pc - byhost[lo - 1][0] > 0x100000) return NULL;
    snprintf(name, sizeof name, "f_%llx", (unsigned long long)byhost[lo - 1][1]);
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
    static int fd = -2;  // (-2: not opened yet)
    if (fd == -2) fd = open("/proc/self/mem", O_RDONLY);
    if (fd < 0) return false;
    return pread(fd, buf, n, (off_t)a) == (ssize_t)n;
}
static bool probe_u32(uint64_t a, uint32_t *v) { return probe_read(a, v, 4); }
static bool probe_u64(uint64_t a, uint64_t *v) { return probe_read(a, v, 8); }

bool rt_probe_u32(uint64_t a, uint32_t *v) { return probe_u32(a, v); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { return probe_u64(a, v); }  // (hle/ use: pre-call reads)
static bool in_guest(uint64_t a) { return a >= GUEST_LO && a < GUEST_HI; }

static bool probe_cstr(uint64_t a, char *out, size_t n) {
    for (size_t o = 0; o < n - 1; o++) {
        uint8_t ch;
        if (!probe_read(a + o, &ch, 1)) return false;
        if (!ch) { out[o] = 0; return o > 0; }
        out[o] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    return false;
}

// (a managed string: klass, monitor, int length at +0x10, UTF-16 chars at +0x14)
static bool probe_utf16(uint64_t a, char *out, size_t n) {
    uint32_t len;
    if (!probe_u32(a + 0x10, &len) || !len || len > 0x20000) return false;
    size_t o = 0;
    for (uint32_t i = 0; i < len && o < n - 1; i++) {
        uint16_t ch;
        if (!probe_read(a + 0x14 + 2 * (uint64_t)i, &ch, 2) || !ch) break;
        if (ch >= 0x20 && ch < 0x7f) out[o++] = (char)ch;
        else if (ch == '\r' || ch == '\n') out[o++] = ' ';
        else { out[o++] = '?'; if (i < o) break; }
    }
    out[o] = 0;
    return o > 0;
}

// A managed (IL2CPP) object: klass, name/namespaze strings, then System.Exception's string fields.
static void probe_managed_object(uint64_t obj, const char *what, int r, uint64_t off) {
    if (!in_guest(obj)) return;
    uint64_t klass;
    if (!probe_u64(obj, &klass) || !in_guest(klass)) return;
    char name[96], ns[96];
    uint64_t kname, kns;
    bool have_name = probe_u64(klass + 0x10, &kname) && kname >= 0x10 && probe_cstr(kname, name, sizeof name);
    bool have_ns = probe_u64(klass + 0x18, &kns) && kns >= 0x10 && probe_cstr(kns, ns, sizeof ns);
    if (!have_name && !have_ns) return;
    LOG("  [x%02d] %s+%llx obj: class %s%s%s\n", r, what, (unsigned long long)off,
        have_ns ? ns : "", have_ns && have_name ? "." : "", have_name ? name : "");
    for (uint64_t f = 0x10; f <= 0x58; f += 8) {  // (System.Exception fields: className, message, stackTrace, ...)
        uint64_t p;
        if (!probe_u64(obj + f, &p) || !in_guest(p)) continue;
        char msg[192];
        if (probe_utf16(p, msg, sizeof msg) && strlen(msg) > 2)
            LOG("  [x%02d] %s+%llx field +0x%llx: '%s'\n", r, what, (unsigned long long)off, (unsigned long long)f, msg);
    }
}

void rt_fatal_trace(void) {
    fprintf(stderr, "host backtrace (guest functions are f_<addr>):\n");
    plat_print_backtrace();
    Cpu *c = kernel_current_cpu();
    if (c) {
        uint64_t off = 0;
        const char *sym = loader_symbol_at(c->x[30], &off);
        fprintf(stderr, "guest: sp=%llx lr=%llx (%s + 0x%llx)\n", c->sp, c->x[30], sym ? sym : "unknown", (unsigned long long)off);
        for (int i = 0; i < 31; ++i)
            fprintf(stderr, "  x%-2d %016llx%s", i, (unsigned long long)c->x[i],
                    i % 4 == 3 || i == 30 ? "\n" : "");
    }
    plat_fatal_notice();
    // (debug) LARECOMP_HWBREAK=1: park this thread forever instead of aborting, so a `gdb -p`
    // can inspect the crash's exact host state (registers, heap structs) before the core goes away
    if (getenv("LARECOMP_HWBREAK")) {
        fprintf(stderr, "hwbreak: stopped pid %d (frozen; gdb -p %d now)\n", (int)getpid(), (int)getpid());
        for (;;) sleep(3600);
    }
    // our crash handler also catches SIGABRT: without this abort() re-entered it forever (a 5 GB log)
    signal(SIGABRT, SIG_DFL);
    abort();
}

// Guest C++ landing pads are not yet connected to the translated host frames.
// Report the real exception and stop; never fabricate a return or abandon a thread.
HLE_FUNC("__cxa_throw", hle_cxa_throw) {
    LOG("[throw] guest C++ exception: obj %llx, type_info %llx, destructor %llx\n",
        (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2]);
    uint64_t name_ptr = 0;
    char name[96] = {0};
    if (in_guest(c->x[1]) && probe_u64(c->x[1] + 8, &name_ptr) && probe_cstr(name_ptr, name, sizeof name))
        LOG("[throw] C++ type '%s'\n", name);
    uint64_t managed = 0;
    if (probe_u64(c->x[0], &managed) && in_guest(managed))
        probe_managed_object(managed, "exception", 0, 0);
    rt_fatal_trace();
}

void rt_udf(Cpu *c, uint64_t pc) {
    (void)c;
    FATAL("undefined instruction / trap at %llx\n", pc);
}

// SDK trap stubs retain their original failure semantics.
void rt_stub(Cpu *c, uint64_t pc) {
    FATAL("guest SDK trap at %llx (lr=%llx)\n", pc, c->x[30]);
}

void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) {
    (void)c;
    FATAL("brk #%u at %llx\n", imm, pc);
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
    fprintf(stderr, "[watch] page %llx protect -> %d\n", (unsigned long long)watch_page, r);
    extern volatile int g_watch_stop;
    while (!g_watch_stop) { plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_NONE); usleep(100); }
    plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_RW);
    fprintf(stderr, "[watch] page %llx released\n", (unsigned long long)watch_page);
    return NULL;
}
volatile int g_watch_stop;  // (set by the watcher's client: the watch ends, the page opens for good)
const char *rt_host_sym(uint64_t pc, uint64_t *off) { return host_sym(pc, off); }  // (the renderer's fault log)
void watch_arm(void) {
    if (!g_watch_lo) {  // (debug) LARECOMP_WATCH_ADDR=<lo>,<hi> (hex): who touches [lo, hi) - one page watched
        const char *e = getenv("LARECOMP_WATCH_ADDR");
        if (e) { g_watch_lo = strtoull(e, (char **)&e, 16); if (*e == ',') g_watch_hi = strtoull(e + 1, NULL, 16); }
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
            char ps[64] = "?";
            if (pfn) snprintf(ps, sizeof ps, "%s", pfn);
            else if (host_sym(pc, NULL)) snprintf(ps, sizeof ps, "%s", host_sym(pc, NULL));
            fprintf(stderr, "[watch] fa %llx pc %llx (%s) lr %llx (%s+%llx)\n", (unsigned long long)fa,
                    (unsigned long long)pc, ps, (unsigned long long)lr, ls ? ls : "?", (unsigned long long)lo);
            // (debug) identify the mapping the writer pc lives in when neither library covers it
            if (!pfn && !host_sym(pc, NULL)) {
                FILE *m = fopen("/proc/self/maps", "r");
                if (m) for (char l[512]; fgets(l, sizeof l, m);) {
                    uint64_t mlo = 0, mhi = 0;
                    if (sscanf(l, "%lx-%lx ", (unsigned long *)&mlo, (unsigned long *)&mhi) != 2) continue;
                    if (pc >= mlo && pc < mhi) { fprintf(stderr, "[watch] writer map: %s", l); break; }
                }
                if (m) fclose(m);
            }
        }
        static char seen[64][48];
        static int nseen;
        const char *nm = host_sym(pc, NULL);
        if (!nm) nm = "?";
        char key[48];  // (each function + offset once)
        snprintf(key, sizeof key, "%.36s+%#llx", nm, (unsigned long long)fa);
        bool found = false;
        for (int i = 0; i < nseen; i++) found |= !strcmp(seen[i], key);
        if (!found && nseen < 64) {
            snprintf(seen[nseen++], 48, "%s", key);
            fprintf(stderr, "[watch] +%#llx by %s\n", fa - g_watch_lo, nm);
        }
    }
    plat_protect(watch_page, PLAT_PAGE, PLAT_PROT_RW);
    return true;
}

void rt_fault_report(const char *what, uint64_t fa, uint64_t pc, uint64_t lr) {
    rt_crash_fault(what, fa, pc);
    uint64_t po = 0, lo = 0;
    const char *ps = host_sym(pc, &po), *ls = host_sym(lr, &lo);
    fprintf(stderr, "\n*** %s accessing %#llx\n    host pc %llx (%s+%llx)\n    host lr %llx (%s)\n", what,
            (unsigned long long)fa, (unsigned long long)pc, ps ? ps : "?", (unsigned long long)po,
            (unsigned long long)lr, ls ? ls : "?");
    rt_fatal_trace();
}

void crash_handler_install(void) { plat_crash_handler_install(); }

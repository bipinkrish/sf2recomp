#include "../constants/platform.h"
// Linux host layer (runtime/platform.h). x86-64 and aarch64.
#define _GNU_SOURCE
// (SF2 fork: SDL is not a build dependency yet — message boxes are plain stderr lines for now.)
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_messagebox.h>
#include <sys/system_properties.h>
#endif

#include "../paths.h"
#include "../platform.h"
#include "posix_log.h"

void rt_crash_explain(char *out, size_t n, const char *log_path);  // runtime/crash_explain.c

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE SF2C_LINUX_MAP_FIXED_NOREPLACE
#endif

// ---- page write tracking (plat_written_pages), the counterpart of Windows' MEM_WRITE_WATCH
// The window is registered with a userfaultfd in asynchronous write-protect mode: the kernel itself lifts a page's
// protection on its first write (no fault handler thread, no signal) and remembers it was written. PAGEMAP_SCAN then
// lists the written pages of a range and protects them again in one call, like GetWriteWatch(WRITE_WATCH_FLAG_RESET).
// Pages are only protected once a query has covered them, so memory nobody asks about never faults. Linux 6.7+;
// without it (older kernel, userfaultfd unavailable): no tracking, as before. LARECOMP_WRITE_TRACKING=0: off.
#ifndef UFFD_USER_MODE_ONLY
#define UFFD_USER_MODE_ONLY 1
#endif
#ifndef UFFD_FEATURE_WP_UNPOPULATED
#define UFFD_FEATURE_WP_UNPOPULATED (1 << SF2C_LINUX_N_13_3FDB)
#endif
#ifndef UFFD_FEATURE_WP_ASYNC
#define UFFD_FEATURE_WP_ASYNC (1 << SF2C_LINUX_N_15_E629)
#endif
// linux/fs.h's PAGEMAP_SCAN ABI, spelled out (the AppImage builds against Ubuntu 22.04's headers, which predate it)
typedef struct { uint64_t start, end, categories; } WtRegion;
typedef struct {
    uint64_t size, flags, start, end, walk_end, vec, vec_len, max_pages;
    uint64_t category_inverted, category_mask, category_anyof_mask, return_mask;
} WtScan;
#define WT_PAGEMAP_SCAN _IOWR(SF2C_LINUX_CH_F_CE92, SF2C_LINUX_N_16_B17E, WtScan)
#define WT_PAGE_IS_WRITTEN (1 << 1)
#define WT_SCAN_WP_MATCHING (1 << 0)    // protect the pages reported again
#define WT_SCAN_CHECK_WPASYNC (1 << 1)  // fail (instead of reporting nothing) where the range isn't registered

static int g_uffd = -1, g_pagemap = -1;
static uint64_t g_wt_lo, g_wt_hi, g_wt_page;

static void wt_off(const char *why) {
    fprintf(stderr, SF2C_LINUX_S_PLAT_NO_PAGE_WRITE_TRACKING__6B63, why, strerror(errno));
    if (g_pagemap >= 0) close(g_pagemap);
    if (g_uffd >= 0) close(g_uffd);  // (unregisters the window)
    g_pagemap = g_uffd = -1;
}

static bool wt_register(uint64_t lo, uint64_t hi) {
    struct uffdio_register r = {.range = {.start = lo, .len = hi - lo}, .mode = UFFDIO_REGISTER_MODE_WP};
    return ioctl(g_uffd, UFFDIO_REGISTER, &r) == 0;
}

static void wt_init(uint64_t lo, uint64_t hi) {
#ifdef __ANDROID__
    // Android's app sandbox does not permit userfaultfd/pagemap tracking.
    (void)lo; (void)hi;
    return;
#endif
    const char *e = getenv(SF2C_LINUX_S_LARECOMP_WRITE_TRACKING_EF34);
    if (e && !atoi(e)) return;
    // user-mode faults only: allowed without privileges (vm.unprivileged_userfaultfd=0); the asynchronous protection
    // is lifted by the kernel either way, also for its own writes (read() into guest memory)
    g_uffd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    if (g_uffd < 0) { wt_off(SF2C_LINUX_S_USERFAULTFD_6B99); return; }
    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED};
    if (ioctl(g_uffd, UFFDIO_API, &api)) { wt_off(SF2C_LINUX_S_ASYNCHRONOUS_WRITE_PROTECTIO_68D2); return; }
    if (!wt_register(lo, hi)) { wt_off(SF2C_LINUX_S_REGISTER_38DD); return; }
    g_pagemap = open(SF2C_LINUX_S_PROC_SELF_PAGEMAP_28AD, O_RDONLY | O_CLOEXEC);
    if (g_pagemap < 0) { wt_off(SF2C_LINUX_S_PROC_SELF_PAGEMAP_28AD); return; }
    WtRegion v;
    uint64_t pg = (uint64_t)sysconf(_SC_PAGESIZE);
    WtScan s = {.size = sizeof s, .flags = WT_SCAN_CHECK_WPASYNC, .start = lo, .end = lo + pg, .vec = (uintptr_t)&v,
                .vec_len = 1, .category_mask = WT_PAGE_IS_WRITTEN, .return_mask = WT_PAGE_IS_WRITTEN};
    if (ioctl(g_pagemap, WT_PAGEMAP_SCAN, &s) < 0) { wt_off(SF2C_LINUX_S_PAGEMAP_SCAN_NEEDS_LINUX_6_7_C77E); return; }
    g_wt_lo = lo;
    g_wt_hi = hi;
    g_wt_page = pg;
}

// Tracking off after all (the Vulkan backend at boot, before any query protected a page: nothing is protected yet), with
// the reason logged; LARECOMP_WRITE_TRACKING=1 keeps it on. Closing the userfaultfd unregisters the window.
void plat_write_tracking_off(const char *why) {
    if (g_uffd < 0 && g_pagemap < 0) return;
    const char *e = getenv(SF2C_LINUX_S_LARECOMP_WRITE_TRACKING_EF34);
    if (e && atoi(e)) { fprintf(stderr, SF2C_LINUX_S_PLAT_PAGE_WRITE_TRACKING_KEP_71B3, why); return; }
    fprintf(stderr, SF2C_LINUX_S_PLAT_PAGE_WRITE_TRACKING_OFF_D150, why);
    if (g_pagemap >= 0) close(g_pagemap);
    if (g_uffd >= 0) close(g_uffd);
    g_pagemap = g_uffd = -1;
}

// ---- guest address space
// Everything readable/writable from the start, like macOS: the guest maps at 4K granularity, and untouched pages
// cost nothing (MAP_NORESERVE: the 12 GB window doesn't count against overcommit).
bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    void *p = mmap((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return false;
    if (p != (void *)(uintptr_t)lo) { munmap(p, hi - lo); return false; }  // (kernels before 4.17: just a hint)
    wt_init(lo, hi);
    return true;
}

// Fresh zero pages over the whole window (the old ones are released at once). The new mapping is registered again.
bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    if (mmap((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
             -1, 0) != (void *)(uintptr_t)lo)
        return false;
    if (g_uffd >= 0 && !wt_register(lo, hi)) wt_off(SF2C_LINUX_S_REGISTER_AFTER_RESET_6164);
    return true;
}

void plat_commit(uint64_t addr, uint64_t size) { (void)addr; (void)size; }

int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    if (g_pagemap < 0 || addr < g_wt_lo || addr + size > g_wt_hi || max <= 0) return -1;
    WtRegion v[SF2C_LINUX_N_64_A68B];
    uint64_t at = addr, end = addr + size;
    int n = 0;
    while (at < end && n < max) {
        WtScan s = {.size = sizeof s, .flags = WT_SCAN_WP_MATCHING | WT_SCAN_CHECK_WPASYNC, .start = at, .end = end,
                    .vec = (uintptr_t)v, .vec_len = SF2C_LINUX_N_64_A68B, .max_pages = (uint64_t)(max - n),
                    .category_mask = WT_PAGE_IS_WRITTEN, .return_mask = WT_PAGE_IS_WRITTEN};
        int r = ioctl(g_pagemap, WT_PAGEMAP_SCAN, &s);  // (stops early when vec or max_pages fills: walk_end)
        if (r < 0 || s.walk_end <= at) return -1;
        for (int i = 0; i < r; i++)
            for (uint64_t p = v[i].start; p < v[i].end && n < max; p += g_wt_page) pages[n++] = p;
        at = s.walk_end;
    }
    if (at < end) {  // (more than fit: protect the rest too, the caller takes all)
        struct uffdio_writeprotect wp = {.range = {.start = addr, .len = size}, .mode = UFFDIO_WRITEPROTECT_MODE_WP};
        ioctl(g_uffd, UFFDIO_WRITEPROTECT, &wp);
    }
    *page = g_wt_page;
    return n;
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    int p = prot == PLAT_PROT_NONE ? PROT_NONE : prot == PLAT_PROT_READ ? PROT_READ : PROT_READ | PROT_WRITE;
    return mprotect((void *)(uintptr_t)addr, size, p);
}

// ---- time
uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * SF2C_LINUX_N_1000000000U_112A + (uint64_t)ts.tv_nsec;
}

uint64_t plat_thread_cpu_ns(void) {
    struct timespec ts;
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0 ? (uint64_t)ts.tv_sec * SF2C_LINUX_N_1000000000ULL_E860 + (uint64_t)ts.tv_nsec : 0;
}

void plat_sleep_until_ns(uint64_t t) {
    struct timespec ts = {.tv_sec = (time_t)(t / SF2C_LINUX_N_1000000000U_112A), .tv_nsec = (long)(t % SF2C_LINUX_N_1000000000U_112A)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

// Real-time scheduling where the user may have it (rtkit / limits.conf), else just a higher priority: keeps the
// vsync thread on time while the guest threads load every core.
int plat_cpu_count(void) {
    cpu_set_t set;
    return sched_getaffinity(0, sizeof set, &set) == 0 ? CPU_COUNT(&set) : 0;
}

void plat_thread_guest(int prio) { (void)prio; }  // (the OS's default scheduling serves the game here)

void plat_thread_realtime(uint64_t period_ns) {
    (void)period_ns;
    struct sched_param sp = {.sched_priority = 1};
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) return;
    setpriority(PRIO_PROCESS, (id_t)gettid(), -SF2C_LINUX_N_10_4A44);  // (per thread on Linux; fails quietly without CAP_SYS_NICE)
}
int plat_thread_realtime_check(uint64_t period_ns) { (void)period_ns; return -1; }

// ---- process
// Started from a desktop (the AppImage) there is no terminal: stdout / stderr go to lasrecomp.log in the data folder
// (runtime/paths.h; rt_paths_init runs later, so the same rule is applied here), with five backups (posix_log.h).
static char g_log_path[SF2C_LINUX_N_1024_E39E];

static void log_to_file_without_terminal(void) {
    if (isatty(STDERR_FILENO)) return;
    char dir[SF2C_LINUX_N_900_BDC5];
    if (!rt_data_root_early(dir, sizeof dir)) return;  // dev mode: wherever stderr goes already
    for (char *c = dir + 1; *c; c++)
        if (*c == SF2C_LINUX_CH_EMPTY_0825) { *c = 0; mkdir(dir, SF2C_LINUX_N_0755_64A5); *c = SF2C_LINUX_CH_EMPTY_0825; }
    mkdir(dir, SF2C_LINUX_N_0755_64A5);
    session_log_open(dir, g_log_path, sizeof g_log_path);
}

#if defined(__x86_64__) && !defined(__ANDROID__)
// The recompiled game code is built for x86-64-v3 (Makefile GENARCH: AVX2, FMA, BMI2): say so instead of crashing
// with an illegal instruction on an older CPU.
static void require_cpu(void) {
    __builtin_cpu_init();
    if (__builtin_cpu_supports(SF2C_LINUX_S_AVX2_C78A) && __builtin_cpu_supports(SF2C_LINUX_S_FMA_FE81) && __builtin_cpu_supports(SF2C_LINUX_S_BMI2_B8F0)) return;
    fprintf(stderr, SF2C_LINUX_S_FATAL_LAS_RECOMP_NEEDS_A_CPU_9B19
                    SF2C_LINUX_S_NEWER_N_687E);
    exit(1);
}
#else
static void require_cpu(void) {}
#endif

// "key=value" / "key\t: value" lines: the value of the first line starting with key (quotes and spaces trimmed)
static bool kv_line(const char *line, const char *key, char sep, char *out, size_t n) {
    size_t k = strlen(key);
    if (strncmp(line, key, k)) return false;
    const char *p = line + k;
    while (*p == SF2C_LINUX_CH_EMPTY_730D || *p == SF2C_LINUX_CH_T_27AE) p++;
    if (*p++ != sep) return false;
    while (*p == SF2C_LINUX_CH_EMPTY_730D || *p == SF2C_LINUX_CH_T_27AE || *p == SF2C_LINUX_S_EMPTY_3251) p++;
    snprintf(out, n, SF2C_LINUX_S_S_0030, p);
    for (size_t l = strlen(out); l && (out[l - 1] == SF2C_LINUX_CH_N_AD65 || out[l - 1] == SF2C_LINUX_S_EMPTY_3251 || out[l - 1] == SF2C_LINUX_CH_EMPTY_730D); l--) out[l - 1] = 0;
    return true;
}

void plat_system_info(PlatSysInfo *si) {
    // the OS: /etc/os-release (the host's, also inside an AppImage; SteamOS: NAME="SteamOS", VERSION_ID), the kernel
    char pretty[SF2C_LINUX_N_96_7B1A] = SF2C_LINUX_S_EMPTY_12AE, name[SF2C_LINUX_N_64_A68B] = SF2C_LINUX_S_EMPTY_12AE, ver[SF2C_LINUX_N_48_9801] = SF2C_LINUX_S_EMPTY_12AE, line[SF2C_LINUX_N_512_94F8];
    FILE *f = fopen(SF2C_LINUX_S_ETC_OS_RELEASE_0257, SF2C_LINUX_S_R_2659);
    if (!f) f = fopen(SF2C_LINUX_S_USR_LIB_OS_RELEASE_6CBC, SF2C_LINUX_S_R_2659);
    for (; f && fgets(line, sizeof line, f);) {
        if (!pretty[0]) kv_line(line, SF2C_LINUX_S_PRETTY_NAME_9A8C, SF2C_LINUX_CH_EMPTY_C874, pretty, sizeof pretty);
        if (!name[0]) kv_line(line, SF2C_LINUX_S_NAME_AEC4, SF2C_LINUX_CH_EMPTY_C874, name, sizeof name);
        if (!ver[0]) kv_line(line, SF2C_LINUX_S_VERSION_ID_97B5, SF2C_LINUX_CH_EMPTY_C874, ver, sizeof ver);
    }
    if (f) fclose(f);
    char os[SF2C_LINUX_N_160_A512];
    if (pretty[0] && (!ver[0] || strstr(pretty, ver))) snprintf(os, sizeof os, SF2C_LINUX_S_S_0030, pretty);
    else if (pretty[0] || name[0]) snprintf(os, sizeof os, SF2C_LINUX_S_S_S_9FC7, pretty[0] ? pretty : name, ver);
    else snprintf(os, sizeof os, SF2C_LINUX_S_LINUX_1CA1);
    struct utsname u = {0};
    if (uname(&u) == 0) snprintf(si->os, sizeof si->os, SF2C_LINUX_S_80S_S_KERNEL_32S_5123, os, u.machine, u.release);
    else snprintf(si->os, sizeof si->os, SF2C_LINUX_S_80S_E16C, os);
    // the CPU: x86 "model name"; ARM boards often have none - the device tree's model then (e.g. "Raspberry Pi 5")
    // cores: distinct (physical id, core id) pairs; threads: the processors online
    si->threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
    enum { MAXC = SF2C_LINUX_N_1024_E39E };
    static int seen[MAXC];
    int nseen = 0, phys = 0;
    f = fopen(SF2C_LINUX_S_PROC_CPUINFO_DC73, SF2C_LINUX_S_R_2659);
    for (char v[SF2C_LINUX_N_128_2747]; f && fgets(line, sizeof line, f);) {
        if (!si->cpu[0] && kv_line(line, SF2C_LINUX_S_MODEL_NAME_5E38, SF2C_LINUX_CH_EMPTY_4266, si->cpu, sizeof si->cpu)) continue;
        if (kv_line(line, SF2C_LINUX_S_PHYSICAL_ID_17EC, SF2C_LINUX_CH_EMPTY_4266, v, sizeof v)) phys = atoi(v);
        else if (kv_line(line, SF2C_LINUX_S_CORE_ID_8ACD, SF2C_LINUX_CH_EMPTY_4266, v, sizeof v)) {
            int key = phys * SF2C_LINUX_N_4096_8B92 + atoi(v), i = 0;
            while (i < nseen && seen[i] != key) i++;
            if (i == nseen && nseen < MAXC) seen[nseen++] = key;
        }
    }
    if (f) fclose(f);
    si->cores = nseen ? nseen : si->threads;
    if (!si->cpu[0] && (f = fopen(SF2C_LINUX_S_PROC_DEVICE_TREE_MODEL_EE84, SF2C_LINUX_S_R_2659))) {
        size_t n = fread(si->cpu, 1, sizeof si->cpu - 1, f);
        si->cpu[n] = 0;  // (NUL-terminated in the file already)
        fclose(f);
    }
    struct sysinfo s;
    if (sysinfo(&s) == 0) si->ram = (uint64_t)s.totalram * s.mem_unit;
#ifdef __ANDROID__
    char release[PROP_VALUE_MAX] = SF2C_LINUX_S_EMPTY_12AE;
    __system_property_get(SF2C_LINUX_S_RO_BUILD_VERSION_RELEASE_A053, release);
    snprintf(si->os, sizeof si->os, SF2C_LINUX_S_ANDROID_S_S_1423, release, u.machine);
    si->package = SF2C_LINUX_S_APK_A9E7;
#else
    si->package = getenv(SF2C_LINUX_S_APPIMAGE_0164) ? SF2C_LINUX_S_APPIMAGE_0BBB : NULL;  // (set by the AppImage runtime; its value is a path: not logged)
#endif
}

void plat_init(void) {
    log_to_file_without_terminal();
    raise_open_file_limit();
    require_cpu();
}

// (inside an AppImage: the binary in its mount, which stays mounted while this process lives - re-exec included)
bool plat_exe_path(char *buf, size_t size) {
#ifdef __ANDROID__
    // Android loads libmain.so from the APK. Resource/config users need our
    // writable resource directory, rather than /proc/self/exe (app_process).
    const char *dir = SDL_GetAndroidExternalStoragePath();
    return dir && snprintf(buf, size, SF2C_LINUX_S_S_SF2_87AF, dir) < (int)size;
#else
    ssize_t n = readlink(SF2C_LINUX_S_PROC_SELF_EXE_3422, buf, size - 1);
    if (n <= 0) return false;
    buf[n] = 0;
    return true;
#endif
}

static int probe_fd = -1;
static pthread_once_t probe_once = PTHREAD_ONCE_INIT;
static void probe_open(void) { probe_fd = open(SF2C_LINUX_S_PROC_SELF_MEM_0605, O_RDONLY | O_CLOEXEC); }
bool plat_probe_read(uint64_t addr, void *out, size_t size) {
    pthread_once(&probe_once, probe_open);
    return probe_fd >= 0 && pread(probe_fd, out, size, (off_t)addr) == (ssize_t)size;
}

int plat_host_thread_id(void) { return (int)syscall(SYS_gettid); }
bool plat_host_thread_alive(int tid) { return syscall(SYS_tgkill, getpid(), tid, 0) == 0; }

// Our own command line, from /proc/self/cmdline (NUL-separated).
static char **self_argv(void) {
    static char buf[SF2C_LINUX_N_32768_D196];
    static char *argv[SF2C_LINUX_N_256_51E8];
    int fd = open(SF2C_LINUX_S_PROC_SELF_CMDLINE_23D1, O_RDONLY);
    if (fd < 0) return NULL;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = 0;
    int k = 0;
    for (char *p = buf; p < buf + n && k < SF2C_LINUX_N_255_9556; p += strlen(p) + 1) argv[k++] = p;
    argv[k] = NULL;
    return argv;
}

void plat_exec(char *const argv[]) {
#ifdef __ANDROID__
    // An Activity cannot re-exec app_process. Restart through the launcher.
    (void)argv;
    errno = ENOTSUP;
#else
    if (argv) { execv(argv[0], argv); return; }
    char path[SF2C_LINUX_N_4096_8B92];
    char **av = self_argv();
    if (av && plat_exe_path(path, sizeof path)) execv(path, av);
#endif
}

// ---- diagnostics
// Exported symbols only (the Makefile links with -rdynamic); frames in the generated code are left to the caller,
// which maps them to guest functions (runtime/dispatch.c). Static runtime functions come out as the nearest guest
// function, so backtrace lines also give the offset in the executable (addr2line -fe lasrecomp <offset>).
const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_sname) return NULL;
    if (offset) *offset = pc - (uint64_t)(uintptr_t)di.dli_saddr;
    return di.dli_sname;
}

// Fatal error without a terminal: say so in the log rather than vanishing (the details are in the log).
// (SF2 fork: no SDL yet, and LAS's crash_explain.c is LA-specific — a plain line until a UI exists.)
void plat_fatal_notice(void) {
    fprintf(stderr, SF2C_LINUX_S_FATAL_A_FATAL_ERROR_ENDED_TH_9FB7);
#ifdef __ANDROID__
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SF2C_LINUX_S_SF2_RECOMP_AA83,
        SF2C_LINUX_S_THE_GAME_STOPPED_DETAILS_ARE_7D74, NULL);
#endif
}

void plat_notice(const char *text) {
    fprintf(stderr, SF2C_LINUX_S_NOTICE_S_N_83D0, text);
}

bool plat_module_name(uint64_t pc, char *out, size_t n) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_fname) return false;
    snprintf(out, n, SF2C_LINUX_S_S_0030, di.dli_fname);
    return true;
}

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);  // runtime/dispatch.c

// One frame, symbolized exactly like plat_print_backtrace prints them.
static void print_bt_pc(void *p) {
    uint64_t pc = (uint64_t)(uintptr_t)p, off = 0;
    Dl_info di = {0};
    if (!dladdr(p, &di)) di.dli_fname = NULL;
    Dl_info self = {0};
    dladdr((void *)(uintptr_t)print_bt_pc, &self);
    bool ours = di.dli_fbase && di.dli_fbase == self.dli_fbase;
    const char *s = plat_symbol(pc, &off);
    if (!s && ours) s = rt_host_fn_name(pc, &off);
    const char *mod = di.dli_fname ? (strrchr(di.dli_fname, SF2C_LINUX_CH_EMPTY_0825) ? strrchr(di.dli_fname, SF2C_LINUX_CH_EMPTY_0825) + 1 : di.dli_fname) : SF2C_LINUX_S_EMPTY_15C5;
    uint64_t moff = pc - (uint64_t)(uintptr_t)di.dli_fbase;
    if (s && ours) fprintf(stderr, SF2C_LINUX_S_016LLX_S_LLX_LASRECOMP_LLX_N_96BB, (unsigned long long)pc, s,
                           (unsigned long long)off, (unsigned long long)moff);
    else if (s) fprintf(stderr, SF2C_LINUX_S_016LLX_S_LLX_S_N_2140, (unsigned long long)pc, s, (unsigned long long)off, mod);
    else fprintf(stderr, SF2C_LINUX_S_016LLX_S_LLX_N_A52C, (unsigned long long)pc, mod, (unsigned long long)moff);
}

void plat_print_backtrace(void) {
    void *bt[SF2C_LINUX_N_64_A68B];
    int n = backtrace(bt, SF2C_LINUX_N_64_A68B);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, SF2C_LINUX_S_2D_7455, i);
        print_bt_pc(bt[i]);
    }
}

// ---- another thread's backtrace (runtime/nvn/rqueue.c's hang dump): SIGUSR2, capture, handshake
static volatile int bt_sig_state;  // 0 idle, 1 asked, 2 done (plain int: signal handler + poller handshake)
static void *bt_capture[SF2C_LINUX_N_64_A68B];
static int bt_capture_n;

static void bt_sig_handler(int sig) {
    (void)sig;
    bt_capture_n = backtrace(bt_capture, SF2C_LINUX_N_64_A68B);
    bt_sig_state = SF2C_LINUX_N_2_D473;
}

static void do_install(void) {
    struct sigaction sa = {0};
    sa.sa_handler = bt_sig_handler;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGUSR2, &sa, NULL);
}

bool plat_bt_other_thread_tid(int host_tid, const char *prefix) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, do_install);
    bt_sig_state = 1;
    long rc = syscall(SYS_tgkill, getpid(), host_tid, SIGUSR2);
    if (rc != 0) {
        fprintf(stderr, SF2C_LINUX_S_S_CAPTURE_FAILED_TGKILL_TID__DF1E, prefix, host_tid, errno, strerror(errno));
        return false;
    }
    for (int i = 0; i < SF2C_LINUX_N_100_AD57 && bt_sig_state != SF2C_LINUX_N_2_D473; i++)
        usleep(SF2C_LINUX_N_1000_4051);  // (1 s max: the thread may be between signals / in a syscall a while)
    if (bt_sig_state != SF2C_LINUX_N_2_D473) {
        bt_sig_state = 0;
        fprintf(stderr, SF2C_LINUX_S_S_CAPTURE_FAILED_TARGET_THRE_2382, prefix);
        return false;
    }
    fprintf(stderr, SF2C_LINUX_S_S_HOST_STACK_D_FRAMES_N_82DA, prefix, bt_capture_n);
    for (int i = 0; i < bt_capture_n; i++) {
        fprintf(stderr, SF2C_LINUX_S_2D_7455, i);
        print_bt_pc(bt_capture[i]);
    }
    bt_sig_state = 0;
    return true;
}

static void crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx;
    static volatile int depth;
    if (sig == SIGABRT && depth++ > 0) { signal(SIGABRT, SIG_DFL); abort(); }  // (nested: just die)
    uint64_t fa = (uint64_t)(uintptr_t)si->si_addr;
#if defined(__aarch64__)
    uint64_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[SF2C_LINUX_N_30_624B];
#else
    // x86-64 has no link register: report the return address on top of the stack (valid at function entry)
    uint64_t pc = (uint64_t)uc->uc_mcontext.gregs[REG_RIP], sp = (uint64_t)uc->uc_mcontext.gregs[REG_RSP], lr = 0;
    if (sig != SIGSEGV || (fa & ~SF2C_LINUX_N_0XFFFULL_6F3A) != (sp & ~SF2C_LINUX_N_0XFFFULL_6F3A)) lr = *(uint64_t *)(uintptr_t)sp;
#endif
    if (rt_fault_watch(fa, pc)) return;
    char what[SF2C_LINUX_N_32_E29C];
    snprintf(what, sizeof what, SF2C_LINUX_S_SIGNAL_D_D3D6, sig);
    rt_fault_report(what, fa, pc, lr);
}

void plat_crash_handler_install(void) {
    stack_t ss = {.ss_sp = malloc(1 << SF2C_LINUX_N_20_F5CA), .ss_size = 1 << SF2C_LINUX_N_20_F5CA};
    sigaltstack(&ss, NULL);
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// ---- child processes
#include <ftw.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;

PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log_path ? log_path : SF2C_LINUX_S_DEV_NULL_9945,
                                     O_WRONLY | O_CREAT | (append_log ? O_APPEND : O_TRUNC), SF2C_LINUX_N_0644_3ED6);
    posix_spawn_file_actions_adddup2(&fa, 1, SF2C_LINUX_N_2_D473);
    posix_spawnattr_t at;  // (its own process group: plat_proc_kill ends what it starts, e.g. a compiler's own child)
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&at, 0);
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &fa, &at, (char *const *)argv, environ);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    return rc ? 0 : (PlatProc)pid;
}

void plat_proc_kill(PlatProc p) {
    kill(-(pid_t)p, SIGKILL);
    waitpid((pid_t)p, NULL, 0);
}

int plat_proc_wait(PlatProc p, bool block) {
    int st = 0;
    pid_t r = waitpid((pid_t)p, &st, block ? 0 : WNOHANG);
    if (r == 0) return -1;
    if (r < 0) return SF2C_LINUX_N_255_9556;
    return WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? PLAT_PROC_SIGNALED + WTERMSIG(st) : SF2C_LINUX_N_255_9556;
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb, (void)flag, (void)ftw;
    return remove(path) == 0 || errno == ENOENT ? 0 : -1;  // (gone already: e.g. a ._ file macOS removed with its folder on exFAT)
}
bool plat_remove_tree(const char *dir) { return nftw(dir, rm_entry, SF2C_LINUX_N_16_B17E, FTW_DEPTH | FTW_PHYS) == 0; }
bool plat_onedrive_synced(const char *dir) { (void)dir; return false; }

// End the process at once, skipping the library teardown: as on Windows, the GPU driver's threads crashed in exit()
// while the game's threads still submitted work.
void plat_terminate(void) { _exit(0); }

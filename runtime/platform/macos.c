#include "../constants/platform.h"
// macOS host layer (runtime/platform.h).
#include <CoreFoundation/CoreFoundation.h>
#include <crt_externs.h>
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "../paths.h"
#include "../platform.h"
#include "posix_log.h"

void rt_crash_explain(char *out, size_t n, const char *log_path);  // runtime/crash_explain.c

static mach_timebase_info_data_t tb;

static char g_log_path[SF2C_MACOS_N_1024_E39E];

static void open_game_log(void) {
    char exe[PATH_MAX];
    uint32_t sz = sizeof exe;
    if (!getenv(SF2C_MACOS_S_SF2_GAME_LOG_74EB) &&
        (isatty(STDERR_FILENO) || _NSGetExecutablePath(exe, &sz) || !strstr(exe, SF2C_MACOS_S_APP_CONTENTS_MACOS_D431)))
        return;
    char dir[SF2C_MACOS_N_900_BDC5];
    if (!rt_data_root_early(dir, sizeof dir)) return;
    mkdir(dir, SF2C_MACOS_N_0755_64A5);
    session_log_open(dir, g_log_path, sizeof g_log_path);
}

static void sysctl_str(const char *name, char *out, size_t n) {
    size_t len = n;
    if (sysctlbyname(name, out, &len, NULL, 0) != 0) out[0] = 0;
    else out[n - 1] = 0;
}
static int64_t sysctl_int(const char *name) {
    int64_t v = 0;  // (hw.memsize is 64-bit, the counts 32-bit: little-endian, so either fits)
    size_t len = sizeof v;
    return sysctlbyname(name, &v, &len, NULL, 0) == 0 ? v : 0;
}

void plat_system_info(PlatSysInfo *si) {
    char ver[SF2C_MACOS_N_32_E29C], machine[SF2C_MACOS_N_32_E29C] = SF2C_MACOS_S_EMPTY_12AE;
    sysctl_str(SF2C_MACOS_S_KERN_OSPRODUCTVERSION_B89F, ver, sizeof ver);
    struct utsname u;
    if (uname(&u) == 0) snprintf(machine, sizeof machine, SF2C_MACOS_S_S_0030, u.machine);
    bool rosetta = sysctl_int(SF2C_MACOS_S_SYSCTL_PROC_TRANSLATED_F518) == 1;
    snprintf(si->os, sizeof si->os, SF2C_MACOS_S_MACOS_S_S_S_211F, ver[0] ? ver : SF2C_MACOS_S_EMPTY_15C5, machine, rosetta ? SF2C_MACOS_S_ROSETTA_D126 : SF2C_MACOS_S_EMPTY_12AE);
    sysctl_str(SF2C_MACOS_S_MACHDEP_CPU_BRAND_STRING_C3D5, si->cpu, sizeof si->cpu);
    si->cores = (int)sysctl_int(SF2C_MACOS_S_HW_PHYSICALCPU_5E6C);
    si->threads = (int)sysctl_int(SF2C_MACOS_S_HW_LOGICALCPU_3DBA);
    int64_t p = sysctl_int(SF2C_MACOS_S_HW_PERFLEVEL0_PHYSICALCPU_6EA3), e = sysctl_int(SF2C_MACOS_S_HW_PERFLEVEL1_PHYSICALCPU_9B55);
    if (p && e) snprintf(si->cores_detail, sizeof si->cores_detail, SF2C_MACOS_S_DP_DE_6E1A, (int)p, (int)e);
    si->ram = (uint64_t)sysctl_int(SF2C_MACOS_S_HW_MEMSIZE_7E98);
    char exe[SF2C_MACOS_N_1024_E39E];
    uint32_t sz = sizeof exe;
    si->package = _NSGetExecutablePath(exe, &sz) == 0 && strstr(exe, SF2C_MACOS_S_APP_CONTENTS_MACOS_D431) ? SF2C_MACOS_S_APP_BE64 : NULL;
}

void plat_init(void) {
    // A converted portable folder carries its own MoltenVK ICD. Preserve an
    // explicit developer/SDK override, otherwise resolve beside this executable.
    if (!getenv(SF2C_MACOS_S_VK_DRIVER_FILES_52BA) && !getenv(SF2C_MACOS_S_VK_ICD_FILENAMES_338A)) {
        char icd[SF2C_MACOS_N_4096_8B92];
        if (plat_exe_path(icd, sizeof icd)) {
            char *slash = strrchr(icd, SF2C_MACOS_CH_EMPTY_0825);
            if (slash && (size_t)(slash - icd) + sizeof SF2C_MACOS_S_MOLTENVK_ICD_JSON_20AD <= sizeof icd) {
                strcpy(slash, SF2C_MACOS_S_MOLTENVK_ICD_JSON_20AD);
                if (access(icd, R_OK) == 0) setenv(SF2C_MACOS_S_VK_DRIVER_FILES_52BA, icd, 0);
            }
        }
    }
    mach_timebase_info(&tb);
    open_game_log();
    raise_open_file_limit();
}

bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    // Everything readable/writable: macOS pages are 16K but the guest maps at 4K granularity.
    // Untouched pages cost nothing.
    mach_vm_address_t a = lo;
    return mach_vm_allocate(mach_task_self(), &a, hi - lo, VM_FLAGS_FIXED) == KERN_SUCCESS;
}

// Fresh zero pages over the whole window (the old ones are released at once).
bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    mach_vm_address_t a = lo;
    return mach_vm_allocate(mach_task_self(), &a, hi - lo, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE) == KERN_SUCCESS;
}

void plat_commit(uint64_t addr, uint64_t size) { (void)addr; (void)size; }

int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {  // (no write tracking)
    (void)addr; (void)size; (void)pages; (void)max; (void)page;
    return -1;
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    int p = prot == PLAT_PROT_NONE ? PROT_NONE : prot == PLAT_PROT_READ ? PROT_READ : PROT_READ | PROT_WRITE;
    return mprotect((void *)(uintptr_t)addr, size, p);
}

uint64_t plat_now_ns(void) {
    if (!tb.denom) mach_timebase_info(&tb);
    return (uint64_t)((__uint128_t)mach_absolute_time() * tb.numer / tb.denom);
}

uint64_t plat_thread_cpu_ns(void) {
    struct timespec ts;
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0 ? (uint64_t)ts.tv_sec * SF2C_MACOS_N_1000000000ULL_E860 + (uint64_t)ts.tv_nsec : 0;
}

void plat_sleep_until_ns(uint64_t t) {
    if (!tb.denom) mach_timebase_info(&tb);
    uint64_t deadline = (uint64_t)(((__uint128_t)t * tb.denom + tb.numer - 1) / tb.numer);
    while (mach_wait_until(deadline) == KERN_ABORTED) {}
}

// Real-time (time-constraint) thread: exempt from the timer coalescing macOS applies to
// ordinary threads when the app isn't frontmost, which otherwise stretches 16ms waits to ~100ms.
void plat_thread_realtime(uint64_t period_ns) {
    if (!tb.denom) mach_timebase_info(&tb);
    uint64_t period = period_ns * tb.denom / tb.numer;
    thread_time_constraint_policy_data_t pol = {
        .period = (uint32_t)period,
        .computation = (uint32_t)(period / SF2C_MACOS_N_20_F5CA),
        .constraint = (uint32_t)(period / SF2C_MACOS_N_4_4B22),
        .preemptible = 1,
    };
    kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol,
                                         THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (kr != KERN_SUCCESS) fprintf(stderr, SF2C_MACOS_S_PLAT_REAL_TIME_THREAD_POLICY_6CB7, (int)kr);
}
// After a late wait: is the time-constraint policy still in effect for this thread? The kernel drops it (back to
// timeshare, timers coalesced to ~100 ms when the app isn't frontmost) for a thread that overruns its computation
// budget; set again when found gone. Called only on the late path: no cost otherwise.
int plat_thread_realtime_check(uint64_t period_ns) {
    thread_time_constraint_policy_data_t pol;
    mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    boolean_t def = FALSE;
    kern_return_t kr = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol, &count, &def);
    if (kr != KERN_SUCCESS) return -1;
    if (!def) return 0;
    plat_thread_realtime(period_ns);
    return 1;
}

// Guest threads start at the default QoS, and on a Mac with efficiency cores (an M1 Air: 4 + 4) the scheduler may run
// the game's frame-critical threads there - the main thread (priority 44) builds and submits every frame. Priorities
// 44 and more urgent get user-interactive (performance cores first), the rest user-initiated. SF2RECOMP_THREAD_QOS=0:
// the default class as before.
int plat_cpu_count(void) { return 0; }  // (no affinity masks here: the caller uses the machine's count)

void plat_thread_guest(int prio) {
    static int on = -1;
    if (on < 0) on = !getenv(SF2C_MACOS_S_SF2RECOMP_THREAD_QOS_8C64) || atoi(getenv(SF2C_MACOS_S_SF2RECOMP_THREAD_QOS_8C64)) != 0;
    if (on) pthread_set_qos_class_self_np(prio <= SF2C_MACOS_N_44_71EE ? QOS_CLASS_USER_INTERACTIVE : QOS_CLASS_USER_INITIATED, 0);
}

static bool actual_executable(char *resolved) {
    char executable[PATH_MAX];
    uint32_t len = sizeof executable;
    return !_NSGetExecutablePath(executable, &len) && realpath(executable, resolved);
}

bool plat_exe_path(char *buf, size_t size) {
    char resolved[PATH_MAX];
    if (!actual_executable(resolved)) return false;
    // Finder launches the Mach-O inside the app. Game assets and settings
    // belong to the portable folder beside the app and the CLI executable.
    static const char suffix[] = SF2C_MACOS_S_APP_CONTENTS_MACOS_SF2_1C4A;
    size_t length = strlen(resolved);
    if (length >= sizeof suffix - 1 && !strcmp(resolved + length - (sizeof suffix - 1), suffix)) {
        char *slash = resolved + length - (sizeof suffix - 1);
        while (slash > resolved && *slash != SF2C_MACOS_CH_EMPTY_0825) slash--;
        strcpy(slash, SF2C_MACOS_S_SF2_ED26);
    }
    return snprintf(buf, size, SF2C_MACOS_S_S_0030, resolved) < (int)size;
}

void plat_exec(char *const argv[]) {
    if (argv) { execv(argv[0], argv); return; }
    char path[PATH_MAX];
    if (actual_executable(path)) execv(path, *_NSGetArgv());
}

const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_sname) return NULL;
    if (offset) *offset = pc - (uint64_t)(uintptr_t)di.dli_saddr;
    return di.dli_sname;
}

bool plat_module_name(uint64_t pc, char *out, size_t n) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_fname) return false;
    snprintf(out, n, SF2C_MACOS_S_S_0030, di.dli_fname);
    return true;
}

// Fatal error: what happened in plain words (runtime/crash_explain.c) - an alert when running as the app (the
// terminal shows the log otherwise). CFUserNotification: safe from any thread, no run loop needed.
void plat_fatal_notice(void) {
    char text[SF2C_MACOS_N_1600_B458];
    rt_crash_explain(text, sizeof text, g_log_path);
    if (!g_log_path[0]) return;
    fflush(stderr);
    CFStringRef msg = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL, CFSTR(SF2C_MACOS_S_SHADOW_FIGHT_2_SF2RECOMP_891C), msg,
                                   CFSTR(SF2C_MACOS_S_OK_98C4), NULL, NULL, NULL);
    if (msg) CFRelease(msg);
}

static void *notice_thread(void *text) {
    CFStringRef msg = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    CFUserNotificationDisplayAlert(0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, CFSTR(SF2C_MACOS_S_SHADOW_FIGHT_2_SF2RECOMP_891C), msg,
                                   CFSTR(SF2C_MACOS_S_OK_98C4), NULL, NULL, NULL);
    if (msg) CFRelease(msg);
    free(text);
    return NULL;
}
void plat_notice(const char *text) {
    if (!g_log_path[0]) return;
    pthread_t t;
    char *copy = strdup(text);
    if (copy && !pthread_create(&t, NULL, notice_thread, copy)) pthread_detach(t);
    else free(copy);
}

void plat_print_backtrace(void) {
    void *bt[SF2C_MACOS_N_64_A68B];
    int n = backtrace(bt, SF2C_MACOS_N_64_A68B);
    char **syms = backtrace_symbols(bt, n);
    if (!syms) return;
    for (int i = 0; i < n; i++) fprintf(stderr, SF2C_MACOS_S_S_N_56B7, syms[i]);
    free(syms);
}

static void crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx;
    static volatile int depth;
    if (sig == SIGABRT && depth++ > 0) { signal(SIGABRT, SIG_DFL); abort(); }  // (nested: just die - the report
                                                                               // itself aborting looped, 5.7 GB log)
    uint64_t fa = (uint64_t)si->si_addr;
    #if defined(__aarch64__)
    uint64_t pc = uc->uc_mcontext->__ss.__pc, lr = uc->uc_mcontext->__ss.__lr;
#else
    uint64_t pc = uc->uc_mcontext->__ss.__rip, lr = 0;
#endif
    { extern uint64_t g_fault_lr; g_fault_lr = lr; }
    if (rt_fault_watch(fa, pc)) return;
    char what[SF2C_MACOS_N_32_E29C];
    snprintf(what, sizeof what, SF2C_MACOS_S_SIGNAL_D_D3D6, sig);
    rt_fault_report(what, fa, pc, lr);
}

void plat_crash_handler_install(void) {
    stack_t ss = {.ss_sp = malloc(1 << SF2C_MACOS_N_20_F5CA), .ss_size = 1 << SF2C_MACOS_N_20_F5CA};
    sigaltstack(&ss, NULL);
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// ---- child processes
#include <fcntl.h>
#include <ftw.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;

PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log_path ? log_path : SF2C_MACOS_S_DEV_NULL_9945,
                                     O_WRONLY | O_CREAT | (append_log ? O_APPEND : O_TRUNC), SF2C_MACOS_N_0644_3ED6);
    posix_spawn_file_actions_adddup2(&fa, 1, SF2C_MACOS_N_2_D473);
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
    if (r < 0) return SF2C_MACOS_N_255_9556;
    return WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? PLAT_PROC_SIGNALED + WTERMSIG(st) : SF2C_MACOS_N_255_9556;
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb, (void)flag, (void)ftw;
    return remove(path) == 0 || errno == ENOENT ? 0 : -1;  // (gone already: e.g. a ._ file macOS removed with its folder on exFAT)
}
bool plat_remove_tree(const char *dir) { return nftw(dir, rm_entry, SF2C_MACOS_N_16_B17E, FTW_DEPTH | FTW_PHYS) == 0; }
bool plat_onedrive_synced(const char *dir) { (void)dir; return false; }

// Mach reads fail safely for unmapped addresses, including diagnostic probes.
bool plat_probe_read(uint64_t addr, void *out, size_t size) {
    mach_vm_size_t read = 0;
    return mach_vm_read_overwrite(mach_task_self(), addr, size, (mach_vm_address_t)out, &read) == KERN_SUCCESS
        && read == size;
}

// A borrowed send right held by pthread, valid until that thread exits.
int plat_host_thread_id(void) { return (int)pthread_mach_thread_np(pthread_self()); }
bool plat_host_thread_alive(int tid) {
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    return thread_info((thread_t)tid, THREAD_BASIC_INFO, (thread_info_t)&info, &count) == KERN_SUCCESS;
}

// Suspending another thread can deadlock the allocator/symbolizer. Report that
// cross-thread capture is unavailable; the normal crash stack is still printed.
bool plat_bt_other_thread_tid(int tid, const char *prefix) { (void)tid; (void)prefix; return false; }

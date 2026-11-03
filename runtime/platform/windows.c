#include "../constants/platform.h"
// Windows host layer, adapted from LAS Recomp (MIT; licenses/LASRecomp.txt).
// Upstream blobs and local adaptations are recorded in docs/windows.md.
#include <windows.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>
#include <timeapi.h>
#include <io.h>
#include <wchar.h>

#include "../platform.h"
#include "windows_cmdline.h"
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);


// ---- guest address space
// The window is reserved up front and committed per region (plat_commit from the loader / kernel). Anything
// the guest touches outside a committed region is committed on its first access by the exception handler
// below, so the window behaves like macOS's all-RW reservation. The OS itself (ReadFile into a guest buffer)
// does not fault: such buffers must be inside committed regions.
static uint64_t g_lo, g_hi;
#define COMMIT_CHUNK SF2C_WINDOWS_COMMIT_CHUNK
static bool lazy_commit(uint64_t a) {
    if (a < g_lo || a >= g_hi) return false;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) return false;
    // Two threads touching the same uncommitted chunk: the second faulted before the first committed it and now finds
    // it committed - retry the access. (A page protected on purpose, plat_protect / LARECOMP_WATCH, isn't plain
    // read-write and still goes on to the fault watch.) Fix from letsgo-recomp.
    if (mi.State == MEM_COMMIT) return (mi.Protect & SF2C_WINDOWS_N_0XFF_7312) == PAGE_READWRITE && !(mi.Protect & PAGE_GUARD);
    if (mi.State != MEM_RESERVE) return false;
    uint64_t base = a & ~(COMMIT_CHUNK - 1);
    return VirtualAlloc((void *)(uintptr_t)base, COMMIT_CHUNK, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

static bool g_report_crashes;

static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < SF2C_WINDOWS_N_2_D473) return EXCEPTION_CONTINUE_SEARCH;
    uint64_t fa = er->ExceptionInformation[1];
    // (not for an execute fault: on a read-write page, retrying it would fault again forever)
    if (er->ExceptionInformation[0] != EXCEPTION_EXECUTE_FAULT && lazy_commit(fa)) return EXCEPTION_CONTINUE_EXECUTION;
    if (g_report_crashes && rt_fault_watch(fa, ep->ContextRecord->Rip)) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

// The window keeps write tracking (MEM_WRITE_WATCH: the Vulkan renderer's vertex / index cache re-copies only pages the
// CPU wrote, runtime/nvn/vulkan/exec.c). Without it (refused): no tracking, the renderer reads guest memory in place.
static bool g_write_watch;

// (the reservation failed: main.c starts the process again) what holds part of the range, for the new process's log
// (main.c, LARECOMP_REEXEC_WHY: this one can't log yet). Windows places heaps, stacks and DLLs' allocations at random
// addresses in the lower terabyte (high-entropy ASLR), so now and then one lands in the guest's 12.5 GB.
static void note_guest_conflict(uint64_t lo, uint64_t hi) {
    char why[SF2C_WINDOWS_N_200_27BA];
    snprintf(why, sizeof why, SF2C_WINDOWS_S_VIRTUALALLOC_ERROR_LU_NOTHIN_69C7, GetLastError());
    MEMORY_BASIC_INFORMATION mi;
    for (uint64_t a = lo; a < hi && VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi); ) {
        uint64_t base = (uint64_t)(uintptr_t)mi.BaseAddress, end = base + mi.RegionSize;
        if (mi.State != MEM_FREE) {
            char mod[MAX_PATH] = SF2C_WINDOWS_S_EMPTY_12AE;
            if (mi.Type == MEM_IMAGE && !GetModuleFileNameA((HMODULE)mi.AllocationBase, mod, sizeof mod)) mod[0] = 0;
            const char *name = strrchr(mod, SF2C_WINDOWS_CH_EMPTY_6672);
            snprintf(why, sizeof why, SF2C_WINDOWS_S_S_S_AT_LLX_LLX_S_S_25E0,
                     mi.State == MEM_COMMIT ? SF2C_WINDOWS_S_COMMITTED_A749 : SF2C_WINDOWS_S_RESERVED_D651,
                     mi.Type == MEM_IMAGE ? SF2C_WINDOWS_S_MODULE_51B0 : mi.Type == MEM_MAPPED ? SF2C_WINDOWS_S_MAPPED_FILE_FAD8 : SF2C_WINDOWS_S_PRIVATE_MEMORY_68EB,
                     (unsigned long long)base, (unsigned long long)end, mod[0] ? SF2C_WINDOWS_S_EMPTY_5210 : SF2C_WINDOWS_S_EMPTY_12AE, name ? name + 1 : mod);
            break;
        }
        if (end <= a) break;
        a = end;
    }
    _putenv_s(SF2C_WINDOWS_S_LARECOMP_REEXEC_WHY_6D9D, why);
}

bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    SYSTEM_INFO sys;
    GetSystemInfo(&sys);
    if (hi <= lo || lo % sys.dwAllocationGranularity || hi % sys.dwPageSize) return false;
    const char *watch = getenv(SF2C_WINDOWS_S_LARECOMP_WRITE_TRACKING_EF34);
    bool tracking = !watch || atoi(watch) != 0;
    void *p = tracking ? VirtualAlloc((void *)(uintptr_t)lo, hi - lo, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE) : NULL;
    if (p == (void *)(uintptr_t)lo) g_write_watch = true;
    else p = VirtualAlloc((void *)(uintptr_t)lo, hi - lo, MEM_RESERVE, PAGE_READWRITE);
    if (p != (void *)(uintptr_t)lo) {
        note_guest_conflict(lo, hi);
        return false;
    }
    g_lo = lo;
    g_hi = hi;
    if (!AddVectoredExceptionHandler(1, vectored_handler)) {
        VirtualFree(p, 0, MEM_RELEASE);
        g_lo = g_hi = 0;
        return false;
    }
    return true;
}

int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    if (!g_write_watch || addr < g_lo || addr >= g_hi || size > g_hi - addr || max <= 0) return -1;
    ULONG_PTR n = (ULONG_PTR)max;
    DWORD gran = SF2C_WINDOWS_N_4096_8B92;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, (void *)(uintptr_t)addr, (SIZE_T)size, (PVOID *)pages, &n, &gran) != 0) return -1;
    *page = gran;
    // At capacity the caller conservatively treats the whole range as dirty.
    // Leave unreported pages dirty; a separate reset could lose concurrent writes.
    return (int)n;
}

// Decommit everything: the range stays reserved, and pages come back zeroed when committed again (lazily).
bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    return lo == g_lo && hi == g_hi && VirtualFree((void *)(uintptr_t)lo, hi - lo, MEM_DECOMMIT) != 0;
}

void plat_commit(uint64_t addr, uint64_t size) {
    if (!size) return;
    if (addr < g_lo || addr >= g_hi || size > g_hi - addr) {
        fprintf(stderr, SF2C_WINDOWS_S_PLAT_INVALID_GUEST_COMMIT_RA_1C49);
        ExitProcess(1);
    }
    uint64_t a = addr & ~SF2C_WINDOWS_N_0XFFFULL_6F3A, e = (addr + size + SF2C_WINDOWS_N_0XFFF_1DF4) & ~SF2C_WINDOWS_N_0XFFFULL_6F3A;
    if (!VirtualAlloc((void *)(uintptr_t)a, e - a, MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, SF2C_WINDOWS_S_PLAT_COMMIT_LLX_LLX_FAILED_L_01CA, (unsigned long long)addr,
                (unsigned long long)size, GetLastError());
        ExitProcess(1);
    }
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    DWORD p = prot == PLAT_PROT_NONE ? PAGE_NOACCESS : prot == PLAT_PROT_READ ? PAGE_READONLY : PAGE_READWRITE, old;
    if (addr >= g_lo && addr < g_hi) plat_commit(addr, size);
    return VirtualProtect((void *)(uintptr_t)addr, size, p, &old) ? 0 : -1;
}

// ---- time
static LARGE_INTEGER qpf;

uint64_t plat_now_ns(void) {
    if (!qpf.QuadPart) QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)((unsigned __int128)t.QuadPart * SF2C_WINDOWS_N_1000000000U_112A / (uint64_t)qpf.QuadPart);
}

// The calling thread's CPU time. GetThreadTimes only advances in whole scheduler ticks (15.6 ms): per frame that reads
// 0 or 15.6. QueryThreadCycleTime counts the thread's cycles exactly, in TSC ticks on x64 (invariant TSC), so
// those are converted with the TSC rate measured once against the performance counter (20 ms, at the first call).
// Elsewhere (or if the cycle count fails): GetThreadTimes, kernel + user.
#if defined(__x86_64__) || defined(_M_X64)
#include <intrin.h>
static double tsc_per_ns;
static INIT_ONCE tsc_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK tsc_calibrate(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o, (void)p, (void)c;
    uint64_t q0 = plat_now_ns(), t0 = __rdtsc();
    Sleep(SF2C_WINDOWS_N_20_F5CA);
    uint64_t q1 = plat_now_ns(), t1 = __rdtsc();
    if (q1 > q0 && t1 > t0) tsc_per_ns = (double)(t1 - t0) / (double)(q1 - q0);
    return TRUE;
}
#endif
uint64_t plat_thread_cpu_ns(void) {
#if defined(__x86_64__) || defined(_M_X64)
    InitOnceExecuteOnce(&tsc_once, tsc_calibrate, NULL, NULL);
    ULONG64 cyc;
    if (tsc_per_ns > 0 && QueryThreadCycleTime(GetCurrentThread(), &cyc)) return (uint64_t)((double)cyc / tsc_per_ns);
#endif
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    uint64_t kt = ((uint64_t)k.dwHighDateTime << SF2C_WINDOWS_N_32_E29C) | k.dwLowDateTime, ut = ((uint64_t)u.dwHighDateTime << SF2C_WINDOWS_N_32_E29C) | u.dwLowDateTime;
    return (kt + ut) * SF2C_WINDOWS_N_100_AD57;  // (100 ns units)
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION SF2C_WINDOWS_CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#endif

void plat_sleep_until_ns(uint64_t t) {
    static __thread HANDLE timer;
    if (!timer) {
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    }
    for (;;) {
        uint64_t now = plat_now_ns();
        if (now >= t) return;
        uint64_t left = t - now;
        if (left < SF2C_WINDOWS_N_200000_B552) { YieldProcessor(); continue; }  // last 0.2 ms: spin (timer slack)
        LARGE_INTEGER due = {.QuadPart = -(LONGLONG)((left - SF2C_WINDOWS_N_100000_3BB7) / SF2C_WINDOWS_N_100_AD57)};  // relative, 100 ns units
        if (!timer || !SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) { Sleep(1); continue; }
        WaitForSingleObject(timer, INFINITE);
    }
}

int plat_cpu_count(void) {
    DWORD_PTR proc = 0, sys = 0;
    return GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) ? __builtin_popcountll((unsigned long long)proc) : 0;
}

void plat_thread_guest(int prio) { (void)prio; }  // (the OS's default scheduling serves the game here)

// ---- performance cores (hybrid CPUs: P-cores + E-cores)
// Windows may run the game, render and vsync threads on efficiency cores, where they run far slower (another Switch
// recomp measured 40%). Each of those threads opts out of power throttling (EcoQoS) and, on a hybrid CPU, selects the
// CPU sets of the faster cores (EfficiencyClass above the lowest). Selected CPU sets are a soft preference, never a
// hard pin, and they are only the cores the process affinity allows: when the affinity already excludes the E-cores
// (or the P-cores), nothing is selected, so an affinity-limited run (e.g. 0xF) schedules exactly as before.
// Not hybrid: no CPU sets. LARECOMP_PCORE=0: none of this (the process-wide opt-out in plat_init stays).
static INIT_ONCE pcore_once = INIT_ONCE_STATIC_INIT;
static bool pcore_off;
static ULONG *pcore_ids;  // the selected CPU set ids (NULL: no selection)
static ULONG pcore_n;
static const char *pcore_how = SF2C_WINDOWS_S_EMPTY_12AE;

static BOOL CALLBACK pcore_init(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o, (void)p, (void)c;
    const char *e = getenv(SF2C_WINDOWS_S_LARECOMP_PCORE_44E2);
    if (e && *e && atoi(e) == 0) {
        pcore_off = true;
        fprintf(stderr, SF2C_WINDOWS_S_PCORE_OFF_LARECOMP_PCORE_0_D_E35F);
        return TRUE;
    }
    ULONG len = 0;
    GetSystemCpuSetInformation(NULL, 0, &len, GetCurrentProcess(), 0);
    uint8_t *buf = len ? malloc(len) : NULL;
    if (!buf || !GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buf, len, &len, GetCurrentProcess(), 0)) {
        free(buf);
        pcore_how = SF2C_WINDOWS_S_CPU_SETS_UNAVAILABLE_B9DE;
        fprintf(stderr, SF2C_WINDOWS_S_PCORE_CPU_SET_INFORMATION_UN_4F1B, GetLastError());
        return TRUE;
    }
    // The process affinity mask covers its primary group only: filter by it when the system has a single group.
    DWORD_PTR proc = 0, sys = 0;
    bool filter = GetActiveProcessorGroupCount() == 1 && GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc;
    int lo = SF2C_WINDOWS_N_255_9556, hi = -1, all_p = 0, all_e = 0, ok_p = 0, ok_e = 0, n = 0;
    for (ULONG off = 0; off < len;) {  // pass 1: the efficiency classes
        SYSTEM_CPU_SET_INFORMATION *i = (SYSTEM_CPU_SET_INFORMATION *)(buf + off);
        if (!i->Size) break;
        if (i->Type == CpuSetInformation) {
            int ec = i->CpuSet.EfficiencyClass;
            if (ec < lo) lo = ec;
            if (ec > hi) hi = ec;
            n++;
        }
        off += i->Size;
    }
    bool hybrid = n && hi > lo;
    if (hybrid) pcore_ids = calloc((size_t)n, sizeof *pcore_ids);
    for (ULONG off = 0; off < len;) {  // pass 2: count, and the P-cores the affinity allows
        SYSTEM_CPU_SET_INFORMATION *i = (SYSTEM_CPU_SET_INFORMATION *)(buf + off);
        if (!i->Size) break;
        if (i->Type == CpuSetInformation) {
            bool perf = i->CpuSet.EfficiencyClass > lo;
            bool allowed = !filter || (i->CpuSet.Group == 0 && i->CpuSet.LogicalProcessorIndex < SF2C_WINDOWS_N_64_A68B &&
                                       (proc >> i->CpuSet.LogicalProcessorIndex) & 1);
            perf ? all_p++ : all_e++;
            if (allowed) perf ? ok_p++ : ok_e++;
            if (allowed && perf && pcore_ids) pcore_ids[pcore_n++] = i->CpuSet.Id;
        }
        off += i->Size;
    }
    free(buf);
    if (!hybrid) {
        pcore_how = SF2C_WINDOWS_S_NOT_HYBRID_NO_CPU_SETS_0D22;
        fprintf(stderr, SF2C_WINDOWS_S_PCORE_CPU_NOT_HYBRID_D_LOGIC_0E39
                        SF2C_WINDOWS_S_NO_CPU_SETS_N_84D2, n);
    } else if (!ok_p || !ok_e) {
        free(pcore_ids), pcore_ids = NULL, pcore_n = 0;
        pcore_how = SF2C_WINDOWS_S_AFFINITY_DECIDES_NO_CPU_SETS_78B9;
        fprintf(stderr, SF2C_WINDOWS_S_PCORE_HYBRID_CPU_D_PERFORMAN_42D0
                        SF2C_WINDOWS_S_ALLOWS_D_D_NO_CPU_SETS_THE_A_6B2E, all_p, all_e, ok_p, ok_e);
    } else {
        pcore_how = SF2C_WINDOWS_S_PERFORMANCE_CORE_CPU_SETS_70B5;
        fprintf(stderr, SF2C_WINDOWS_S_PCORE_HYBRID_CPU_D_PERFORMAN_B5DF
                        SF2C_WINDOWS_S_THREADS_PREFER_THE_D_PERFORM_CB50, all_p, all_e,
                filter && (ok_p != all_p || ok_e != all_e) ? SF2C_WINDOWS_S_AFFINITY_LIMITED_593E : SF2C_WINDOWS_S_EMPTY_12AE, ok_p);
    }
    return TRUE;
}

void plat_thread_perf_core(int role) {
    InitOnceExecuteOnce(&pcore_once, pcore_init, NULL, NULL);
    if (pcore_off) return;
    THREAD_POWER_THROTTLING_STATE tp = {
        .Version = THREAD_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED,
        .StateMask = 0,  // (controlled, and off: never EcoQoS)
    };
    bool thr = SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &tp, sizeof tp) != 0;
    bool sets = !pcore_ids || SetThreadSelectedCpuSets(GetCurrentThread(), pcore_ids, pcore_n) != 0;
    static volatile LONG logged;  // (once per role: a reboot's new game thread applies the same, quietly)
    static const char *const names[] = {SF2C_WINDOWS_S_GAME_12B5, SF2C_WINDOWS_S_RENDER_CCC9, SF2C_WINDOWS_S_VSYNC_4729};
    if (role < 0 || role > SF2C_WINDOWS_N_2_D473 || (InterlockedOr(&logged, 1 << role) & (1 << role))) return;
    fprintf(stderr, SF2C_WINDOWS_S_PCORE_S_THREAD_POWER_THROTTL_B141, names[role], thr ? SF2C_WINDOWS_S_OFF_9F3B : SF2C_WINDOWS_S_UNCHANGED_REFUSED_FD0C,
            pcore_how, sets ? SF2C_WINDOWS_S_EMPTY_12AE : SF2C_WINDOWS_S_CPU_SETS_REFUSED_9D87);
}

void plat_thread_realtime(uint64_t period_ns) {
    (void)period_ns;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}
int plat_thread_realtime_check(uint64_t period_ns) { (void)period_ns; return -1; }

// ---- process
// GUI launches keep their own append-only log; lasrecomp.log names the latest run, with five recent backups.
// A launcher and the game it starts share one session, so another launch cannot truncate either writer.
static char g_log_path[MAX_PATH], g_log_latest[MAX_PATH];
static char g_log_tag[MAX_PATH + SF2C_WINDOWS_N_16_B17E];
static HANDLE g_log_mutex;

static bool lock_logs(void) {
    DWORD r = g_log_mutex ? WaitForSingleObject(g_log_mutex, SF2C_WINDOWS_N_5000_0F8E) : WAIT_FAILED;
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}

// A shared lock outside the log's data marks it as live, including across the launcher's child processes.
// Pruning takes the exclusive form, so an older session still running keeps its path until the next cleanup.
static bool log_live_lock(HANDLE file, bool exclusive) {
    OVERLAPPED at = {.Offset = SF2C_WINDOWS_N_0XFFFFFFFFU_7BAF, .OffsetHigh = SF2C_WINDOWS_N_0X7FFFFFFFU_7A69};
    return LockFileEx(file, LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &at);
}

typedef struct { char name[MAX_PATH]; FILETIME created; } SessionLog;
static int newest_log(const void *a, const void *b) {
    const SessionLog *x = a, *y = b;
    int time = CompareFileTime(&y->created, &x->created);
    return time ? time : strcmp(y->name, x->name);
}
static bool session_log_name(const char *name) {  // lasrecomp-backup-<filetime hex>-<pid>.log
    if (strncmp(name, SF2C_WINDOWS_S_LASRECOMP_BACKUP_9CFD, SF2C_WINDOWS_N_17_4523)) return false;
    const char *p = name + SF2C_WINDOWS_N_17_4523;
    while ((*p >= SF2C_WINDOWS_CH_0_59A9 && *p <= SF2C_WINDOWS_CH_9_E453) || (*p >= SF2C_WINDOWS_CH_A_D749 && *p <= SF2C_WINDOWS_CH_F_CE92) || *p == SF2C_WINDOWS_CH_EMPTY_01F8) p++;
    return p > name + SF2C_WINDOWS_N_17_4523 && !strcmp(p, SF2C_WINDOWS_S_LOG_0386);
}
static void keep_legacy_log(const char *dir) {
    WIN32_FILE_ATTRIBUTE_DATA old;
    if (!GetFileAttributesExA(g_log_latest, GetFileExInfoStandard, &old) ||
        old.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return;
    // The fallback copy has its own creation time, but still names the original session in its first line.
    char old_tag[MAX_PATH + SF2C_WINDOWS_N_16_B17E] = {0};
    HANDLE old_file = CreateFileA(g_log_latest, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (old_file != INVALID_HANDLE_VALUE) {
        DWORD got;
        ReadFile(old_file, old_tag, sizeof old_tag - 1, &got, NULL); CloseHandle(old_file);
    }
    char path[MAX_PATH];
    snprintf(path, sizeof path, SF2C_WINDOWS_S_SLASRECOMP_BACKUP_LOG_F9E4, dir);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(path, &data);
    bool kept = false;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            char tag[MAX_PATH + SF2C_WINDOWS_N_16_B17E]; snprintf(tag, sizeof tag, SF2C_WINDOWS_S_SESSION_S_N_980F, data.cFileName);
            kept |= !strncmp(old_tag, tag, strlen(tag)) || !CompareFileTime(&data.ftCreationTime, &old.ftCreationTime);
        }
        while (!kept && FindNextFileA(find, &data));
        FindClose(find);
    }
    // The first launch after upgrading may only have the old lasrecomp.log; retain that report too.
    if (!kept) {
        unsigned long long stamp = ((unsigned long long)old.ftCreationTime.dwHighDateTime << SF2C_WINDOWS_N_32_E29C) | old.ftCreationTime.dwLowDateTime;
        if (snprintf(path, sizeof path, SF2C_WINDOWS_S_SLASRECOMP_BACKUP_016LLX_0_L_A116, dir, stamp) < (int)sizeof path)
            CopyFileA(g_log_latest, path, TRUE);
    }
}
static void prune_logs(const char *dir) {
    char path[MAX_PATH];
    if (snprintf(path, sizeof path, SF2C_WINDOWS_S_SLASRECOMP_BACKUP_LOG_F9E4, dir) >= (int)sizeof path) return;
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(path, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    SessionLog *logs = NULL;
    size_t n = 0;
    do {
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || !session_log_name(data.cFileName)) continue;
        SessionLog *next = realloc(logs, (n + 1) * sizeof *logs);
        if (!next) { free(logs); FindClose(find); return; }
        logs = next;
        snprintf(logs[n].name, sizeof logs[n].name, SF2C_WINDOWS_S_S_0030, data.cFileName);
        logs[n++].created = data.ftCreationTime;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    if (n > 1) qsort(logs, n, sizeof *logs, newest_log);
    for (size_t i = SF2C_WINDOWS_N_5_EF2D; i < n; i++) {
        if (snprintf(path, sizeof path, SF2C_WINDOWS_S_S_S_72BD, dir, logs[i].name) >= (int)sizeof path) continue;
        HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        if (log_live_lock(f, true)) DeleteFileA(path);
        CloseHandle(f);
    }
    free(logs);
}

// NTFS can give the newest session both names, keeping lasrecomp.log current even after a crash. On filesystems
// without hard links, copy on exit instead. The unique session header identifies who owns the latest report;
// creation times can collide for simultaneous launches. The startup mutex also covers this replacement.
static void publish_log(void) {
    if (!g_log_path[0] || !lock_logs()) return;
    fflush(stdout); fflush(stderr);
    HANDLE latest = CreateFileA(g_log_latest, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    BY_HANDLE_FILE_INFORMATION current, session;
    HANDLE source = (HANDLE)_get_osfhandle(_fileno(stderr));
    char tag[sizeof g_log_tag]; DWORD got = 0;
    bool copy = latest != INVALID_HANDLE_VALUE && ReadFile(latest, tag, strlen(g_log_tag), &got, NULL) &&
                got == strlen(g_log_tag) && !memcmp(tag, g_log_tag, got) && GetFileInformationByHandle(latest, &current) &&
                GetFileInformationByHandle(source, &session) &&
                (current.dwVolumeSerialNumber != session.dwVolumeSerialNumber || current.nFileIndexHigh != session.nFileIndexHigh ||
                 current.nFileIndexLow != session.nFileIndexLow);
    if (latest != INVALID_HANDLE_VALUE) CloseHandle(latest);
    if (copy) CopyFileA(g_log_path, g_log_latest, FALSE);
    ReleaseMutex(g_log_mutex);
}

static void log_to_file_without_console(void) {
    // An inherited pipe/file (tools, terminal) is already somewhere useful. Only the GUI launch needs redirecting.
    // A relaunched game must still join its GUI session and publish its fallback copy on exit.
    const char *inherited = getenv(SF2C_WINDOWS_S_LASRECOMP_LOG_FILE_AAE4);
    bool fresh = !inherited || !*inherited;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (fresh && err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN) return;
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof dir);
    if (!n || n >= sizeof dir) return;
    char *slash = strrchr(dir, SF2C_WINDOWS_CH_EMPTY_6672);
    if (!slash) return;
    slash[1] = 0;
    if (snprintf(g_log_latest, sizeof g_log_latest, SF2C_WINDOWS_S_SLASRECOMP_LOG_DB16, dir) >= (int)sizeof g_log_latest) return;
    unsigned long long hash = SF2C_WINDOWS_N_14695981039346656037ULL_11FE;
    for (const unsigned char *p = (const unsigned char *)g_log_latest; *p; p++) hash = (hash ^ *p) * SF2C_WINDOWS_N_1099511628211ULL_D3BD;
    char mutex[SF2C_WINDOWS_N_64_A68B]; snprintf(mutex, sizeof mutex, SF2C_WINDOWS_S_LOCAL_LASRECOMP_LOGS_016LLX_47FF, hash);
    g_log_mutex = CreateMutexA(NULL, FALSE, mutex);
    if (!lock_logs()) return;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    unsigned long long stamp = ((unsigned long long)now.dwHighDateTime << SF2C_WINDOWS_N_32_E29C) | now.dwLowDateTime;
    int used = fresh ? snprintf(g_log_path, sizeof g_log_path, SF2C_WINDOWS_S_SLASRECOMP_BACKUP_016LLX_LU__4157, dir, stamp,
                               (unsigned long)GetCurrentProcessId())
                     : snprintf(g_log_path, sizeof g_log_path, SF2C_WINDOWS_S_S_0030, inherited);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (used >= 0 && (size_t)used < sizeof g_log_path)
        file = CreateFileA(g_log_path, GENERIC_READ | FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           fresh ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !log_live_lock(file, false)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        g_log_path[0] = 0; ReleaseMutex(g_log_mutex); return;
    }
    const char *name = strrchr(g_log_path, SF2C_WINDOWS_CH_EMPTY_6672);
    snprintf(g_log_tag, sizeof g_log_tag, SF2C_WINDOWS_S_SESSION_S_N_980F, name ? name + 1 : g_log_path);
    // A GUI-subsystem process with no inherited handles starts with invalid CRT stream descriptors.
    // Give both streams real descriptors before duplicating the shared-delete log handle into them.
    if (!freopen(SF2C_WINDOWS_S_NUL_DB9B, SF2C_WINDOWS_S_AB_782B, stderr) || !freopen(SF2C_WINDOWS_S_NUL_DB9B, SF2C_WINDOWS_S_AB_782B, stdout)) {
        CloseHandle(file); g_log_path[0] = 0; ReleaseMutex(g_log_mutex); return;
    }
    int fd = _open_osfhandle((intptr_t)file, _O_WRONLY | _O_APPEND | _O_BINARY);
    if (fd < 0 || _dup2(fd, _fileno(stderr)) != 0) {
        if (fd < 0) CloseHandle(file); else _close(fd);
        g_log_path[0] = 0; ReleaseMutex(g_log_mutex); return;
    }
    _close(fd);
    setvbuf(stderr, NULL, _IONBF, 0);
    _dup2(_fileno(stderr), _fileno(stdout));
    _putenv_s(SF2C_WINDOWS_S_LASRECOMP_LOG_FILE_AAE4, g_log_path);
    if (fresh) {
        fputs(g_log_tag, stderr);
        keep_legacy_log(dir);
        char next[MAX_PATH];
        // A shorter name than the session path, which was already checked above.
        snprintf(next, sizeof next, SF2C_WINDOWS_S_SLASRECOMP_CURRENT_016LLX_LU_C19D, dir, stamp, (unsigned long)GetCurrentProcessId());
        bool staged = CreateHardLinkA(next, g_log_path, NULL) != 0;
        if (!staged) {
            // Keep the session header in the placeholder too; the full copy at exit preserves it.
            HANDLE marker = CreateFileA(next, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (marker != INVALID_HANDLE_VALUE) {
                DWORD written;
                staged = WriteFile(marker, g_log_tag, strlen(g_log_tag), &written, NULL) && written == strlen(g_log_tag);
                CloseHandle(marker);
                if (!staged) DeleteFileA(next);
            }
        }
        if (staged) {
            MoveFileExA(next, g_log_latest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            DeleteFileA(next);
        }
        prune_logs(dir);
    }
    atexit(publish_log);
    ReleaseMutex(g_log_mutex);
}

// Generated atomics use CMPXCHG16B (-mcx16); AVX2 is not required by this build.
static void require_cpu(void) {
    unsigned a, b, c, d;
    __asm__ volatile(SF2C_WINDOWS_S_CPUID_A8C5 : SF2C_WINDOWS_S_A_5E34(a), SF2C_WINDOWS_S_B_6A25(b), SF2C_WINDOWS_S_C_0E76(c), SF2C_WINDOWS_S_D_81BF(d) : SF2C_WINDOWS_S_A_AC8D(1), SF2C_WINDOWS_S_C_8799(0));
    if (c & (SF2C_WINDOWS_N_1U_B8E6 << SF2C_WINDOWS_N_13_3FDB)) return;
    MessageBoxA(NULL, SF2C_WINDOWS_S_SHADOW_FIGHT_2_RECOMP_REQUIR_9D48,
                SF2C_WINDOWS_S_SHADOW_FIGHT_2_RECOMP_3AC8, MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

// Fatal error (rt_fatal_trace): what happened in plain words (runtime/crash_explain.c) in a message box, when there is
// no console; the technical lines stay in the log.
void plat_fatal_notice(void) {
    publish_log();
    char text[SF2C_WINDOWS_N_1600_B458];
    snprintf(text, sizeof text, SF2C_WINDOWS_S_SHADOW_FIGHT_2_STOPPED_BECAU_22CC, g_log_path);
    if (!g_log_path[0]) return;
    fflush(stderr);
    MessageBoxA(NULL, text, SF2C_WINDOWS_S_SHADOW_FIGHT_2_RECOMP_3AC8, MB_ICONERROR | MB_OK);
}

static DWORD WINAPI notice_thread(LPVOID text) {
    MessageBoxA(NULL, text, SF2C_WINDOWS_S_SHADOW_FIGHT_2_RECOMP_3AC8, MB_ICONWARNING | MB_OK);
    free(text);
    return 0;
}
void plat_notice(const char *text) {
    if (!g_log_path[0]) return;
    char *copy = _strdup(text);
    HANDLE t = copy ? CreateThread(NULL, 0, notice_thread, copy, 0, NULL) : NULL;
    if (t) CloseHandle(t);
    else free(copy);
}

bool plat_module_name(uint64_t pc, char *out, size_t n) {
    HMODULE m;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(uintptr_t)pc, &m))
        return false;
    return GetModuleFileNameA(m, out, (DWORD)n) > 0;
}

// RtlGetVersion (ntdll): the real version - GetVersionEx answers with the manifest's idea of it
typedef LONG(WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
void plat_system_info(PlatSysInfo *si) {
    OSVERSIONINFOW v = {.dwOSVersionInfoSize = sizeof v};
    HMODULE ntdll = GetModuleHandleA(SF2C_WINDOWS_S_NTDLL_DLL_20E2);
    RtlGetVersionFn rtl = ntdll ? (RtlGetVersionFn)(void *)GetProcAddress(ntdll, SF2C_WINDOWS_S_RTLGETVERSION_DFBA) : NULL;
    if (!rtl || rtl(&v) != 0) v.dwMajorVersion = 0;
    SYSTEM_INFO sys;
    GetNativeSystemInfo(&sys);
    const char *arch = sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? SF2C_WINDOWS_S_X64_DD35
                     : sys.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? SF2C_WINDOWS_S_ARM64_20EF : SF2C_WINDOWS_S_EMPTY_15C5;
    const char *name = v.dwMajorVersion != SF2C_WINDOWS_N_10_4A44 ? SF2C_WINDOWS_S_WINDOWS_0B1D : v.dwBuildNumber >= SF2C_WINDOWS_N_22000_7D9A ? SF2C_WINDOWS_S_WINDOWS_11_4438 : SF2C_WINDOWS_S_WINDOWS_10_19C2;
    if (v.dwMajorVersion)  // (Windows 11 still says 10.0: build 22000 and later)
        snprintf(si->os, sizeof si->os, SF2C_WINDOWS_S_S_LU_LU_LU_S_56C6, name, v.dwMajorVersion, v.dwMinorVersion,
                 v.dwBuildNumber, arch);
    else snprintf(si->os, sizeof si->os, SF2C_WINDOWS_S_WINDOWS_S_0043, arch);
    // the CPU's name: the registry's (what Task Manager shows), else the CPUID brand string
    DWORD len = sizeof si->cpu;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, SF2C_WINDOWS_S_HARDWARE_DESCRIPTION_SYSTEM__DEFC, SF2C_WINDOWS_S_PROCESSORNAMESTRING_AE33,
                     RRF_RT_REG_SZ, NULL, si->cpu, &len) != ERROR_SUCCESS) {
        si->cpu[0] = 0;
#if defined(__x86_64__) || defined(__i386__)
        unsigned r[SF2C_WINDOWS_N_12_6B51];
        for (unsigned i = 0; i < SF2C_WINDOWS_N_3_4E07; i++)
            __asm__ volatile(SF2C_WINDOWS_S_CPUID_A8C5 : SF2C_WINDOWS_S_A_5E34(r[i * SF2C_WINDOWS_N_4_4B22]), SF2C_WINDOWS_S_B_6A25(r[i * SF2C_WINDOWS_N_4_4B22 + 1]), SF2C_WINDOWS_S_C_0E76(r[i * SF2C_WINDOWS_N_4_4B22 + SF2C_WINDOWS_N_2_D473]), SF2C_WINDOWS_S_D_81BF(r[i * SF2C_WINDOWS_N_4_4B22 + SF2C_WINDOWS_N_3_4E07]) : SF2C_WINDOWS_S_A_AC8D(SF2C_WINDOWS_N_0X80000002U_5A8A + i), SF2C_WINDOWS_S_C_8799(0));
        memcpy(si->cpu, r, sizeof r);
        si->cpu[sizeof r] = 0;
#endif
    }
    char *c = si->cpu;  // (both pad the name with spaces)
    while (*c == SF2C_WINDOWS_CH_EMPTY_730D) c++;
    memmove(si->cpu, c, strlen(c) + 1);
    for (size_t l = strlen(si->cpu); l && si->cpu[l - 1] == SF2C_WINDOWS_CH_EMPTY_730D; l--) si->cpu[l - 1] = 0;
    // cores: one RelationProcessorCore record each; threads: every processor group's
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info = bytes ? malloc(bytes) : NULL;
    if (info && GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bytes))
        for (char *p = (char *)info; p < (char *)info + bytes; p += ((SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)p)->Size)
            si->cores++;
    free(info);
    si->threads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    MEMORYSTATUSEX m = {.dwLength = sizeof m};
    if (GlobalMemoryStatusEx(&m)) si->ram = m.ullTotalPhys;
#ifdef LARECOMP_ZIP_BUILD
    si->package = SF2C_WINDOWS_S_ZIP_732D;  // (make dist: the release zip)
#endif
}

void plat_init(void) {
    // abort() (rt_fatal_trace, after the crash dialog) without the C runtime's own "Microsoft Visual C++ Runtime
    // Library - abort() has been called" box and the Windows error report: the player sees only LAS Recomp's dialog
#ifdef _WRITE_ABORT_MSG  // (MinGW's stdlib.h declares it with these flags)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    log_to_file_without_console();
    require_cpu();
    _fmode = _O_BINARY;  // fopen/open default to binary (the runtime's "r"/"w" files are binary or LF text)
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetConsoleOutputCP(CP_UTF8);
    timeBeginPeriod(1);  // 1 ms Sleep / condition-variable timeouts (default 15.6 ms)
    // Opt out of EcoQoS and of Windows 11 ignoring the timer resolution while the window is occluded.
    PROCESS_POWER_THROTTLING_STATE pt = {
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,
        .StateMask = 0,
    };
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
}

bool plat_exe_path(char *buf, size_t size) {
    wchar_t path[SF2C_WINDOWS_N_32768_D196];
    DWORD n = GetModuleFileNameW(NULL, path, SF2C_WINDOWS_N_32768_D196);
    if (!n || n >= SF2C_WINDOWS_N_32768_D196 || size > INT_MAX ||
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, buf, (int)size, NULL, NULL)) return false;
    for (char *p = buf; *p; p++) if (*p == SF2C_WINDOWS_CH_EMPTY_6672) *p = SF2C_WINDOWS_CH_EMPTY_0825;
    return true;
}

char *realpath(const char *path, char *out) {
    wchar_t input[SF2C_WINDOWS_N_32768_D196], final[SF2C_WINDOWS_N_32768_D196];
    if (!out || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, input, SF2C_WINDOWS_N_32768_D196)) {
        errno = EINVAL; return NULL;
    }
    HANDLE h = CreateFileW(input, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) { errno = ENOENT; return NULL; }
    DWORD n = GetFinalPathNameByHandleW(h, final, SF2C_WINDOWS_N_32768_D196, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (!n || n >= SF2C_WINDOWS_N_32768_D196) { errno = ENAMETOOLONG; return NULL; }
    wchar_t *p = final;
    if (!wcsncmp(p, SF2C_WINDOWS_S_L_UNC_2256, SF2C_WINDOWS_N_8_2C62)) { p += SF2C_WINDOWS_N_6_E7F6; p[0] = SF2C_WINDOWS_CH_L_ACE1; }
    else if (!wcsncmp(p, SF2C_WINDOWS_S_L_99A3, SF2C_WINDOWS_N_4_4B22)) p += SF2C_WINDOWS_N_4_4B22;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p, -1, out, PATH_MAX, NULL, NULL)) {
        errno = ENAMETOOLONG; return NULL;
    }
    for (char *c = out; *c; c++) if (*c == SF2C_WINDOWS_CH_EMPTY_6672) *c = SF2C_WINDOWS_CH_EMPTY_0825;
    return out;
}

bool plat_probe_read(uint64_t addr, void *out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)addr, out, size, &got) && got == size;
}
int plat_host_thread_id(void) { return (int)GetCurrentThreadId(); }
bool plat_host_thread_alive(int tid) {
    HANDLE h = OpenThread(SYNCHRONIZE, FALSE, (DWORD)tid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

bool plat_bt_other_thread_tid(int tid, const char *prefix) {
    if (tid == plat_host_thread_id()) { plat_print_backtrace(); return true; }
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, (DWORD)tid);
    if (!thread) return false;
    CONTEXT ctx = {.ContextFlags = CONTEXT_CONTROL};
    bool got = false;
    if (SuspendThread(thread) != (DWORD)-1) {
        got = GetThreadContext(thread, &ctx) != 0;
        // Resume before logging or symbol lookup: the sampled thread may hold a
        // CRT/loader lock. Stack reads below are guarded and only diagnostic.
        ResumeThread(thread);
    }
    CloseHandle(thread);
    if (!got) return false;
    fprintf(stderr, SF2C_WINDOWS_S_S_HOST_TID_D_RIP_LLX_RSP_LLX_6F0E, prefix, tid,
            (unsigned long long)ctx.Rip, (unsigned long long)ctx.Rsp, (unsigned long long)ctx.Rbp);
    uint64_t frame = ctx.Rbp;
    for (int i = 0; i < SF2C_WINDOWS_N_24_C235 && frame >= ctx.Rsp && frame - ctx.Rsp < (SF2C_WINDOWS_N_256ULL_3175 << SF2C_WINDOWS_N_20_F5CA) && !(frame & SF2C_WINDOWS_N_7_7902); i++) {
        uint64_t pair[SF2C_WINDOWS_N_2_D473], off = 0;
        if (!plat_probe_read(frame, pair, sizeof pair)) break;
        const char *symbol = rt_host_fn_name(pair[1], &off);
        if (!symbol) symbol = plat_symbol(pair[1], &off);
        fprintf(stderr, SF2C_WINDOWS_S_S_D_LLX_S_LLX_N_08FF, prefix, i, (unsigned long long)pair[1],
                symbol ? symbol : SF2C_WINDOWS_S_EMPTY_15C5, (unsigned long long)off);
        if (pair[0] <= frame) break;
        frame = pair[0];
    }
    return true;
}

// No exec() on Windows: start the new process with the same console / standard handles, wait for it, and exit with
// its exit code. (Exiting at once ended the process a launcher, Steam or a test script started about a second in,
// with code 0, while the game ran on in another process.)
void plat_exec(char *const argv[]) {
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    BOOL ok;
    fflush(stdout);
    fflush(stderr);
    if (!argv) {
        wchar_t *command = _wcsdup(GetCommandLineW());
        if (!command) return;
        ok = CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        free(command);
    } else {
        static char cmd[SF2C_WINDOWS_N_32768_D196];
        cmd[0] = 0;
        for (int i = 0; argv[i]; i++)
            if (!win_append_arg(cmd, sizeof cmd, argv[i])) { errno = E2BIG; return; }
        static wchar_t wcmd[SF2C_WINDOWS_N_32768_D196];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cmd, -1, wcmd, SF2C_WINDOWS_N_32768_D196)) {
            errno = EINVAL; return;
        }
        ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    }
    if (!ok) return;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = 1;
    CloseHandle(pi.hProcess);
    ExitProcess(code);
}

// ---- diagnostics
// DLL frames are named module+offset; frames in our own executable are left to the caller, which maps
// them to guest functions (runtime/dispatch.c) - mingw builds carry DWARF, not PDB symbols.
const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    static __thread char name[MAX_PATH];
    HMODULE m;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(uintptr_t)pc, &m))
        return NULL;
    if (m == GetModuleHandleA(NULL)) return NULL;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(m, path, sizeof path)) return NULL;
    const char *b = strrchr(path, SF2C_WINDOWS_CH_EMPTY_6672);
    snprintf(name, sizeof name, SF2C_WINDOWS_S_S_0030, b ? b + 1 : path);
    if (offset) *offset = pc - (uint64_t)(uintptr_t)m;
    return name;
}

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);  // runtime/dispatch.c

void plat_print_backtrace(void) {
    void *bt[SF2C_WINDOWS_N_64_A68B];
    USHORT n = RtlCaptureStackBackTrace(0, SF2C_WINDOWS_N_64_A68B, bt, NULL);
    uint64_t exe = (uint64_t)(uintptr_t)GetModuleHandleA(NULL);
    for (int i = 0; i < n; i++) {
        uint64_t pc = (uint64_t)(uintptr_t)bt[i], off = 0;
        const char *s = plat_symbol(pc, &off);
        if (!s) s = rt_host_fn_name(pc, &off);
        if (s) fprintf(stderr, SF2C_WINDOWS_S_2D_016LLX_S_LLX_N_454B, i, (unsigned long long)pc, s, (unsigned long long)off);
        else fprintf(stderr, SF2C_WINDOWS_S_2D_016LLX_LASRECOMP_LLX_N_84FF, i, (unsigned long long)pc,
                     (unsigned long long)(pc - exe));
    }
}

static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uint64_t fa = er->NumberParameters >= SF2C_WINDOWS_N_2_D473 ? er->ExceptionInformation[1] : 0;
    char what[SF2C_WINDOWS_N_48_9801];
    snprintf(what, sizeof what, SF2C_WINDOWS_S_EXCEPTION_08LX_1CEE, er->ExceptionCode);
    // x64 has no link register: report the return address on top of the stack (valid at function entry).
    uint64_t lr = 0;
    plat_probe_read(ep->ContextRecord->Rsp, &lr, sizeof lr);
    rt_fault_report(what, fa, ep->ContextRecord->Rip, lr);
}

void plat_crash_handler_install(void) {
    ULONG guarantee = SF2C_WINDOWS_N_64_A68B << SF2C_WINDOWS_N_10_4A44;  // room for the report after a stack overflow
    SetThreadStackGuarantee(&guarantee);
    if (g_report_crashes) return;
    g_report_crashes = true;
    SetUnhandledExceptionFilter(unhandled_filter);
}

// ---- POSIX shims (win_compat.h)
static ssize_t prw(int fd, void *buf, size_t n, off_t off, bool write) {
    if (off < 0 || n > (uint64_t)INT64_MAX - (uint64_t)off) { errno = EINVAL; return -1; }
    HANDLE original = (HANDLE)_get_osfhandle(fd);
    if (original == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    if (!n) return 0;
    // ReadFile with OVERLAPPED on a synchronous CRT handle still changes its
    // shared file pointer. Reopen the same file object for independent positioned
    // I/O, without looking it up by name (saves may already have been renamed).
    HANDLE h = ReOpenFile(original, write ? GENERIC_WRITE : GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS);
    if (h == INVALID_HANDLE_VALUE) { errno = EIO; return -1; }
    // Kernel I/O does not invoke our user-mode lazy-commit handler.
    uint64_t address = (uint64_t)(uintptr_t)buf;
    if (address >= g_lo && address < g_hi) plat_commit(address, n);
    size_t done = 0;
    bool failed = false;
    while (done < n) {
        OVERLAPPED ov = {0};
        uint64_t o = (uint64_t)off + done;
        ov.Offset = (DWORD)o;
        ov.OffsetHigh = (DWORD)(o >> SF2C_WINDOWS_N_32_E29C);
        DWORD chunk = n - done > SF2C_WINDOWS_N_0X40000000_A930 ? SF2C_WINDOWS_N_0X40000000_A930 : (DWORD)(n - done), got = 0;
        BOOL ok = write ? WriteFile(h, (const char *)buf + done, chunk, &got, &ov)
                        : ReadFile(h, (char *)buf + done, chunk, &got, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(h, &ov, &got, TRUE);
        if (!ok) {
            DWORD error = GetLastError();
            if (!write && error == ERROR_HANDLE_EOF) break;
            errno = error == ERROR_DISK_FULL ? ENOSPC : EIO;
            failed = true;
            break;
        }
        if (!got) break;
        done += got;
    }
    CloseHandle(h);
    return failed && !done ? -1 : (ssize_t)done;
}

ssize_t pread(int fd, void *buf, size_t n, off_t off) { return prw(fd, buf, n, off, false); }
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) { return prw(fd, (void *)buf, n, off, true); }

// ---- child processes
typedef struct { HANDLE process, job; } WinProcess;

PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    if (!argv || !argv[0] || !*argv[0]) { errno = EINVAL; return 0; }
    static __thread char cmd[SF2C_WINDOWS_N_32768_D196];
    cmd[0] = 0;
    for (int i = 0; argv[i]; i++)
        if (!win_append_arg(cmd, sizeof cmd, argv[i])) { errno = E2BIG; return 0; }
    static __thread wchar_t wcmd[SF2C_WINDOWS_N_32768_D196], wlog[SF2C_WINDOWS_N_32768_D196], wprog[SF2C_WINDOWS_N_32768_D196], wfull[SF2C_WINDOWS_N_32768_D196];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cmd, -1, wcmd, SF2C_WINDOWS_N_32768_D196) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, log_path ? log_path : SF2C_WINDOWS_S_NUL_DB9B, -1, wlog, SF2C_WINDOWS_N_32768_D196) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[0], -1, wprog, SF2C_WINDOWS_N_32768_D196)) {
        errno = EINVAL; return 0;
    }
    for (wchar_t *p = wprog; *p; p++) if (*p == SF2C_WINDOWS_CH_L_A8E7) *p = SF2C_WINDOWS_CH_L_ACE1;
    DWORD length = GetFullPathNameW(wprog, SF2C_WINDOWS_N_32768_D196, wfull, NULL);
    if (!length || length >= SF2C_WINDOWS_N_32768_D196) { errno = ENAMETOOLONG; return 0; }
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof sa, .bInheritHandle = TRUE};
    HANDLE log = CreateFileW(wlog, append_log ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, append_log ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) return 0;
    STARTUPINFOW si = {.cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = NULL, .hStdOutput = log,
                       .hStdError = log};
    PROCESS_INFORMATION pi;
    // CreateProcess doesn't resolve relative paths written with '/' (e.g. a helper under tools/): pass the program
    // as a full path, like posix_spawn takes it as given
    BOOL ok = CreateProcessW(wfull, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             NULL, NULL, &si, &pi);
    if (!ok) fprintf(stderr, SF2C_WINDOWS_S_PLAT_COULD_NOT_START_S_ERROR_F8C8, argv[0], GetLastError());
    CloseHandle(log);
    if (!ok) return 0;
    // A separate job per helper: cancelling one compiler also stops its children immediately,
    // without killing the other compilation jobs. Handles close on app exit as well.
    WinProcess *child = calloc(1, sizeof *child);
    if (!child) {
        TerminateProcess(pi.hProcess, SF2C_WINDOWS_N_255_9556);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 0;
    }
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li) ||
        !AssignProcessToJobObject(job, pi.hProcess)) {
        // A restricted parent job can prevent assignment. Still run the helper, as before jobs were added;
        // cancellation can stop this process only in that case, not its descendants.
        fprintf(stderr, SF2C_WINDOWS_S_PLAT_COULD_NOT_SUPERVISE_S_E_15E3, argv[0], GetLastError());
        if (job) CloseHandle(job);
        job = NULL;
    }
    child->process = pi.hProcess, child->job = job;
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        TerminateProcess(pi.hProcess, SF2C_WINDOWS_N_255_9556);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (job) CloseHandle(job);
        free(child);
        return 0;
    }
    CloseHandle(pi.hThread);
    return (PlatProc)child;
}

void plat_proc_kill(PlatProc p) {
    WinProcess *child = (WinProcess *)p;
    if (child->job) TerminateJobObject(child->job, SF2C_WINDOWS_N_255_9556);
    else TerminateProcess(child->process, SF2C_WINDOWS_N_255_9556);
    WaitForSingleObject(child->process, INFINITE);
    CloseHandle(child->process);
    if (child->job) CloseHandle(child->job);
    free(child);
}

int plat_proc_wait(PlatProc p, bool block) {
    WinProcess *child = (WinProcess *)p;
    DWORD w = WaitForSingleObject(child->process, block ? INFINITE : 0);
    if (w == WAIT_TIMEOUT) return -1;
    DWORD code = SF2C_WINDOWS_N_255_9556;
    if (w == WAIT_OBJECT_0) GetExitCodeProcess(child->process, &code);
    CloseHandle(child->process);
    if (child->job) CloseHandle(child->job);
    free(child);
    return code > SF2C_WINDOWS_N_255_9556 ? SF2C_WINDOWS_N_255_9556 : (int)code;  // (a crash's NTSTATUS, e.g. 0xC0000005, is not "still running" (-1))
}

// rm -rf that never goes through a link: a junction or symlink inside is removed itself (RemoveDirectory /
// DeleteFile on the reparse point), never what it points to. Cloud placeholders (OneDrive: also reparse points, but
// not redirects) are ordinary files and folders. w: a \\?\ path in a 32768-wide buffer, len its length; attr and
// tag: its FindFirstFile attributes and reparse tag.
static bool cloud_placeholder(DWORD tag) { return (tag & SF2C_WINDOWS_N_0XFFFF0FFF_5D5D) == SF2C_WINDOWS_N_0X9000001A_90FB; }  // IO_REPARSE_TAG_CLOUD(_1.._F)
static bool remove_tree_w(wchar_t *w, size_t len, DWORD attr, DWORD tag) {
    bool dir = attr & FILE_ATTRIBUTE_DIRECTORY;
    if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) && !cloud_placeholder(tag))  // junction / symlink: the link only
        return dir ? RemoveDirectoryW(w) != 0 : DeleteFileW(w) != 0;
    if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(w, attr & ~FILE_ATTRIBUTE_READONLY);
    if (!dir) return DeleteFileW(w) != 0;
    bool ok = true;
    if (len + SF2C_WINDOWS_N_3_4E07 >= SF2C_WINDOWS_N_32768_D196) return false;
    wcscpy(w + len, SF2C_WINDOWS_S_L_35DA);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(w, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    w[len] = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, SF2C_WINDOWS_S_L_9BA3) || !wcscmp(fd.cFileName, SF2C_WINDOWS_S_L_D295)) continue;
            size_t k = wcslen(fd.cFileName);
            if (len + 1 + k >= SF2C_WINDOWS_N_32768_D196) { ok = false; continue; }
            w[len] = SF2C_WINDOWS_CH_L_ACE1;
            wcscpy(w + len + 1, fd.cFileName);
            ok &= remove_tree_w(w, len + 1 + k, fd.dwFileAttributes,
                                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? fd.dwReserved0 : 0);
            w[len] = 0;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(w) != 0 && ok;
}

bool plat_remove_tree(const char *dir) {
    static __thread wchar_t rel[MAX_PATH * SF2C_WINDOWS_N_4_4B22], w[SF2C_WINDOWS_N_32768_D196];
    if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, rel, MAX_PATH * SF2C_WINDOWS_N_4_4B22) <= 1) return false;
    for (wchar_t *p = rel; *p; p++) if (*p == SF2C_WINDOWS_CH_L_A8E7) *p = SF2C_WINDOWS_CH_L_ACE1;
    DWORD n = GetFullPathNameW(rel, SF2C_WINDOWS_N_32768_D196 - SF2C_WINDOWS_N_8_2C62, w + SF2C_WINDOWS_N_4_4B22, NULL);  // (absolute, then \\?\ for long paths)
    if (!n || n >= SF2C_WINDOWS_N_32768_D196 - SF2C_WINDOWS_N_8_2C62) return false;
    size_t len;
    if (w[SF2C_WINDOWS_N_4_4B22] == SF2C_WINDOWS_CH_L_ACE1 && w[SF2C_WINDOWS_N_5_EF2D] == SF2C_WINDOWS_CH_L_ACE1) {  // \\server\share -> \\?\UNC\server\share
        memmove(w + SF2C_WINDOWS_N_8_2C62, w + SF2C_WINDOWS_N_6_E7F6, (n - 1) * sizeof *w);
        memcpy(w, SF2C_WINDOWS_S_L_UNC_2256, SF2C_WINDOWS_N_8_2C62 * sizeof *w);
        len = n + SF2C_WINDOWS_N_6_E7F6;
    } else {
        memcpy(w, SF2C_WINDOWS_S_L_99A3, SF2C_WINDOWS_N_4_4B22 * sizeof *w);
        len = n + SF2C_WINDOWS_N_4_4B22;
    }
    while (len > SF2C_WINDOWS_N_7_7902 && w[len - 1] == SF2C_WINDOWS_CH_L_ACE1) len--;  // (no trailing separator: "\\*" is appended)
    w[len] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(w, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return remove_tree_w(w, len, fd.dwFileAttributes,
                         (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? fd.dwReserved0 : 0);
}

// ---- OneDrive
// A folder's final path (links and junctions resolved), without the \\?\ prefix, backslashes, no trailing one
static bool final_path(const wchar_t *p, wchar_t *out, DWORD n) {
    HANDLE h = CreateFileW(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD k = 0;
    if (h != INVALID_HANDLE_VALUE) {
        k = GetFinalPathNameByHandleW(h, out, n, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(h);
    }
    if (!k || k >= n) k = GetFullPathNameW(p, n, out, NULL);  // (can't be opened: as written)
    if (!k || k >= n) return false;
    if (!wcsncmp(out, SF2C_WINDOWS_S_L_UNC_2256, SF2C_WINDOWS_N_8_2C62)) memmove(out + SF2C_WINDOWS_N_2_D473, out + SF2C_WINDOWS_N_8_2C62, (wcslen(out + SF2C_WINDOWS_N_8_2C62) + 1) * sizeof *out);
    else if (!wcsncmp(out, SF2C_WINDOWS_S_L_99A3, SF2C_WINDOWS_N_4_4B22)) memmove(out, out + SF2C_WINDOWS_N_4_4B22, (wcslen(out + SF2C_WINDOWS_N_4_4B22) + 1) * sizeof *out);
    for (wchar_t *c = out; *c; c++) if (*c == SF2C_WINDOWS_CH_L_A8E7) *c = SF2C_WINDOWS_CH_L_ACE1;
    for (size_t l = wcslen(out); l > SF2C_WINDOWS_N_3_4E07 && out[l - 1] == SF2C_WINDOWS_CH_L_ACE1;) out[--l] = 0;
    return true;
}

#ifndef FILE_ATTRIBUTE_PINNED
#define FILE_ATTRIBUTE_PINNED SF2C_WINDOWS_FILE_ATTRIBUTE_PINNED
#define FILE_ATTRIBUTE_UNPINNED SF2C_WINDOWS_FILE_ATTRIBUTE_UNPINNED
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS SF2C_WINDOWS_FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#endif

// Under one of the OneDrive folders the environment names (personal, work / school), or a cloud file itself
bool plat_onedrive_synced(const char *dir) {
    wchar_t w[MAX_PATH], d[SF2C_WINDOWS_N_1024_E39E], r[SF2C_WINDOWS_N_1024_E39E], v[SF2C_WINDOWS_N_1024_E39E];
    if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, w, MAX_PATH) <= 1 || !final_path(w, d, SF2C_WINDOWS_N_1024_E39E)) return false;
    DWORD a = GetFileAttributesW(d);
    if (a != INVALID_FILE_ATTRIBUTES &&
        (a & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_PINNED | FILE_ATTRIBUTE_UNPINNED)))
        return true;
    static const wchar_t *const vars[] = {SF2C_WINDOWS_S_L_ONEDRIVE_146A, SF2C_WINDOWS_S_L_ONEDRIVECONSUMER_7B8C, SF2C_WINDOWS_S_L_ONEDRIVECOMMERCIAL_1458};
    for (int i = 0; i < SF2C_WINDOWS_N_3_4E07; i++) {
        DWORD k = GetEnvironmentVariableW(vars[i], v, SF2C_WINDOWS_N_1024_E39E);
        if (!k || k >= SF2C_WINDOWS_N_1024_E39E || !final_path(v, r, SF2C_WINDOWS_N_1024_E39E)) continue;
        size_t l = wcslen(r);
        if (r[l - 1] == SF2C_WINDOWS_CH_L_ACE1) l--;  // (a drive's root: "D:\")
        if (!_wcsnicmp(d, r, l) && (!d[l] || d[l] == SF2C_WINDOWS_CH_L_ACE1)) return true;
    }
    return false;
}

// End the process at once, skipping DLL teardown (from an atexit handler, after flushing). Exit status 0.
void plat_terminate(void) { publish_log(); TerminateProcess(GetCurrentProcess(), 0); }

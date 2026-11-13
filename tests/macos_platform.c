// Native source-only smoke gate: no proprietary data or GPU required.
#include "platform.h"
#include "paths.h"
#include "cpu.h"
#include <assert.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint64_t g_fault_lr;
bool rt_fault_watch(uint64_t addr, uint64_t pc) { (void)addr; (void)pc; return false; }
void rt_fault_report(const char *what, uint64_t addr, uint64_t pc, uint64_t lr) {
    (void)what; (void)addr; (void)pc; (void)lr; _Exit(99);
}
void rt_crash_explain(char *out, size_t n, const char *path) { (void)path; snprintf(out, n, "test"); }
static void *touch(void *p) { *(volatile unsigned char *)p = 77; return NULL; }
static void __attribute__((noinline)) tail_target(int *p) { *p = 1; }
static void tail(int *p) { __attribute__((musttail)) return tail_target(p); }

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--child")) {
        assert(argc == 5 && !strcmp(argv[2], "") && !strcmp(argv[3], "two words") && !strcmp(argv[4], "Unicode-é"));
        return 23;
    }
    plat_init();
    plat_crash_handler_install();
    const uint64_t lo = 0x7000000000ull, hi = lo + (4ull << 20);
    mach_vm_address_t conflict = lo;
    assert(mach_vm_allocate(mach_task_self(), &conflict, PLAT_PAGE, VM_FLAGS_FIXED) == KERN_SUCCESS);
    *(volatile unsigned char *)(uintptr_t)lo = 91;
    assert(!plat_reserve_guest(lo, hi));
    assert(*(volatile unsigned char *)(uintptr_t)lo == 91);
    assert(mach_vm_deallocate(mach_task_self(), lo, PLAT_PAGE) == KERN_SUCCESS);
    assert(plat_reserve_guest(lo, hi));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, touch, (void *)(uintptr_t)(lo + PLAT_PAGE)));
    assert(!pthread_join(thread, NULL));
    unsigned char byte;
    assert(plat_probe_read(lo + PLAT_PAGE, &byte, 1) && byte == 77);
    assert(!plat_probe_read(1, &byte, 1));
    assert(!plat_protect(lo + PLAT_PAGE, PLAT_PAGE, PLAT_PROT_READ));
    assert(plat_written_pages(lo, hi - lo, NULL, 0, NULL) == -1);
    assert(plat_reset_guest(lo, hi));
    assert(*(volatile unsigned char *)(uintptr_t)(lo + PLAT_PAGE) == 0);
    assert(plat_host_thread_alive(plat_host_thread_id()));
    uint64_t start = plat_now_ns();
    plat_sleep_until_ns(start + 2000000);
    assert(plat_now_ns() >= start + 2000000);

    // Generated code depends on Clang musttail and lock-free aligned 128-bit CAS.
    int called = 0;
    tail(&called);
    assert(called == 1);
    _Alignas(16) __uint128_t value = 5;
    Cpu cpu = {0};
    uint64_t low, high;
    ldxp64(&cpu, (uint64_t)(uintptr_t)&value, &low, &high);
    assert(low == 5 && high == 0);
    assert(!stxp64(&cpu, (uint64_t)(uintptr_t)&value, 7, 9));
    assert(value == ((__uint128_t)9 << 64 | 7));

    char exe[4096];
    assert(plat_exe_path(exe, sizeof exe));
    const char *args[] = {exe, "--child", "", "two words", "Unicode-é", NULL};
    PlatProc child = plat_spawn(args, NULL, false);
    assert(child && plat_proc_wait(child, true) == 23);
    char directory[] = "/tmp/sf2-macos-XXXXXX", data[4096], previous[4096];
    assert(mkdtemp(directory) && getcwd(previous, sizeof previous));
    snprintf(data, sizeof data, "%s/nested/Unicode-é", directory);
    assert(!setenv("SF2RECOMP_DATA_ROOT", data, 1));
    assert(!setenv("SF2_CONFIG", "relative.ini", 1));
    rt_paths_init();
    assert(!strcmp(rt_data_root(), data) || strstr(rt_data_root(), data));
    assert(strstr(getenv("SF2_CONFIG"), "/relative.ini"));
    assert(!chdir(previous) && plat_remove_tree(directory));
    puts("macOS memory, threads, paths, atomics and child-process smoke test passed.");
    return 0;
}

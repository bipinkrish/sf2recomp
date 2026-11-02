// Run on a 64-bit Android device/emulator. No game files or GPU required.
#include "platform.h"
#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <SDL3/SDL_messagebox.h>

// The source-only executable does not run under SDLActivity.
const char *SDL_GetAndroidInternalStoragePath(void) { return "/data/local/tmp"; }
const char *SDL_GetAndroidExternalStoragePath(void) { return "/data/local/tmp"; }
bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window) {
    (void)flags; (void)title; (void)message; (void)window; return true;
}
bool rt_data_root_early(char *out, size_t size) { (void)out; (void)size; return false; }
bool rt_fault_watch(uint64_t addr, uint64_t pc) { (void)addr; (void)pc; return false; }
void rt_fault_report(const char *what, uint64_t addr, uint64_t pc, uint64_t lr) {
    (void)what; (void)addr; (void)pc; (void)lr; _Exit(99);
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { (void)pc; (void)offset; return NULL; }
static void *touch(void *p) { *(volatile unsigned char *)p = 77; return NULL; }
static void __attribute__((noinline)) tail_target(int *p) { *p = 1; }
static void tail(int *p) { __attribute__((musttail)) return tail_target(p); }

int main(void) {
    plat_init();
    plat_crash_handler_install();
    const uint64_t lo = 0x7000000000ull, hi = 0x7320000000ull;
    // A collision must fail without overwriting the existing mapping.
    void *conflict = mmap((void *)(uintptr_t)lo, PLAT_PAGE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(conflict == (void *)(uintptr_t)lo);
    *(volatile unsigned char *)conflict = 91;
    assert(!plat_reserve_guest(lo, hi));
    assert(*(volatile unsigned char *)conflict == 91);
    assert(!munmap(conflict, PLAT_PAGE));
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
    char path[128];
    assert(plat_exe_path(path, sizeof path) && !strcmp(path, "/data/local/tmp/sf2"));
    assert(!plat_exe_path(path, 4));
    PlatSysInfo info = {0};
    plat_system_info(&info);
    assert(strstr(info.os, "Android") && info.ram && info.threads > 0);
    assert(!munmap((void *)(uintptr_t)lo, hi - lo));
    printf("Android platform PASS: page=%ld, guest reservation/reset/protection, threads, atomics, musttail, time, paths\n",
        sysconf(_SC_PAGESIZE));
    return 0;
}

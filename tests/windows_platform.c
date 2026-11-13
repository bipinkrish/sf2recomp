// Source-only native Windows smoke test: no game dump, window or Vulkan device.
#include "platform.h"
#include "paths.h"
#include "platform/windows_threads.h"
#include <windows.h>
#include <winioctl.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(off_t) >= 8, "64-bit file offsets are required");

bool rt_fault_watch(uint64_t addr, uint64_t pc) { (void)addr; (void)pc; return false; }
void rt_fault_report(const char *what, uint64_t addr, uint64_t pc, uint64_t lr) {
    fprintf(stderr, "%s addr=%llx pc=%llx lr=%llx\n", what, addr, pc, lr);
    ExitProcess(99);
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *off) { (void)pc; (void)off; return NULL; }
static void *touch(void *p) { *(volatile unsigned char *)p = 77; return NULL; }

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--child")) {
        assert(argc == 6 && !strcmp(argv[2], "") && !strcmp(argv[3], "two words") &&
               !strcmp(argv[4], "a\"b\\") && !strcmp(argv[5], "Unicode-\xc3\xa9"));
        return 23;
    }
    plat_init();
    plat_crash_handler_install();
    const uint64_t lo = 0x7000000000ull, hi = lo + (4ull << 20);
    // Failed reservations must leave somebody else's mapping intact.
    void *conflict = VirtualAlloc((void *)(uintptr_t)lo, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(conflict == (void *)(uintptr_t)lo);
    *(volatile unsigned char *)conflict = 91;
    assert(!plat_reserve_guest(lo, hi));
    assert(*(volatile unsigned char *)conflict == 91);
    assert(VirtualFree(conflict, 0, MEM_RELEASE));
    assert(plat_reserve_guest(lo, hi));
    uintptr_t thread = guest_thread_create(touch, (void *)(uintptr_t)(lo + 65536), 256u << 20);
    assert(thread && guest_thread_join(thread)); // lazy commit works on another thread
    assert(*(volatile unsigned char *)(uintptr_t)(lo + 65536) == 77);
    uint64_t pages[64], page = 0;
    int n = plat_written_pages(lo, hi - lo, pages, 64, &page);
    if (n >= 0) {
        assert(n > 0 && page == 4096);
        assert(plat_written_pages(lo, hi - lo, pages, 64, &page) == 0);
    }
    assert(plat_protect(lo + 65536, 4096, PLAT_PROT_READ) == 0);
    MEMORY_BASIC_INFORMATION mi;
    assert(VirtualQuery((void *)(uintptr_t)(lo + 65536), &mi, sizeof mi));
    assert(mi.Protect == PAGE_READONLY);
    unsigned char byte;
    assert(plat_probe_read(lo + 65536, &byte, 1) && byte == 77);
    assert(!plat_probe_read(1, &byte, 1));
    assert(plat_reset_guest(lo, hi));
    assert(*(volatile unsigned char *)(uintptr_t)(lo + 65536) == 0);
    assert(plat_host_thread_alive(plat_host_thread_id()));
    uint64_t start = plat_now_ns();
    plat_sleep_until_ns(start + 2000000);
    assert(plat_now_ns() >= start + 2000000);

    char exe[4096], resolved[4096];
    assert(plat_exe_path(exe, sizeof exe) && !strchr(exe, '\\'));
    assert(realpath(exe, resolved) && !strchr(resolved, '\\'));
    const char *args[] = {exe, "--child", "", "two words", "a\"b\\", "Unicode-\xc3\xa9", NULL};
    PlatProc child = plat_spawn(args, NULL, false);
    assert(child && plat_proc_wait(child, true) == 23);

    char temp[MAX_PATH], file[MAX_PATH];
    assert(GetTempPathA(sizeof temp, temp) && GetTempFileNameA(temp, "sf2", 0, file));
    int fd = open(file, O_RDWR | O_BINARY);
    assert(fd >= 0);
    DWORD returned;
    assert(DeviceIoControl((HANDLE)_get_osfhandle(fd), FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL));
    assert(_lseeki64(fd, 17, SEEK_SET) == 17);
    assert(pwrite(fd, "x", 1, (off_t)0x100000005ull) == 1);
    assert(pread(fd, &byte, 1, (off_t)0x100000005ull) == 1 && byte == 'x');
    assert(_lseeki64(fd, 0, SEEK_CUR) == 17);
    // ReadFile itself cannot lazy-commit the guest destination. The pread shim must.
    assert(pread(fd, (void *)(uintptr_t)(lo + 131072), 1, (off_t)0x100000005ull) == 1);
    assert(*(volatile char *)(uintptr_t)(lo + 131072) == 'x');
    close(fd);
    DeleteFileA(file);
    assert(mkdir(file, 0755) == 0);
    char previous[4096], data[4096];
    assert(getcwd(previous, sizeof previous));
    snprintf(data, sizeof data, "%s/nested/Unicode-\xc3\xa9", file);
    assert(!setenv("SF2RECOMP_DATA_ROOT", data, 1));
    assert(!setenv("SF2_CONFIG", "relative.ini", 1));
    rt_paths_init();
    assert(!strchr(rt_data_root(), '\\') && strstr(rt_data_root(), "Unicode-\xc3\xa9"));
    assert(strstr(getenv("SF2_CONFIG"), "/relative.ini") && !strchr(getenv("SF2_CONFIG"), '\\'));
    assert(chdir(previous) == 0 && plat_remove_tree(file));
    puts("Windows memory, threads, paths, I/O and child-process smoke test passed.");
    return 0;
}

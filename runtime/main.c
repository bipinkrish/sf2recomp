// SF2 Recomp entry: load modules, set up the kernel, run nn::init::Start on the main thread.
// Native platform integration adapted from LAS Recomp (MIT; see LICENSE).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_addresses.h"
#include "game_info.h"
#include "gamelib.h"
#include "paths.h"
#include "hle.h"
#include "kernel.h"
#include "rt.h"
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

const char *g_romfs_dir;

// rtld normally passes these to nn::init::Start: run every module's init/fini arrays.
static void run_array(Cpu *c, uint64_t arr, uint64_t size) {
    for (uint64_t i = 0; i < size / 8; i++) {
        uint64_t fn = ((uint64_t *)arr)[i];
        if (fn) guest_call(c, fn);
    }
}

// nn::init::Start calls x2 right after nnosInitialize and x3 after nninitStartup, just before
// nnMain: SDK constructors run early, the game's run once nninitStartup has set up its heap.
// rtld runs a module's DT_INIT, which registers it (and its TLS template) with musl via
// __nnmusl_init_dso and then runs its init_array; fall back to the array if there is no DT_INIT.
static void module_init(Cpu *c, Module *m) {
    LOG("init %s (DT_INIT %llx, %llu init_array entries)\n", m->name, m->init, m->init_array_sz / 8);
    if (m->init) guest_call(c, m->init);
    else run_array(c, m->init_array, m->init_array_sz);
}

static void rtld_init_sdk(Cpu *c) { module_init(c, &g_modules[1]); }
static void rtld_init_main(Cpu *c) { module_init(c, &g_modules[0]); }

// Claim the guest address range before anything else can map there. The host allocator places
// regions at random addresses in the same window; on a collision, re-exec for a fresh layout.
// LARECOMP_REEXEC_FORCE=1 (test): the first process skips the reservation once and starts again, as when the range is
// taken (the new process has LARECOMP_REEXEC set and reserves normally).
#define REEXEC_FORCED "forced by LARECOMP_REEXEC_FORCE"
#ifndef __ANDROID__
__attribute__((constructor(101))) static void reserve_guest_space(void) {
    const char *force = getenv("LARECOMP_REEXEC_FORCE");
    bool forced = force && *force && strcmp(force, "0") && !getenv("LARECOMP_REEXEC");
    if (forced) setenv("LARECOMP_REEXEC_WHY", REEXEC_FORCED, 1);
    else if (plat_reserve_guest(GUEST_LO, GUEST_HI)) return;
    else setenv("LARECOMP_REEXEC_WHY", "", 0);  // (the platform may have said what holds the range; else nothing more)
    const char *tries = getenv("LARECOMP_REEXEC");
    int n = tries ? atoi(tries) : 0;
    if (n >= 20) {
        fprintf(stderr, "FATAL: guest address range %llx-%llx is not free\n", GUEST_LO, GUEST_HI);
        exit(1);
    }
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n + 1);
    setenv("LARECOMP_REEXEC", buf, 1);
    plat_exec(NULL);
    perror("re-exec");
    exit(1);
}
#endif

static KThread *boot(const char *root) {
    char exefs[1024], npdm_path[1024];
    snprintf(exefs, sizeof exefs, "%s/exefs", root);
    snprintf(npdm_path, sizeof npdm_path, "%s/main.npdm", exefs);

    loader_load(exefs);
    kernel_init();
    dispatch_init();
    watch_arm();  // Optional address watch for debugging, without changing guest behavior.

    uint32_t stack_size = 0x100000;
    int prio = 44;
    FILE *f = fopen(npdm_path, "rb");
    if (f) {
        uint8_t meta[0x80];
        if (fread(meta, 1, sizeof meta, f) == sizeof meta && !memcmp(meta, "META", 4)) {
            prio = meta[0xE];
            stack_size = *(uint32_t *)(meta + 0x1C);
        }
        fclose(f);
    }
    LOG("main thread: prio %d, stack 0x%x\n", prio, stack_size);

    uint64_t start = loader_find_symbol("_ZN2nn4init5StartEmmPFvvES2_");
    if (!start) FATAL("nn::init::Start not found\n");
    uint64_t init_fn = hle_register("<rtld_init_sdk>", rtld_init_sdk);
    uint64_t fini_fn = hle_register("<rtld_init_main>", rtld_init_main);
    KThread *t = kernel_create_main_thread(start, stack_size, prio);
    kernel_run_main(t, t->handle, 0, init_fn, fini_fn);  // nn::init::Start(thread_handle, args_region, init, fini)
    return t;
}

static void *boot_worker(void *arg) {
    boot((const char *)arg);
    return NULL;
}

static void log_startup_info(void) {
    PlatSysInfo si = {0};
    plat_system_info(&si);
    fprintf(stderr, "[info] SF2 Recomp runtime (LAS Recomp core), %s, %s, %d cores, %llu MB\n",
            si.os[0] ? si.os : "unknown OS", si.cpu[0] ? si.cpu : "unknown CPU", si.cores,
            (unsigned long long)(si.ram >> 20));
}

int main(int argc, char **argv) {
#ifdef __ANDROID__
    const char *data = SDL_GetAndroidInternalStoragePath();
    if (!data || setenv("LARECOMP_DATA_ROOT", data, 1)) return 1;
    // The library is loaded into an existing Activity process; never re-exec
    // Android's app_process when its address layout conflicts with the guest.
    if (!plat_reserve_guest(GUEST_LO, GUEST_HI)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SF2 Recomp",
            "Cannot reserve the guest address range. Close the game and launch it again.", NULL);
        return 1;
    }
#endif
    plat_init();
    log_startup_info();
#ifdef _WIN32
    setvbuf(stderr, NULL, _IONBF, 0);  // the Windows CRT treats _IOLBF as full buffering
#else
    setvbuf(stderr, NULL, _IOLBF, 0);
#endif
    const char *argument = NULL;
    bool check_only = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check-game")) check_only = true;
        else if (!strcmp(argv[i], "--help")) {
            printf("Usage: %s [--check-game] [extracted-game-directory]\n", argv[0]);
            return 0;
        } else if (argv[i][0] == '-' || argument) {
            fprintf(stderr, "Usage: %s [--check-game] [extracted-game-directory]\n", argv[0]);
            return 2;
        } else argument = argv[i];
    }
    char default_root[4096], resolved_root[4096];
    if (!argument) {
        if (!plat_exe_path(default_root, sizeof default_root)) return 1;
        char *slash = strrchr(default_root, '/');
        if (!slash) return 1;
        *slash = 0;
        size_t used = strlen(default_root);
        if (used + sizeof "/extracted" > sizeof default_root) return 1;
        strcat(default_root, "/extracted");
        argument = default_root;
    }
    if (!realpath(argument, resolved_root)) {
        fprintf(stderr, "Cannot open game directory %s: %s\n", argument, strerror(errno));
#ifdef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SF2 Recomp", "Cannot open the imported game directory.", NULL);
#endif
        return 2;
    }
    const char *root = resolved_root;
    char why[512];
    if (!game_version_select(root, why, sizeof why) || !gamelib_load(root, why, sizeof why)) {
        fprintf(stderr, "Cannot run these game files: %s\n", why);
#ifdef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SF2 Recomp", why, NULL);
#endif
        return 2;
    }
    if (check_only) {
        printf("Game files match the translated main and SDK builds.\n");
        return 0;
    }
    game_info_load(root);
    rt_paths_init();
    char romfs[sizeof resolved_root + sizeof "/romfs"];
    snprintf(romfs, sizeof romfs, "%s/romfs", root);
    g_romfs_dir = strdup(romfs);
    if (!g_romfs_dir) return 1;
    // The guest's first SetHeapSize must be able to establish the GPU-backed heap
    // before the SDK starts allocating in it. The old boot order raced this setup.
    extern void ui_window_init(void);
    ui_window_init();
    rt_hooks_configure();
    // Module loading and kernel setup do not use SDL. Keep them off the window
    // thread so the prepared intro can present while the engine starts.
    pthread_t startup_thread;
    int startup_error = pthread_create(&startup_thread, NULL, boot_worker, (void *)root);
    if (startup_error) {
        LOG("[boot] cannot start background initialization: %s; loading on the main thread\n", strerror(startup_error));
        boot(root);
    }
    extern int ui_run(void);
    ui_run();
    if (!startup_error) pthread_join(startup_thread, NULL);
    LOG("[boot] ui_run returned\n");
    return 0;
}

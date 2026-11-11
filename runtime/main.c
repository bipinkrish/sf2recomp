#include "constants/core.h"
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
    for (uint64_t i = 0; i < size / SF2C_MAIN_N_8_2C62; i++) {
        uint64_t fn = ((uint64_t *)arr)[i];
        if (fn) guest_call(c, fn);
    }
}

// nn::init::Start calls x2 right after nnosInitialize and x3 after nninitStartup, just before
// nnMain: SDK constructors run early, the game's run once nninitStartup has set up its heap.
// rtld runs a module's DT_INIT, which registers it (and its TLS template) with musl via
// __nnmusl_init_dso and then runs its init_array; fall back to the array if there is no DT_INIT.
static void module_init(Cpu *c, Module *m) {
    LOG(SF2C_MAIN_S_INIT_S_DT_INIT_LLX_LLU_INIT__E048, m->name, m->init, m->init_array_sz / SF2C_MAIN_N_8_2C62);
    if (m->init) guest_call(c, m->init);
    else run_array(c, m->init_array, m->init_array_sz);
}

static void rtld_init_sdk(Cpu *c) { module_init(c, &g_modules[1]); }
static void rtld_init_main(Cpu *c) { module_init(c, &g_modules[0]); }

// Claim the guest address range before anything else can map there. The host allocator places
// regions at random addresses in the same window; on a collision, re-exec for a fresh layout.
// LARECOMP_REEXEC_FORCE=1 (test): the first process skips the reservation once and starts again, as when the range is
// taken (the new process has LARECOMP_REEXEC set and reserves normally).
#define REEXEC_FORCED SF2C_MAIN_REEXEC_FORCED
#ifndef __ANDROID__
__attribute__((constructor(SF2C_MAIN_N_101_16DC))) static void reserve_guest_space(void) {
    const char *force = getenv(SF2C_MAIN_S_LARECOMP_REEXEC_FORCE_8398);
    bool forced = force && *force && strcmp(force, SF2C_MAIN_S_0_9808) && !getenv(SF2C_MAIN_S_LARECOMP_REEXEC_1A16);
    if (forced) setenv(SF2C_MAIN_S_LARECOMP_REEXEC_WHY_6D9D, REEXEC_FORCED, 1);
    else if (plat_reserve_guest(GUEST_LO, GUEST_HI)) return;
    else setenv(SF2C_MAIN_S_LARECOMP_REEXEC_WHY_6D9D, SF2C_MAIN_S_EMPTY_12AE, 0);  // (the platform may have said what holds the range; else nothing more)
    const char *tries = getenv(SF2C_MAIN_S_LARECOMP_REEXEC_1A16);
    int n = tries ? atoi(tries) : 0;
    if (n >= SF2C_MAIN_N_20_F5CA) {
        fprintf(stderr, SF2C_MAIN_S_FATAL_GUEST_ADDRESS_RANGE_LL_15E7, GUEST_LO, GUEST_HI);
        exit(1);
    }
    char buf[SF2C_MAIN_N_16_B17E];
    snprintf(buf, sizeof buf, SF2C_MAIN_S_D_42B3, n + 1);
    setenv(SF2C_MAIN_S_LARECOMP_REEXEC_1A16, buf, 1);
    plat_exec(NULL);
    perror(SF2C_MAIN_S_RE_EXEC_44E0);
    exit(1);
}
#endif

static KThread *boot(const char *root) {
    uint64_t started = plat_now_ns();
    char exefs[SF2C_MAIN_N_1024_E39E], npdm_path[SF2C_MAIN_N_1024_E39E];
    snprintf(exefs, sizeof exefs, SF2C_MAIN_S_S_EXEFS_2DFB, root);
    snprintf(npdm_path, sizeof npdm_path, SF2C_MAIN_S_S_MAIN_NPDM_A264, exefs);

    loader_load(exefs);
    LOG(SF2C_MAIN_S_STARTUP_GUEST_MODULES_LOADED_53F3, (plat_now_ns() - started) / SF2C_MAIN_N_1000000_6CCE);
    kernel_init();
    dispatch_init();
    LOG(SF2C_MAIN_S_STARTUP_KERNEL_AND_DISPATCH__D685, (plat_now_ns() - started) / SF2C_MAIN_N_1000000_6CCE);
    watch_arm();  // Optional address watch for debugging, without changing guest behavior.

    uint32_t stack_size = SF2C_MAIN_N_0X100000_0A2A;
    int prio = SF2C_MAIN_N_44_71EE;
    FILE *f = fopen(npdm_path, SF2C_MAIN_S_RB_46CA);
    if (f) {
        uint8_t meta[SF2C_MAIN_N_0X80_D41C];
        if (fread(meta, 1, sizeof meta, f) == sizeof meta && !memcmp(meta, SF2C_MAIN_S_META_0C27, SF2C_MAIN_N_4_4B22)) {
            prio = meta[SF2C_MAIN_N_0XE_6A51];
            stack_size = *(uint32_t *)(meta + SF2C_MAIN_N_0X1C_E409);
        }
        fclose(f);
    }
    LOG(SF2C_MAIN_S_MAIN_THREAD_PRIO_D_STACK_0X__7110, prio, stack_size);

    uint64_t start = loader_find_symbol(SF2C_MAIN_S_ZN2NN4INIT5STARTEMMPFVVES2_2B45);
    if (!start) FATAL(SF2C_MAIN_S_NN_INIT_START_NOT_FOUND_N_162D);
    uint64_t init_fn = hle_register(SF2C_MAIN_S_RTLD_INIT_SDK_CAAC, rtld_init_sdk);
    uint64_t fini_fn = hle_register(SF2C_MAIN_S_RTLD_INIT_MAIN_C24D, rtld_init_main);
    KThread *t = kernel_create_main_thread(start, stack_size, prio);
    kernel_run_main(t, t->handle, 0, init_fn, fini_fn);  // nn::init::Start(thread_handle, args_region, init, fini)
    return t;
}

static void *boot_worker(void *arg) {
    boot((const char *)arg);
    return NULL;
}

#include "version.h"

static void log_startup_info(void) {
    PlatSysInfo si = {0};
    plat_system_info(&si);
    fprintf(stderr, SF2C_MAIN_S_INFO_SF2RECOMP_672D SF2RECOMP_VERSION SF2C_MAIN_S_S_S_D_CORES_LLU_MB_N_BAEC,
            si.os[0] ? si.os : SF2C_MAIN_S_UNKNOWN_OS_DB5F, si.cpu[0] ? si.cpu : SF2C_MAIN_S_UNKNOWN_CPU_D8F2, si.cores,
            (unsigned long long)(si.ram >> SF2C_MAIN_N_20_F5CA));
}

int main(int argc, char **argv) {
    if(argc==SF2C_MAIN_N_2_D473 && !strcmp(argv[1],SF2C_MAIN_S_VERSION_99AD)) {
        puts(SF2C_MAIN_S_SF2RECOMP_6431 SF2RECOMP_VERSION);return 0;
    }
#ifdef __ANDROID__
    const char *data = SDL_GetAndroidExternalStoragePath();
    if (!data || setenv(SF2C_MAIN_S_LARECOMP_DATA_ROOT_B22A, data, 1)) return 1;
    // The library is loaded into an existing Activity process; never re-exec
    // Android's app_process when its address layout conflicts with the guest.
    if (!plat_reserve_guest(GUEST_LO, GUEST_HI)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SF2C_MAIN_S_SF2_RECOMP_AA83,
            SF2C_MAIN_S_CANNOT_RESERVE_THE_GUEST_ADD_66A0, NULL);
        return 1;
    }
#endif
    const char *argument = NULL;
    bool check_only = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], SF2C_MAIN_S_CHECK_GAME_C9D1)) check_only = true;
        else if (!strcmp(argv[i], SF2C_MAIN_S_HELP_A603)) {
            printf(SF2C_MAIN_S_USAGE_S_CHECK_GAME_EXTRACTED_0167, argv[0]);
            return 0;
        } else if (argv[i][0] == SF2C_MAIN_CH_EMPTY_01F8 || argument) {
            fprintf(stderr, SF2C_MAIN_S_USAGE_S_CHECK_GAME_EXTRACTED_0167, argv[0]);
            return SF2C_MAIN_N_2_D473;
        } else argument = argv[i];
    }
    if (!check_only) setenv(SF2C_MAIN_S_SF2_GAME_LOG_74EB, SF2C_MAIN_S_1_3915, 0);
    plat_init();
    log_startup_info();
#ifdef _WIN32
    setvbuf(stderr, NULL, _IONBF, 0);  // the Windows CRT treats _IOLBF as full buffering
#else
    setvbuf(stderr, NULL, _IOLBF, 0);
#endif
    char default_root[SF2C_MAIN_N_4096_8B92], resolved_root[SF2C_MAIN_N_4096_8B92];
    if (!argument) {
        if (!plat_exe_path(default_root, sizeof default_root)) return 1;
        char *slash = strrchr(default_root, SF2C_MAIN_CH_EMPTY_0825);
        if (!slash) return 1;
        *slash = 0;
        size_t used = strlen(default_root);
        if (used + sizeof SF2C_MAIN_S_EXTRACTED_D952 > sizeof default_root) return 1;
        strcat(default_root, SF2C_MAIN_S_EXTRACTED_D952);
        argument = default_root;
    }
    if (!realpath(argument, resolved_root)) {
        fprintf(stderr, SF2C_MAIN_S_CANNOT_OPEN_GAME_DIRECTORY_S_8A72, argument, strerror(errno));
#ifdef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SF2C_MAIN_S_SHADOW_FIGHT_2_5A40, SF2C_MAIN_S_CANNOT_OPEN_THE_BUNDLED_GAME_20C8, NULL);
#endif
        return SF2C_MAIN_N_2_D473;
    }
    const char *root = resolved_root;
    char why[SF2C_MAIN_N_512_94F8];
    if (!game_version_select(root, why, sizeof why) || !gamelib_load(root, why, sizeof why)) {
        fprintf(stderr, SF2C_MAIN_S_CANNOT_RUN_THESE_GAME_FILES__27D4, why);
#ifdef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SF2C_MAIN_S_SF2_RECOMP_AA83, why, NULL);
#endif
        return SF2C_MAIN_N_2_D473;
    }
    if (check_only) {
        printf(SF2C_MAIN_S_GAME_FILES_MATCH_THE_TRANSLA_7408);
        return 0;
    }
    game_info_load(root);
    rt_paths_init();
    char romfs[sizeof resolved_root + sizeof SF2C_MAIN_S_ROMFS_12A2];
    snprintf(romfs, sizeof romfs, SF2C_MAIN_S_S_ROMFS_1B4C, root);
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
        LOG(SF2C_MAIN_S_BOOT_CANNOT_START_BACKGROUND_A453, strerror(startup_error));
        boot(root);
    }
    extern int ui_run(void);
    ui_run();
    if (!startup_error) pthread_join(startup_thread, NULL);
    LOG(SF2C_MAIN_S_BOOT_UI_RUN_RETURNED_N_8BF9);
    return 0;
}

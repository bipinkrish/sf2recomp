#include "../constants/hle.h"
// nn::vi (display/layer) and nv:: (graphics driver init), implemented on the host.
#include <stdlib.h>
#include <time.h>

#include "../hle.h"
#include "../kernel.h"
#include "../settings.h"

static uint32_t vsync_r, vsync_w;
static KEvent *vsync_event;
uint32_t vi_vsync_handle(void) { return vsync_r; }
volatile uint64_t g_vsync_count;  // vsyncs signalled so far (runtime/hle/framepace.c, runtime/nvn/pace.c)
volatile uint64_t g_vsync_ns;     // plat_now_ns() of the latest (stored after the count: count >= the vsync it's from)
uint64_t g_native_window;

static void *vsync_thread(void *arg) {
    (void)arg;
    const uint32_t hz = g_settings.target_fps >= SF2C_VI_N_30_624B && g_settings.target_fps <= SF2C_VI_N_360_838F ?
                        (uint32_t)g_settings.target_fps : SF2C_VI_N_60_39FA;
    const uint64_t period = SF2C_VI_N_1000000000ULL_E860 / hz;
    const uint32_t remainder = (uint32_t)(SF2C_VI_N_1000000000ULL_E860 % hz);
    plat_thread_realtime(period);
    plat_thread_perf_core(PLAT_PERF_VSYNC);
    uint64_t next = plat_now_ns();
    uint32_t fractional_ns = 0;
    LOG(SF2C_VI_S_VI_EMULATED_DISPLAY_REFRESH__71CD, hz);
    for (;;) {
        next += period;
        fractional_ns += remainder;
        if (fractional_ns >= hz) {
            next++;
            fractional_ns -= hz;
        }
        plat_sleep_until_ns(next);
        uint64_t now = plat_now_ns();
        if (now > next + period * SF2C_VI_N_3_4E07) {
            // (~100 ms late every frame: macOS coalescing the timers of a thread that lost its real-time policy while
            // the app isn't frontmost - checked and set again here; the window's focus says whether it was frontmost)
            static int warned;
            int rt = plat_thread_realtime_check(period);
            extern bool ui_window_focused(void);
            if (warned++ < SF2C_VI_N_40_D59E) LOG(SF2C_VI_S_VI_VSYNC_TIMER_LATE_BY_LLUMS_5712, (unsigned long long)((now - next) / SF2C_VI_N_1000000_6CCE),
                                    ui_window_focused(), rt == 0 ? SF2C_VI_S_HOLDS_89F6 : rt == 1 ? SF2C_VI_S_WAS_GONE_SET_AGAIN_7B81 : SF2C_VI_S_UNKNOWN_EB8B);
            next = now;  // don't burst-signal to catch up
            fractional_ns = 0;
        }
        __atomic_add_fetch(&g_vsync_count, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&g_vsync_ns, now, __ATOMIC_RELEASE);
        pthread_mutex_lock(&g_klock);
        kernel_signal(vsync_event);
        pthread_mutex_unlock(&g_klock);
    }
    return NULL;
}

HLE_FUNC(SF2C_VI_S_ZN2NN2VI10INITIALIZEEV_EA98, vi_initialize) { (void)c; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI8FINALIZEEV_4CC3, vi_finalize) { (void)c; }

HLE_FUNC(SF2C_VI_S_ZN2NN2VI18OPENDEFAULTDISPLAY_5FDA, vi_open_default_display) {
    *(uint64_t *)(uintptr_t)c->x[0] = (uint64_t)hle_alloc(SF2C_VI_N_0X100_BE6E);
    c->x[0] = 0;
}

HLE_FUNC(SF2C_VI_S_ZN2NN2VI12CLOSEDISPLAYEPNS0__02C4, vi_close_display) { (void)c; }

HLE_FUNC(SF2C_VI_S_ZN2NN2VI11CREATELAYEREPPNS0__302C, vi_create_layer) {
    *(uint64_t *)(uintptr_t)c->x[0] = (uint64_t)hle_alloc(SF2C_VI_N_0X100_BE6E);
    c->x[0] = 0;
}

HLE_FUNC(SF2C_VI_S_ZN2NN2VI12DESTROYLAYEREPNS0__A924, vi_destroy_layer) { (void)c; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI12SETLAYERCROPEPNS0__8DE2, vi_set_layer_crop) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI19SETLAYERSCALINGMOD_1DAA, vi_set_layer_scaling_mode) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI16SETLAYERPOSITIONEP_63EA, vi_set_layer_position) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI12SETLAYERSIZEEPNS0__3B86, vi_set_layer_size) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI9SETLAYERZEPNS0_5LAY_3C97, vi_set_layer_z) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI18SETLAYERVISIBILITY_13DE, vi_set_layer_visibility) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI13SETLAYERALPHAEPNS0_36A3, vi_set_layer_alpha) { c->x[0] = 0; }
HLE_FUNC(SF2C_VI_S_ZN2NN2VI20GETDISPLAYRESOLUTI_600A, vi_get_display_resolution) {
    if (c->x[0]) *(int32_t *)(uintptr_t)c->x[0] = SF2C_VI_N_1280_6F13;
    if (c->x[1]) *(int32_t *)(uintptr_t)c->x[1] = SF2C_VI_N_720_D829;
    c->x[0] = 0;
}

HLE_FUNC(SF2C_VI_S_ZN2NN2VI15GETNATIVEWINDOWEPP_7850, vi_get_native_window) {
    if (!g_native_window) g_native_window = (uint64_t)hle_alloc(SF2C_VI_N_0X100_BE6E);
    *(uint64_t *)(uintptr_t)c->x[0] = g_native_window;
    c->x[0] = 0;
}

// Reboot: the vsync timer and its event live on (the kernel's handle table doesn't): new handles at the next boot.
RT_ON_RESET(vi_reset) {
    vsync_r = vsync_w = 0;
    g_native_window = 0;  // (HLE heap)
}

HLE_FUNC(SF2C_VI_S_ZN2NN2VI20GETDISPLAYVSYNCEVE_12EC, vi_get_vsync_event) {
    if (!vsync_event) {
        vsync_event = kernel_create_event_pair(&vsync_w, &vsync_r);
        pthread_t t;
        pthread_create(&t, NULL, vsync_thread, NULL);
    } else if (!vsync_r) {
        vsync_w = kernel_new_handle(vsync_event);
        vsync_r = kernel_new_handle(vsync_event);
    }
    // nn::os::AttachReadableHandleToSystemEvent(event, handle, managed=false, clear_mode=AutoClear)
    c->x[1] = vsync_r;
    c->x[SF2C_VI_N_2_D473] = 0;
    c->x[SF2C_VI_N_3_4E07] = 1;
    guest_call(c, guest_sym(SF2C_VI_S_ZN2NN2OS33ATTACHREADABLEHAND_FA24));
    c->x[0] = 0;
}

HLE_FUNC(SF2C_VI_S_ZN2NV18INITIALIZEGRAPHICSEPV_BC4E, nv_initialize_graphics) { (void)c; }
HLE_FUNC(SF2C_VI_S_ZN2NV16FINALIZEGRAPHICSEV_E30E, nv_finalize_graphics) { (void)c; }
HLE_FUNC(SF2C_VI_S_ZN2NV20SETGRAPHICSALLOCATORE_3746, nv_set_allocator) { (void)c; }
HLE_FUNC(SF2C_VI_S_ZN2NV28SETGRAPHICSDEVTOOLSAL_3887, nv_set_dt_allocator) { (void)c; }

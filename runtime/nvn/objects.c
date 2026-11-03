#include "../constants/nvn.h"
// NVN objects, builders, and state objects (everything except command recording).
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel.h"
#include "../settings.h"
#include "nvn.h"
#include "rqueue.h"

void renderer_sync(void);

static uint64_t next_debug_id = 1;

// ================================================================== device
// NVNdeviceInfo -> value
static uint32_t device_integer(uint32_t which) {
    static const uint32_t v[] = {
        [0] = SF2C_OBJECTS_N_53_2858, [1] = SF2C_OBJECTS_N_209_83F8,           // API version
        [SF2C_OBJECTS_N_2_D473] = SF2C_OBJECTS_N_14_8527, [SF2C_OBJECTS_N_3_4E07] = SF2C_OBJECTS_N_65536_0F25, [SF2C_OBJECTS_N_4_4B22] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_5_EF2D] = SF2C_OBJECTS_N_8_2C62, [SF2C_OBJECTS_N_6_E7F6] = SF2C_OBJECTS_N_16_B17E, [SF2C_OBJECTS_N_7_7902] = SF2C_OBJECTS_N_4_4B22, [SF2C_OBJECTS_N_8_2C62] = SF2C_OBJECTS_N_16_B17E, [SF2C_OBJECTS_N_9_1958] = SF2C_OBJECTS_N_32_E29C,
        [SF2C_OBJECTS_N_10_4A44] = SF2C_OBJECTS_N_16_B17E, [SF2C_OBJECTS_N_11_4FC8] = SF2C_OBJECTS_N_4_4B22, [SF2C_OBJECTS_N_12_6B51] = SF2C_OBJECTS_N_4_4B22, [SF2C_OBJECTS_N_13_3FDB] = SF2C_OBJECTS_N_4_4B22, [SF2C_OBJECTS_N_14_8527] = SF2C_OBJECTS_N_16_B17E, [SF2C_OBJECTS_N_15_E629] = SF2C_OBJECTS_N_32_E29C, [SF2C_OBJECTS_N_16_B17E] = SF2C_OBJECTS_N_32_E29C,
        [SF2C_OBJECTS_N_17_4523] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_18_4EC9] = SF2C_OBJECTS_N_256_51E8,        // reserved texture / sampler descriptors
        [SF2C_OBJECTS_N_19_9400] = SF2C_OBJECTS_N_4_4B22, [SF2C_OBJECTS_N_20_F5CA] = SF2C_OBJECTS_N_8_2C62, [SF2C_OBJECTS_N_21_6F4B] = SF2C_OBJECTS_N_0X10000_4939, [SF2C_OBJECTS_N_22_785F] = SF2C_OBJECTS_N_0X1000_6ABE,
        [SF2C_OBJECTS_N_23_535F] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_24_C235] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_25_B7A5] = SF2C_OBJECTS_N_4096_8B92, [SF2C_OBJECTS_N_26_5F9C] = SF2C_OBJECTS_N_0X20000_422F, [SF2C_OBJECTS_N_27_6706] = SF2C_OBJECTS_N_16_B17E,
        [SF2C_OBJECTS_N_28_59E1] = SF2C_OBJECTS_N_1536_B51E, [SF2C_OBJECTS_N_29_3513] = SF2C_OBJECTS_N_1024_E39E, [SF2C_OBJECTS_N_30_624B] = SF2C_OBJECTS_N_64_A68B, [SF2C_OBJECTS_N_31_EB1E] = SF2C_OBJECTS_N_1536_B51E,
        [SF2C_OBJECTS_N_32_E29C] = SF2C_OBJECTS_N_0X7FFFFFFF_452F, [SF2C_OBJECTS_N_33_C6F3] = SF2C_OBJECTS_N_65535_F2F8, [SF2C_OBJECTS_N_34_86E5] = SF2C_OBJECTS_N_65535_F2F8,
        [SF2C_OBJECTS_N_35_9F14] = SF2C_OBJECTS_N_8_2C62, [SF2C_OBJECTS_N_36_76A5] = SF2C_OBJECTS_N_1048576_50B4, [SF2C_OBJECTS_N_37_7A61] = SF2C_OBJECTS_N_4096_8B92, [SF2C_OBJECTS_N_38_AEA9] = SF2C_OBJECTS_N_16_B17E, [SF2C_OBJECTS_N_39_0B91] = SF2C_OBJECTS_N_0X10000_4939,
        [SF2C_OBJECTS_N_40_D59E] = 1, [SF2C_OBJECTS_N_41_3D91] = 1, [SF2C_OBJECTS_N_42_7347] = 1,  // min/max filtering, stencil8, ASTC
        [SF2C_OBJECTS_N_43_44CB] = SF2C_OBJECTS_N_0X40000_1443, [SF2C_OBJECTS_N_44_71EE] = SF2C_OBJECTS_N_15_E629, [SF2C_OBJECTS_N_45_8117] = SF2C_OBJECTS_N_2048_BFA0,
        [SF2C_OBJECTS_N_46_25FC] = 1, [SF2C_OBJECTS_N_47_3148] = SF2C_OBJECTS_N_5_EF2D, [SF2C_OBJECTS_N_48_9801] = 1, [SF2C_OBJECTS_N_49_0E17] = SF2C_OBJECTS_N_15_E629,
        [SF2C_OBJECTS_N_50_1A65] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_51_031B] = 1, [SF2C_OBJECTS_N_52_41CF] = 0, [SF2C_OBJECTS_N_53_2858] = 0, [SF2C_OBJECTS_N_54_2FCA] = 0, [SF2C_OBJECTS_N_55_02D2] = SF2C_OBJECTS_N_256_51E8, [SF2C_OBJECTS_N_56_7688] = 1, [SF2C_OBJECTS_N_57_C837] = SF2C_OBJECTS_N_0X1000_6ABE,
        [SF2C_OBJECTS_N_58_6208] = 1, [SF2C_OBJECTS_N_59_3E1E] = 1, [SF2C_OBJECTS_N_60_39FA] = 1, [SF2C_OBJECTS_N_61_D029] = 1, [SF2C_OBJECTS_N_62_81B8] = 1, [SF2C_OBJECTS_N_63_DA4E] = 1, [SF2C_OBJECTS_N_64_A68B] = 1,
    };
    return which < sizeof v / sizeof *v ? v[which] : 0;
}

NVN(nvnDeviceBuilderSetDefaults) { G(uint32_t, A(0), 0) = 0; }
NVN(nvnDeviceBuilderSetFlags) { G(uint32_t, A(0), 0) = (uint32_t)A(1); }
NVN(nvnDeviceBuilderGetFlags) { RET(G(uint32_t, A(0), 0)); }
NVN(nvnDeviceInitialize) {
    static bool once;
    if (!once) {
        once = true;
        rq_hang_watch_start();  // (before anything of the renderer can hang: rqueue.c)
        backend_init();
    }
    LOG(SF2C_OBJECTS_S_NVN_DEVICE_INITIALIZED_N_11B6);
    RET(1);
}
NVN(nvnDeviceFinalize) { (void)c; }
NVN(nvnDeviceSetDebugLabel) { (void)c; }
NVN(nvnDeviceGetInteger) { *(int32_t *)AP(SF2C_OBJECTS_N_2_D473) = (int32_t)device_integer((uint32_t)A(1)); }
NVN(nvnDeviceGetCurrentTimestampInNanoseconds) {
    RET(plat_now_ns());
}
NVN(nvnDeviceGetTimestampInNanoseconds) {  // counter.timestamp: Switch GPU ticks (614.4 MHz, as the counters are written)
    RET((uint64_t)((unsigned __int128)G(uint64_t, A(1), SF2C_OBJECTS_N_8_2C62) * SF2C_OBJECTS_N_625_AD72 / SF2C_OBJECTS_N_384_37B7));
}
NVN(nvnDeviceSetIntermediateShaderCache) { (void)c; }
NVN(nvnDeviceGetTextureHandle) { RET((A(1) & SF2C_OBJECTS_N_0XFFFFF_9137) | ((A(SF2C_OBJECTS_N_2_D473) & SF2C_OBJECTS_N_0XFFF_1DF4) << SF2C_OBJECTS_N_20_F5CA)); }
NVN(nvnDeviceGetTexelFetchHandle) { RET(A(1) & SF2C_OBJECTS_N_0XFFFFF_9137); }
NVN(nvnDeviceGetImageHandle) { RET(A(1) & SF2C_OBJECTS_N_0XFFFFF_9137); }
NVN(nvnDeviceGetSeparateTextureHandle) { RET(A(1) & SF2C_OBJECTS_N_0XFFFFF_9137); }
NVN(nvnDeviceGetSeparateSamplerHandle) { RET((A(1) & SF2C_OBJECTS_N_0XFFF_1DF4) << SF2C_OBJECTS_N_20_F5CA); }
NVN(nvnDeviceInstallDebugCallback) { (void)c; }
NVN(nvnDeviceGenerateDebugDomainId) { RET(1); }
static uint32_t window_origin, depth_mode;
RT_ON_RESET(nvn_device_reset) { window_origin = depth_mode = 0; }  // (the next boot sets its own)
NVN(nvnDeviceSetWindowOriginMode) { window_origin = (uint32_t)A(1); LOG(SF2C_OBJECTS_S_NVN_WINDOW_ORIGIN_MODE_U_N_9C07, window_origin); }
uint32_t nvn_window_origin(void) { return window_origin; }
uint32_t nvn_depth_mode(void) { return depth_mode; }
NVN(nvnDeviceGetWindowOriginMode) { RET(window_origin); }
NVN(nvnDeviceSetDepthMode) { depth_mode = (uint32_t)A(1); LOG(SF2C_OBJECTS_S_NVN_DEPTH_MODE_U_N_F147, depth_mode); }
NVN(nvnDeviceGetDepthMode) { RET(depth_mode); }
NVN(nvnDeviceRegisterFastClearColor) { RET(1); }
NVN(nvnDeviceRegisterFastClearColori) { RET(1); }
NVN(nvnDeviceRegisterFastClearColorui) { RET(1); }
NVN(nvnDeviceRegisterFastClearDepth) { RET(1); }
NVN(nvnDeviceApplyDeferredFinalizes) { (void)c; }
NVN(nvnDeviceFinalizeCommandHandle) { nvn_commands_finalize(A(1)); }
NVN(nvnDeviceIsExternalDebuggerAttached) { RET(0); }
NVN(nvnDeviceWalkDebugDatabase) { (void)c; }

// ================================================================== queue
// QueueBuilder layout: device@0 flags@8 cmd@0xC compute@0x10 control@0x14 mem@0x18 memsize@0x20
//                      flush@0x28 priority@0x2C
NVN(nvnQueueBuilderSetDevice) { G(uint64_t, A(0), 0) = A(1); }
NVN(nvnQueueBuilderSetDefaults) {
    memset(AP(0), 0, SF2C_OBJECTS_N_0X30_8BAD);
    G(uint32_t, A(0), SF2C_OBJECTS_N_0X0C_49AA) = SF2C_OBJECTS_N_0X10000_4939;
    G(uint32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = SF2C_OBJECTS_N_0X40000_1443;
    G(uint32_t, A(0), SF2C_OBJECTS_N_0X14_F942) = SF2C_OBJECTS_N_0X4000_98D9;
    G(uint32_t, A(0), SF2C_OBJECTS_N_0X28_EAB9) = SF2C_OBJECTS_N_0X8000_7702;
    G(uint32_t, A(0), SF2C_OBJECTS_N_0X2C_8A63) = 1;
}
NVN(nvnQueueBuilderSetFlags) { G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62) = (uint32_t)A(1); }
NVN(nvnQueueBuilderSetCommandMemorySize) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X0C_49AA) = (uint32_t)A(1); }
NVN(nvnQueueBuilderSetComputeMemorySize) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); }
NVN(nvnQueueBuilderSetControlMemorySize) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X14_F942) = (uint32_t)A(1); }
NVN(nvnQueueBuilderSetQueueMemory) { G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C) = A(1); G(uint64_t, A(0), SF2C_OBJECTS_N_0X20_A766) = A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnQueueBuilderSetCommandFlushThreshold) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X28_EAB9) = (uint32_t)A(1); }
NVN(nvnQueueBuilderSetQueuePriority) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X2C_8A63) = (uint32_t)A(1); }
NVN(nvnQueueBuilderGetDevice) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnQueueBuilderGetFlags) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnQueueBuilderGetCommandMemorySize) { *(int32_t *)AP(1) = G(int32_t, A(0), SF2C_OBJECTS_N_0X0C_49AA); RET(1); }
NVN(nvnQueueBuilderGetComputeMemorySize) { *(int32_t *)AP(1) = G(int32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C); RET(1); }
NVN(nvnQueueBuilderGetControlMemorySize) { *(int32_t *)AP(1) = G(int32_t, A(0), SF2C_OBJECTS_N_0X14_F942); RET(1); }
NVN(nvnQueueBuilderGetCommandFlushThreshold) { *(int32_t *)AP(1) = G(int32_t, A(0), SF2C_OBJECTS_N_0X28_EAB9); RET(1); }
NVN(nvnQueueBuilderGetQueuePriority) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_0X2C_8A63)); }
NVN(nvnQueueBuilderGetMemorySize) { RET(SF2C_OBJECTS_N_0X10000_4939); }  // required queue memory
NVN(nvnQueueBuilderGetQueueMemorySize) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X20_A766)); }
NVN(nvnQueueBuilderGetMemory) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C)); }

NVN(nvnQueueInitialize) { RET(1); }
NVN(nvnQueueFinalize) { (void)c; }
NVN(nvnQueueSetDebugLabel) { (void)c; }
NVN(nvnQueueGetError) { RET(0); }
NVN(nvnQueueGetTotalCommandMemoryUsed) { RET(0); }
NVN(nvnQueueGetTotalControlMemoryUsed) { RET(0); }
NVN(nvnQueueGetTotalComputeMemoryUsed) { RET(0); }
NVN(nvnQueueResetMemoryUsageCounts) { (void)c; }
NVN(nvnQueueSubmitCommands) {
    int n = (int)A(1);
    const uint64_t *handles = AP(SF2C_OBJECTS_N_2_D473);
    bool rq = rq_on();
    uint64_t t0 = plat_now_ns();
    for (int i = 0; i < n; i++) {
        if (rq) rq_submit(handles[i]);  // the render thread translates it (rqueue.c)
        else {
            NvnCommands *commands = nvn_commands_acquire(handles[i]);
            backend_execute(handles[i]);
            nvn_commands_release(commands);
        }
    }
    g_submit_ns += plat_now_ns() - t0;
}
__thread uint64_t g_submit_ns, g_gpu_wait_ns;
NVN(nvnQueueFlush) { (void)c; }
NVN(nvnQueueFinish) {
    (void)c;
    if (rq_on()) rq_drain();
    uint64_t t0 = plat_now_ns();
    rq_watch(SF2C_OBJECTS_S_QUEUEFINISH_THE_GPU_BF94, 0);
    renderer_sync();
    rq_watch(NULL, 0);
    g_gpu_wait_ns += plat_now_ns() - t0;
}

static NvnSync *sync_of(uint64_t guest) {
    NvnSync *s = HOST(NvnSync, guest);
    if (!s) { s = calloc(1, sizeof *s); SET_HOST(guest, s); }
    return s;
}
uint64_t renderer_fence_seq(void);
void renderer_wait_seq(uint64_t seq);
// A fence's seq: the render thread's item number when it's on (rqueue.c), else the backend's submission number.
NVN(nvnQueueFenceSync) { NvnSync *s = sync_of(A(1)); s->signaled = 1; s->seq = rq_on() ? rq_fence_seq() : renderer_fence_seq(); }
void nvn_sync_set_seq(uint64_t guest, uint64_t seq) {
    // (the render thread: the list's number was set when it was queued; the game may have placed this sync again in
    // a later list since, and that number must stay)
    if (rq_current_item()) return;
    NvnSync *s = sync_of(guest);
    s->signaled = 1;
    s->seq = seq;
}
NVN(nvnQueueWaitSync) { RET(1); }

// ================================================================== sync / event
NVN(nvnSyncInitialize) { SET_HOST(A(0), calloc(1, sizeof(NvnSync))); RET(1); }
NVN(nvnSyncFinalize) { free(HOST(NvnSync, A(0))); SET_HOST(A(0), 0); }
NVN(nvnSyncSetDebugLabel) { (void)c; }
void renderer_flush(void);
NVN(nvnSyncWait) {  // NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED
    static int flush = -1;
    static uint64_t n;
    if (flush < 0) flush = getenv(SF2C_OBJECTS_S_LARECOMP_SYNC_FLUSH_2EFC) ? 1 : 0;
    if (getenv(SF2C_OBJECTS_S_LARECOMP_LOG_SYNC_0941) && n++ % SF2C_OBJECTS_N_60_39FA == 0) LOG(SF2C_OBJECTS_S_NVN_SYNCWAIT_LLU_LR_LLX_N_7DAE, (unsigned long long)n, c->x[SF2C_OBJECTS_N_30_624B]);
    static int whole = -1;  // LARECOMP_SYNC_ALL=1: old behaviour, wait for all submitted work
    if (whole < 0) whole = getenv(SF2C_OBJECTS_S_LARECOMP_SYNC_ALL_2142) ? 1 : 0;
    uint64_t t0 = 0;
    if (flush) { if (rq_on()) rq_flush(); else { t0 = plat_now_ns(); renderer_flush(); } }
    else if (whole) { if (rq_on()) rq_drain(); t0 = plat_now_ns(); rq_watch(SF2C_OBJECTS_S_SYNCWAIT_ALL_THE_GPU_76A4, 0); renderer_sync(); }
    else {  // only the submission that holds this fence
        uint64_t seq = sync_of(A(0))->seq, q = seq;
        static int stats = -1;  // (LARECOMP_LOG_PACE: which fences the game waits on, and how long)
        if (stats < 0) stats = getenv(SF2C_OBJECTS_S_LARECOMP_LOG_PACE_ED01) != NULL;
        uint64_t behind = 0, w0 = 0;
        bool cur = false, untranslated = false;
        if (seq && rq_on()) {
            if (stats) { cur = rq_fence_info(seq, &behind, &untranslated); w0 = plat_now_ns(); }
            seq = rq_wait_item(seq);  // translated and submitted; then its GPU work
        }
        t0 = plat_now_ns();
        rq_watch(SF2C_OBJECTS_S_SYNCWAIT_THE_GPU_SUBMISSION_69A5, seq);
        renderer_wait_seq(seq);
        static long tr_at = -1;  // (debug, rqueue.c LARECOMP_RQ_TRACE) the game thread's fence waits in those frames
        if (tr_at < 0) tr_at = getenv(SF2C_OBJECTS_S_LARECOMP_RQ_TRACE_50B2) ? atol(getenv(SF2C_OBJECTS_S_LARECOMP_RQ_TRACE_50B2)) : 0;
        extern uint64_t renderer_present_count(void);
        if (tr_at && renderer_present_count() >= (uint64_t)tr_at && renderer_present_count() < (uint64_t)tr_at + SF2C_OBJECTS_N_4_4B22)
        {
            extern uint64_t rq_trace_t0;
            LOG(SF2C_OBJECTS_S_RQ_TRACE_GAME_FRAME_LLU_FENC_305D,
                (unsigned long long)renderer_present_count(), (unsigned long long)q, (unsigned long long)seq,
                ((w0 ? w0 : t0) - rq_trace_t0) / SF2C_OBJECTS_N_1E6_8C96, w0 ? (t0 - w0) / SF2C_OBJECTS_N_1E6_8C96 : 0.0, (plat_now_ns() - t0) / SF2C_OBJECTS_N_1E6_8C96);
        }
        if (stats && q && rq_on()) {
            static struct { uint64_t n, untranslated, behind, rq_ns, gpu_ns, tr_ns, head_ns; } st[SF2C_OBJECTS_N_2_D473];
            static uint64_t total;
            uint64_t t1 = plat_now_ns(), tr, head;
            extern void rq_fence_times(uint64_t wait_start, uint64_t *translate_ns, uint64_t *head_ns);
            rq_fence_times(w0, &tr, &head);
            st[cur].n++, st[cur].untranslated += untranslated, st[cur].behind += behind;
            st[cur].rq_ns += t0 - w0, st[cur].gpu_ns += t1 - t0, st[cur].tr_ns += tr, st[cur].head_ns += head;
            if (++total % SF2C_OBJECTS_N_600_284B == 0) {
                for (int k = 1; k >= 0; k--)
                    if (st[k].n)
                        LOG(SF2C_OBJECTS_S_RQ_FENCE_WAITS_S_FRAME_LLU_N_020F
                            SF2C_OBJECTS_S_AVG_WAIT_FOR_TRANSLATION_3F__A8FA
                            SF2C_OBJECTS_S_TRANSLATED_3F_MS_BEFORE_THE__3F48, k ? SF2C_OBJECTS_S_THIS_6C8D : SF2C_OBJECTS_S_AN_EARLIER_9D8A,
                            (unsigned long long)st[k].n, (unsigned long long)st[k].untranslated, (double)st[k].behind / st[k].n,
                            st[k].rq_ns / SF2C_OBJECTS_N_1E6_8C96 / st[k].n, st[k].gpu_ns / SF2C_OBJECTS_N_1E6_8C96 / st[k].n, st[k].tr_ns / SF2C_OBJECTS_N_1E6_8C96 / st[k].n,
                            st[k].head_ns / SF2C_OBJECTS_N_1E6_8C96 / st[k].n);
                memset(st, 0, sizeof st);
            }
        }
    }
    rq_watch(NULL, 0);
    if (t0) g_gpu_wait_ns += plat_now_ns() - t0;
    RET(0);
}
NVN(nvnSyncCreateGLSync) { RET(0); }
NVN(nvnSyncInitializeFromFencedGLSync) { RET(0); }

// EventBuilder: pool@0 offset@8 ; Event: storage address (u32 value lives in guest memory)
NVN(nvnEventBuilderSetDefaults) { G(uint64_t, A(0), 0) = 0; G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = 0; }
NVN(nvnEventBuilderSetStorage) { G(uint64_t, A(0), 0) = A(1); G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnEventBuilderGetStorage) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnEventBuilderGetMemoryPool) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnEventBuilderGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
static uint64_t event_addr(uint64_t ev) { return G(uint64_t, ev, 0); }
NVN(nvnEventInitialize) {
    uint64_t pool = G(uint64_t, A(1), 0), off = G(uint64_t, A(1), SF2C_OBJECTS_N_8_2C62);
    G(uint64_t, A(0), 0) = HOST(NvnPool, pool)->storage + off;
    G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = pool;
    G(uint64_t, A(0), SF2C_OBJECTS_N_16_B17E) = off;
    RET(1);
}
NVN(nvnEventFinalize) { (void)c; }
NVN(nvnEventGetValue) { RET(*(uint32_t *)(uintptr_t)event_addr(A(0))); }
NVN(nvnEventSignal) { *(uint32_t *)(uintptr_t)event_addr(A(0)) = (uint32_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnEventGetMemoryPool) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnEventGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_16_B17E)); }

// ================================================================== memory pools
// Builder: device@0 flags@8 storage@0x10 size@0x18
NVN(nvnMemoryPoolBuilderSetDevice) { G(uint64_t, A(0), 0) = A(1); }
NVN(nvnMemoryPoolBuilderSetDefaults) { memset(AP(0), 0, SF2C_OBJECTS_N_0X20_A766); G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62) = SF2C_OBJECTS_N_0X9_7D3B; }
NVN(nvnMemoryPoolBuilderSetFlags) { G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62) = (uint32_t)A(1); }
NVN(nvnMemoryPoolBuilderSetStorage) { G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = A(1); G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C) = A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnMemoryPoolBuilderGetDevice) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnMemoryPoolBuilderGetFlags) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnMemoryPoolBuilderGetMemory) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnMemoryPoolBuilderGetSize) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C)); }

static NvnPool pools[SF2C_OBJECTS_N_256_51E8];
static int npools;
NVN(nvnMemoryPoolInitialize) {
    NvnPool *p = calloc(1, sizeof *p);
    p->storage = G(uint64_t, A(1), SF2C_OBJECTS_N_0X10_BF9C);
    p->size = G(uint64_t, A(1), SF2C_OBJECTS_N_0X18_D47C);
    p->flags = G(uint32_t, A(1), SF2C_OBJECTS_N_8_2C62);
    SET_HOST(A(0), p);
    if (getenv(SF2C_OBJECTS_S_LARECOMP_LOG_BINDS_8A9A)) LOG(SF2C_OBJECTS_S_POOL_LLX_LLU_KB_FLAGS_X_N_98EF, (unsigned long long)p->storage, (unsigned long long)(p->size >> SF2C_OBJECTS_N_10_4A44), p->flags);
    if (npools < SF2C_OBJECTS_N_256_51E8) pools[npools++] = *p;  // (debug: nvn_pool_flags_at)
    RET(1);
}
// (debug) the flags of the pool holding a guest address (-1: none): CPU_NO_ACCESS 1, CPU_UNCACHED 2, CPU_CACHED 4,
// GPU_NO_ACCESS 8, GPU_UNCACHED 0x10, GPU_CACHED 0x20, SHADER_CODE 0x40, COMPRESSIBLE 0x80, PHYSICAL 0x100, VIRTUAL 0x200
int nvn_pool_flags_at(uint64_t addr) {
    for (int i = npools - 1; i >= 0; i--)
        if (addr >= pools[i].storage && addr < pools[i].storage + pools[i].size) return (int)pools[i].flags;
    return -1;
}
NVN(nvnMemoryPoolFinalize) { if (A(0)) { free(HOST(NvnPool, A(0))); SET_HOST(A(0), 0); } }
NVN(nvnMemoryPoolSetDebugLabel) { (void)c; }
NVN(nvnMemoryPoolMap) { NvnPool *p = A(0) ? HOST(NvnPool, A(0)) : NULL; RET(p ? p->storage : 0); }
NVN(nvnMemoryPoolGetBufferAddress) { NvnPool *p = A(0) ? HOST(NvnPool, A(0)) : NULL; RET(p ? p->storage : 0); }  // GPU address == CPU address
NVN(nvnMemoryPoolGetSize) { NvnPool *p = A(0) ? HOST(NvnPool, A(0)) : NULL; RET(p ? p->size : 0); }
NVN(nvnMemoryPoolGetFlags) { NvnPool *p = A(0) ? HOST(NvnPool, A(0)) : NULL; RET(p ? p->flags : 0); }
NVN(nvnMemoryPoolFlushMappedRange) {
    NvnPool *p = A(0) ? HOST(NvnPool, A(0)) : NULL;
    if (p) backend_pool_written(p->storage + A(1), A(SF2C_OBJECTS_N_2_D473));
}
NVN(nvnMemoryPoolInvalidateMappedRange) { (void)c; }
NVN(nvnMemoryPoolMapVirtual) { RET(1); }

// ================================================================== buffers
// Builder: device@0 pool@8 offset@0x10 size@0x18. Buffer: address@0 pool@8 offset@0x10 size@0x18 id@0x20
NVN(nvnBufferBuilderSetDevice) { G(uint64_t, A(0), 0) = A(1); }
NVN(nvnBufferBuilderSetDefaults) { memset(AP(0) + SF2C_OBJECTS_N_8_2C62, 0, SF2C_OBJECTS_N_0X18_D47C); }
NVN(nvnBufferBuilderSetStorage) {
    G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = A(1); G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = A(SF2C_OBJECTS_N_2_D473); G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C) = A(SF2C_OBJECTS_N_3_4E07);
}
NVN(nvnBufferBuilderGetDevice) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnBufferBuilderGetMemoryPool) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnBufferBuilderGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnBufferBuilderGetSize) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C)); }
NVN(nvnBufferInitialize) {
    uint64_t pool = G(uint64_t, A(1), SF2C_OBJECTS_N_8_2C62), off = G(uint64_t, A(1), SF2C_OBJECTS_N_0X10_BF9C);
    G(uint64_t, A(0), 0) = HOST(NvnPool, pool)->storage + off;
    G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = pool;
    G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = off;
    G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C) = G(uint64_t, A(1), SF2C_OBJECTS_N_0X18_D47C);
    G(uint64_t, A(0), SF2C_OBJECTS_N_0X20_A766) = next_debug_id++;
    RET(1);
}
NVN(nvnBufferFinalize) { (void)c; }
NVN(nvnBufferSetDebugLabel) { (void)c; }
NVN(nvnBufferMap) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnBufferGetAddress) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnBufferGetMemoryPool) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnBufferGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnBufferGetSize) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C)); }
NVN(nvnBufferGetDebugID) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_0X20_A766)); }
NVN(nvnBufferFlushMappedRange) { backend_pool_written(G(uint64_t, A(0), 0) + A(1), A(SF2C_OBJECTS_N_2_D473)); }
NVN(nvnBufferInvalidateMappedRange) { (void)c; }

// ================================================================== textures
// TextureBuilder: device@0 flags@8 target@0xC width@0x10 height@0x14 depth@0x18 levels@0x1C
//   format@0x20 samples@0x24 swizzle@0x28 dsmode@0x2C stride@0x30 pool@0x38 offset@0x40
//   packaged@0x48 layout@0x50 glname@0x58
#define TB(T, off) G(T, A(0), off)
NVN(nvnTextureBuilderSetDevice) { TB(uint64_t, 0) = A(1); }
NVN(nvnTextureBuilderSetDefaults) {
    memset(AP(0) + SF2C_OBJECTS_N_8_2C62, 0, SF2C_OBJECTS_N_0X58_08FB);
    TB(uint32_t, SF2C_OBJECTS_N_0XC_D882) = 1;  // 2D
    TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C) = TB(uint32_t, SF2C_OBJECTS_N_0X14_F942) = TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C) = 1;
    TB(uint32_t, SF2C_OBJECTS_N_0X1C_E409) = 1;
    TB(uint8_t, SF2C_OBJECTS_N_0X28_EAB9) = SF2C_OBJECTS_N_2_D473; TB(uint8_t, SF2C_OBJECTS_N_0X29_F941) = SF2C_OBJECTS_N_3_4E07; TB(uint8_t, SF2C_OBJECTS_N_0X2A_9EC1) = SF2C_OBJECTS_N_4_4B22; TB(uint8_t, SF2C_OBJECTS_N_0X2B_28A6) = SF2C_OBJECTS_N_5_EF2D;
}
NVN(nvnTextureBuilderSetFlags) { TB(uint32_t, SF2C_OBJECTS_N_8_2C62) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetTarget) { TB(uint32_t, SF2C_OBJECTS_N_0XC_D882) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetWidth) { TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetHeight) { TB(uint32_t, SF2C_OBJECTS_N_0X14_F942) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetDepth) { TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetSize1D) { TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); TB(uint32_t, SF2C_OBJECTS_N_0X14_F942) = 1; TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C) = 1; }
NVN(nvnTextureBuilderSetSize2D) { TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); TB(uint32_t, SF2C_OBJECTS_N_0X14_F942) = (uint32_t)A(SF2C_OBJECTS_N_2_D473); TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C) = 1; }
NVN(nvnTextureBuilderSetSize3D) {
    TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); TB(uint32_t, SF2C_OBJECTS_N_0X14_F942) = (uint32_t)A(SF2C_OBJECTS_N_2_D473); TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C) = (uint32_t)A(SF2C_OBJECTS_N_3_4E07);
}
NVN(nvnTextureBuilderSetLevels) { TB(uint32_t, SF2C_OBJECTS_N_0X1C_E409) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetFormat) { TB(uint32_t, SF2C_OBJECTS_N_0X20_A766) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetSamples) { TB(uint32_t, SF2C_OBJECTS_N_0X24_EB21) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetSwizzle) {
    TB(uint8_t, SF2C_OBJECTS_N_0X28_EAB9) = (uint8_t)A(1); TB(uint8_t, SF2C_OBJECTS_N_0X29_F941) = (uint8_t)A(SF2C_OBJECTS_N_2_D473);
    TB(uint8_t, SF2C_OBJECTS_N_0X2A_9EC1) = (uint8_t)A(SF2C_OBJECTS_N_3_4E07); TB(uint8_t, SF2C_OBJECTS_N_0X2B_28A6) = (uint8_t)A(SF2C_OBJECTS_N_4_4B22);
}
NVN(nvnTextureBuilderSetDepthStencilMode) { TB(uint32_t, SF2C_OBJECTS_N_0X2C_8A63) = (uint32_t)A(1); }
NVN(nvnTextureBuilderSetStride) { TB(uint64_t, SF2C_OBJECTS_N_0X30_8BAD) = A(1); }
NVN(nvnTextureBuilderSetStorage) { TB(uint64_t, SF2C_OBJECTS_N_0X38_AE4F) = A(1); TB(uint64_t, SF2C_OBJECTS_N_0X40_1AF3) = A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnTextureBuilderSetPackagedTextureData) { TB(uint64_t, SF2C_OBJECTS_N_0X48_0EF0) = A(1); }
NVN(nvnTextureBuilderSetPackagedTextureLayout) { TB(uint64_t, SF2C_OBJECTS_N_0X50_4171) = A(1) ? *(uint64_t *)AP(1) : 0; }
NVN(nvnTextureBuilderSetGLTextureName) { TB(uint32_t, SF2C_OBJECTS_N_0X58_08FB) = (uint32_t)A(1); }
NVN(nvnTextureBuilderGetDevice) { RET(TB(uint64_t, 0)); }
NVN(nvnTextureBuilderGetFlags) { RET(TB(uint32_t, SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnTextureBuilderGetTarget) { RET(TB(uint32_t, SF2C_OBJECTS_N_0XC_D882)); }
NVN(nvnTextureBuilderGetWidth) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnTextureBuilderGetHeight) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X14_F942)); }
NVN(nvnTextureBuilderGetDepth) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X18_D47C)); }
NVN(nvnTextureBuilderGetLevels) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X1C_E409)); }
NVN(nvnTextureBuilderGetFormat) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X20_A766)); }
NVN(nvnTextureBuilderGetSamples) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X24_EB21)); }
NVN(nvnTextureBuilderGetSwizzle) {
    for (int i = 0; i < SF2C_OBJECTS_N_4_4B22; i++) *(uint32_t *)AP(1 + i) = TB(uint8_t, SF2C_OBJECTS_N_0X28_EAB9 + i);
}
NVN(nvnTextureBuilderGetDepthStencilMode) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X2C_8A63)); }
NVN(nvnTextureBuilderGetStride) { RET(TB(uint64_t, SF2C_OBJECTS_N_0X30_8BAD)); }
NVN(nvnTextureBuilderGetMemoryPool) { RET(TB(uint64_t, SF2C_OBJECTS_N_0X38_AE4F)); }
NVN(nvnTextureBuilderGetMemoryOffset) { RET(TB(uint64_t, SF2C_OBJECTS_N_0X40_1AF3)); }
NVN(nvnTextureBuilderGetPackagedTextureData) { RET(TB(uint64_t, SF2C_OBJECTS_N_0X48_0EF0)); }
NVN(nvnTextureBuilderGetPackagedTextureLayout) { RET(0); }
NVN(nvnTextureBuilderGetGLTextureName) { RET(TB(uint32_t, SF2C_OBJECTS_N_0X58_08FB)); }
NVN(nvnTextureBuilderGetSparseTileLayout) { RET(0); }
NVN(nvnTextureBuilderGetZCullStorageSize) { RET(0); }
NVN(nvnTextureBuilderGetStorageClass) { RET(0); }
NVN(nvnTextureBuilderGetRawStorageClass) { RET(0); }

static bool target_is_array(uint32_t t) { return t == SF2C_OBJECTS_N_3_4E07 || t == SF2C_OBJECTS_N_4_4B22 || t == SF2C_OBJECTS_N_6_E7F6 || t == SF2C_OBJECTS_N_8_2C62 || t == SF2C_OBJECTS_N_9_1958; }

static void builder_layout(uint64_t b, TexLayout *t) {
    uint32_t target = G(uint32_t, b, SF2C_OBJECTS_N_0XC_D882);
    t->fmt = nvn_format_info(G(uint32_t, b, SF2C_OBJECTS_N_0X20_A766));
    t->width = G(uint32_t, b, SF2C_OBJECTS_N_0X10_BF9C);
    t->height = target == 0 || target == SF2C_OBJECTS_N_3_4E07 ? 1 : G(uint32_t, b, SF2C_OBJECTS_N_0X14_F942);
    if (target == SF2C_OBJECTS_N_3_4E07) {  // 1D array: height is the layer count
        t->layers = G(uint32_t, b, SF2C_OBJECTS_N_0X14_F942);
    }
    t->depth = target == SF2C_OBJECTS_N_2_D473 ? G(uint32_t, b, SF2C_OBJECTS_N_0X18_D47C) : 1;
    if (target_is_array(target) && target != SF2C_OBJECTS_N_3_4E07) t->layers = G(uint32_t, b, SF2C_OBJECTS_N_0X18_D47C);
    if (target == SF2C_OBJECTS_N_8_2C62) t->layers = SF2C_OBJECTS_N_6_E7F6;
    if (!t->layers) t->layers = 1;
    t->levels = G(uint32_t, b, SF2C_OBJECTS_N_0X1C_E409) ? G(uint32_t, b, SF2C_OBJECTS_N_0X1C_E409) : 1;
    tegra_layout_init(t);
    // Packaged textures (BNTX) were swizzled offline with their own block height, which can differ
    // from the driver default: NVNpackagedTextureLayout's low word is log2(GOBs per block).
    uint64_t pl = G(uint64_t, b, SF2C_OBJECTS_N_0X50_4171);
    if (pl && (uint32_t)pl <= SF2C_OBJECTS_N_5_EF2D) t->block_height_log2 = (uint32_t)pl;
    if (pl && getenv(SF2C_OBJECTS_S_LARECOMP_LOG_PACKAGED_A712)) {
        static int n;
        if (n++ < SF2C_OBJECTS_N_4000_B090) LOG(SF2C_OBJECTS_S_NVN_PACKAGED_LAYOUT_016LLX_F_DBE6, pl, G(uint32_t, b, SF2C_OBJECTS_N_0X20_A766), t->width, t->height, t->levels, t->block_height_log2);
    }
}

static bool is_linear(uint64_t b) { return (G(uint32_t, b, SF2C_OBJECTS_N_8_2C62) & SF2C_OBJECTS_N_0X10_BF9C) || G(uint32_t, b, SF2C_OBJECTS_N_0XC_D882) == SF2C_OBJECTS_N_10_4A44; }

static uint64_t builder_storage_size(uint64_t b) {
    if (G(uint32_t, b, SF2C_OBJECTS_N_0XC_D882) == SF2C_OBJECTS_N_10_4A44) {  // buffer texture
        return (uint64_t)G(uint32_t, b, SF2C_OBJECTS_N_0X10_BF9C) * nvn_format_info(G(uint32_t, b, SF2C_OBJECTS_N_0X20_A766))->bpb;
    }
    if (is_linear(b)) return G(uint64_t, b, SF2C_OBJECTS_N_0X30_8BAD) * G(uint32_t, b, SF2C_OBJECTS_N_0X14_F942);
    TexLayout t = {0};
    builder_layout(b, &t);
    return tegra_total_size(&t);
}
NVN(nvnTextureBuilderGetStorageSize) { RET(builder_storage_size(A(0))); }
NVN(nvnTextureBuilderGetStorageAlignment) { RET(is_linear(A(0)) ? SF2C_OBJECTS_N_32_E29C : SF2C_OBJECTS_N_512_94F8); }

static NvnTexture *texture_from_builder(uint64_t b) {
    NvnTexture *t = calloc(1, sizeof *t);
    t->refs = 1;
    t->target = G(uint32_t, b, SF2C_OBJECTS_N_0XC_D882);
    t->flags = G(uint32_t, b, SF2C_OBJECTS_N_8_2C62);
    t->width = G(uint32_t, b, SF2C_OBJECTS_N_0X10_BF9C);
    t->height = G(uint32_t, b, SF2C_OBJECTS_N_0X14_F942);
    t->depth = G(uint32_t, b, SF2C_OBJECTS_N_0X18_D47C);
    t->levels = G(uint32_t, b, SF2C_OBJECTS_N_0X1C_E409) ? G(uint32_t, b, SF2C_OBJECTS_N_0X1C_E409) : 1;
    t->format = G(uint32_t, b, SF2C_OBJECTS_N_0X20_A766);
    t->samples = G(uint32_t, b, SF2C_OBJECTS_N_0X24_EB21);
    memcpy(t->swizzle, (void *)(uintptr_t)(b + SF2C_OBJECTS_N_0X28_EAB9), SF2C_OBJECTS_N_4_4B22);
    t->ds_mode = G(uint32_t, b, SF2C_OBJECTS_N_0X2C_8A63);
    t->stride = G(uint64_t, b, SF2C_OBJECTS_N_0X30_8BAD);
    uint64_t pool = G(uint64_t, b, SF2C_OBJECTS_N_0X38_AE4F);
    t->guest_pool = pool;
    t->pool = pool ? HOST(NvnPool, pool) : NULL;
    t->pool_offset = G(uint64_t, b, SF2C_OBJECTS_N_0X40_1AF3);
    t->addr = t->pool ? t->pool->storage + t->pool_offset : 0;
    builder_layout(b, &t->layout);
    t->debug_id = next_debug_id++;
    return t;
}

NVN(nvnTextureInitialize) {
    NvnTexture *t = texture_from_builder(A(1));
    if (t->flags & SF2C_OBJECTS_N_2_D473)
        LOG(SF2C_OBJECTS_S_NVN_VIDEO_TEXTURE_P_UX_U_FMT_83E4, t, t->width, t->height,
            t->format, t->flags, t->target, t->addr, t->stride);
    SET_HOST(A(0), t);
    RET(1);
}
static void pool_entries_forget(NvnTexture *t);
// Finalization unregisters descriptor slots in queue order. Direct references in recorded
// lists keep the object and backend alive until those lists and their executions retire.
void nvn_texture_retain(NvnTexture *t) {
    __atomic_add_fetch(&t->refs, 1, __ATOMIC_RELAXED);
}
void nvn_texture_release(NvnTexture *t) {
    if (__atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL)) return;
    if (rq_on() && !rq_current_item()) rq_destroy_texture(t);
    else nvn_texture_destroy(t);
}
void nvn_texture_finalize(NvnTexture *t) {
    pool_entries_forget(t);
    nvn_texture_release(t);
}
void nvn_texture_destroy(NvnTexture *t) {
    pool_entries_forget(t);
    backend_texture_destroyed(t);
    free(t);
}
NVN(nvnTextureFinalize) {
    NvnTexture *t = HOST(NvnTexture, A(0));
    if (t) {
        if (rq_on()) rq_finalize_texture(t);
        else nvn_texture_finalize(t);
    }
    SET_HOST(A(0), 0);
}
NVN(nvnTextureSetDebugLabel) {
    NvnTexture *t = HOST(NvnTexture, A(0));
    if (t && A(1)) strncpy(t->label, (const char *)AP(1), sizeof t->label - 1);
}
#define TEX HOST(NvnTexture, A(0))
NVN(nvnTextureGetFlags) { RET(TEX->flags); }
NVN(nvnTextureGetTarget) { RET(TEX->target); }
NVN(nvnTextureGetWidth) { RET(TEX->width); }
NVN(nvnTextureGetHeight) { RET(TEX->height); }
NVN(nvnTextureGetDepth) { RET(TEX->depth); }
NVN(nvnTextureGetLevels) { RET(TEX->levels); }
NVN(nvnTextureGetFormat) { RET(TEX->format); }
NVN(nvnTextureGetSamples) { RET(TEX->samples); }
NVN(nvnTextureGetSwizzle) { for (int i = 0; i < SF2C_OBJECTS_N_4_4B22; i++) *(uint32_t *)AP(1 + i) = TEX->swizzle[i]; }
NVN(nvnTextureGetDepthStencilMode) { RET(TEX->ds_mode); }
NVN(nvnTextureGetStride) { RET(TEX->stride); }
NVN(nvnTextureGetTextureAddress) { RET(TEX ? TEX->addr : 0); }
NVN(nvnTextureGetMemoryPool) { RET(TEX ? TEX->guest_pool : 0); }
NVN(nvnTextureGetMemoryOffset) { RET(TEX ? TEX->pool_offset : 0); }
NVN(nvnTextureGetStorageSize) { RET(tegra_total_size(&TEX->layout)); }
NVN(nvnTextureGetDebugID) { RET(TEX->debug_id); }
NVN(nvnTextureGetViewOffset) { RET(0); }
NVN(nvnTextureGetStorageClass) { RET(0); }
NVN(nvnTextureGetRawStorageClass) { RET(0); }
NVN(nvnTextureGetSparseTileLayout) { RET(0); }
NVN(nvnTextureGetZCullStorageSize) { RET(0); }
NVN(nvnTextureCompare) { RET(A(0) == A(1)); }
NVN(nvnTextureFlushTexels) { (void)c; }
NVN(nvnTextureInvalidateTexels) { (void)c; }

// Texture views (our layout of the 0x28-byte guest struct)
#define TV(T, off) G(T, A(0), off)
NVN(nvnTextureViewSetDefaults) { memset(AP(0), 0, SF2C_OBJECTS_N_0X28_EAB9); }
NVN(nvnTextureViewSetLevels) { TV(uint32_t, 0) |= 1; TV(uint16_t, SF2C_OBJECTS_N_4_4B22) = (uint16_t)A(1); TV(uint16_t, SF2C_OBJECTS_N_6_E7F6) = (uint16_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnTextureViewSetLayers) { TV(uint32_t, 0) |= SF2C_OBJECTS_N_2_D473; TV(uint16_t, SF2C_OBJECTS_N_8_2C62) = (uint16_t)A(1); TV(uint16_t, SF2C_OBJECTS_N_10_4A44) = (uint16_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnTextureViewSetFormat) { TV(uint32_t, 0) |= SF2C_OBJECTS_N_4_4B22; TV(uint32_t, SF2C_OBJECTS_N_12_6B51) = (uint32_t)A(1); }
NVN(nvnTextureViewSetSwizzle) {
    NvnTextureView *v = AP(0);
    v->setmask |= SF2C_OBJECTS_N_8_2C62;
    v->swizzle[0] = (uint8_t)A(1); v->swizzle[1] = (uint8_t)A(SF2C_OBJECTS_N_2_D473); v->swizzle[SF2C_OBJECTS_N_2_D473] = (uint8_t)A(SF2C_OBJECTS_N_3_4E07); v->swizzle[SF2C_OBJECTS_N_3_4E07] = (uint8_t)A(SF2C_OBJECTS_N_4_4B22);
}
NVN(nvnTextureViewSetDepthStencilMode) { NvnTextureView *v = AP(0); v->setmask |= SF2C_OBJECTS_N_16_B17E; v->ds_mode = (uint32_t)A(1); }
NVN(nvnTextureViewSetTarget) { NvnTextureView *v = AP(0); v->setmask |= SF2C_OBJECTS_N_32_E29C; v->target = (uint32_t)A(1); }
NVN(nvnTextureViewGetLevels) { *(int32_t *)AP(1) = TV(uint16_t, SF2C_OBJECTS_N_4_4B22); *(int32_t *)AP(SF2C_OBJECTS_N_2_D473) = TV(uint16_t, SF2C_OBJECTS_N_6_E7F6); RET(TV(uint32_t, 0) & 1); }
NVN(nvnTextureViewGetLayers) { *(int32_t *)AP(1) = TV(uint16_t, SF2C_OBJECTS_N_8_2C62); *(int32_t *)AP(SF2C_OBJECTS_N_2_D473) = TV(uint16_t, SF2C_OBJECTS_N_10_4A44); RET(!!(TV(uint32_t, 0) & SF2C_OBJECTS_N_2_D473)); }
NVN(nvnTextureViewGetFormat) { *(uint32_t *)AP(1) = TV(uint32_t, SF2C_OBJECTS_N_12_6B51); RET(!!(TV(uint32_t, 0) & SF2C_OBJECTS_N_4_4B22)); }
NVN(nvnTextureViewGetSwizzle) { NvnTextureView *v = AP(0); for (int i = 0; i < SF2C_OBJECTS_N_4_4B22; i++) *(uint32_t *)AP(1 + i) = v->swizzle[i]; RET(!!(v->setmask & SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnTextureViewGetDepthStencilMode) { NvnTextureView *v = AP(0); *(uint32_t *)AP(1) = v->ds_mode; RET(!!(v->setmask & SF2C_OBJECTS_N_16_B17E)); }
NVN(nvnTextureViewGetTarget) { NvnTextureView *v = AP(0); *(uint32_t *)AP(1) = v->target; RET(!!(v->setmask & SF2C_OBJECTS_N_32_E29C)); }
NVN(nvnTextureViewCompare) { RET(memcmp(AP(0), AP(1), SF2C_OBJECTS_N_0X1C_E409) == 0); }

NVN(nvnTextureWriteTexels) {
    // (texture, view, region, data) -- linear data into block-linear storage
    NvnTexture *t = HOST(NvnTexture, A(0));
    if (!t) return;
    const NvnTextureView *v = AP(1);
    uint32_t level = (v && (v->setmask & 1)) ? v->base_level : 0;
    LevelLayout l = tegra_level(&t->layout, level);
    uint32_t lw = t->width >> level ? t->width >> level : 1;
    uint32_t lh = t->height >> level ? t->height >> level : 1;
    const int32_t *r = AP(SF2C_OBJECTS_N_2_D473);  // xoffset, yoffset, zoffset, width, height, depth
    int32_t def_r[SF2C_OBJECTS_N_6_E7F6] = {0, 0, 0, (int32_t)lw, (int32_t)lh, 1};
    if (!r) r = def_r;
    LOG(SF2C_OBJECTS_S_NVN_WRITETEXELS_T_P_UX_U_LVL_9086,
        t, t->width, t->height, level, lw, lh, r[0], r[1], r[SF2C_OBJECTS_N_2_D473], r[SF2C_OBJECTS_N_3_4E07], r[SF2C_OBJECTS_N_4_4B22], r[SF2C_OBJECTS_N_5_EF2D]);
    if (r[0] == 0 && r[1] == 0 && (uint32_t)r[SF2C_OBJECTS_N_3_4E07] == lw && (uint32_t)r[SF2C_OBJECTS_N_4_4B22] == lh) {
        tegra_swizzle(AP(SF2C_OBJECTS_N_3_4E07), (uint8_t *)(uintptr_t)t->addr + l.offset, l.wb, l.hb, r[SF2C_OBJECTS_N_5_EF2D] > 0 ? (uint32_t)r[SF2C_OBJECTS_N_5_EF2D] : 1,
                      t->layout.fmt->bpb, l.block_height_log2, l.block_depth_log2);
        backend_pool_written(t->addr, tegra_total_size(&t->layout));
    } else {
        tegra_swizzle_region(AP(SF2C_OBJECTS_N_3_4E07), (uint8_t *)(uintptr_t)t->addr + l.offset,
                             r[0], r[1], r[SF2C_OBJECTS_N_2_D473], r[SF2C_OBJECTS_N_3_4E07], r[SF2C_OBJECTS_N_4_4B22], r[SF2C_OBJECTS_N_5_EF2D] > 0 ? r[SF2C_OBJECTS_N_5_EF2D] : 1,
                             l.wb, l.hb, t->layout.fmt->bpb, l.block_height_log2, l.block_depth_log2);
        backend_pool_written(t->addr, tegra_total_size(&t->layout));
    }
}
NVN(nvnTextureWriteTexelsStrided) { LOG(SF2C_OBJECTS_S_NVN_WRITETEXELSSTRIDED_NOT_I_7C3E); (void)c; }
NVN(nvnTextureReadTexels) { LOG(SF2C_OBJECTS_S_NVN_READTEXELS_NOT_IMPLEMENT_FE6A); (void)c; }
NVN(nvnTextureReadTexelsStrided) { (void)c; }

// ================================================================== samplers
// SamplerBuilder: device@0 then an NvnSamplerDesc at 8
#define SD ((NvnSamplerDesc *)(AP(0) + SF2C_OBJECTS_N_8_2C62))
NVN(nvnSamplerBuilderSetDevice) { G(uint64_t, A(0), 0) = A(1); }
NVN(nvnSamplerBuilderSetDefaults) {
    NvnSamplerDesc *d = SD;
    memset(d, 0, sizeof *d);
    d->min_filter = 0; d->mag_filter = 0;
    d->wrap[0] = d->wrap[1] = d->wrap[SF2C_OBJECTS_N_2_D473] = 1;  // REPEAT
    d->lod_min = 0; d->lod_max = SF2C_OBJECTS_N_1000_0F_2FBA; d->max_aniso = SF2C_OBJECTS_N_1_0F_CA71;
    d->compare_func = SF2C_OBJECTS_N_4_4B22;
}
NVN(nvnSamplerBuilderSetMinMagFilter) { SD->min_filter = (uint32_t)A(1); SD->mag_filter = (uint32_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnSamplerBuilderSetWrapMode) { SD->wrap[0] = (uint32_t)A(1); SD->wrap[1] = (uint32_t)A(SF2C_OBJECTS_N_2_D473); SD->wrap[SF2C_OBJECTS_N_2_D473] = (uint32_t)A(SF2C_OBJECTS_N_3_4E07); }
NVN(nvnSamplerBuilderSetLodClamp) { SD->lod_min = AF(0); SD->lod_max = AF(1); }
NVN(nvnSamplerBuilderSetLodBias) { SD->lod_bias = AF(0); }
NVN(nvnSamplerBuilderSetCompare) { SD->compare_mode = (uint32_t)A(1); SD->compare_func = (uint32_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnSamplerBuilderSetBorderColor) { memcpy(SD->border, AP(1), SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderSetBorderColori) { memcpy(SD->border, AP(1), SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderSetBorderColorui) { memcpy(SD->border, AP(1), SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderSetMaxAnisotropy) { SD->max_aniso = AF(0); }
NVN(nvnSamplerBuilderSetReductionFilter) { SD->reduction = (uint32_t)A(1); }
NVN(nvnSamplerBuilderSetLodSnap) { (void)c; }
NVN(nvnSamplerBuilderGetDevice) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnSamplerBuilderGetMinMagFilter) { *(uint32_t *)AP(1) = SD->min_filter; *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473) = SD->mag_filter; }
NVN(nvnSamplerBuilderGetWrapMode) { for (int i = 0; i < SF2C_OBJECTS_N_3_4E07; i++) *(uint32_t *)AP(1 + i) = SD->wrap[i]; }
NVN(nvnSamplerBuilderGetLodClamp) { *(float *)AP(1) = SD->lod_min; *(float *)AP(SF2C_OBJECTS_N_2_D473) = SD->lod_max; }
NVN(nvnSamplerBuilderGetLodBias) { RETF(SD->lod_bias); }
NVN(nvnSamplerBuilderGetCompare) { *(uint32_t *)AP(1) = SD->compare_mode; *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473) = SD->compare_func; }
NVN(nvnSamplerBuilderGetBorderColor) { memcpy(AP(1), SD->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderGetBorderColori) { memcpy(AP(1), SD->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderGetBorderColorui) { memcpy(AP(1), SD->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerBuilderGetMaxAnisotropy) { RETF(SD->max_aniso); }
NVN(nvnSamplerBuilderGetReductionFilter) { RET(SD->reduction); }
NVN(nvnSamplerBuilderGetLodSnap) { RETF(0); }

NVN(nvnSamplerInitialize) {
    NvnSamplerDesc *d = malloc(sizeof *d);
    *d = *(NvnSamplerDesc *)(AP(1) + SF2C_OBJECTS_N_8_2C62);
    SET_HOST(A(0), d);
    RET(1);
}
NVN(nvnSamplerFinalize) { free(HOST(NvnSamplerDesc, A(0))); SET_HOST(A(0), 0); }
NVN(nvnSamplerSetDebugLabel) { (void)c; }
#define SMP HOST(NvnSamplerDesc, A(0))
NVN(nvnSamplerGetMinMagFilter) { *(uint32_t *)AP(1) = SMP->min_filter; *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473) = SMP->mag_filter; }
NVN(nvnSamplerGetWrapMode) { for (int i = 0; i < SF2C_OBJECTS_N_3_4E07; i++) *(uint32_t *)AP(1 + i) = SMP->wrap[i]; }
NVN(nvnSamplerGetLodClamp) { *(float *)AP(1) = SMP->lod_min; *(float *)AP(SF2C_OBJECTS_N_2_D473) = SMP->lod_max; }
NVN(nvnSamplerGetLodBias) { RETF(SMP->lod_bias); }
NVN(nvnSamplerGetCompare) { *(uint32_t *)AP(1) = SMP->compare_mode; *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473) = SMP->compare_func; }
NVN(nvnSamplerGetBorderColor) { memcpy(AP(1), SMP->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerGetBorderColori) { memcpy(AP(1), SMP->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerGetBorderColorui) { memcpy(AP(1), SMP->border, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnSamplerGetMaxAnisotropy) { RETF(SMP->max_aniso); }
NVN(nvnSamplerGetReductionFilter) { RET(SMP->reduction); }
NVN(nvnSamplerCompare) { RET(memcmp(HOST(NvnSamplerDesc, A(0)), HOST(NvnSamplerDesc, A(1)), sizeof(NvnSamplerDesc)) == 0); }
NVN(nvnSamplerGetDebugID) { RET(A(0)); }

// ================================================================== descriptor pools
NvnDescPool *g_texture_pools[SF2C_OBJECTS_N_64_A68B], *g_sampler_pools[SF2C_OBJECTS_N_64_A68B];
// Texture pool registrations (the game's threads) and the emptying of a finalized texture's slots (the render thread
// when it's on: nvn_texture_destroy) both change entries and pool_refs: this lock keeps them from crossing. (Reads by
// the translation take no lock: the game doesn't change a slot that queued work still uses before its fence, as on
// the Switch, and the emptying runs on the translating thread itself.)
static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
RT_ON_RESET(desc_pools_reset) { memset(g_texture_pools, 0, sizeof g_texture_pools); memset(g_sampler_pools, 0, sizeof g_sampler_pools); }

static NvnDescPool *desc_pool_init(uint64_t guest, uint64_t pool, uint64_t off, int count, size_t entry, NvnDescPool **reg) {
    NvnDescPool *d = calloc(1, sizeof *d);
    d->addr = HOST(NvnPool, pool)->storage + off;
    d->count = count;
    d->entries = calloc(count, entry);
    SET_HOST(guest, d);
    G(uint64_t, guest, SF2C_OBJECTS_N_8_2C62) = pool;
    G(uint64_t, guest, SF2C_OBJECTS_N_16_B17E) = off;
    G(uint32_t, guest, SF2C_OBJECTS_N_24_C235) = (uint32_t)count;
    pthread_mutex_lock(&g_pool_lock);  // (pool_entries_forget walks the registry)
    for (int i = 0; i < SF2C_OBJECTS_N_64_A68B; i++) if (!reg[i]) { reg[i] = d; break; }
    pthread_mutex_unlock(&g_pool_lock);
    return d;
}

NVN(nvnTexturePoolInitialize) {
    desc_pool_init(A(0), A(1), A(SF2C_OBJECTS_N_2_D473), (int)A(SF2C_OBJECTS_N_3_4E07), sizeof(NvnTexEntry), g_texture_pools);
    RET(1);
}
NVN(nvnTexturePoolFinalize) { (void)c; }
NVN(nvnTexturePoolSetDebugLabel) { (void)c; }
NVN(nvnTexturePoolGetMemoryPool) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnTexturePoolGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_16_B17E)); }
NVN(nvnTexturePoolGetSize) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_24_C235)); }
static void register_texture(Cpu *c) {
    NvnDescPool *d = HOST(NvnDescPool, A(0));
    int id = (int)A(1);
    if (id < 0 || id >= d->count) return;
    NvnTexEntry *e = &((NvnTexEntry *)d->entries)[id];
    pthread_mutex_lock(&g_pool_lock);
    if (e->tex && e->tex->pool_refs) e->tex->pool_refs--;
    e->tex = HOST(NvnTexture, A(SF2C_OBJECTS_N_2_D473));
    if (e->tex) e->tex->pool_refs++;
    e->dead_format = 0;
    e->has_view = A(SF2C_OBJECTS_N_3_4E07) != 0;
    if (e->has_view) memcpy(&e->view, AP(SF2C_OBJECTS_N_3_4E07), sizeof e->view);
    if (e->has_view) nvn_texture_note_view(e->tex, &e->view);
    pthread_mutex_unlock(&g_pool_lock);
}

// A finalized texture can stay registered: the game frees a room's textures on a level / room change and leaves
// their pool slots as they are until it registers something else there, and a draw can still name such a slot.
// Entries held its NvnTexture pointer, so the next texture allocated at the same address (another format) got the
// old entry's view: a BC4 / BC5 / BC1 texture viewed as RGBA8 or the reverse, a Metal assertion that stopped the
// game (about 1 in 6 long dungeon runs). Those entries are emptied (sampled as nothing, like an unregistered slot).
static void pool_entries_forget(NvnTexture *t) {
    pthread_mutex_lock(&g_pool_lock);
    if (!t->pool_refs) { pthread_mutex_unlock(&g_pool_lock); return; }
    int n = 0;
    for (int p = 0; p < SF2C_OBJECTS_N_64_A68B && t->pool_refs; p++) {
        NvnDescPool *d = g_texture_pools[p];
        if (!d) continue;
        NvnTexEntry *ents = (NvnTexEntry *)d->entries;
        for (int i = 0; i < d->count && t->pool_refs; i++)
            if (ents[i].tex == t) {
                ents[i].tex = NULL;
                ents[i].has_view = false;
                ents[i].dead_format = t->format + 1;
                t->pool_refs--;
                n++;
            }
    }
    pthread_mutex_unlock(&g_pool_lock);
    static int nlog;
    if (n && nlog++ < SF2C_OBJECTS_N_8_2C62)
        LOG(SF2C_OBJECTS_S_NVN_TEXTURE_UX_U_FMT_X_FINAL_F717, t->width, t->height,
            t->format, n, nlog == SF2C_OBJECTS_N_8_2C62 ? SF2C_OBJECTS_S_FURTHER_ONES_NOT_LOGGED_70FB : SF2C_OBJECTS_S_EMPTY_12AE);
}
void nvn_dead_slot_sampled(const NvnTexEntry *e, uint32_t slot) {
    static int nlog;
    if (nlog < SF2C_OBJECTS_N_8_2C62) {
        nlog++;
        LOG(SF2C_OBJECTS_S_NVN_DRAW_SAMPLES_TEXTURE_POO_A73F, slot,
            e->dead_format - 1, nlog == SF2C_OBJECTS_N_8_2C62 ? SF2C_OBJECTS_S_FURTHER_ONES_NOT_LOGGED_70FB : SF2C_OBJECTS_S_EMPTY_12AE);
    }
}
NVN(nvnTexturePoolRegisterTexture) { register_texture(c); }
NVN(nvnTexturePoolRegisterImage) { register_texture(c); }

NVN(nvnSamplerPoolInitialize) {
    desc_pool_init(A(0), A(1), A(SF2C_OBJECTS_N_2_D473), (int)A(SF2C_OBJECTS_N_3_4E07), sizeof(NvnSamplerDesc), g_sampler_pools);
    RET(1);
}
NVN(nvnSamplerPoolFinalize) { (void)c; }
NVN(nvnSamplerPoolSetDebugLabel) { (void)c; }
NVN(nvnSamplerPoolGetMemoryPool) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnSamplerPoolGetMemoryOffset) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_16_B17E)); }
NVN(nvnSamplerPoolGetSize) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_24_C235)); }
NVN(nvnSamplerPoolRegisterSampler) {
    NvnDescPool *d = HOST(NvnDescPool, A(0));
    int id = (int)A(1);
    if (id >= 0 && id < d->count) ((NvnSamplerDesc *)d->entries)[id] = *HOST(NvnSamplerDesc, A(SF2C_OBJECTS_N_2_D473));
}
NVN(nvnSamplerPoolRegisterSamplerBuilder) {
    NvnDescPool *d = HOST(NvnDescPool, A(0));
    int id = (int)A(1);
    if (id >= 0 && id < d->count) ((NvnSamplerDesc *)d->entries)[id] = *(NvnSamplerDesc *)(AP(SF2C_OBJECTS_N_2_D473) + SF2C_OBJECTS_N_8_2C62);
}

NvnDescPool *nvn_find_pool(NvnDescPool **reg, uint64_t guest_pool) {
    return guest_pool ? HOST(NvnDescPool, guest_pool) : NULL;
}

// ================================================================== programs
NVN(nvnProgramInitialize) { SET_HOST(A(0), calloc(1, sizeof(NvnProgram))); RET(1); }
NVN(nvnProgramFinalize) { free(HOST(NvnProgram, A(0))); SET_HOST(A(0), 0); }
NVN(nvnProgramSetDebugLabel) { (void)c; }
NVN(nvnProgramSetSampleShading) { (void)c; }
NVN(nvnProgramSetSubroutineLinkage) { RET(1); }
static void set_shaders(Cpu *c, int stride) {
    NvnProgram *p = HOST(NvnProgram, A(0));
    int n = (int)A(1);
    const uint8_t *data = AP(SF2C_OBJECTS_N_2_D473);  // NVNshaderData[n] = {u64 data (GPU addr), const void* control}
    memset(p->stages, 0, sizeof p->stages);
    p->stage_mask = 0;
    p->backend = NULL;
    for (int i = 0; i < n; i++) {
        uint64_t code = *(uint64_t *)(data + i * stride);
        const uint8_t *control = (const uint8_t *)(uintptr_t) * (uint64_t *)(data + i * stride + SF2C_OBJECTS_N_8_2C62);
        uint32_t sph0 = *(uint32_t *)(uintptr_t)(code + SF2C_OBJECTS_N_0X30_8BAD);
        uint32_t stage;
        switch ((sph0 >> SF2C_OBJECTS_N_10_4A44) & SF2C_OBJECTS_N_0XF_7419) {
        case 1: stage = 0; break;  // vertex
        case SF2C_OBJECTS_N_5_EF2D: stage = 1; break;  // fragment
        case SF2C_OBJECTS_N_4_4B22: stage = SF2C_OBJECTS_N_2_D473; break;  // geometry
        case SF2C_OBJECTS_N_2_D473: stage = SF2C_OBJECTS_N_3_4E07; break;  // tess control
        case SF2C_OBJECTS_N_3_4E07: stage = SF2C_OBJECTS_N_4_4B22; break;  // tess eval
        default: stage = SF2C_OBJECTS_N_5_EF2D; break; // compute (no SPH)
        }
        LOG(SF2C_OBJECTS_S_NVN_SET_SHADERS_P_P_I_D_D_CO_3296,
            p, i, n, (unsigned long long)code, control, sph0, stage);
        p->stages[stage].stage = stage;
        p->stages[stage].code_addr = code;
        p->stages[stage].control = control;
        p->stages[stage].const_offset = *(const uint32_t *)(control + SF2C_OBJECTS_N_0X700_5C68);
        p->stages[stage].const_size = *(const uint32_t *)(control + SF2C_OBJECTS_N_0X6FC_D425);
        p->stage_mask |= SF2C_OBJECTS_N_1U_B8E6 << stage;
    }
    LOG(SF2C_OBJECTS_S_NVN_SET_SHADERS_RESULT_P_P_S_FA00, p, p->stage_mask);
    RET(1);
}

NVN(nvnProgramSetShaders) { set_shaders(c, SF2C_OBJECTS_N_16_B17E); }    // NVNshaderData {data, control}
NVN(nvnProgramSetShadersExt) { set_shaders(c, SF2C_OBJECTS_N_24_C235); } // NVNshaderDataExt {data, control, debug hash}

// ================================================================== window
// WindowBuilder: device@0 native@8 num@0x10 textures@0x18 interval@0x20 active@0x24
NVN(nvnWindowBuilderSetDevice) { G(uint64_t, A(0), 0) = A(1); }
NVN(nvnWindowBuilderSetDefaults) { memset(AP(0) + SF2C_OBJECTS_N_8_2C62, 0, SF2C_OBJECTS_N_0X20_A766); G(uint32_t, A(0), SF2C_OBJECTS_N_0X20_A766) = 1; }
NVN(nvnWindowBuilderSetNativeWindow) { G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62) = A(1); }
NVN(nvnWindowBuilderSetTextures) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = (uint32_t)A(1); G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C) = A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnWindowBuilderSetPresentInterval) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X20_A766) = (uint32_t)A(1); }
NVN(nvnWindowBuilderSetNumActiveTextures) { G(uint32_t, A(0), SF2C_OBJECTS_N_0X24_EB21) = (uint32_t)A(1); }
NVN(nvnWindowBuilderGetDevice) { RET(G(uint64_t, A(0), 0)); }
NVN(nvnWindowBuilderGetNativeWindow) { RET(G(uint64_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnWindowBuilderGetNumTextures) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnWindowBuilderGetTexture) {
    int i = (int)A(1);
    RET(i < (int)G(uint32_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) ? ((uint64_t *)(uintptr_t)G(uint64_t, A(0), SF2C_OBJECTS_N_0X18_D47C))[i] : 0);
}
NVN(nvnWindowBuilderGetPresentInterval) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_0X20_A766)); }
NVN(nvnWindowBuilderGetNumActiveTextures) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_0X24_EB21)); }

NVN(nvnWindowInitialize) {
    NvnWindow *w = calloc(1, sizeof *w);
    w->num = (int)G(uint32_t, A(1), SF2C_OBJECTS_N_0X10_BF9C);
    if (w->num > SF2C_OBJECTS_N_8_2C62) w->num = SF2C_OBJECTS_N_8_2C62;
    memcpy(w->textures, (void *)(uintptr_t)G(uint64_t, A(1), SF2C_OBJECTS_N_0X18_D47C), w->num * SF2C_OBJECTS_N_8_2C62);
    w->interval = (int)G(uint32_t, A(1), SF2C_OBJECTS_N_0X20_A766);
    w->active = G(uint32_t, A(1), SF2C_OBJECTS_N_0X24_EB21) ? (int)G(uint32_t, A(1), SF2C_OBJECTS_N_0X24_EB21) : w->num;
    SET_HOST(A(0), w);
    LOG(SF2C_OBJECTS_S_NVN_WINDOW_WITH_D_TEXTURES_N_E49B, w->num);
    RET(1);
}
NVN(nvnWindowFinalize) { (void)c; }
NVN(nvnWindowSetDebugLabel) { (void)c; }
#define WIN HOST(NvnWindow, A(0))
NVN(nvnWindowGetNativeWindow) { RET(0); }
NVN(nvnWindowGetNumTextures) { RET(WIN->num); }
NVN(nvnWindowGetNumActiveTextures) { RET(WIN->active); }
NVN(nvnWindowSetNumActiveTextures) { WIN->active = (int)A(1); }
NVN(nvnWindowGetPresentInterval) { RET(WIN->interval); }
NVN(nvnWindowSetPresentInterval) {  // (the game's 60 / 30 fps mode: 1 / 2; LARECOMP_LOG_INTERVAL=1 logs each change)
    static int log = -1;
    if (log < 0) log = getenv(SF2C_OBJECTS_S_LARECOMP_LOG_INTERVAL_D6E7) != NULL;
    if (log && WIN->interval != (int)A(1)) LOG(SF2C_OBJECTS_S_NVN_PRESENT_INTERVAL_D_D_N_0899, WIN->interval, (int)A(1));
    WIN->interval = (int)A(1);
}
NVN(nvnWindowSetCrop) { int *cr = WIN->crop; cr[0] = (int)A(1); cr[1] = (int)A(SF2C_OBJECTS_N_2_D473); cr[SF2C_OBJECTS_N_2_D473] = (int)A(SF2C_OBJECTS_N_3_4E07); cr[SF2C_OBJECTS_N_3_4E07] = (int)A(SF2C_OBJECTS_N_4_4B22); }
NVN(nvnWindowGetCrop) { memcpy(AP(1), WIN->crop, SF2C_OBJECTS_N_16_B17E); }
NVN(nvnWindowAcquireTexture) {
    NvnWindow *w = WIN;
    static int nacq;
    if (nacq++ < SF2C_OBJECTS_N_10_4A44) LOG(SF2C_OBJECTS_S_NVN_WINDOWACQUIRETEXTURE_W_P_3718, w, w ? w->next : -1);
    if (A(1)) sync_of(A(1))->signaled = 1;
    *(int32_t *)AP(SF2C_OBJECTS_N_2_D473) = w->next;
    w->next = (w->next + 1) % (w->active ? w->active : 1);
    RET(0);  // NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS
}
NVN(nvnQueueAcquireTexture) {
    NvnWindow *w = HOST(NvnWindow, A(1));
    *(int32_t *)AP(SF2C_OBJECTS_N_2_D473) = w->next;
    w->next = (w->next + 1) % (w->active ? w->active : 1);
    RET(0);
}

static void window_wait_present(NvnWindow *w) {
    uint32_t hz = g_settings.target_fps >= SF2C_OBJECTS_N_30_624B && g_settings.target_fps <= SF2C_OBJECTS_N_360_838F ?
                  (uint32_t)g_settings.target_fps : SF2C_OBJECTS_N_60_39FA;
    // A zero interval disables vsync, but the configured FPS cap still applies.
    uint32_t interval = w->interval > 0 ? (uint32_t)w->interval : 1;
    uint64_t period = SF2C_OBJECTS_N_1000000000ULL_E860 * interval / hz;
    if (period != w->present_period) {
        w->present_period = period;
        w->present_deadline = 0;
        LOG(SF2C_OBJECTS_S_PACE_NVN_INTERVAL_D_VIRTUAL__2325,
            w->interval, hz, (double)hz / interval);
    }
    uint64_t now = plat_now_ns();
    if (w->present_deadline > now) {
        plat_sleep_until_ns(w->present_deadline);
        now = plat_now_ns();
    }
    // Rebase on the actual present after a late/slow frame: never burst frames
    // to catch up after loading, a debugger pause, or restoring the window.
    w->present_deadline = now + period;
}

NVN(nvnQueuePresentTexture) {
    NvnWindow *w = HOST(NvnWindow, A(1));
    int i = (int)A(SF2C_OBJECTS_N_2_D473);
    static int npresent;
    npresent++;
    if (npresent <= SF2C_OBJECTS_N_5_EF2D || (npresent % SF2C_OBJECTS_N_600_284B) == 0 || (getenv(SF2C_OBJECTS_S_SF2_TRACE_RENDER_5E88) && (npresent % SF2C_OBJECTS_N_10_4A44) == 0)) {
        extern const char *renderer_stats(void);
        LOG(SF2C_OBJECTS_S_NVN_QUEUEPRESENTTEXTURE_FRAM_6399, npresent, i, renderer_stats());
    }
    if (w && i >= 0 && i < w->num) {
        window_wait_present(w);
        backend_present(HOST(NvnTexture, w->textures[i]));
    }
}

// ================================================================== small state objects
// BlendState (8): target, srcC, dstC, srcA, dstA, eqC, eqA, advanced
NVN(nvnBlendStateSetDefaults) {
    uint8_t *b = AP(0);
    b[0] = 0; b[1] = SF2C_OBJECTS_N_2_D473; b[SF2C_OBJECTS_N_2_D473] = 1; b[SF2C_OBJECTS_N_3_4E07] = SF2C_OBJECTS_N_2_D473; b[SF2C_OBJECTS_N_4_4B22] = 1; b[SF2C_OBJECTS_N_5_EF2D] = 1; b[SF2C_OBJECTS_N_6_E7F6] = 1; b[SF2C_OBJECTS_N_7_7902] = 0;
}
NVN(nvnBlendStateSetBlendTarget) { G(uint8_t, A(0), 0) = (uint8_t)A(1); }
NVN(nvnBlendStateSetBlendFunc) {
    uint8_t *b = AP(0);
    b[1] = (uint8_t)A(1); b[SF2C_OBJECTS_N_2_D473] = (uint8_t)A(SF2C_OBJECTS_N_2_D473); b[SF2C_OBJECTS_N_3_4E07] = (uint8_t)A(SF2C_OBJECTS_N_3_4E07); b[SF2C_OBJECTS_N_4_4B22] = (uint8_t)A(SF2C_OBJECTS_N_4_4B22);
}
NVN(nvnBlendStateSetBlendEquation) { G(uint8_t, A(0), SF2C_OBJECTS_N_5_EF2D) = (uint8_t)A(1); G(uint8_t, A(0), SF2C_OBJECTS_N_6_E7F6) = (uint8_t)A(SF2C_OBJECTS_N_2_D473); }
NVN(nvnBlendStateSetAdvancedMode) { G(uint8_t, A(0), SF2C_OBJECTS_N_7_7902) = (uint8_t)A(1); }
NVN(nvnBlendStateSetAdvancedOverlap) { (void)c; }
NVN(nvnBlendStateSetAdvancedPremultipliedSrc) { (void)c; }
NVN(nvnBlendStateSetAdvancedNormalizedDst) { (void)c; }
NVN(nvnBlendStateGetBlendTarget) { RET(G(uint8_t, A(0), 0)); }
NVN(nvnBlendStateGetBlendFunc) {
    uint8_t *b = AP(0);
    for (int i = 0; i < SF2C_OBJECTS_N_4_4B22; i++) *(uint32_t *)AP(1 + i) = b[1 + i];
}
NVN(nvnBlendStateGetBlendEquation) { *(uint32_t *)AP(1) = G(uint8_t, A(0), SF2C_OBJECTS_N_5_EF2D); *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473) = G(uint8_t, A(0), SF2C_OBJECTS_N_6_E7F6); }
NVN(nvnBlendStateGetAdvancedMode) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_7_7902)); }
NVN(nvnBlendStateGetAdvancedOverlap) { RET(0); }
NVN(nvnBlendStateGetAdvancedPremultipliedSrc) { RET(1); }
NVN(nvnBlendStateGetAdvancedNormalizedDst) { RET(1); }

// ChannelMaskState (4): 4 bits per target
NVN(nvnChannelMaskStateSetDefaults) { G(uint32_t, A(0), 0) = SF2C_OBJECTS_N_0XFFFFFFFF_BEFA; }
NVN(nvnChannelMaskStateSetChannelMask) {
    int t = (int)A(1);
    uint32_t m = (A(SF2C_OBJECTS_N_2_D473) ? 1 : 0) | (A(SF2C_OBJECTS_N_3_4E07) ? SF2C_OBJECTS_N_2_D473 : 0) | (A(SF2C_OBJECTS_N_4_4B22) ? SF2C_OBJECTS_N_4_4B22 : 0) | (A(SF2C_OBJECTS_N_5_EF2D) ? SF2C_OBJECTS_N_8_2C62 : 0);
    G(uint32_t, A(0), 0) = (G(uint32_t, A(0), 0) & ~(SF2C_OBJECTS_N_0XFU_6358 << (t * SF2C_OBJECTS_N_4_4B22))) | (m << (t * SF2C_OBJECTS_N_4_4B22));
}
NVN(nvnChannelMaskStateGetChannelMask) {
    uint32_t m = G(uint32_t, A(0), 0) >> ((int)A(1) * SF2C_OBJECTS_N_4_4B22);
    for (int i = 0; i < SF2C_OBJECTS_N_4_4B22; i++) *(uint8_t *)AP(SF2C_OBJECTS_N_2_D473 + i) = (m >> i) & 1;
}

// ColorState (4): blend enable mask, logic op, alpha test
NVN(nvnColorStateSetDefaults) { G(uint8_t, A(0), 0) = 0; G(uint8_t, A(0), 1) = SF2C_OBJECTS_N_3_4E07; G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473) = SF2C_OBJECTS_N_8_2C62; }
NVN(nvnColorStateSetBlendEnable) {
    uint8_t m = G(uint8_t, A(0), 0), bit = (uint8_t)(SF2C_OBJECTS_N_1U_B8E6 << A(1));
    G(uint8_t, A(0), 0) = A(SF2C_OBJECTS_N_2_D473) ? (m | bit) : (m & ~bit);
}
NVN(nvnColorStateSetLogicOp) { G(uint8_t, A(0), 1) = (uint8_t)A(1); }
NVN(nvnColorStateSetAlphaTest) { G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473) = (uint8_t)A(1); }
NVN(nvnColorStateGetBlendEnable) { RET((G(uint8_t, A(0), 0) >> A(1)) & 1); }
NVN(nvnColorStateGetLogicOp) { RET(G(uint8_t, A(0), 1)); }
NVN(nvnColorStateGetAlphaTest) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473)); }

// DepthStencilState (8): [0]=depthTest [1]=depthWrite [2]=depthFunc [3]=stencilTest
//   [4]=front func|back func<<4 [5..7]=ops (front fail,zfail,zpass / back) in 4-bit fields
NVN(nvnDepthStencilStateSetDefaults) {
    uint8_t *d = AP(0);
    d[0] = 0; d[1] = 1; d[SF2C_OBJECTS_N_2_D473] = SF2C_OBJECTS_N_2_D473; d[SF2C_OBJECTS_N_3_4E07] = 0; d[SF2C_OBJECTS_N_4_4B22] = SF2C_OBJECTS_N_0X88_AC70;
    d[SF2C_OBJECTS_N_5_EF2D] = SF2C_OBJECTS_N_0X11_A3D6; d[SF2C_OBJECTS_N_6_E7F6] = SF2C_OBJECTS_N_0X11_A3D6; d[SF2C_OBJECTS_N_7_7902] = SF2C_OBJECTS_N_0X11_A3D6;
}
NVN(nvnDepthStencilStateSetDepthTestEnable) { G(uint8_t, A(0), 0) = (uint8_t)A(1); }
NVN(nvnDepthStencilStateSetDepthWriteEnable) { G(uint8_t, A(0), 1) = (uint8_t)A(1); }
NVN(nvnDepthStencilStateSetDepthFunc) { G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473) = (uint8_t)A(1); }
NVN(nvnDepthStencilStateSetStencilTestEnable) { G(uint8_t, A(0), SF2C_OBJECTS_N_3_4E07) = (uint8_t)A(1); }
NVN(nvnDepthStencilStateSetStencilFunc) {
    uint8_t *d = AP(0);
    int face = (int)A(1);  // 1 front, 2 back, 3 both
    if (face & 1) d[SF2C_OBJECTS_N_4_4B22] = (d[SF2C_OBJECTS_N_4_4B22] & SF2C_OBJECTS_N_0XF0_EB46) | (A(SF2C_OBJECTS_N_2_D473) & SF2C_OBJECTS_N_0XF_7419);
    if (face & SF2C_OBJECTS_N_2_D473) d[SF2C_OBJECTS_N_4_4B22] = (d[SF2C_OBJECTS_N_4_4B22] & SF2C_OBJECTS_N_0X0F_1F1D) | (uint8_t)((A(SF2C_OBJECTS_N_2_D473) & SF2C_OBJECTS_N_0XF_7419) << SF2C_OBJECTS_N_4_4B22);
}
NVN(nvnDepthStencilStateSetStencilOp) {
    uint8_t *d = AP(0);
    int face = (int)A(1);
    uint32_t ops = (uint32_t)((A(SF2C_OBJECTS_N_2_D473) & SF2C_OBJECTS_N_0XF_7419) | (A(SF2C_OBJECTS_N_3_4E07) & SF2C_OBJECTS_N_0XF_7419) << SF2C_OBJECTS_N_4_4B22 | (A(SF2C_OBJECTS_N_4_4B22) & SF2C_OBJECTS_N_0XF_7419) << SF2C_OBJECTS_N_8_2C62);
    uint32_t packed = d[SF2C_OBJECTS_N_5_EF2D] | d[SF2C_OBJECTS_N_6_E7F6] << SF2C_OBJECTS_N_8_2C62 | d[SF2C_OBJECTS_N_7_7902] << SF2C_OBJECTS_N_16_B17E;
    if (face & 1) packed = (packed & ~SF2C_OBJECTS_N_0XFFFU_0B3F) | ops;
    if (face & SF2C_OBJECTS_N_2_D473) packed = (packed & SF2C_OBJECTS_N_0XFFF_1DF4) | ops << SF2C_OBJECTS_N_12_6B51;
    d[SF2C_OBJECTS_N_5_EF2D] = (uint8_t)packed; d[SF2C_OBJECTS_N_6_E7F6] = (uint8_t)(packed >> SF2C_OBJECTS_N_8_2C62); d[SF2C_OBJECTS_N_7_7902] = (uint8_t)(packed >> SF2C_OBJECTS_N_16_B17E);
}
NVN(nvnDepthStencilStateGetDepthTestEnable) { RET(G(uint8_t, A(0), 0)); }
NVN(nvnDepthStencilStateGetDepthWriteEnable) { RET(G(uint8_t, A(0), 1)); }
NVN(nvnDepthStencilStateGetDepthFunc) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473)); }
NVN(nvnDepthStencilStateGetStencilTestEnable) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_3_4E07)); }
NVN(nvnDepthStencilStateGetStencilFunc) { RET((A(1) & SF2C_OBJECTS_N_2_D473) ? G(uint8_t, A(0), SF2C_OBJECTS_N_4_4B22) >> SF2C_OBJECTS_N_4_4B22 : G(uint8_t, A(0), SF2C_OBJECTS_N_4_4B22) & SF2C_OBJECTS_N_0XF_7419); }
NVN(nvnDepthStencilStateGetStencilOp) {
    uint8_t *d = AP(0);
    uint32_t packed = d[SF2C_OBJECTS_N_5_EF2D] | d[SF2C_OBJECTS_N_6_E7F6] << SF2C_OBJECTS_N_8_2C62 | d[SF2C_OBJECTS_N_7_7902] << SF2C_OBJECTS_N_16_B17E;
    if (A(1) & SF2C_OBJECTS_N_2_D473) packed >>= SF2C_OBJECTS_N_12_6B51;
    for (int i = 0; i < SF2C_OBJECTS_N_3_4E07; i++) *(uint32_t *)AP(SF2C_OBJECTS_N_2_D473 + i) = (packed >> (SF2C_OBJECTS_N_4_4B22 * i)) & SF2C_OBJECTS_N_0XF_7419;
}

// MultisampleState (0x18): enable@0 samples@4 raster@8 a2c@0xC a2cdither@0xD cov2color@0xE output@0xF
NVN(nvnMultisampleStateSetDefaults) { memset(AP(0), 0, SF2C_OBJECTS_N_0X18_D47C); G(uint8_t, A(0), 0) = 1; G(uint8_t, A(0), SF2C_OBJECTS_N_0XD_4003) = 1; }
NVN(nvnMultisampleStateSetMultisampleEnable) { G(uint8_t, A(0), 0) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetSamples) { G(uint32_t, A(0), SF2C_OBJECTS_N_4_4B22) = (uint32_t)A(1); }
NVN(nvnMultisampleStateSetRasterSamples) { G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62) = (uint32_t)A(1); }
NVN(nvnMultisampleStateSetAlphaToCoverageEnable) { G(uint8_t, A(0), SF2C_OBJECTS_N_0XC_D882) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetAlphaToCoverageDither) { G(uint8_t, A(0), SF2C_OBJECTS_N_0XD_4003) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetCoverageToColorEnable) { G(uint8_t, A(0), SF2C_OBJECTS_N_0XE_6A51) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetCoverageToColorOutput) { G(uint8_t, A(0), SF2C_OBJECTS_N_0XF_7419) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetCoverageModulationMode) { G(uint8_t, A(0), SF2C_OBJECTS_N_0X10_BF9C) = (uint8_t)A(1); }
NVN(nvnMultisampleStateSetSampleLocations) { (void)c; }
NVN(nvnMultisampleStateSetSampleLocationsEnable) { (void)c; }
NVN(nvnMultisampleStateSetSampleLocationsGridEnable) { (void)c; }
NVN(nvnMultisampleStateGetMultisampleEnable) { RET(G(uint8_t, A(0), 0)); }
NVN(nvnMultisampleStateGetSamples) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_4_4B22)); }
NVN(nvnMultisampleStateGetRasterSamples) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_8_2C62)); }
NVN(nvnMultisampleStateGetAlphaToCoverageEnable) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_0XC_D882)); }
NVN(nvnMultisampleStateGetAlphaToCoverageDither) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_0XD_4003)); }
NVN(nvnMultisampleStateGetCoverageToColorEnable) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_0XE_6A51)); }
NVN(nvnMultisampleStateGetCoverageToColorOutput) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_0XF_7419)); }
NVN(nvnMultisampleStateGetCoverageModulationMode) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_0X10_BF9C)); }
NVN(nvnMultisampleStateGetSampleLocationsEnable) { RET(0); }
NVN(nvnMultisampleStateGetSampleLocationsGrid) { (void)c; }
NVN(nvnMultisampleStateGetSampleLocationsGridEnable) { RET(0); }

// PolygonState (4): cull, front face, polygon mode, offset enables
NVN(nvnPolygonStateSetDefaults) { uint8_t *p = AP(0); p[0] = 0; p[1] = 1; p[SF2C_OBJECTS_N_2_D473] = SF2C_OBJECTS_N_2_D473; p[SF2C_OBJECTS_N_3_4E07] = 0; }
NVN(nvnPolygonStateSetCullFace) { G(uint8_t, A(0), 0) = (uint8_t)A(1); }
NVN(nvnPolygonStateSetFrontFace) { G(uint8_t, A(0), 1) = (uint8_t)A(1); }
NVN(nvnPolygonStateSetPolygonMode) { G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473) = (uint8_t)A(1); }
NVN(nvnPolygonStateSetPolygonOffsetEnables) { G(uint8_t, A(0), SF2C_OBJECTS_N_3_4E07) = (uint8_t)A(1); }
NVN(nvnPolygonStateGetCullFace) { RET(G(uint8_t, A(0), 0)); }
NVN(nvnPolygonStateGetFrontFace) { RET(G(uint8_t, A(0), 1)); }
NVN(nvnPolygonStateGetPolygonMode) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_2_D473)); }
NVN(nvnPolygonStateGetPolygonOffsetEnables) { RET(G(uint8_t, A(0), SF2C_OBJECTS_N_3_4E07)); }

// VertexAttribState (4): format bits 0-7, stream bits 8-12, relative offset bits 13-31
NVN(nvnVertexAttribStateSetDefaults) { G(uint32_t, A(0), 0) = 0; }
NVN(nvnVertexAttribStateSetFormat) {
    uint32_t v = G(uint32_t, A(0), 0);
    G(uint32_t, A(0), 0) = (v & SF2C_OBJECTS_N_0X1F00_C2B7) | (uint32_t)(A(1) & SF2C_OBJECTS_N_0XFF_7312) | (uint32_t)(A(SF2C_OBJECTS_N_2_D473) << SF2C_OBJECTS_N_13_3FDB);
}
NVN(nvnVertexAttribStateSetStreamIndex) {
    G(uint32_t, A(0), 0) = (G(uint32_t, A(0), 0) & ~SF2C_OBJECTS_N_0X1F00U_B53D) | (uint32_t)((A(1) & SF2C_OBJECTS_N_0X1F_A571) << SF2C_OBJECTS_N_8_2C62);
}
NVN(nvnVertexAttribStateGetFormat) {
    uint32_t v = G(uint32_t, A(0), 0);
    *(uint32_t *)AP(1) = v & SF2C_OBJECTS_N_0XFF_7312;
    *(int64_t *)AP(SF2C_OBJECTS_N_2_D473) = v >> SF2C_OBJECTS_N_13_3FDB;
}
NVN(nvnVertexAttribStateGetStreamIndex) { RET((G(uint32_t, A(0), 0) >> SF2C_OBJECTS_N_8_2C62) & SF2C_OBJECTS_N_0X1F_A571); }

// VertexStreamState (8): stride u32, divisor u32
NVN(nvnVertexStreamStateSetDefaults) { G(uint64_t, A(0), 0) = 0; }
NVN(nvnVertexStreamStateSetStride) { G(uint32_t, A(0), 0) = (uint32_t)A(1); }
NVN(nvnVertexStreamStateSetDivisor) { G(uint32_t, A(0), SF2C_OBJECTS_N_4_4B22) = (uint32_t)A(1); }
NVN(nvnVertexStreamStateGetStride) { RET(G(uint32_t, A(0), 0)); }
NVN(nvnVertexStreamStateGetDivisor) { RET(G(uint32_t, A(0), SF2C_OBJECTS_N_4_4B22)); }

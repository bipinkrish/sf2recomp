// Exercise actual recording, finalization and render-queue ownership without a GPU.
#include <assert.h>
#include <stdatomic.h>
#include <time.h>
#include "nvn/nvn.h"
#undef NVN
#define NVN(name) static void hle_##name(Cpu *c)
#undef RT_ON_RESET
#define RT_ON_RESET(name) static void name(void)
#include "nvn/objects.c"
#include "nvn/cmdbuf.c"
#include "nvn/command_lifetime.c"
#include "nvn/rqueue.c"
#include "nvn/tegra.c"
#include "nvn/formats.c"

volatile int g_rt_unwind;
__thread uint64_t g_blocked_ns;
static unsigned destroyed, uploads;
static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cv = PTHREAD_COND_INITIALIZER;
static bool gate, entered;

void rt_fatal_msg(const char *fmt, ...) { (void)fmt; abort(); }
void rt_fatal_trace(void) { abort(); }
void guest_call(Cpu *c, uint64_t addr) { (void)c; (void)addr; abort(); }
void backend_texture_view_format(NvnTexture *t, uint32_t fmt) { (void)t; (void)fmt; }
void backend_texture_destroyed(NvnTexture *t) { assert(t->refs == 0); ++destroyed; }
void backend_present_now(NvnTexture *t, uint64_t f, bool a) { (void)t; (void)f; (void)a; }
void backend_upload_now(NvnTexture *t, int k, void *d, uint32_t w, uint32_t h, uint32_t s, uint32_t u) {
    (void)t; (void)k; (void)d; (void)w; (void)h; (void)s; (void)u;
}
void *backend_pool_push(void) { return NULL; }
void backend_pool_pop(void *p) { (void)p; }
void renderer_flush(void) {}
uint64_t renderer_issued_seq(void) { return 0; }
uint64_t renderer_submit_seq(void) { return 0; }
uint64_t renderer_present_count(void) { return 0; }
void plat_thread_guest(int p) { (void)p; }
int plat_host_thread_id(void) { return 1; }
uint64_t kernel_current_thread_id(void) { return 0; }
void kernel_hang_hooks(bool (*check)(uint64_t, char *, size_t), void (*dump)(const char *)) { (void)check; (void)dump; }
bool plat_bt_other_thread_tid(int t, const char *p) { (void)t; (void)p; return true; }
uint64_t plat_now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
uint64_t plat_thread_cpu_ns(void) { return plat_now_ns(); }

static void check_texture(uint64_t p) {
    if (!p) return;
    NvnTexture *t = (NvnTexture *)(uintptr_t)p;
    assert(t->width == 32 && t->height == 32 && t->format == 0x25 && t->refs > 0);
}
static void execute_records(uint64_t handle) {
    const CmdRec *r = (const CmdRec *)(uintptr_t)handle;
    for (;;) {
        if (r->op == CMD_END) return;
        if (r->op == CMD_JUMP) { r = (const CmdRec *)(uintptr_t)r->x[0]; continue; }
        if (r->op == CMD_COPY_BUFFER_TO_TEXTURE) { check_texture(((const TexRef *)r->extra)->tex); ++uploads; }
        if (r->op == CMD_COPY_TEXTURE_TO_BUFFER) check_texture(((const TexRef *)r->extra)->tex);
        if (r->op == CMD_COPY_TEXTURE_TO_TEXTURE || r->op == CMD_DOWNSAMPLE) {
            check_texture(((const TexRef *)r->extra)[0].tex);
            check_texture(((const TexRef *)r->extra)[1].tex);
        }
        if (r->op == CMD_CLEAR_TEXTURE) check_texture(((const ClearTexRec *)r->extra)->tex);
        if (r->op == CMD_SET_RENDER_TARGETS) {
            const RtRec *rt = (const RtRec *)r->extra;
            for (unsigned i = 0; i < r->x[0]; ++i) check_texture(rt->tex[i]);
            check_texture(rt->depth);
        }
        if (r->op == CMD_CALL) {
            const uint64_t *calls = (const uint64_t *)r->extra;
            for (unsigned i = 0; i < r->x[0]; ++i) execute_records(calls[i]);
        }
        r = (const CmdRec *)((const char *)r + r->size);
    }
}
void backend_execute(uint64_t handle) {
    pthread_mutex_lock(&test_lock);
    entered = true; pthread_cond_broadcast(&test_cv);
    while (gate) pthread_cond_wait(&test_cv, &test_lock);
    pthread_mutex_unlock(&test_lock);
    execute_records(handle);
}

static NvnTexture *texture(void) {
    uint64_t builder[16] = {0};
    G(uint32_t, (uintptr_t)builder, 0xc) = 1;
    G(uint32_t, (uintptr_t)builder, 0x10) = 32;
    G(uint32_t, (uintptr_t)builder, 0x14) = 32;
    G(uint32_t, (uintptr_t)builder, 0x18) = 1;
    G(uint32_t, (uintptr_t)builder, 0x1c) = 1;
    G(uint32_t, (uintptr_t)builder, 0x20) = 0x25;
    return texture_from_builder((uintptr_t)builder);
}
static void finalize_texture(NvnTexture *t) {
    uint64_t guest = (uintptr_t)t;
    Cpu c = {0}; c.x[0] = (uintptr_t)&guest;
    hle_nvnTextureFinalize(&c);
    assert(!guest); rq_drain();
}
static void begin(NvnCmdBuf *cb, void *memory) {
    *cb = (NvnCmdBuf){.mem = (uintptr_t)memory, .size = 65536};
    uint64_t guest = (uintptr_t)cb;
    Cpu c = {0}; c.x[0] = (uintptr_t)&guest;
    hle_nvnCommandBufferBeginRecording(&c);
}
static uint64_t end(NvnCmdBuf *cb) {
    uint64_t guest = (uintptr_t)cb;
    Cpu c = {0}; c.x[0] = (uintptr_t)&guest;
    hle_nvnCommandBufferEndRecording(&c);
    return c.x[0];
}
static void upload(NvnCmdBuf *cb, NvnTexture *t) {
    uint64_t guest_cb = (uintptr_t)cb, guest_tex = (uintptr_t)t;
    Cpu c = {0}; c.x[0] = (uintptr_t)&guest_cb; c.x[2] = (uintptr_t)&guest_tex;
    hle_nvnCommandBufferCopyBufferToTexture(&c);
}
static void submit(uint64_t handle) {
    Cpu c = {0}; c.x[1] = 1; c.x[2] = (uintptr_t)&handle;
    hle_nvnQueueSubmitCommands(&c);
}
static void finalize_handle(uint64_t handle) {
    Cpu c = {0}; c.x[1] = handle;
    hle_nvnDeviceFinalizeCommandHandle(&c);
}

int main(void) {
    void *memory = calloc(1, 65536), *parent_memory = calloc(1, 65536);
    NvnCmdBuf cb, parent;
    NvnTexture *t = texture();
    begin(&cb, memory); upload(&cb, t); upload(&cb, t);
    assert(t->refs == 2); // one owner per list, regardless of repeated references
    NvnTexEntry entry = {.tex = t};
    NvnDescPool pool = {.entries = &entry, .count = 1};
    g_texture_pools[0] = &pool; t->pool_refs = 1;
    finalize_texture(t); // destruction before this recorded upload is submitted
    assert(!destroyed && !entry.tex && entry.dead_format == 0x26 && t->refs == 1);
    g_texture_pools[0] = NULL;
    uint64_t handle = end(&cb);
    submit(handle); rq_drain(); submit(handle); rq_drain();
    assert(uploads == 4 && !destroyed); // reusable handle still owns its texture
    if (rq_on()) {
        pthread_mutex_lock(&test_lock); gate = true; entered = false; pthread_mutex_unlock(&test_lock);
        submit(handle);
        pthread_mutex_lock(&test_lock);
        while (!entered) pthread_cond_wait(&test_cv, &test_lock);
        pthread_mutex_unlock(&test_lock);
        finalize_handle(handle); assert(!destroyed);
        pthread_mutex_lock(&test_lock); gate = false; pthread_cond_broadcast(&test_cv); pthread_mutex_unlock(&test_lock);
    } else finalize_handle(handle);
    rq_drain(); assert(destroyed == 1);

    // A parent list owns its called lists even if their handles are finalized.
    t = texture(); begin(&cb, memory); upload(&cb, t); handle = end(&cb);
    begin(&parent, parent_memory);
    uint64_t guest_parent = (uintptr_t)&parent;
    Cpu c = {0}; c.x[0] = (uintptr_t)&guest_parent; c.x[1] = 1; c.x[2] = (uintptr_t)&handle;
    hle_nvnCommandBufferCallCommands(&c); hle_nvnCommandBufferCopyCommands(&c);
    uint64_t parent_handle = end(&parent);
    finalize_texture(t); finalize_handle(handle);
    submit(parent_handle); rq_drain(); assert(destroyed == 1);
    finalize_handle(parent_handle); rq_drain(); assert(destroyed == 2);

    // Reusing a command address retires its old resources without leaking them.
    t = texture(); begin(&cb, memory); upload(&cb, t); handle = end(&cb);
    finalize_texture(t);
    begin(&cb, memory); uint64_t replacement = end(&cb);
    rq_drain(); assert(handle == replacement && destroyed == 3);
    finalize_handle(replacement); finalize_handle(replacement);

    // Abandoning an unfinished command buffer releases captured resources too.
    t = texture(); NvnCmdBuf *unfinished = calloc(1, sizeof *unfinished);
    begin(unfinished, memory); upload(unfinished, t); finalize_texture(t);
    uint64_t guest_unfinished = (uintptr_t)unfinished;
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_unfinished;
    hle_nvnCommandBufferFinalize(&c); rq_drain(); assert(destroyed == 4);

    // Every command form that captures a texture must own both source and destination.
    NvnTexture *src = texture(), *dst = texture();
    begin(&cb, memory);
    uint64_t guest_cb = (uintptr_t)&cb, guest_src = (uintptr_t)src, guest_dst = (uintptr_t)dst;
    uint64_t colors[] = {(uintptr_t)&guest_dst};
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = 1;
    c.x[2] = (uintptr_t)colors; c.x[4] = (uintptr_t)&guest_src;
    hle_nvnCommandBufferSetRenderTargets(&c);
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = (uintptr_t)&guest_dst;
    hle_nvnCommandBufferClearTexture(&c);
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = (uintptr_t)&guest_src;
    hle_nvnCommandBufferCopyTextureToBuffer(&c);
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = (uintptr_t)&guest_src; c.x[4] = (uintptr_t)&guest_dst;
    hle_nvnCommandBufferCopyTextureToTexture(&c);
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = (uintptr_t)&guest_src; c.x[2] = (uintptr_t)&guest_dst;
    hle_nvnCommandBufferDownsample(&c);
    c = (Cpu){0}; c.x[0] = (uintptr_t)&guest_cb; c.x[1] = (uintptr_t)&guest_src; c.x[3] = (uintptr_t)&guest_dst;
    hle_nvnCommandBufferDownsampleTextureView(&c);
    assert(src->refs == 2 && dst->refs == 2);
    handle = end(&cb); finalize_texture(src); finalize_texture(dst);
    submit(handle); rq_drain(); assert(destroyed == 4);
    finalize_handle(handle); rq_drain(); assert(destroyed == 6);

    // Reset drops any remaining handle ownership once translations have drained.
    t = texture(); begin(&cb, memory); upload(&cb, t); end(&cb); finalize_texture(t);
    command_handles_reset();
    rq_drain(); assert(destroyed == 7);
    free(memory); free(parent_memory);
    rq_stop();
}

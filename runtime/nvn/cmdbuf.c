#include "../constants/nvn.h"
// NVN command buffers: commands are recorded into the guest-provided command memory as CmdRec
// records, so the game's own memory management and out-of-memory callbacks work as designed.
// A command handle is the guest address of a list's first record.
#include <stdlib.h>
#include <string.h>

#include "nvn.h"

#define JUMP_RESERVE (sizeof(CmdRec) + SF2C_CMDBUF_N_8_2C62)

static NvnCmdBuf *cb_of(uint64_t guest) { return HOST(NvnCmdBuf, guest); }

NVN(nvnCommandBufferInitialize) {
    NvnCmdBuf *cb = calloc(1, sizeof *cb);
    cb->guest = A(0);
    SET_HOST(A(0), cb);
    RET(1);
}
NVN(nvnCommandBufferFinalize) {
    NvnCmdBuf *cb = cb_of(A(0));
    if (cb) { nvn_commands_release(cb->commands); free(cb); }
    SET_HOST(A(0), 0);
}
NVN(nvnCommandBufferSetDebugLabel) { (void)c; }

static CmdRec *raw_rec(uint64_t at, uint32_t op, uint32_t size) {
    CmdRec *r = (CmdRec *)(uintptr_t)at;
    memset(r, 0, sizeof *r);
    r->op = op;
    r->size = size;
    return r;
}

NVN(nvnCommandBufferAddCommandMemory) {
    NvnCmdBuf *cb = cb_of(A(0));
    uint64_t mem = HOST(NvnPool, A(1))->storage + A(SF2C_CMDBUF_N_2_D473), size = A(SF2C_CMDBUF_N_3_4E07);
    if (cb->recording && cb->mem) {
        // chain the list being recorded into the new chunk
        CmdRec *j = raw_rec(cb->mem + cb->used, CMD_JUMP, sizeof(CmdRec));
        j->x[0] = mem;
    }
    cb->mem = mem;
    cb->size = size;
    cb->used = 0;
}
NVN(nvnCommandBufferAddControlMemory) { (void)c; }
NVN(nvnCommandBufferGetCommandMemorySize) { RET(cb_of(A(0))->size); }
NVN(nvnCommandBufferGetCommandMemoryUsed) { RET(cb_of(A(0))->used); }
NVN(nvnCommandBufferGetCommandMemoryFree) {
    NvnCmdBuf *cb = cb_of(A(0));
    RET(cb->size > cb->used + JUMP_RESERVE ? cb->size - cb->used - JUMP_RESERVE : 0);
}
NVN(nvnCommandBufferGetControlMemorySize) { RET(SF2C_CMDBUF_N_0X10000_4939); }
NVN(nvnCommandBufferGetControlMemoryUsed) { RET(0); }
NVN(nvnCommandBufferGetControlMemoryFree) { RET(SF2C_CMDBUF_N_0X10000_4939); }
NVN(nvnCommandBufferSetMemoryCallback) { cb_of(A(0))->callback = A(1); }
NVN(nvnCommandBufferGetMemoryCallback) { RET(cb_of(A(0))->callback); }
NVN(nvnCommandBufferSetMemoryCallbackData) { cb_of(A(0))->callback_data = A(1); }
NVN(nvnCommandBufferGetMemoryCallbackData) { RET(cb_of(A(0))->callback_data); }
NVN(nvnCommandBufferSetCommandMemoryCallbackEnabled) { (void)c; }

// Reserve a record. May call back into the guest for more memory, so callers snapshot their
// arguments first.
static CmdRec *rec(Cpu *c, NvnCmdBuf *cb, uint32_t op, uint32_t extra) {
    uint32_t size = (uint32_t)((sizeof(CmdRec) + extra + SF2C_CMDBUF_N_7_7902) & ~SF2C_CMDBUF_N_7U_7330);
    for (int tries = 0; !cb->mem || cb->used + size + JUMP_RESERVE > cb->size; tries++) {
        if (!cb->callback || tries == SF2C_CMDBUF_N_2_D473) FATAL(SF2C_CMDBUF_S_NVN_OUT_OF_COMMAND_MEMORY_NE_42D3, size);
        uint64_t saved[SF2C_CMDBUF_N_8_2C62];
        memcpy(saved, c->x, sizeof saved);
        c->x[0] = cb->guest;
        c->x[1] = 0;  // NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY
        c->x[SF2C_CMDBUF_N_2_D473] = size + JUMP_RESERVE;
        c->x[SF2C_CMDBUF_N_3_4E07] = cb->callback_data;
        guest_call(c, cb->callback);
        memcpy(c->x, saved, sizeof saved);
    }
    CmdRec *r = raw_rec(cb->mem + cb->used, op, size);
    cb->used += size;
    return r;
}

NVN(nvnCommandBufferBeginRecording) {
    NvnCmdBuf *cb = cb_of(A(0));
    nvn_commands_release(cb->commands);
    cb->commands = nvn_commands_begin();
    cb->recording = true;
    rec(c, cb, CMD_BARRIER, 0);  // makes sure memory exists; the list starts here
    cb->start = cb->mem + cb->used - sizeof(CmdRec);
}
NVN(nvnCommandBufferEndRecording) {
    NvnCmdBuf *cb = cb_of(A(0));
    rec(c, cb, CMD_END, 0);
    cb->recording = false;
    nvn_commands_end(cb->commands, cb->start);
    cb->commands = NULL;
    RET(cb->start);
}
NVN(nvnCommandBufferIsRecording) { RET(cb_of(A(0))->recording); }

// ---- generic recorders
typedef struct { uint64_t x[SF2C_CMDBUF_N_8_2C62]; float f[SF2C_CMDBUF_N_4_4B22]; } Snap;
static Snap snap(Cpu *c) {
    Snap s;
    memcpy(s.x, c->x, sizeof s.x);
    for (int i = 0; i < SF2C_CMDBUF_N_4_4B22; i++) s.f[i] = *(float *)&c->v[i];
    return s;
}
static CmdRec *record(Cpu *c, uint32_t op, uint32_t extra, Snap *s) {
    CmdRec *r = rec(c, cb_of(s->x[0]), op, extra);
    memcpy(r->x, s->x + 1, sizeof r->x);
    memcpy(r->f, s->f, sizeof r->f);
    return r;
}
// record with a copy of `len` bytes from the guest pointer in argument `arg`
static CmdRec *record_copy(Cpu *c, uint32_t op, int arg, uint32_t len) {
    Snap s = snap(c);
    CmdRec *r = record(c, op, len, &s);
    if (s.x[arg] && len) memcpy(r->extra, (void *)(uintptr_t)s.x[arg], len);
    return r;
}
#define PLAIN(name, op) NVN(name) { Snap s = snap(c); record(c, op, 0, &s); }

// ---- state binding
NVN(nvnCommandBufferBindProgram) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_PROGRAM, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnProgram, s.x[1]);  // host program
    r->x[1] = s.x[SF2C_CMDBUF_N_2_D473];  // NVNshaderStageBits
}
NVN(nvnCommandBufferBindBlendState) { record_copy(c, CMD_BIND_BLEND, 1, SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferBindChannelMaskState) { record_copy(c, CMD_BIND_CHANNEL_MASK, 1, SF2C_CMDBUF_N_4_4B22); }
NVN(nvnCommandBufferBindColorState) { record_copy(c, CMD_BIND_COLOR, 1, SF2C_CMDBUF_N_4_4B22); }
NVN(nvnCommandBufferBindDepthStencilState) { record_copy(c, CMD_BIND_DEPTH_STENCIL, 1, SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferBindMultisampleState) { record_copy(c, CMD_BIND_MULTISAMPLE, 1, SF2C_CMDBUF_N_0X18_D47C); }
NVN(nvnCommandBufferBindPolygonState) { record_copy(c, CMD_BIND_POLYGON, 1, SF2C_CMDBUF_N_4_4B22); }
NVN(nvnCommandBufferBindVertexAttribState) { record_copy(c, CMD_BIND_VERTEX_ATTRIB, SF2C_CMDBUF_N_2_D473, (uint32_t)A(1) * SF2C_CMDBUF_N_4_4B22); }
NVN(nvnCommandBufferBindVertexStreamState) { record_copy(c, CMD_BIND_VERTEX_STREAM, SF2C_CMDBUF_N_2_D473, (uint32_t)A(1) * SF2C_CMDBUF_N_8_2C62); }

// (cmd, first, count, const NVNbufferRange*) and the single-binding forms
NVN(nvnCommandBufferBindVertexBuffers) { record_copy(c, CMD_BIND_VERTEX_BUFFERS, SF2C_CMDBUF_N_3_4E07, (uint32_t)A(SF2C_CMDBUF_N_2_D473) * SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferBindVertexBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_VERTEX_BUFFERS, SF2C_CMDBUF_N_16_B17E, &s);
    r->x[0] = s.x[1]; r->x[1] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_2_D473], SF2C_CMDBUF_N_16_B17E);
}
// (cmd, stage, first, count, const NVNbufferRange*)
NVN(nvnCommandBufferBindUniformBuffers) { record_copy(c, CMD_BIND_UNIFORM_BUFFERS, SF2C_CMDBUF_N_4_4B22, (uint32_t)A(SF2C_CMDBUF_N_3_4E07) * SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferBindUniformBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_UNIFORM_BUFFERS, SF2C_CMDBUF_N_16_B17E, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[SF2C_CMDBUF_N_2_D473]; r->x[SF2C_CMDBUF_N_2_D473] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_3_4E07], SF2C_CMDBUF_N_16_B17E);
}
NVN(nvnCommandBufferBindStorageBuffers) { record_copy(c, CMD_BIND_STORAGE_BUFFERS, SF2C_CMDBUF_N_4_4B22, (uint32_t)A(SF2C_CMDBUF_N_3_4E07) * SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferBindStorageBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_STORAGE_BUFFERS, SF2C_CMDBUF_N_16_B17E, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[SF2C_CMDBUF_N_2_D473]; r->x[SF2C_CMDBUF_N_2_D473] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_3_4E07], SF2C_CMDBUF_N_16_B17E);
}
// (cmd, stage, first, count, const handle*)
NVN(nvnCommandBufferBindTextures) { record_copy(c, CMD_BIND_TEXTURES, SF2C_CMDBUF_N_4_4B22, (uint32_t)A(SF2C_CMDBUF_N_3_4E07) * SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferBindTexture) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_TEXTURES, SF2C_CMDBUF_N_8_2C62, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[SF2C_CMDBUF_N_2_D473]; r->x[SF2C_CMDBUF_N_2_D473] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_3_4E07], SF2C_CMDBUF_N_8_2C62);
}
NVN(nvnCommandBufferBindImages) { record_copy(c, CMD_BIND_IMAGES, SF2C_CMDBUF_N_4_4B22, (uint32_t)A(SF2C_CMDBUF_N_3_4E07) * SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferBindImage) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_IMAGES, SF2C_CMDBUF_N_8_2C62, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[SF2C_CMDBUF_N_2_D473]; r->x[SF2C_CMDBUF_N_2_D473] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_3_4E07], SF2C_CMDBUF_N_8_2C62);
}
NVN(nvnCommandBufferBindSeparateTextures) { record_copy(c, CMD_BIND_TEXTURES, SF2C_CMDBUF_N_4_4B22, (uint32_t)A(SF2C_CMDBUF_N_3_4E07) * SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferBindSeparateTexture) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_TEXTURES, SF2C_CMDBUF_N_8_2C62, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[SF2C_CMDBUF_N_2_D473]; r->x[SF2C_CMDBUF_N_2_D473] = 1;
    memcpy(r->extra, &s.x[SF2C_CMDBUF_N_3_4E07], SF2C_CMDBUF_N_8_2C62);
}
NVN(nvnCommandBufferBindSeparateSamplers) { (void)c; }
NVN(nvnCommandBufferBindSeparateSampler) { (void)c; }
NVN(nvnCommandBufferBindTransformFeedbackBuffers) { (void)c; }
NVN(nvnCommandBufferBindTransformFeedbackBuffer) { (void)c; }
NVN(nvnCommandBufferBindCoverageModulationTable) { (void)c; }

NVN(nvnCommandBufferSetTexturePool) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_TEXTURE_POOL, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnDescPool, s.x[1]);
}
NVN(nvnCommandBufferSetSamplerPool) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_SAMPLER_POOL, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnDescPool, s.x[1]);
}

// SetRenderTargets(cmd, n, colors[], colorViews[], depth, depthView)
// extra: { u64 tex[8]; NvnTextureView views[8]; u8 has_view[8]; u64 depth; NvnTextureView dview; u8 has_dview }
typedef struct {
    uint64_t tex[SF2C_CMDBUF_N_8_2C62];
    NvnTextureView views[SF2C_CMDBUF_N_8_2C62];
    uint8_t has_view[SF2C_CMDBUF_N_8_2C62];
    uint64_t depth;
    NvnTextureView dview;
    uint8_t has_dview;
} RtRec;
NVN(nvnCommandBufferSetRenderTargets) {
    Snap s = snap(c);
    int n = (int)s.x[1];
    if (n > SF2C_CMDBUF_N_8_2C62) n = SF2C_CMDBUF_N_8_2C62;
    RtRec rt = {0};
    const uint64_t *colors = (const uint64_t *)(uintptr_t)s.x[SF2C_CMDBUF_N_2_D473], *views = (const uint64_t *)(uintptr_t)s.x[SF2C_CMDBUF_N_3_4E07];
    for (int i = 0; i < n; i++) {
        rt.tex[i] = colors && colors[i] ? (uint64_t)(uintptr_t)HOST(NvnTexture, colors[i]) : 0;
        nvn_commands_texture(cb_of(s.x[0])->commands, (NvnTexture *)(uintptr_t)rt.tex[i]);
        if (views && views[i]) {
            rt.has_view[i] = 1;
            memcpy(&rt.views[i], (void *)(uintptr_t)views[i], sizeof(NvnTextureView));
            nvn_texture_note_view((NvnTexture *)(uintptr_t)rt.tex[i], &rt.views[i]);
        }
    }
    rt.depth = s.x[SF2C_CMDBUF_N_4_4B22] ? (uint64_t)(uintptr_t)HOST(NvnTexture, s.x[SF2C_CMDBUF_N_4_4B22]) : 0;
    nvn_commands_texture(cb_of(s.x[0])->commands, (NvnTexture *)(uintptr_t)rt.depth);
    if (s.x[SF2C_CMDBUF_N_5_EF2D]) {
        rt.has_dview = 1;
        memcpy(&rt.dview, (void *)(uintptr_t)s.x[SF2C_CMDBUF_N_5_EF2D], sizeof(NvnTextureView));
        nvn_texture_note_view((NvnTexture *)(uintptr_t)rt.depth, &rt.dview);
    }
    CmdRec *r = record(c, CMD_SET_RENDER_TARGETS, sizeof rt, &s);
    r->x[0] = (uint64_t)n;
    memcpy(r->extra, &rt, sizeof rt);
}

// viewports/scissors/depth ranges
NVN(nvnCommandBufferSetViewports) { record_copy(c, CMD_SET_VIEWPORTS, SF2C_CMDBUF_N_3_4E07, (uint32_t)A(SF2C_CMDBUF_N_2_D473) * SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferSetViewport) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_VIEWPORTS, SF2C_CMDBUF_N_16_B17E, &s);
    float v[SF2C_CMDBUF_N_4_4B22] = {(float)(int32_t)s.x[1], (float)(int32_t)s.x[SF2C_CMDBUF_N_2_D473], (float)(int32_t)s.x[SF2C_CMDBUF_N_3_4E07], (float)(int32_t)s.x[SF2C_CMDBUF_N_4_4B22]};
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, v, SF2C_CMDBUF_N_16_B17E);
}
NVN(nvnCommandBufferSetScissors) { record_copy(c, CMD_SET_SCISSORS, SF2C_CMDBUF_N_3_4E07, (uint32_t)A(SF2C_CMDBUF_N_2_D473) * SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferSetScissor) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_SCISSORS, SF2C_CMDBUF_N_16_B17E, &s);
    int32_t v[SF2C_CMDBUF_N_4_4B22] = {(int32_t)s.x[1], (int32_t)s.x[SF2C_CMDBUF_N_2_D473], (int32_t)s.x[SF2C_CMDBUF_N_3_4E07], (int32_t)s.x[SF2C_CMDBUF_N_4_4B22]};
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, v, SF2C_CMDBUF_N_16_B17E);
}
NVN(nvnCommandBufferSetDepthRanges) { record_copy(c, CMD_SET_DEPTH_RANGES, SF2C_CMDBUF_N_3_4E07, (uint32_t)A(SF2C_CMDBUF_N_2_D473) * SF2C_CMDBUF_N_8_2C62); }
NVN(nvnCommandBufferSetDepthRange) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_DEPTH_RANGES, SF2C_CMDBUF_N_8_2C62, &s);
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, s.f, SF2C_CMDBUF_N_8_2C62);
}
NVN(nvnCommandBufferSetBlendColor) { record_copy(c, CMD_SET_BLEND_COLOR, 1, SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferSetStencilRef) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[SF2C_CMDBUF_N_6_E7F6] = 0; }
NVN(nvnCommandBufferSetStencilMask) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[SF2C_CMDBUF_N_6_E7F6] = 1; }
NVN(nvnCommandBufferSetStencilValueMask) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[SF2C_CMDBUF_N_6_E7F6] = SF2C_CMDBUF_N_2_D473; }
PLAIN(nvnCommandBufferSetPrimitiveRestart, CMD_SET_PRIMITIVE_RESTART)
PLAIN(nvnCommandBufferSetAlphaRef, CMD_SET_ALPHA_REF)
PLAIN(nvnCommandBufferSetDepthBounds, CMD_SET_DEPTH_BOUNDS)
PLAIN(nvnCommandBufferSetPolygonOffsetClamp, CMD_SET_POLYGON_OFFSET)
PLAIN(nvnCommandBufferSetLineWidth, CMD_SET_LINE_WIDTH)
PLAIN(nvnCommandBufferSetPointSize, CMD_SET_POINT_SIZE)
PLAIN(nvnCommandBufferSetSampleMask, CMD_SET_SAMPLE_MASK)
PLAIN(nvnCommandBufferSetRenderEnable, CMD_SET_RENDER_ENABLE)
NVN(nvnCommandBufferSetRenderEnableConditional) { (void)c; }
NVN(nvnCommandBufferSetDepthClamp) { (void)c; }
NVN(nvnCommandBufferSetRasterizerDiscard) { (void)c; }
NVN(nvnCommandBufferSetConservativeRasterEnable) { (void)c; }
NVN(nvnCommandBufferSetConservativeRasterDilate) { (void)c; }
NVN(nvnCommandBufferSetSubpixelPrecisionBias) { (void)c; }
NVN(nvnCommandBufferSetViewportSwizzles) { (void)c; }
NVN(nvnCommandBufferSetStencilCullCriteria) { (void)c; }
NVN(nvnCommandBufferSetColorReductionEnable) { (void)c; }
NVN(nvnCommandBufferSetColorReductionThresholds) { (void)c; }
NVN(nvnCommandBufferSetTiledCacheAction) { (void)c; }
NVN(nvnCommandBufferSetTiledCacheTileSize) { (void)c; }
NVN(nvnCommandBufferSetShaderScratchMemory) { (void)c; }
NVN(nvnCommandBufferSetPatchSize) { (void)c; }
NVN(nvnCommandBufferSetInnerTessellationLevels) { (void)c; }
NVN(nvnCommandBufferSetOuterTessellationLevels) { (void)c; }
NVN(nvnCommandBufferSetProgramSubroutines) { (void)c; }
NVN(nvnCommandBufferSaveZCullData) { (void)c; }
NVN(nvnCommandBufferRestoreZCullData) { (void)c; }

static uint64_t copy_row_stride, copy_image_stride;
RT_ON_RESET(cmdbuf_reset) { copy_row_stride = copy_image_stride = 0; }
NVN(nvnCommandBufferSetCopyRowStride) {
    Snap s = snap(c);
    copy_row_stride = s.x[1];
    CmdRec *r = record(c, CMD_SET_COPY_STRIDES, 0, &s);
    r->x[0] = copy_row_stride; r->x[1] = copy_image_stride;
}
NVN(nvnCommandBufferSetCopyImageStride) {
    Snap s = snap(c);
    copy_image_stride = s.x[1];
    CmdRec *r = record(c, CMD_SET_COPY_STRIDES, 0, &s);
    r->x[0] = copy_row_stride; r->x[1] = copy_image_stride;
}
NVN(nvnCommandBufferGetCopyRowStride) { RET(copy_row_stride); }
NVN(nvnCommandBufferGetCopyImageStride) { RET(copy_image_stride); }

// ---- clears
NVN(nvnCommandBufferClearColor) { record_copy(c, CMD_CLEAR_COLOR, SF2C_CMDBUF_N_2_D473, SF2C_CMDBUF_N_16_B17E); }
NVN(nvnCommandBufferClearColori) { CmdRec *r = record_copy(c, CMD_CLEAR_COLOR, SF2C_CMDBUF_N_2_D473, SF2C_CMDBUF_N_16_B17E); r->x[SF2C_CMDBUF_N_5_EF2D] = 1; }
NVN(nvnCommandBufferClearColorui) { CmdRec *r = record_copy(c, CMD_CLEAR_COLOR, SF2C_CMDBUF_N_2_D473, SF2C_CMDBUF_N_16_B17E); r->x[SF2C_CMDBUF_N_5_EF2D] = SF2C_CMDBUF_N_2_D473; }
PLAIN(nvnCommandBufferClearDepthStencil, CMD_CLEAR_DEPTH_STENCIL)
// ClearTexture(cmd, texture, view, region, color, mask)
typedef struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[SF2C_CMDBUF_N_6_E7F6]; float color[SF2C_CMDBUF_N_4_4B22]; } ClearTexRec;
static void clear_texture(Cpu *c, int kind) {
    Snap s = snap(c);
    ClearTexRec ct = {0};
    ct.tex = (uint64_t)(uintptr_t)HOST(NvnTexture, s.x[1]);
    nvn_commands_texture(cb_of(s.x[0])->commands, (NvnTexture *)(uintptr_t)ct.tex);
    if (s.x[SF2C_CMDBUF_N_2_D473]) { ct.has_view = 1; memcpy(&ct.view, (void *)(uintptr_t)s.x[SF2C_CMDBUF_N_2_D473], sizeof ct.view); nvn_texture_note_view((NvnTexture *)(uintptr_t)ct.tex, &ct.view); }
    if (s.x[SF2C_CMDBUF_N_3_4E07]) memcpy(ct.region, (void *)(uintptr_t)s.x[SF2C_CMDBUF_N_3_4E07], sizeof ct.region);
    if (s.x[SF2C_CMDBUF_N_4_4B22]) memcpy(ct.color, (void *)(uintptr_t)s.x[SF2C_CMDBUF_N_4_4B22], SF2C_CMDBUF_N_16_B17E);
    CmdRec *r = record(c, CMD_CLEAR_TEXTURE, sizeof ct, &s);
    r->x[SF2C_CMDBUF_N_6_E7F6] = (uint64_t)kind;
    memcpy(r->extra, &ct, sizeof ct);
}
NVN(nvnCommandBufferClearTexture) { clear_texture(c, 0); }
NVN(nvnCommandBufferClearTexturei) { clear_texture(c, 1); }
NVN(nvnCommandBufferClearTextureui) { clear_texture(c, SF2C_CMDBUF_N_2_D473); }
PLAIN(nvnCommandBufferClearBuffer, CMD_CLEAR_BUFFER)
// DiscardColor(cmdbuf, index): the target's contents become undefined. On the Switch a discarded (compressed)
// target reads back cleared; areas the game then leaves undrawn (e.g. a pit after a floor tile falls away) stay
// black there. Treated as a clear to (0,0,0,0) (LARECOMP_KEEP_DISCARD=1: old no-op behaviour).
NVN(nvnCommandBufferDiscardColor) {
    static int keep = -1, logged;
    if (keep < 0) keep = getenv(SF2C_CMDBUF_S_LARECOMP_KEEP_DISCARD_3335) != NULL;
    if (logged < SF2C_CMDBUF_N_5_EF2D) { logged++; LOG(SF2C_CMDBUF_S_NVN_DISCARDCOLOR_INDEX_LLU_S_0AB6, (unsigned long long)c->x[1], keep ? SF2C_CMDBUF_S_KEPT_F05B : SF2C_CMDBUF_S_CLEAR_5F40); }
    if (keep) return;
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_CLEAR_COLOR, SF2C_CMDBUF_N_16_B17E, &s);
    memset(r->extra, 0, SF2C_CMDBUF_N_16_B17E);
    r->x[SF2C_CMDBUF_N_2_D473] = SF2C_CMDBUF_N_0XF_7419;  // all channels
    r->x[SF2C_CMDBUF_N_5_EF2D] = 0;    // float clear
}
NVN(nvnCommandBufferDiscardDepthStencil) { (void)c; }

NVN(nvnCommandBufferDrawArrays) {
    static int ndraw; if (ndraw++ < SF2C_CMDBUF_N_20_F5CA) LOG(SF2C_CMDBUF_S_CMDBUF_DRAWARRAYS_COUNT_LLU__D5F2, (unsigned long long)c->x[SF2C_CMDBUF_N_3_4E07]);
    Snap s = snap(c); CmdRec *r = record(c, CMD_DRAW_ARRAYS, 0, &s); r->x[SF2C_CMDBUF_N_3_4E07] = 0; r->x[SF2C_CMDBUF_N_4_4B22] = 1;
}
NVN(nvnCommandBufferDrawArraysInstanced) {
    static int ndraw; if (ndraw++ < SF2C_CMDBUF_N_20_F5CA) LOG(SF2C_CMDBUF_S_CMDBUF_DRAWARRAYSINSTANCED_C_E045, (unsigned long long)c->x[SF2C_CMDBUF_N_3_4E07]);
    Snap s = snap(c); record(c, CMD_DRAW_ARRAYS, 0, &s);
}
// Debug: LARECOMP_TRACE_COUNT=<n> logs the guest call chain of indexed draws with n indices.
static void trace_draw(Cpu *c, uint64_t count) {
    static int ndraw; if (ndraw++ < SF2C_CMDBUF_N_20_F5CA) LOG(SF2C_CMDBUF_S_CMDBUF_DRAWELEMENTS_COUNT_LL_FC3A, (unsigned long long)count, (unsigned long long)c->x[SF2C_CMDBUF_N_30_624B]);
    static long want = -SF2C_CMDBUF_N_2_D473;
    if (want == -SF2C_CMDBUF_N_2_D473) want = getenv(SF2C_CMDBUF_S_LARECOMP_TRACE_COUNT_D3AB) ? atol(getenv(SF2C_CMDBUF_S_LARECOMP_TRACE_COUNT_D3AB)) : -1;
    if (want < 0 || (long)count != want) return;
    static int n;
    if (n++ > SF2C_CMDBUF_N_40_D59E) return;
    LOG(SF2C_CMDBUF_S_TRACE_DRAW_COUNT_LLU_LR_LLX__B2BF, (unsigned long long)count, (unsigned long long)c->x[SF2C_CMDBUF_N_30_624B]);
    uint64_t fp = c->x[SF2C_CMDBUF_N_29_3513];
    for (int i = 0; i < SF2C_CMDBUF_N_14_8527 && fp >= SF2C_CMDBUF_N_0X7000000000ULL_65B2 && fp < SF2C_CMDBUF_N_0X8000000000ULL_6587 && (fp & SF2C_CMDBUF_N_7_7902) == 0; i++) {
        uint64_t next = *(uint64_t *)(uintptr_t)fp, lr = *(uint64_t *)(uintptr_t)(fp + SF2C_CMDBUF_N_8_2C62);
        LOG(SF2C_CMDBUF_S_LLX_3290, (unsigned long long)lr);
        if (next <= fp) break;
        fp = next;
    }
    LOG(SF2C_CMDBUF_S_N_D7A5);
}

NVN(nvnCommandBufferDrawElements) {
    trace_draw(c, c->x[SF2C_CMDBUF_N_3_4E07]);
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_DRAW_ELEMENTS, 0, &s);
    r->x[SF2C_CMDBUF_N_4_4B22] = 0; r->x[SF2C_CMDBUF_N_5_EF2D] = 0; r->x[SF2C_CMDBUF_N_6_E7F6] = 1;  // base vertex, base instance, instances
}
NVN(nvnCommandBufferDrawElementsBaseVertex) {
    trace_draw(c, c->x[SF2C_CMDBUF_N_3_4E07]);
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_DRAW_ELEMENTS, 0, &s);
    r->x[SF2C_CMDBUF_N_5_EF2D] = 0; r->x[SF2C_CMDBUF_N_6_E7F6] = 1;
}
NVN(nvnCommandBufferDrawElementsInstanced) { trace_draw(c, c->x[SF2C_CMDBUF_N_3_4E07]); Snap s = snap(c); record(c, CMD_DRAW_ELEMENTS, 0, &s); }
PLAIN(nvnCommandBufferDrawArraysIndirect, CMD_DRAW_ARRAYS_INDIRECT)
PLAIN(nvnCommandBufferDrawElementsIndirect, CMD_DRAW_ELEMENTS_INDIRECT)
NVN(nvnCommandBufferMultiDrawArraysIndirectCount) { (void)c; }
NVN(nvnCommandBufferMultiDrawElementsIndirectCount) { (void)c; }
NVN(nvnCommandBufferDrawTransformFeedback) { (void)c; }
NVN(nvnCommandBufferDrawTexture) { (void)c; }
PLAIN(nvnCommandBufferDispatchCompute, CMD_DISPATCH)
PLAIN(nvnCommandBufferDispatchComputeIndirect, CMD_DISPATCH_INDIRECT)
NVN(nvnCommandBufferBeginTransformFeedback) { (void)c; }
NVN(nvnCommandBufferEndTransformFeedback) { (void)c; }
NVN(nvnCommandBufferPauseTransformFeedback) { (void)c; }
NVN(nvnCommandBufferResumeTransformFeedback) { (void)c; }

// ---- copies
typedef struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[SF2C_CMDBUF_N_6_E7F6]; } TexRef;
static void tex_ref(NvnCmdBuf *cb, TexRef *t, uint64_t tex, uint64_t view, uint64_t region) {
    t->tex = tex ? (uint64_t)(uintptr_t)HOST(NvnTexture, tex) : 0;
    nvn_commands_texture(cb->commands, (NvnTexture *)(uintptr_t)t->tex);
    if (view) { t->has_view = 1; memcpy(&t->view, (void *)(uintptr_t)view, sizeof t->view); nvn_texture_note_view((NvnTexture *)(uintptr_t)t->tex, &t->view); }
    if (region) memcpy(t->region, (void *)(uintptr_t)region, sizeof t->region);
}
NVN(nvnCommandBufferCopyBufferToTexture) {  // (cmd, src, dst, dstView, dstRegion, flags)
    Snap s = snap(c);
    TexRef t = {0};
    tex_ref(cb_of(s.x[0]), &t, s.x[SF2C_CMDBUF_N_2_D473], s.x[SF2C_CMDBUF_N_3_4E07], s.x[SF2C_CMDBUF_N_4_4B22]);
    CmdRec *r = record(c, CMD_COPY_BUFFER_TO_TEXTURE, sizeof t, &s);
    memcpy(r->extra, &t, sizeof t);
}
NVN(nvnCommandBufferCopyTextureToBuffer) {  // (cmd, src, srcView, srcRegion, dst, flags)
    Snap s = snap(c);
    TexRef t = {0};
    tex_ref(cb_of(s.x[0]), &t, s.x[1], s.x[SF2C_CMDBUF_N_2_D473], s.x[SF2C_CMDBUF_N_3_4E07]);
    CmdRec *r = record(c, CMD_COPY_TEXTURE_TO_BUFFER, sizeof t, &s);
    memcpy(r->extra, &t, sizeof t);
}
NVN(nvnCommandBufferCopyTextureToTexture) {  // (cmd, src, srcView, srcRegion, dst, dstView, dstRegion, flags)
    Snap s = snap(c);
    TexRef t[SF2C_CMDBUF_N_2_D473] = {{0}};
    tex_ref(cb_of(s.x[0]), &t[0], s.x[1], s.x[SF2C_CMDBUF_N_2_D473], s.x[SF2C_CMDBUF_N_3_4E07]);
    tex_ref(cb_of(s.x[0]), &t[1], s.x[SF2C_CMDBUF_N_4_4B22], s.x[SF2C_CMDBUF_N_5_EF2D], s.x[SF2C_CMDBUF_N_6_E7F6]);
    CmdRec *r = record(c, CMD_COPY_TEXTURE_TO_TEXTURE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
PLAIN(nvnCommandBufferCopyBufferToBuffer, CMD_COPY_BUFFER_TO_BUFFER)
NVN(nvnCommandBufferDownsample) {
    Snap s = snap(c);
    TexRef t[SF2C_CMDBUF_N_2_D473] = {{0}};
    tex_ref(cb_of(s.x[0]), &t[0], s.x[1], 0, 0);
    tex_ref(cb_of(s.x[0]), &t[1], s.x[SF2C_CMDBUF_N_2_D473], 0, 0);
    CmdRec *r = record(c, CMD_DOWNSAMPLE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
NVN(nvnCommandBufferDownsampleTextureView) {
    Snap s = snap(c);
    TexRef t[SF2C_CMDBUF_N_2_D473] = {{0}};
    tex_ref(cb_of(s.x[0]), &t[0], s.x[1], s.x[SF2C_CMDBUF_N_2_D473], 0);
    tex_ref(cb_of(s.x[0]), &t[1], s.x[SF2C_CMDBUF_N_3_4E07], s.x[SF2C_CMDBUF_N_4_4B22], 0);
    CmdRec *r = record(c, CMD_DOWNSAMPLE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
NVN(nvnCommandBufferTiledDownsample) { (void)c; }
NVN(nvnCommandBufferTiledDownsampleTextureView) { (void)c; }
NVN(nvnCommandBufferResolveDepthBuffer) { (void)c; }
// UpdateUniformBuffer(cmd, buffer, size, offset, dataSize, data)
NVN(nvnCommandBufferUpdateUniformBuffer) { record_copy(c, CMD_UPDATE_UNIFORM_BUFFER, SF2C_CMDBUF_N_5_EF2D, (uint32_t)A(SF2C_CMDBUF_N_4_4B22)); }

// ---- synchronization and misc
NVN(nvnCommandBufferFenceSync) {
    // work executes at submit time, so the sync is satisfied once this list has run
    Snap s = snap(c);
    record(c, CMD_FENCE_SYNC, 0, &s);
}
PLAIN(nvnCommandBufferWaitSync, CMD_WAIT_SYNC)
PLAIN(nvnCommandBufferBarrier, CMD_BARRIER)
PLAIN(nvnCommandBufferSignalEvent, CMD_SIGNAL_EVENT)
PLAIN(nvnCommandBufferWaitEvent, CMD_WAIT_EVENT)
PLAIN(nvnCommandBufferReportCounter, CMD_REPORT_COUNTER)
NVN(nvnCommandBufferReportValue) { (void)c; }
NVN(nvnCommandBufferResetCounter) { (void)c; }
static void call_commands(Cpu *c) {
    NvnCmdBuf *cb = cb_of(A(0));
    const uint64_t *handles = AP(SF2C_CMDBUF_N_2_D473);
    for (uint64_t i = 0; i < A(1); ++i) nvn_commands_call(cb->commands, handles[i]);
    record_copy(c, CMD_CALL, SF2C_CMDBUF_N_2_D473, (uint32_t)A(1) * SF2C_CMDBUF_N_8_2C62);
}
NVN(nvnCommandBufferCallCommands) { call_commands(c); }
NVN(nvnCommandBufferCopyCommands) { call_commands(c); }
NVN(nvnCommandBufferInsertDebugMarker) { (void)c; }
NVN(nvnCommandBufferInsertDebugMarkerDynamic) { (void)c; }
NVN(nvnCommandBufferInsertDebugMarkerStatic) { (void)c; }
NVN(nvnCommandBufferPushDebugGroup) { (void)c; }
NVN(nvnCommandBufferPushDebugGroupDynamic) { (void)c; }
NVN(nvnCommandBufferPushDebugGroupStatic) { (void)c; }
NVN(nvnCommandBufferPopDebugGroup) { (void)c; }
NVN(nvnCommandBufferPopDebugGroupId) { (void)c; }

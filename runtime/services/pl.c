#include "../constants/services.h"
// pl:u shared fonts. Unity polls GetLoadState synchronously when rebuilding its
// font fallback list; a successful stub returning zero leaves that loop stuck.
// Supply open-licensed Noto substitutes through the normal shared-memory API.
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../platform.h"

#define FONT_MEMORY_SIZE SF2C_PL_FONT_MEMORY_SIZE
#define FONT_TYPES SF2C_PL_FONT_TYPES
typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t size, offset;
} SharedFont;

static SharedFont fonts[] = {{.name = SF2C_PL_S_NOTOSANS_REGULAR_TTF_0AC6}, {.name = SF2C_PL_S_NOTOSANSCJKSC_REGULAR_OTF_23C5}};
static pthread_mutex_t font_lock = PTHREAD_MUTEX_INITIALIZER;

// The CJK face covers simplified/traditional Chinese, Japanese and Korean.
// Nintendo's private icon font is unavailable; its slot uses the standard face.
static SharedFont *font_for_type(uint32_t type) { return &fonts[type >= 1 && type <= SF2C_PL_N_4_4B22]; }

static void load_fonts(void) {
    if (fonts[0].data) return;
    char directory[SF2C_PL_N_4096_8B92];
    if (!plat_exe_path(directory, sizeof directory)) FATAL(SF2C_PL_S_PL_CANNOT_LOCATE_BUNDLED_FON_3325);
    char *slash = strrchr(directory, SF2C_PL_CH_EMPTY_0825);
    if (!slash) FATAL(SF2C_PL_S_PL_CANNOT_LOCATE_BUNDLED_FON_3325);
    *slash = 0;
    uint32_t offset = SF2C_PL_N_8_2C62;
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i) {
        SharedFont *font = &fonts[i];
        char path[SF2C_PL_N_4352_8AF1];
        snprintf(path, sizeof path, SF2C_PL_S_S_LIB_FONTS_S_71B0, directory, font->name);
        FILE *f = fopen(path, SF2C_PL_S_RB_46CA);
        if (!f) FATAL(SF2C_PL_S_PL_BUNDLED_FONT_MISSING_S_RU_6C74, path);
        if (fseek(f, 0, SEEK_END)) FATAL(SF2C_PL_S_PL_CANNOT_READ_FONT_S_N_ECD8, path);
        long size = ftell(f);
        if (size < SF2C_PL_N_12_6B51 || (uint64_t)size > FONT_MEMORY_SIZE - offset)
            FATAL(SF2C_PL_S_PL_INVALID_BUNDLED_FONT_SIZE_9344, path);
        rewind(f);
        font->data = malloc((size_t)size);
        if (!font->data || fread(font->data, 1, (size_t)size, f) != (size_t)size)
            FATAL(SF2C_PL_S_PL_CANNOT_READ_FONT_S_N_ECD8, path);
        fclose(f);
        if (memcmp(font->data, SF2C_PL_S_0_1_0_0_1744, SF2C_PL_N_4_4B22) && memcmp(font->data, SF2C_PL_S_OTTO_1A59, SF2C_PL_N_4_4B22))
            FATAL(SF2C_PL_S_PL_INVALID_BUNDLED_FONT_SIGN_70B6, path);
        font->size = (uint32_t)size;
        font->offset = offset;
        offset = (offset + font->size + SF2C_PL_N_7U_7330) & ~SF2C_PL_N_7U_7330;
        LOG(SF2C_PL_S_PL_FALLBACK_FONT_S_U_BYTES_O_B674, font->name, font->size, font->offset);
    }
}

static void font_memory_map(KSharedMem *s) {
    pthread_mutex_lock(&font_lock);
    load_fonts();
    uint8_t *base = (uint8_t *)(uintptr_t)s->guest_addr;
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i)
        memcpy(base + fonts[i].offset, fonts[i].data, fonts[i].size);
    pthread_mutex_unlock(&font_lock);
    LOG(SF2C_PL_S_PL_SHARED_FONT_MEMORY_MAPPED_20C8, (unsigned long long)s->guest_addr);
}

RT_ON_RESET(reset_shared_fonts) {
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i) {
        free(fonts[i].data);
        fonts[i].data = NULL;
        fonts[i].size = fonts[i].offset = 0;
    }
}

SERVICE(SF2C_PL_S_PL_U_1B49, pl_user) {
    (void)s;
    uint32_t type = 0;
    if (req->cmd <= SF2C_PL_N_3_4E07) {
        if (req->data_size < sizeof type) { resp->result = SF2C_PL_N_0XF601_0432; return; }
        memcpy(&type, req->data, sizeof type);
        if (type >= FONT_TYPES) { resp->result = SF2C_PL_N_0XF601_0432; return; }
    }
    if (req->cmd > SF2C_PL_N_5_EF2D) { resp->result = SF2C_PL_N_0XF601_0432; return; }
    pthread_mutex_lock(&font_lock);
    load_fonts();
    SharedFont *font = font_for_type(type);
    switch (req->cmd) {
    case 0: break;  // RequestLoad: files have already been loaded synchronously
    case 1: OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_PL_N_4_4B22); break;  // GetLoadState: loaded
    case SF2C_PL_N_2_D473: OUT(uint32_t, 0) = font->size; resp_size(resp, SF2C_PL_N_4_4B22); break;
    case SF2C_PL_N_3_4E07: OUT(uint32_t, 0) = font->offset; resp_size(resp, SF2C_PL_N_4_4B22); break;
    case SF2C_PL_N_4_4B22: {  // GetSharedMemoryNativeHandle: each caller owns its returned handle
        uint32_t handle;
        kernel_create_shared_memory(FONT_MEMORY_SIZE, font_memory_map, &handle);
        resp->copy_handles[resp->ncopy++] = handle;
        break;
    }
    case SF2C_PL_N_5_EF2D: {  // GetSharedFontInOrderOfPriority: unique standard and CJK faces
        IpcBuf *buffers[SF2C_PL_N_3_4E07] = {out_buf(req, 0), out_buf(req, 1), out_buf(req, SF2C_PL_N_2_D473)};
        uint32_t count = SF2C_PL_N_2_D473;
        for (int i = 0; i < SF2C_PL_N_3_4E07; ++i) {
            uint64_t capacity = buffers[i] && buffers[i]->addr ? buffers[i]->size / SF2C_PL_N_4_4B22 : 0;
            if (capacity < count) count = (uint32_t)capacity;
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t values[SF2C_PL_N_3_4E07] = {i, fonts[i].offset, fonts[i].size};
            for (int j = 0; j < SF2C_PL_N_3_4E07; ++j)
                memcpy((void *)(uintptr_t)(buffers[j]->addr + i * SF2C_PL_N_4_4B22), &values[j], SF2C_PL_N_4_4B22);
        }
        OUT(uint8_t, 0) = 1;
        OUT(uint32_t, SF2C_PL_N_4_4B22) = count;
        resp_size(resp, SF2C_PL_N_8_2C62);
        break;
    }
    }
    pthread_mutex_unlock(&font_lock);
}

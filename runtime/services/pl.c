// pl:u shared fonts. Unity polls GetLoadState synchronously when rebuilding its
// font fallback list; a successful stub returning zero leaves that loop stuck.
// Supply open-licensed Noto substitutes through the normal shared-memory API.
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../platform.h"

#define FONT_MEMORY_SIZE 0x1100000u
#define FONT_TYPES 6

typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t size, offset;
} SharedFont;

static SharedFont fonts[] = {{.name = "NotoSans-Regular.ttf"}, {.name = "NotoSansCJKsc-Regular.otf"}};
static pthread_mutex_t font_lock = PTHREAD_MUTEX_INITIALIZER;

// The CJK face covers simplified/traditional Chinese, Japanese and Korean.
// Nintendo's private icon font is unavailable; its slot uses the standard face.
static SharedFont *font_for_type(uint32_t type) { return &fonts[type >= 1 && type <= 4]; }

static void load_fonts(void) {
    if (fonts[0].data) return;
    char directory[4096];
    if (!plat_exe_path(directory, sizeof directory)) FATAL("[pl] Cannot locate bundled fonts\n");
    char *slash = strrchr(directory, '/');
    if (!slash) FATAL("[pl] Cannot locate bundled fonts\n");
    *slash = 0;
    uint32_t offset = 8;
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i) {
        SharedFont *font = &fonts[i];
        char path[4352];
        snprintf(path, sizeof path, "%s/lib/fonts/%s", directory, font->name);
        FILE *f = fopen(path, "rb");
        if (!f) FATAL("[pl] Bundled font missing: %s. Run make to prepare runtime fonts.\n", path);
        if (fseek(f, 0, SEEK_END)) FATAL("[pl] Cannot read font %s\n", path);
        long size = ftell(f);
        if (size < 12 || (uint64_t)size > FONT_MEMORY_SIZE - offset)
            FATAL("[pl] Invalid bundled font size: %s\n", path);
        rewind(f);
        font->data = malloc((size_t)size);
        if (!font->data || fread(font->data, 1, (size_t)size, f) != (size_t)size)
            FATAL("[pl] Cannot read font %s\n", path);
        fclose(f);
        if (memcmp(font->data, "\0\1\0\0", 4) && memcmp(font->data, "OTTO", 4))
            FATAL("[pl] Invalid bundled font signature: %s\n", path);
        font->size = (uint32_t)size;
        font->offset = offset;
        offset = (offset + font->size + 7u) & ~7u;
        LOG("[pl] fallback font %s: %u bytes, offset %#x\n", font->name, font->size, font->offset);
    }
}

static void font_memory_map(KSharedMem *s) {
    pthread_mutex_lock(&font_lock);
    load_fonts();
    uint8_t *base = (uint8_t *)(uintptr_t)s->guest_addr;
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i)
        memcpy(base + fonts[i].offset, fonts[i].data, fonts[i].size);
    pthread_mutex_unlock(&font_lock);
    LOG("[pl] shared font memory mapped at %llx\n", (unsigned long long)s->guest_addr);
}

RT_ON_RESET(reset_shared_fonts) {
    for (size_t i = 0; i < sizeof fonts / sizeof *fonts; ++i) {
        free(fonts[i].data);
        fonts[i].data = NULL;
        fonts[i].size = fonts[i].offset = 0;
    }
}

SERVICE("pl:u", pl_user) {
    (void)s;
    uint32_t type = 0;
    if (req->cmd <= 3) {
        if (req->data_size < sizeof type) { resp->result = 0xF601; return; }
        memcpy(&type, req->data, sizeof type);
        if (type >= FONT_TYPES) { resp->result = 0xF601; return; }
    }
    if (req->cmd > 5) { resp->result = 0xF601; return; }
    pthread_mutex_lock(&font_lock);
    load_fonts();
    SharedFont *font = font_for_type(type);
    switch (req->cmd) {
    case 0: break;  // RequestLoad: files have already been loaded synchronously
    case 1: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetLoadState: loaded
    case 2: OUT(uint32_t, 0) = font->size; resp_size(resp, 4); break;
    case 3: OUT(uint32_t, 0) = font->offset; resp_size(resp, 4); break;
    case 4: {  // GetSharedMemoryNativeHandle: each caller owns its returned handle
        uint32_t handle;
        kernel_create_shared_memory(FONT_MEMORY_SIZE, font_memory_map, &handle);
        resp->copy_handles[resp->ncopy++] = handle;
        break;
    }
    case 5: {  // GetSharedFontInOrderOfPriority: unique standard and CJK faces
        IpcBuf *buffers[3] = {out_buf(req, 0), out_buf(req, 1), out_buf(req, 2)};
        uint32_t count = 2;
        for (int i = 0; i < 3; ++i) {
            uint64_t capacity = buffers[i] && buffers[i]->addr ? buffers[i]->size / 4 : 0;
            if (capacity < count) count = (uint32_t)capacity;
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t values[3] = {i, fonts[i].offset, fonts[i].size};
            for (int j = 0; j < 3; ++j)
                memcpy((void *)(uintptr_t)(buffers[j]->addr + i * 4), &values[j], 4);
        }
        OUT(uint8_t, 0) = 1;
        OUT(uint32_t, 4) = count;
        resp_size(resp, 8);
        break;
    }
    }
    pthread_mutex_unlock(&font_lock);
}

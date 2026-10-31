#include "texture_color.h"
#include <string.h>

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static void rgb565(uint16_t v, uint8_t rgb[3]) {
    rgb[0] = ((v >> 11) * 255 + 15) / 31;
    rgb[1] = (((v >> 5) & 63) * 255 + 31) / 63;
    rgb[2] = ((v & 31) * 255 + 15) / 31;
}

// The game's UI atlas is BC3. Decode only the sampled texel, without GPU
// readback or changing Unity's non-readable texture import settings.
static void bc3_pixel(const uint8_t *b, unsigned pixel, uint8_t rgba[4]) {
    uint8_t palette[4][3];
    rgb565(read16(b + 8), palette[0]);
    rgb565(read16(b + 10), palette[1]);
    for (int i = 0; i < 3; ++i) {
        palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3;
        palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3;
    }
    unsigned color = (b[12 + pixel / 4] >> (2 * (pixel % 4))) & 3;
    memcpy(rgba, palette[color], 3);
    uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) indices |= (uint64_t)b[2 + i] << (8 * i);
    unsigned a = (indices >> (3 * pixel)) & 7;
    if (a < 2) rgba[3] = b[a];
    else if (b[0] > b[1]) rgba[3] = ((8 - a) * b[0] + (a - 1) * b[1]) / 7;
    else if (a < 6) rgba[3] = ((6 - a) * b[0] + (a - 1) * b[1]) / 5;
    else rgba[3] = a == 6 ? 0 : 255;
}

bool nvn_texture_region_color(const NvnTexture *t, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, float rgb[3]) {
    if (!t || !t->addr || !t->layout.fmt || !w || !h || x >= t->width || y >= t->height ||
        w > t->width - x || h > t->height - y || t->depth != 1 || !t->pool ||
        t->pool_offset > t->pool->size) return false;
    bool bc3 = t->format == 0x44 || t->format == 0x48;
    if (!bc3 && t->format != 0x25 && t->format != 0x38) return false;
    const FormatInfo *f = t->layout.fmt;
    LevelLayout l = tegra_level(&t->layout, 0);
    if (l.w != t->width || l.h != t->height) return false;
    bool linear = (t->flags & 0x10) || t->target == 10;
    if (linear && t->stride < (uint64_t)l.wb * f->bpb) return false;
    struct { unsigned weight, sum[3]; } bins[512] = {0};
    unsigned best = 0;
    uint32_t nx = w < 32 ? w : 32, ny = h < 32 ? h : 32;
    for (uint32_t j = 0; j < ny; ++j) for (uint32_t i = 0; i < nx; ++i) {
        uint32_t px = x + (uint64_t)(2 * i + 1) * w / (2 * nx);
        uint32_t py = y + (uint64_t)(2 * j + 1) * h / (2 * ny);
        uint32_t bx = px / f->bw, by = py / f->bh;
        uint64_t offset = linear
            ? (uint64_t)by * t->stride + bx * f->bpb
            : tegra_block_offset(&l, f->bpb, bx, by);
        if (offset > t->pool->size - t->pool_offset ||
            f->bpb > t->pool->size - t->pool_offset - offset) return false;
        const uint8_t *block = (const void *)(uintptr_t)(t->addr + offset);
        uint8_t rgba[4];
        if (bc3) bc3_pixel(block, (py % 4) * 4 + px % 4, rgba);
        else memcpy(rgba, block, 4);
        if (rgba[3] < 128) continue; // transparent padding and antialiased fringes
        unsigned bin = (rgba[0] >> 5) * 64 + (rgba[1] >> 5) * 8 + (rgba[2] >> 5);
        bins[bin].weight += rgba[3];
        for (int k = 0; k < 3; ++k) bins[bin].sum[k] += rgba[k] * rgba[3];
        if (bins[bin].weight > bins[best].weight) best = bin;
    }
    if (!bins[best].weight) return false;
    for (int k = 0; k < 3; ++k) rgb[k] = (float)bins[best].sum[k] / (255.0f * bins[best].weight);
    return true;
}

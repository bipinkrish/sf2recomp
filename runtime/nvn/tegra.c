#include "../constants/nvn.h"
// Tegra X1 block-linear layout. A GOB is 64 bytes x 8 rows; blocks stack GOBs vertically
// (block height) and in depth (block depth). Levels shrink their block height as they shrink.
#include "tegra.h"

#include <string.h>

static uint32_t div_up(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
static uint64_t align_up(uint64_t a, uint64_t b) { return (a + b - 1) / b * b; }

uint32_t tegra_default_block_height_log2(uint32_t height_in_blocks) {
    uint32_t gobs = div_up(height_in_blocks, SF2C_TEGRA_N_8_2C62);
    uint32_t log2 = 0;
    while ((SF2C_TEGRA_N_1U_B8E6 << log2) < gobs && log2 < SF2C_TEGRA_N_4_4B22) log2++;
    return log2;
}

static uint32_t block_depth_for(uint32_t depth) {
    uint32_t log2 = 0;
    while ((SF2C_TEGRA_N_1U_B8E6 << log2) < depth && log2 < SF2C_TEGRA_N_5_EF2D) log2++;
    return log2;
}

void tegra_layout_init(TexLayout *t) {
    uint32_t hb = div_up(t->height, t->fmt->bh);
    t->block_height_log2 = tegra_default_block_height_log2(hb);
    t->block_depth_log2 = t->depth > 1 ? block_depth_for(t->depth) : 0;
}

// Block height shrinks with the level so a block never exceeds the level height.
static uint32_t level_block_height(uint32_t bh_log2, uint32_t hb) {
    while (bh_log2 > 0 && (SF2C_TEGRA_N_8U_7D77 << (bh_log2 - 1)) >= hb) bh_log2--;
    return bh_log2;
}

static uint32_t level_block_depth(uint32_t bd_log2, uint32_t d) {
    while (bd_log2 > 0 && (SF2C_TEGRA_N_1U_B8E6 << (bd_log2 - 1)) >= d) bd_log2--;
    return bd_log2;
}

static uint64_t level_size(const TexLayout *t, uint32_t level, LevelLayout *out) {
    uint32_t w = t->width >> level, h = t->height >> level, d = t->depth >> level;
    if (!w) w = 1;
    if (!h) h = 1;
    if (!d) d = 1;
    uint32_t wb = div_up(w, t->fmt->bw), hb = div_up(h, t->fmt->bh);
    uint32_t bh = level_block_height(t->block_height_log2, hb);
    uint32_t bd = level_block_depth(t->block_depth_log2, d);
    uint32_t gob_rows = SF2C_TEGRA_N_8U_7D77 << bh, gob_depth = SF2C_TEGRA_N_1U_B8E6 << bd;
    uint64_t width_gobs = div_up(wb * t->fmt->bpb, SF2C_TEGRA_N_64_A68B);
    uint64_t size = width_gobs * SF2C_TEGRA_N_512_94F8 * (SF2C_TEGRA_N_1U_B8E6 << bh) * (SF2C_TEGRA_N_1U_B8E6 << bd) * div_up(hb, gob_rows) * div_up(d, gob_depth);
    if (out) {
        out->w = w; out->h = h; out->d = d;
        out->wb = wb; out->hb = hb;
        out->block_height_log2 = bh;
        out->block_depth_log2 = bd;
    }
    return size;
}

LevelLayout tegra_level(const TexLayout *t, uint32_t level) {
    LevelLayout l = {0};
    uint64_t off = 0;
    for (uint32_t i = 0; i < level; i++) off += level_size(t, i, NULL);
    level_size(t, level, &l);
    l.offset = off;
    return l;
}

uint64_t tegra_layer_size(const TexLayout *t) {
    uint64_t size = 0;
    for (uint32_t i = 0; i < t->levels; i++) size += level_size(t, i, NULL);
    if (t->layers > 1) {
        // layers are aligned to the level-0 block size
        uint32_t bh = t->block_height_log2;
        size = align_up(size, SF2C_TEGRA_N_512ULL_5E84 << bh);
    }
    return size;
}

uint64_t tegra_total_size(const TexLayout *t) {
    return tegra_layer_size(t) * (t->layers ? t->layers : 1);
}

// Offset of byte (x bytes, y rows, z slices) within a block-linear surface.
static inline uint64_t gob_offset(uint32_t xb, uint32_t y, uint32_t z, uint32_t width_gobs, uint32_t bh_log2,
                                  uint32_t bd_log2, uint64_t slice_blocks_rows) {
    uint32_t gob_x = xb >> SF2C_TEGRA_N_6_E7F6, gob_y = y >> SF2C_TEGRA_N_3_4E07;
    uint32_t block_y = gob_y >> bh_log2, gob_in_block_y = gob_y & ((SF2C_TEGRA_N_1U_B8E6 << bh_log2) - 1);
    uint32_t block_z = z >> bd_log2, z_in_block = z & ((SF2C_TEGRA_N_1U_B8E6 << bd_log2) - 1);
    uint64_t block_size = SF2C_TEGRA_N_512ULL_5E84 << (bh_log2 + bd_log2);
    uint64_t off = ((uint64_t)block_z * slice_blocks_rows + block_y) * width_gobs * block_size;
    off += (uint64_t)gob_x * block_size;
    off += ((uint64_t)z_in_block << bh_log2 | gob_in_block_y) * SF2C_TEGRA_N_512_94F8;
    // within the GOB: 64x8 bytes arranged in 16-byte sectors
    uint32_t xi = xb & SF2C_TEGRA_N_63_DA4E, yi = y & SF2C_TEGRA_N_7_7902;
    off += ((xi >> SF2C_TEGRA_N_5_EF2D) << SF2C_TEGRA_N_8_2C62) | ((yi >> 1) << SF2C_TEGRA_N_6_E7F6) | (((xi >> SF2C_TEGRA_N_4_4B22) & 1) << SF2C_TEGRA_N_5_EF2D) | ((yi & 1) << SF2C_TEGRA_N_4_4B22) | (xi & SF2C_TEGRA_N_0X0F_1F1D);
    return off;
}

uint64_t tegra_block_offset(const LevelLayout *l, uint32_t bpb, uint32_t x, uint32_t y) {
    uint32_t width_gobs = (l->wb * bpb + SF2C_TEGRA_N_63_DA4E) / SF2C_TEGRA_N_64_A68B;
    uint32_t gob_rows = SF2C_TEGRA_N_8U_7D77 << l->block_height_log2;
    return l->offset + gob_offset(x * bpb, y, 0, width_gobs,
        l->block_height_log2, l->block_depth_log2, (l->hb + gob_rows - 1) / gob_rows);
}

static void copy_surface(const uint8_t *src, uint8_t *dst, uint32_t wb, uint32_t hb, uint32_t d, uint32_t bpb,
                         uint32_t bh_log2, uint32_t bd_log2, bool to_linear) {
    uint32_t row_bytes = wb * bpb;
    uint32_t width_gobs = (row_bytes + SF2C_TEGRA_N_63_DA4E) / SF2C_TEGRA_N_64_A68B;
    uint32_t gob_rows = SF2C_TEGRA_N_8U_7D77 << bh_log2;
    uint64_t blocks_rows = (hb + gob_rows - 1) / gob_rows;
    for (uint32_t z = 0; z < d; z++) {
        for (uint32_t y = 0; y < hb; y++) {
            uint8_t *lin = dst + ((uint64_t)z * hb + y) * row_bytes;
            const uint8_t *lin_src = src + ((uint64_t)z * hb + y) * row_bytes;
            // 16-byte runs are contiguous in both layouts
            for (uint32_t x = 0; x < row_bytes; x += SF2C_TEGRA_N_16_B17E) {
                uint32_t n = row_bytes - x < SF2C_TEGRA_N_16_B17E ? row_bytes - x : SF2C_TEGRA_N_16_B17E;
                uint64_t o = gob_offset(x, y, z, width_gobs, bh_log2, bd_log2, blocks_rows);
                if (to_linear) memcpy(lin + x, src + o, n);
                else memcpy(dst + o, lin_src + x, n);
            }
        }
    }
}

void tegra_deswizzle(const uint8_t *src, uint8_t *dst, uint32_t wb, uint32_t hb, uint32_t d, uint32_t bpb,
                     uint32_t bh_log2, uint32_t bd_log2) {
    copy_surface(src, dst, wb, hb, d, bpb, bh_log2, bd_log2, true);
}

void tegra_swizzle(const uint8_t *src, uint8_t *dst, uint32_t wb, uint32_t hb, uint32_t d, uint32_t bpb,
                   uint32_t bh_log2, uint32_t bd_log2) {
    copy_surface(src, dst, wb, hb, d, bpb, bh_log2, bd_log2, false);
}

void tegra_swizzle_region(const uint8_t *src, uint8_t *dst,
                          uint32_t xoff, uint32_t yoff, uint32_t zoff,
                          uint32_t rw, uint32_t rh, uint32_t rd,
                          uint32_t wb, uint32_t hb, uint32_t bpb,
                          uint32_t bh_log2, uint32_t bd_log2) {
    uint32_t dst_row_bytes = wb * bpb;
    uint32_t width_gobs = (dst_row_bytes + SF2C_TEGRA_N_63_DA4E) / SF2C_TEGRA_N_64_A68B;
    uint32_t gob_rows = SF2C_TEGRA_N_8U_7D77 << bh_log2;
    uint64_t blocks_rows = (hb + gob_rows - 1) / gob_rows;
    uint32_t src_row_bytes = rw * bpb;
    for (uint32_t z = 0; z < rd; z++) {
        for (uint32_t y = 0; y < rh; y++) {
            const uint8_t *src_row = src + ((uint64_t)z * rh + y) * src_row_bytes;
            uint32_t dst_y = yoff + y;
            uint32_t dst_z = zoff + z;
            for (uint32_t x = 0; x < src_row_bytes; x++) {
                uint64_t o = gob_offset((xoff * bpb) + x, dst_y, dst_z, width_gobs, bh_log2, bd_log2, blocks_rows);
                dst[o] = src_row[x];
            }
        }
    }
}

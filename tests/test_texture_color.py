"""Keyboard prompt palettes come from the replaced sprite, including BC3 artwork."""
import pathlib
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class TextureColorTests(unittest.TestCase):
    def test_palette_ignores_transparency_and_stays_in_sprite_rect(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "nvn/texture_color.c"
#include "nvn/tegra.c"
#include "nvn/formats.c"

static NvnTexture texture(unsigned format, unsigned width, unsigned height, NvnPool *pool) {
    NvnTexture t = {.format=format, .width=width, .height=height, .depth=1, .levels=1};
    t.layout = (TexLayout){.width=width, .height=height, .depth=1, .layers=1,
                           .levels=1, .fmt=nvn_format_info(format)};
    tegra_layout_init(&t.layout);
    pool->size = tegra_total_size(&t.layout);
    pool->storage = (uintptr_t)calloc(1, pool->size);
    t.pool = pool; t.addr = pool->storage;
    return t;
}

int main(void) {
    float rgb[3];
    NvnPool pool;
    NvnTexture t = texture(0x25, 32, 32, &pool);
    uint8_t pixels[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; ++i) {
        pixels[i*4] = 255; pixels[i*4+1] = 0; pixels[i*4+2] = 0; pixels[i*4+3] = 255;
    }
    for (int y = 8; y < 20; ++y) for (int x = 4; x < 16; ++x) {
        uint8_t *p = pixels + (y * 32 + x) * 4;
        p[0] = 232; p[1] = 208; p[2] = 176;
        p[3] = (x < 8 || y < 12) ? 0 : 255;
    }
    LevelLayout l = tegra_level(&t.layout, 0);
    tegra_swizzle(pixels, (void *)(uintptr_t)t.addr, l.wb, l.hb, 1, 4,
                  l.block_height_log2, l.block_depth_log2);
    assert(nvn_texture_region_color(&t, 4, 8, 12, 12, rgb));
    assert(fabsf(rgb[0] - 232/255.f) < .001f && fabsf(rgb[1] - 208/255.f) < .001f);
    assert(!nvn_texture_region_color(&t, 4, 8, 4, 4, rgb)); // transparent padding
    assert(!nvn_texture_region_color(&t, 31, 31, 2, 2, rgb));
    free((void *)(uintptr_t)pool.storage);

    t = texture(0x44, 16, 16, &pool);
    uint8_t blocks[4 * 4 * 16] = {0};
    for (int i = 0; i < 16; ++i) {
        uint8_t *b = blocks + i * 16;
        b[0] = 255; // alpha indices all zero -> opaque
        b[8] = 0xa1; b[9] = 0x18; // constant dark RGB565 artwork
    }
    // Transparent white occupies more area than the visible dark foreground.
    for (int i = 0; i < 10; ++i) {
        blocks[i*16] = 0; blocks[i*16+8] = 255; blocks[i*16+9] = 255;
    }
    l = tegra_level(&t.layout, 0);
    tegra_swizzle(blocks, (void *)(uintptr_t)t.addr, l.wb, l.hb, 1, 16,
                  l.block_height_log2, l.block_depth_log2);
    assert(nvn_texture_region_color(&t, 0, 0, 16, 16, rgb));
    assert(fabsf(rgb[0] - 25/255.f) < .001f && fabsf(rgb[1] - 20/255.f) < .001f);
    assert(fabsf(rgb[2] - 8/255.f) < .001f);
    pool.size = 1;
    assert(!nvn_texture_region_color(&t, 0, 0, 16, 16, rgb));
    free((void *)(uintptr_t)pool.storage);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-palette-test-") as temp:
            root = pathlib.Path(temp)
            (root / "test.c").write_text(source)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True)

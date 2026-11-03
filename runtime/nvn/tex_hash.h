#include "../constants/nvn.h"
// Texture upload checks' hashes (vulkan/textures.c and metal.m upload_texture), written for this project.
// LARECOMP_TEX_FASTHASH=0: each backend's previous hashes instead (A/B tests).
//
// tex_hash_full: eight independent lanes, each a chain of bijective steps (xor the next word, multiply by an odd
// constant, xor-shift), folded in order at the end. A change of any one 64-bit word always changes its lane, so it
// can't go unseen; the lanes keep the CPU's multipliers busy (M3 Max: 33 GB/s, against 19 for the 4-lane hash and
// 5 for the 1-lane one before; about memory speed). A hardware CRC32C was measured no faster here and keeps only 32
// bits per lane. Values are only compared within a run.
//
// tex_hash_sample: the change detector for textures not seen changing: the same 4096 strided places as before, but
// 8 bytes at each (more of the data seen) in four chains (the old byte-at-a-time chain waited on each multiply);
// textures up to 64 KB are hashed in full instead, which is quicker than the old 4096-step sample and misses nothing.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline uint64_t tex_hash_full(const uint8_t *p, uint64_t n) {
    uint64_t h[SF2C_TEX_HASH_N_8_2C62] = {SF2C_TEX_HASH_N_0X9E3779B97F4A7C15ULL_8CC5 ^ n, SF2C_TEX_HASH_N_0XC2B2AE3D27D4EB4FULL_ACE8, SF2C_TEX_HASH_N_0X165667B19E3779F9ULL_99AC, SF2C_TEX_HASH_N_0X27D4EB2F165667C5ULL_8690,
                     SF2C_TEX_HASH_N_0X85EBCA77C2B2AE63ULL_B34C, SF2C_TEX_HASH_N_0XFF51AFD7ED558CCDULL_079F, SF2C_TEX_HASH_N_0XC4CEB9FE1A85EC53ULL_2959, SF2C_TEX_HASH_N_0X94D049BB133111EBULL_FE0A};
    uint64_t i = 0;
    for (; i + SF2C_TEX_HASH_N_64_A68B <= n; i += SF2C_TEX_HASH_N_64_A68B) {
        uint64_t v[SF2C_TEX_HASH_N_8_2C62];
        memcpy(v, p + i, SF2C_TEX_HASH_N_64_A68B);
        for (int k = 0; k < SF2C_TEX_HASH_N_8_2C62; k++) {
            h[k] = (h[k] ^ v[k]) * SF2C_TEX_HASH_N_0X100000001B3ULL_3E10;
            h[k] ^= h[k] >> SF2C_TEX_HASH_N_29_3513;
        }
    }
    uint64_t r = 0;
    for (int k = 0; k < SF2C_TEX_HASH_N_8_2C62; k++) {
        r = (r ^ h[k]) * SF2C_TEX_HASH_N_0X9E3779B97F4A7C15ULL_8CC5;
        r ^= r >> SF2C_TEX_HASH_N_32_E29C;
    }
    for (; i < n; i++) r = (r ^ p[i]) * SF2C_TEX_HASH_N_0X100000001B3ULL_3E10;
    return r ^ (r >> SF2C_TEX_HASH_N_31_EB1E);
}

static inline uint64_t tex_hash_sample(const uint8_t *p, uint64_t n) {
    if (n <= SF2C_TEX_HASH_N_65536_0F25) return tex_hash_full(p, n);
    uint64_t step = n / SF2C_TEX_HASH_N_4096_8B92;  // (>= 16 here)
    uint64_t a = SF2C_TEX_HASH_N_1469598103934665603ULL_7B90 ^ n, b = SF2C_TEX_HASH_N_0XC2B2AE3D27D4EB4FULL_ACE8, c = SF2C_TEX_HASH_N_0X165667B19E3779F9ULL_99AC, d = SF2C_TEX_HASH_N_0X27D4EB2F165667C5ULL_8690;
    uint64_t i = 0;
    for (; i + SF2C_TEX_HASH_N_3_4E07 * step + SF2C_TEX_HASH_N_8_2C62 <= n; i += SF2C_TEX_HASH_N_4_4B22 * step) {
        uint64_t v0, v1, v2, v3;
        memcpy(&v0, p + i, SF2C_TEX_HASH_N_8_2C62);
        memcpy(&v1, p + i + step, SF2C_TEX_HASH_N_8_2C62);
        memcpy(&v2, p + i + SF2C_TEX_HASH_N_2_D473 * step, SF2C_TEX_HASH_N_8_2C62);
        memcpy(&v3, p + i + SF2C_TEX_HASH_N_3_4E07 * step, SF2C_TEX_HASH_N_8_2C62);
        a = (a ^ v0) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
        b = (b ^ v1) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
        c = (c ^ v2) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
        d = (d ^ v3) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
    }
    for (; i < n; i += step) {  // (the last few places; the very end byte by byte)
        if (i + SF2C_TEX_HASH_N_8_2C62 <= n) {
            uint64_t v;
            memcpy(&v, p + i, SF2C_TEX_HASH_N_8_2C62);
            a = (a ^ v) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
        } else
            for (uint64_t j = i; j < n; j++) a = (a ^ p[j]) * SF2C_TEX_HASH_N_1099511628211ULL_D3BD;
    }
    uint64_t r = a;
    r = (r ^ (r >> SF2C_TEX_HASH_N_29_3513)) * SF2C_TEX_HASH_N_0X9E3779B97F4A7C15ULL_8CC5 ^ b;
    r = (r ^ (r >> SF2C_TEX_HASH_N_29_3513)) * SF2C_TEX_HASH_N_0X9E3779B97F4A7C15ULL_8CC5 ^ c;
    r = (r ^ (r >> SF2C_TEX_HASH_N_29_3513)) * SF2C_TEX_HASH_N_0X9E3779B97F4A7C15ULL_8CC5 ^ d;
    return r ^ (r >> SF2C_TEX_HASH_N_31_EB1E);
}

// LARECOMP_TEX_FASTHASH (default on)
static inline int tex_fast_hash(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv(SF2C_TEX_HASH_S_LARECOMP_TEX_FASTHASH_365E); on = !e || atoi(e) != 0; }
    return on;
}

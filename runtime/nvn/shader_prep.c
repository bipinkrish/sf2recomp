#include "../constants/nvn.h"
// Shader variant records, the recorder and first-visit counts (see shader_prep.h).
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nvn.h"
#include "shader_prep.h"

uint64_t shader_code_hash(const uint8_t *code, size_t n) {  // FNV-1a, 64-bit
    uint64_t h = SF2C_SHADER_PREP_N_0XCBF29CE484222325ULL_7EE8;
    for (size_t i = 0; i < n; i++) { h ^= code[i]; h *= SF2C_SHADER_PREP_N_0X100000001B3ULL_3E10; }
    return h;
}

uint64_t shader_stage_hash(const struct NvnShaderStage *s) {
    if (!s->code_addr || !s->const_offset) return 0;
    return shader_code_hash((const uint8_t *)(uintptr_t)s->code_addr, (size_t)s->const_offset + s->const_size);
}

// ------------------------------------------------------------------ stale-program detector (shader_prep.h)
static uint32_t fp_len(const struct NvnShaderStage *s) { return s->const_offset ? s->const_offset + s->const_size : 0; }

static void fp_sample(const struct NvnShaderStage *s, uint32_t len, uint32_t w[SF2C_SHADER_PREP_N_4_4B22]) {
    const uint32_t *code = (const uint32_t *)(uintptr_t)s->code_addr;
    uint32_t n = len / SF2C_SHADER_PREP_N_4_4B22;  // (len >= 4: checked by the caller)
    w[0] = code[n > SF2C_SHADER_PREP_N_0X20_A766 ? SF2C_SHADER_PREP_N_0X20_A766 : 0];  // the first instruction word (after the NVN header and the program header)
    w[1] = code[n / SF2C_SHADER_PREP_N_3_4E07];
    w[SF2C_SHADER_PREP_N_2_D473] = code[SF2C_SHADER_PREP_N_2_D473 * (n / SF2C_SHADER_PREP_N_3_4E07)];
    w[SF2C_SHADER_PREP_N_3_4E07] = code[n - 1];                // the last word of the constants
}

void shader_fp_check(ShaderFp *fp, const struct NvnShaderStage *s, const char *stage) {
    if (fp->state == SF2C_SHADER_PREP_N_2_D473) return;
    uint32_t len = fp_len(s), w[SF2C_SHADER_PREP_N_4_4B22];
    if (fp->state == 0) {
        if (!s->code_addr || len < SF2C_SHADER_PREP_N_4_4B22) { fp->state = SF2C_SHADER_PREP_N_2_D473; return; }
        fp->hash = shader_stage_hash(s);
        fp->len = len;
        fp_sample(s, len, fp->w);
        fp->state = 1;
        return;
    }
    if (len == fp->len && s->code_addr) {
        fp_sample(s, len, w);
        if (!memcmp(w, fp->w, sizeof w)) return;
    }
    fp->state = SF2C_SHADER_PREP_N_2_D473;  // (reported once per fingerprint: no per-draw rehash after this)
    static _Atomic int logged;
    if (atomic_fetch_add(&logged, 1) < SF2C_SHADER_PREP_N_16_B17E)
        LOG(SF2C_SHADER_PREP_S_SHADER_STALE_PROGRAM_CODE_B39B PRIx64 SF2C_SHADER_PREP_S_WAS_016_FBB1 PRIx64 SF2C_SHADER_PREP_S_NOW_016_360B PRIx64 SF2C_SHADER_PREP_S_STAGE_S_X_X_BYTES_N_2CA1,
            s->code_addr, fp->hash, s->code_addr && len ? shader_stage_hash(s) : 0, stage, fp->len, len);
}

uint8_t shader_topo_class(uint32_t mode) { return mode == 0 ? 0 : mode == 1 || mode == SF2C_SHADER_PREP_N_3_4E07 ? 1 : SF2C_SHADER_PREP_N_2_D473; }

// ------------------------------------------------------------------ text form
// G vs=<hash> fs=<hash> f=<flags> a=<attrib,...> s=<stride/divisor,...> c=<format[:view],...> d=<depth> n=<samples>
//   m=<channel mask> b=<blend enables> B=<blend state of each target, 16 hex digits,...> k=<a2c> t=<topology> p=<fill>
// C cs=<hash> l=<x,y,z>
void shader_rec_format(const ShaderRec *r, char *out, size_t n) {
    size_t o = 0;
#define PUT(...) do { if (o < n) o += (size_t)snprintf(out + o, n - o, __VA_ARGS__); } while (0)
    if (r->kind == SF2C_SHADER_PREP_CH_C_809F) {
        PUT(SF2C_SHADER_PREP_S_C_CS_016_BCFA PRIx64 SF2C_SHADER_PREP_S_L_U_U_U_B6DA, r->vs, r->local[0], r->local[1], r->local[SF2C_SHADER_PREP_N_2_D473]);
        return;
    }
    PUT(SF2C_SHADER_PREP_S_G_VS_016_D793 PRIx64 SF2C_SHADER_PREP_S_FS_016_7F7D PRIx64 SF2C_SHADER_PREP_S_F_X_A_798E, r->vs, r->fs, r->flags);
    for (int i = 0; i < r->nattrib; i++) PUT(SF2C_SHADER_PREP_S_S_X_7B91, i ? SF2C_SHADER_PREP_S_EMPTY_08B5 : SF2C_SHADER_PREP_S_EMPTY_12AE, r->attrib[i]);
    PUT(SF2C_SHADER_PREP_S_S_19FB);
    for (int i = 0; i < r->nstream; i++) PUT(SF2C_SHADER_PREP_S_S_X_X_2814, i ? SF2C_SHADER_PREP_S_EMPTY_08B5 : SF2C_SHADER_PREP_S_EMPTY_12AE, r->stream[i][0], r->stream[i][1]);
    PUT(SF2C_SHADER_PREP_S_C_EFEC);
    for (int i = 0; i < r->ncolor; i++) {
        PUT(SF2C_SHADER_PREP_S_S_X_7B91, i ? SF2C_SHADER_PREP_S_EMPTY_08B5 : SF2C_SHADER_PREP_S_EMPTY_12AE, r->color[i]);
        if (r->cview[i]) PUT(SF2C_SHADER_PREP_S_X_20FF, r->cview[i]);
    }
    PUT(SF2C_SHADER_PREP_S_D_X_N_U_M_X_B_X_B_5B22, r->depth, r->samples, r->channel_mask, r->blend_on);
    bool first = true;
    for (int i = 0; i < SF2C_SHADER_PREP_N_8_2C62; i++) {
        if (!((r->blend_on >> i) & 1)) continue;
        PUT(SF2C_SHADER_PREP_S_S_0030, first ? SF2C_SHADER_PREP_S_EMPTY_12AE : SF2C_SHADER_PREP_S_EMPTY_08B5);
        first = false;
        for (int k = 0; k < SF2C_SHADER_PREP_N_8_2C62; k++) PUT(SF2C_SHADER_PREP_S_02X_5FB1, r->blend[i][k]);
    }
    PUT(SF2C_SHADER_PREP_S_K_U_T_U_P_U_01AA, r->a2c, r->topo, r->poly_fill);
#undef PUT
}

static const char *field(const char *line, const char *name) {  // value after " name=" (or at the start)
    size_t l = strlen(name);
    for (const char *p = line; (p = strstr(p, name)); p++)
        if ((p == line || p[-1] == SF2C_SHADER_PREP_CH_EMPTY_730D) && p[l] == SF2C_SHADER_PREP_CH_EMPTY_C874) return p + l + 1;
    return NULL;
}

static int hex_list(const char *p, uint32_t *out, int max) {  // "a,b,c" (hex) up to a space / the end
    int n = 0;
    while (p && *p && *p != SF2C_SHADER_PREP_CH_EMPTY_730D && *p != SF2C_SHADER_PREP_CH_N_AD65 && *p != SF2C_SHADER_PREP_CH_R_4859 && n < max) {
        char *e;
        out[n++] = (uint32_t)strtoul(p, &e, SF2C_SHADER_PREP_N_16_B17E);
        if (e == p) return -1;
        p = *e == SF2C_SHADER_PREP_CH_EMPTY_EE1B ? e + 1 : e;
    }
    return n;
}

bool shader_rec_parse(const char *line, ShaderRec *r) {
    memset(r, 0, sizeof *r);
    while (*line == SF2C_SHADER_PREP_CH_EMPTY_730D || *line == SF2C_SHADER_PREP_CH_T_27AE) line++;
    if (line[0] != SF2C_SHADER_PREP_CH_G_C429 && line[0] != SF2C_SHADER_PREP_CH_C_809F) return false;
    r->kind = line[0];
    const char *v;
    if (r->kind == SF2C_SHADER_PREP_CH_C_809F) {
        if (!(v = field(line, SF2C_SHADER_PREP_S_CS_DE6E))) return false;
        r->vs = strtoull(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
        if (!(v = field(line, SF2C_SHADER_PREP_S_L_FBDC))) return false;
        for (int i = 0; i < SF2C_SHADER_PREP_N_3_4E07; i++) {  // (decimal)
            char *e;
            r->local[i] = (uint32_t)strtoul(v, &e, SF2C_SHADER_PREP_N_10_4A44);
            if (e == v) return false;
            v = *e == SF2C_SHADER_PREP_CH_EMPTY_EE1B ? e + 1 : e;
        }
        return r->vs != 0;
    }
    if (!(v = field(line, SF2C_SHADER_PREP_S_VS_35ED))) return false;
    r->vs = strtoull(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    if (!(v = field(line, SF2C_SHADER_PREP_S_FS_FEFF))) return false;
    r->fs = strtoull(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    if ((v = field(line, SF2C_SHADER_PREP_S_F_30F8))) r->flags = (uint8_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    int n = (v = field(line, SF2C_SHADER_PREP_S_A_AC8D)) ? hex_list(v, r->attrib, SF2C_SHADER_PREP_N_16_B17E) : 0;
    if (n < 0) return false;
    r->nattrib = (uint8_t)n;
    if ((v = field(line, SF2C_SHADER_PREP_S_S_0F46))) {
        while (*v && *v != SF2C_SHADER_PREP_CH_EMPTY_730D && r->nstream < SF2C_SHADER_PREP_N_16_B17E) {
            char *e;
            r->stream[r->nstream][0] = (uint32_t)strtoul(v, &e, SF2C_SHADER_PREP_N_16_B17E);
            if (*e != SF2C_SHADER_PREP_CH_EMPTY_0825) return false;
            r->stream[r->nstream][1] = (uint32_t)strtoul(e + 1, &e, SF2C_SHADER_PREP_N_16_B17E);
            r->nstream++;
            v = *e == SF2C_SHADER_PREP_CH_EMPTY_EE1B ? e + 1 : e;
        }
    }
    if ((v = field(line, SF2C_SHADER_PREP_S_C_8799))) {
        while (*v && *v != SF2C_SHADER_PREP_CH_EMPTY_730D && r->ncolor < SF2C_SHADER_PREP_N_8_2C62) {
            char *e;
            r->color[r->ncolor] = (uint32_t)strtoul(v, &e, SF2C_SHADER_PREP_N_16_B17E);
            if (*e == SF2C_SHADER_PREP_CH_EMPTY_4266) r->cview[r->ncolor] = (uint32_t)strtoul(e + 1, &e, SF2C_SHADER_PREP_N_16_B17E);
            r->ncolor++;
            v = *e == SF2C_SHADER_PREP_CH_EMPTY_EE1B ? e + 1 : e;
        }
    }
    if ((v = field(line, SF2C_SHADER_PREP_S_D_3FA5))) r->depth = (uint32_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    r->samples = (v = field(line, SF2C_SHADER_PREP_S_N_46A3)) ? (uint32_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E) : 1;
    if (!r->samples) r->samples = 1;
    if ((v = field(line, SF2C_SHADER_PREP_S_M_1D57))) r->channel_mask = (uint32_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    if ((v = field(line, SF2C_SHADER_PREP_S_B_C100))) r->blend_on = (uint8_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    if ((v = field(line, SF2C_SHADER_PREP_S_B_955C))) {
        for (int i = 0; i < SF2C_SHADER_PREP_N_8_2C62; i++) {
            if (!((r->blend_on >> i) & 1)) continue;
            for (int k = 0; k < SF2C_SHADER_PREP_N_8_2C62; k++) {
                unsigned b;
                if (sscanf(v, SF2C_SHADER_PREP_S_2X_A4B9, &b) != 1) return false;
                r->blend[i][k] = (uint8_t)b;
                v += SF2C_SHADER_PREP_N_2_D473;
            }
            if (*v == SF2C_SHADER_PREP_CH_EMPTY_EE1B) v++;
        }
    }
    if ((v = field(line, SF2C_SHADER_PREP_S_K_3766))) r->a2c = (uint8_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E);
    r->topo = (v = field(line, SF2C_SHADER_PREP_S_T_8CA5)) ? (uint8_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E) : SF2C_SHADER_PREP_N_2_D473;
    r->poly_fill = (v = field(line, SF2C_SHADER_PREP_S_P_1FB6)) ? (uint8_t)strtoul(v, NULL, SF2C_SHADER_PREP_N_16_B17E) : 1;
    return r->vs && r->fs;
}

// ------------------------------------------------------------------ recorder (SF2RECOMP_RECORD_KEYS)
static pthread_mutex_t rec_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t *rec_seen;  // line hashes (open addressing, 0 = empty)
static size_t rec_cap, rec_n;

static bool seen_add(uint64_t h) {  // false: already there
    if (!h) h = 1;
    if (rec_n * SF2C_SHADER_PREP_N_2_D473 >= rec_cap) {
        size_t cap = rec_cap ? rec_cap * SF2C_SHADER_PREP_N_2_D473 : SF2C_SHADER_PREP_N_4096_8B92;
        uint64_t *t = calloc(cap, sizeof *t);
        for (size_t i = 0; i < rec_cap; i++)
            if (rec_seen[i]) {
                size_t j = rec_seen[i] & (cap - 1);
                while (t[j]) j = (j + 1) & (cap - 1);
                t[j] = rec_seen[i];
            }
        free(rec_seen);
        rec_seen = t, rec_cap = cap;
    }
    size_t j = h & (rec_cap - 1);
    while (rec_seen[j]) {
        if (rec_seen[j] == h) return false;
        j = (j + 1) & (rec_cap - 1);
    }
    rec_seen[j] = h;
    rec_n++;
    return true;
}

static const char *rec_path(void) {
    static const char *p;
    static int done;
    if (!done) { p = getenv(SF2C_SHADER_PREP_S_SF2RECOMP_RECORD_KEYS_8236); if (p && !*p) p = NULL; done = 1; }
    return p;
}

bool shader_rec_recording(void) { return rec_path() != NULL || shader_seen_on(); }

void shader_rec_note(const ShaderRec *r) {
    shader_seen_note(r);  // (the player's own list: queued, written off this thread)
    const char *path = rec_path();
    if (!path) return;
    char line[SF2C_SHADER_PREP_N_2048_BFA0];
    shader_rec_format(r, line, sizeof line);
    pthread_mutex_lock(&rec_lock);
    static bool loaded;
    if (!loaded) {  // (an earlier run's records: appended to, never repeated)
        loaded = true;
        FILE *f = fopen(path, SF2C_SHADER_PREP_S_R_2659);
        char l[SF2C_SHADER_PREP_N_2048_BFA0];
        while (f && fgets(l, sizeof l, f)) {
            l[strcspn(l, SF2C_SHADER_PREP_S_R_N_645E)] = 0;
            if (l[0] == SF2C_SHADER_PREP_CH_G_C429 || l[0] == SF2C_SHADER_PREP_CH_C_809F) seen_add(shader_code_hash((const uint8_t *)l, strlen(l)));
        }
        if (f) fclose(f);
    }
    if (seen_add(shader_code_hash((const uint8_t *)line, strlen(line)))) {
        FILE *f = fopen(path, SF2C_SHADER_PREP_S_A_AC8D);
        if (f) { fprintf(f, SF2C_SHADER_PREP_S_S_N_1F0E, line); fclose(f); }
    }
    pthread_mutex_unlock(&rec_lock);
}

// ------------------------------------------------------------------ first visits
// (once per program / pipeline / compute shader for the process: a reboot into another seed builds them again, from
// the system's shader caches)
void shader_first_visit(const char *what, uint64_t a, uint64_t b, uint64_t id) {
    static _Atomic int n;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static uint64_t *seen;
    static size_t cap, cnt;
    uint64_t h = id ^ (uint64_t)(uint8_t)what[1] << SF2C_SHADER_PREP_N_56_7688;
    if (!h) h = 1;
    pthread_mutex_lock(&lock);
    if (cnt * SF2C_SHADER_PREP_N_2_D473 >= cap) {
        size_t nc = cap ? cap * SF2C_SHADER_PREP_N_2_D473 : SF2C_SHADER_PREP_N_1024_E39E;
        uint64_t *t = calloc(nc, sizeof *t);
        for (size_t i = 0; i < cap; i++)
            if (seen[i]) {
                size_t j = seen[i] & (nc - 1);
                while (t[j]) j = (j + 1) & (nc - 1);
                t[j] = seen[i];
            }
        free(seen);
        seen = t, cap = nc;
    }
    size_t j = h & (cap - 1);
    while (seen[j] && seen[j] != h) j = (j + 1) & (cap - 1);
    bool fresh = !seen[j];
    if (fresh) seen[j] = h, cnt++;
    pthread_mutex_unlock(&lock);
    if (!fresh) return;
    int k = ++n;
    if (b) LOG(SF2C_SHADER_PREP_S_SHADER_FIRST_VISIT_D_S_016_9950 PRIx64 SF2C_SHADER_PREP_S_016_4A1E PRIx64 SF2C_SHADER_PREP_S_N_D7A5, k, what, a, b);
    else LOG(SF2C_SHADER_PREP_S_SHADER_FIRST_VISIT_D_S_016_9950 PRIx64 SF2C_SHADER_PREP_S_N_D7A5, k, what, a);
}

// ------------------------------------------------------------------ cache tags
#ifndef SHADER_TRANSLATOR_ID
#define SHADER_TRANSLATOR_ID SF2C_SHADER_PREP_SHADER_TRANSLATOR_ID
#endif
const char *sf2recomp_version(void);  // runtime/ui/menu_stubs.c

void shader_cache_tag(char *out, size_t n, const char *gpu) {
    snprintf(out, n, SF2C_SHADER_PREP_S_S_TRANSLATOR_S_CACHE_D_S_73DF, sf2recomp_version(), SHADER_TRANSLATOR_ID, SHADER_CACHE_VERSION, gpu);
}

bool shader_cache_tag_ok(const char *tag_path, const char *tag) {
    FILE *f = fopen(tag_path, SF2C_SHADER_PREP_S_RB_46CA);
    if (!f) return false;
    char have[SF2C_SHADER_PREP_N_1024_E39E];
    size_t got = fread(have, 1, sizeof have - 1, f);
    fclose(f);
    have[got] = 0;
    return !strcmp(have, tag);
}

bool shader_cache_write(const char *path, const void *data, size_t n) {
    char tmp[SF2C_SHADER_PREP_N_1100_AB98];
    snprintf(tmp, sizeof tmp, SF2C_SHADER_PREP_S_S_TMP_A54A, path);
    mkdir(SF2C_SHADER_PREP_S_CACHE_C87A, SF2C_SHADER_PREP_N_0755_64A5);
    FILE *f = fopen(tmp, SF2C_SHADER_PREP_S_WB_3D6A);
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok &= fclose(f) == 0;
    if (ok) {
#ifdef _WIN32
        remove(path);  // (rename doesn't replace on Windows; a crash here loses the cache, never corrupts it)
#endif
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    return ok;
}

void shader_cache_tag_write(const char *tag_path, const char *tag) { shader_cache_write(tag_path, tag, strlen(tag)); }

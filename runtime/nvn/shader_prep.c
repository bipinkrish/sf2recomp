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
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= code[i]; h *= 0x100000001B3ull; }
    return h;
}

uint64_t shader_stage_hash(const struct NvnShaderStage *s) {
    if (!s->code_addr || !s->const_offset) return 0;
    return shader_code_hash((const uint8_t *)(uintptr_t)s->code_addr, (size_t)s->const_offset + s->const_size);
}

// ------------------------------------------------------------------ stale-program detector (shader_prep.h)
static uint32_t fp_len(const struct NvnShaderStage *s) { return s->const_offset ? s->const_offset + s->const_size : 0; }

static void fp_sample(const struct NvnShaderStage *s, uint32_t len, uint32_t w[4]) {
    const uint32_t *code = (const uint32_t *)(uintptr_t)s->code_addr;
    uint32_t n = len / 4;  // (len >= 4: checked by the caller)
    w[0] = code[n > 0x20 ? 0x20 : 0];  // the first instruction word (after the NVN header and the program header)
    w[1] = code[n / 3];
    w[2] = code[2 * (n / 3)];
    w[3] = code[n - 1];                // the last word of the constants
}

void shader_fp_check(ShaderFp *fp, const struct NvnShaderStage *s, const char *stage) {
    if (fp->state == 2) return;
    uint32_t len = fp_len(s), w[4];
    if (fp->state == 0) {
        if (!s->code_addr || len < 4) { fp->state = 2; return; }
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
    fp->state = 2;  // (reported once per fingerprint: no per-draw rehash after this)
    static _Atomic int logged;
    if (atomic_fetch_add(&logged, 1) < 16)
        LOG("[shader] stale program: code %" PRIx64 " was %016" PRIx64 " now %016" PRIx64 " (stage %s, %#x -> %#x bytes)\n",
            s->code_addr, fp->hash, s->code_addr && len ? shader_stage_hash(s) : 0, stage, fp->len, len);
}

uint8_t shader_topo_class(uint32_t mode) { return mode == 0 ? 0 : mode == 1 || mode == 3 ? 1 : 2; }

// ------------------------------------------------------------------ text form
// G vs=<hash> fs=<hash> f=<flags> a=<attrib,...> s=<stride/divisor,...> c=<format[:view],...> d=<depth> n=<samples>
//   m=<channel mask> b=<blend enables> B=<blend state of each target, 16 hex digits,...> k=<a2c> t=<topology> p=<fill>
// C cs=<hash> l=<x,y,z>
void shader_rec_format(const ShaderRec *r, char *out, size_t n) {
    size_t o = 0;
#define PUT(...) do { if (o < n) o += (size_t)snprintf(out + o, n - o, __VA_ARGS__); } while (0)
    if (r->kind == 'C') {
        PUT("C cs=%016" PRIx64 " l=%u,%u,%u", r->vs, r->local[0], r->local[1], r->local[2]);
        return;
    }
    PUT("G vs=%016" PRIx64 " fs=%016" PRIx64 " f=%x a=", r->vs, r->fs, r->flags);
    for (int i = 0; i < r->nattrib; i++) PUT("%s%x", i ? "," : "", r->attrib[i]);
    PUT(" s=");
    for (int i = 0; i < r->nstream; i++) PUT("%s%x/%x", i ? "," : "", r->stream[i][0], r->stream[i][1]);
    PUT(" c=");
    for (int i = 0; i < r->ncolor; i++) {
        PUT("%s%x", i ? "," : "", r->color[i]);
        if (r->cview[i]) PUT(":%x", r->cview[i]);
    }
    PUT(" d=%x n=%u m=%x b=%x B=", r->depth, r->samples, r->channel_mask, r->blend_on);
    bool first = true;
    for (int i = 0; i < 8; i++) {
        if (!((r->blend_on >> i) & 1)) continue;
        PUT("%s", first ? "" : ",");
        first = false;
        for (int k = 0; k < 8; k++) PUT("%02x", r->blend[i][k]);
    }
    PUT(" k=%u t=%u p=%u", r->a2c, r->topo, r->poly_fill);
#undef PUT
}

static const char *field(const char *line, const char *name) {  // value after " name=" (or at the start)
    size_t l = strlen(name);
    for (const char *p = line; (p = strstr(p, name)); p++)
        if ((p == line || p[-1] == ' ') && p[l] == '=') return p + l + 1;
    return NULL;
}

static int hex_list(const char *p, uint32_t *out, int max) {  // "a,b,c" (hex) up to a space / the end
    int n = 0;
    while (p && *p && *p != ' ' && *p != '\n' && *p != '\r' && n < max) {
        char *e;
        out[n++] = (uint32_t)strtoul(p, &e, 16);
        if (e == p) return -1;
        p = *e == ',' ? e + 1 : e;
    }
    return n;
}

bool shader_rec_parse(const char *line, ShaderRec *r) {
    memset(r, 0, sizeof *r);
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] != 'G' && line[0] != 'C') return false;
    r->kind = line[0];
    const char *v;
    if (r->kind == 'C') {
        if (!(v = field(line, "cs"))) return false;
        r->vs = strtoull(v, NULL, 16);
        if (!(v = field(line, "l"))) return false;
        for (int i = 0; i < 3; i++) {  // (decimal)
            char *e;
            r->local[i] = (uint32_t)strtoul(v, &e, 10);
            if (e == v) return false;
            v = *e == ',' ? e + 1 : e;
        }
        return r->vs != 0;
    }
    if (!(v = field(line, "vs"))) return false;
    r->vs = strtoull(v, NULL, 16);
    if (!(v = field(line, "fs"))) return false;
    r->fs = strtoull(v, NULL, 16);
    if ((v = field(line, "f"))) r->flags = (uint8_t)strtoul(v, NULL, 16);
    int n = (v = field(line, "a")) ? hex_list(v, r->attrib, 16) : 0;
    if (n < 0) return false;
    r->nattrib = (uint8_t)n;
    if ((v = field(line, "s"))) {
        while (*v && *v != ' ' && r->nstream < 16) {
            char *e;
            r->stream[r->nstream][0] = (uint32_t)strtoul(v, &e, 16);
            if (*e != '/') return false;
            r->stream[r->nstream][1] = (uint32_t)strtoul(e + 1, &e, 16);
            r->nstream++;
            v = *e == ',' ? e + 1 : e;
        }
    }
    if ((v = field(line, "c"))) {
        while (*v && *v != ' ' && r->ncolor < 8) {
            char *e;
            r->color[r->ncolor] = (uint32_t)strtoul(v, &e, 16);
            if (*e == ':') r->cview[r->ncolor] = (uint32_t)strtoul(e + 1, &e, 16);
            r->ncolor++;
            v = *e == ',' ? e + 1 : e;
        }
    }
    if ((v = field(line, "d"))) r->depth = (uint32_t)strtoul(v, NULL, 16);
    r->samples = (v = field(line, "n")) ? (uint32_t)strtoul(v, NULL, 16) : 1;
    if (!r->samples) r->samples = 1;
    if ((v = field(line, "m"))) r->channel_mask = (uint32_t)strtoul(v, NULL, 16);
    if ((v = field(line, "b"))) r->blend_on = (uint8_t)strtoul(v, NULL, 16);
    if ((v = field(line, "B"))) {
        for (int i = 0; i < 8; i++) {
            if (!((r->blend_on >> i) & 1)) continue;
            for (int k = 0; k < 8; k++) {
                unsigned b;
                if (sscanf(v, "%2x", &b) != 1) return false;
                r->blend[i][k] = (uint8_t)b;
                v += 2;
            }
            if (*v == ',') v++;
        }
    }
    if ((v = field(line, "k"))) r->a2c = (uint8_t)strtoul(v, NULL, 16);
    r->topo = (v = field(line, "t")) ? (uint8_t)strtoul(v, NULL, 16) : 2;
    r->poly_fill = (v = field(line, "p")) ? (uint8_t)strtoul(v, NULL, 16) : 1;
    return r->vs && r->fs;
}

// ------------------------------------------------------------------ recorder (LARECOMP_RECORD_KEYS)
static pthread_mutex_t rec_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t *rec_seen;  // line hashes (open addressing, 0 = empty)
static size_t rec_cap, rec_n;

static bool seen_add(uint64_t h) {  // false: already there
    if (!h) h = 1;
    if (rec_n * 2 >= rec_cap) {
        size_t cap = rec_cap ? rec_cap * 2 : 4096;
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
    if (!done) { p = getenv("LARECOMP_RECORD_KEYS"); if (p && !*p) p = NULL; done = 1; }
    return p;
}

bool shader_rec_recording(void) { return rec_path() != NULL || shader_seen_on(); }

void shader_rec_note(const ShaderRec *r) {
    shader_seen_note(r);  // (the player's own list: queued, written off this thread)
    const char *path = rec_path();
    if (!path) return;
    char line[2048];
    shader_rec_format(r, line, sizeof line);
    pthread_mutex_lock(&rec_lock);
    static bool loaded;
    if (!loaded) {  // (an earlier run's records: appended to, never repeated)
        loaded = true;
        FILE *f = fopen(path, "r");
        char l[2048];
        while (f && fgets(l, sizeof l, f)) {
            l[strcspn(l, "\r\n")] = 0;
            if (l[0] == 'G' || l[0] == 'C') seen_add(shader_code_hash((const uint8_t *)l, strlen(l)));
        }
        if (f) fclose(f);
    }
    if (seen_add(shader_code_hash((const uint8_t *)line, strlen(line)))) {
        FILE *f = fopen(path, "a");
        if (f) { fprintf(f, "%s\n", line); fclose(f); }
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
    uint64_t h = id ^ (uint64_t)(uint8_t)what[1] << 56;
    if (!h) h = 1;
    pthread_mutex_lock(&lock);
    if (cnt * 2 >= cap) {
        size_t nc = cap ? cap * 2 : 1024;
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
    if (b) LOG("[shader] first visit %d: %s %016" PRIx64 " %016" PRIx64 "\n", k, what, a, b);
    else LOG("[shader] first visit %d: %s %016" PRIx64 "\n", k, what, a);
}

// ------------------------------------------------------------------ cache tags
#ifndef SHADER_TRANSLATOR_ID
#define SHADER_TRANSLATOR_ID "dev"
#endif
const char *larecomp_version(void);  // runtime/ui/menu_stubs.c

void shader_cache_tag(char *out, size_t n, const char *gpu) {
    snprintf(out, n, "%s translator %s cache %d | %s", larecomp_version(), SHADER_TRANSLATOR_ID, SHADER_CACHE_VERSION, gpu);
}

bool shader_cache_tag_ok(const char *tag_path, const char *tag) {
    FILE *f = fopen(tag_path, "rb");
    if (!f) return false;
    char have[1024];
    size_t got = fread(have, 1, sizeof have - 1, f);
    fclose(f);
    have[got] = 0;
    return !strcmp(have, tag);
}

bool shader_cache_write(const char *path, const void *data, size_t n) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    mkdir("cache", 0755);
    FILE *f = fopen(tmp, "wb");
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

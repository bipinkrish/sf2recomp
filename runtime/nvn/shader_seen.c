#include "../constants/nvn.h"
// The player's own diagnostic shader variants, kept in cache/shader_seen.txt.
// A writer thread deduplicates and appends hash/pipeline-state records in batches;
// the render thread only enqueues them. These are not shader code or an offline
// prewarm list: SF2 shaders are translated as the game supplies their buffers.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "nvn.h"
#include "shader_prep.h"

#define SEEN_FILE SF2C_SHADER_SEEN_SEEN_FILE
#define SEEN_MAX SF2C_SHADER_SEEN_N_20000_876C  // records kept at most

bool shader_seen_on(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv(SF2C_SHADER_SEEN_S_LARECOMP_SHADER_SEEN_DA2E);
        v = !(e && *e && !atoi(e));
    }
    return v;
}

// ------------------------------------------------------------------ a set of line hashes (open addressing, 0 = empty)
typedef struct { uint64_t *t; size_t cap, n; } HSet;

static uint64_t line_hash(const char *l, size_t n) {
    uint64_t h = shader_code_hash((const uint8_t *)l, n);
    return h ? h : 1;
}

static bool hs_add(HSet *s, uint64_t h) {  // false: already there
    if (s->n * SF2C_SHADER_SEEN_N_2_D473 >= s->cap) {
        size_t cap = s->cap ? s->cap * SF2C_SHADER_SEEN_N_2_D473 : SF2C_SHADER_SEEN_N_4096_8B92;
        uint64_t *t = calloc(cap, sizeof *t);
        for (size_t i = 0; i < s->cap; i++)
            if (s->t[i]) {
                size_t j = s->t[i] & (cap - 1);
                while (t[j]) j = (j + 1) & (cap - 1);
                t[j] = s->t[i];
            }
        free(s->t);
        s->t = t, s->cap = cap;
    }
    size_t j = h & (s->cap - 1);
    while (s->t[j]) {
        if (s->t[j] == h) return false;
        j = (j + 1) & (s->cap - 1);
    }
    s->t[j] = h;
    s->n++;
    return true;
}

static bool hs_has(const HSet *s, uint64_t h) {
    if (!s->cap) return false;
    for (size_t j = h & (s->cap - 1); s->t[j]; j = (j + 1) & (s->cap - 1))
        if (s->t[j] == h) return true;
    return false;
}

// Each G / C line of text (trimmed of \r) -> fn
static void each_record(const char *text, void (*fn)(const char *line, size_t n, void *ctx), void *ctx) {
    for (const char *p = text; p && *p;) {
        const char *e = strchr(p, SF2C_SHADER_SEEN_CH_N_AD65);
        size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t m = n;
        while (m && (p[m - 1] == SF2C_SHADER_SEEN_CH_R_4859 || p[m - 1] == SF2C_SHADER_SEEN_CH_EMPTY_730D)) m--;
        if (m && (p[0] == SF2C_SHADER_SEEN_CH_G_C429 || p[0] == SF2C_SHADER_SEEN_CH_C_809F)) fn(p, m, ctx);
        p += n + (e != NULL);
    }
}

static void add_line(const char *l, size_t n, void *s) { hs_add(s, line_hash(l, n)); }

static char *read_all(const char *path) {
    FILE *f = fopen(path, SF2C_SHADER_SEEN_S_RB_46CA);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = n >= 0 ? calloc(1, (size_t)n + 1) : NULL;
    if (d && n > 0 && fread(d, 1, (size_t)n, f) != (size_t)n) d[0] = 0;
    fclose(f);
    return d;
}

// ------------------------------------------------------------------ the queue and the writer
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;   // the queue, the process set
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t f_lock = PTHREAD_MUTEX_INITIALIZER;   // the file, the known set
static HSet q_seen;     // queued by this process
static char **q_tab;
static size_t q_n, q_cap;
static bool writer_started;

static HSet known;      // records already in the file
static bool known_loaded;
static size_t file_n;   // records in the file

static void known_load(void) {  // (f_lock held)
    if (known_loaded) return;
    known_loaded = true;
    char *d = read_all(SEEN_FILE);
    each_record(d, add_line, &known);
    file_n = known.n;
    free(d);
}

static void write_batch(void) {  // (f_lock held) everything queued -> the file
    pthread_mutex_lock(&q_lock);
    char **tab = q_tab;
    size_t n = q_n;
    q_tab = NULL, q_n = q_cap = 0;
    pthread_mutex_unlock(&q_lock);
    if (!n) return;
    known_load();
    FILE *f = NULL;
    for (size_t i = 0; i < n; i++) {
        uint64_t h = line_hash(tab[i], strlen(tab[i]));
        if (file_n < SEEN_MAX && !hs_has(&known, h)) {
            if (!f) {
                mkdir(SF2C_SHADER_SEEN_S_CACHE_C87A, SF2C_SHADER_SEEN_N_0755_64A5);
                f = fopen(SEEN_FILE, SF2C_SHADER_SEEN_S_AB_782B);
            }
            if (f && fprintf(f, SF2C_SHADER_SEEN_S_S_N_1F0E, tab[i]) > 0) { hs_add(&known, h); file_n++; }
            if (file_n == SEEN_MAX) LOG(SF2C_SHADER_SEEN_S_SHADER_SEEN_LIST_FULL_D_RECO_E8EC, SEEN_MAX);
        }
        free(tab[i]);
    }
    free(tab);
    if (f) fclose(f);
}

static void *writer(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&q_lock);
        while (!q_n) pthread_cond_wait(&q_cond, &q_lock);
        pthread_mutex_unlock(&q_lock);
        struct timespec ts = {1, 0};  // (gather a batch: new pipelines come in bursts)
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&f_lock);
        write_batch();
        pthread_mutex_unlock(&f_lock);
    }
    return NULL;
}

static void at_exit(void) {
    if (pthread_mutex_trylock(&f_lock)) return;  // (the writer is at it, or quit came from inside it)
    write_batch();
    pthread_mutex_unlock(&f_lock);
}

void shader_seen_note(const ShaderRec *r) {
    if (!shader_seen_on() || !r->vs || (r->kind == SF2C_SHADER_SEEN_CH_G_C429 && !r->fs)) return;
    char line[SF2C_SHADER_SEEN_N_2048_BFA0];
    shader_rec_format(r, line, sizeof line);
    pthread_mutex_lock(&q_lock);
    if (hs_add(&q_seen, line_hash(line, strlen(line)))) {
        if (q_n == q_cap) q_tab = realloc(q_tab, sizeof *q_tab * (q_cap = q_cap ? q_cap * SF2C_SHADER_SEEN_N_2_D473 : SF2C_SHADER_SEEN_N_256_51E8));
        q_tab[q_n++] = strdup(line);
        if (!writer_started) {
            writer_started = true;
            pthread_t t;
            if (pthread_create(&t, NULL, writer, NULL) == 0) pthread_detach(t);
            atexit(at_exit);
        }
        pthread_cond_signal(&q_cond);
    }
    pthread_mutex_unlock(&q_lock);
}

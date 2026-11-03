#include "constants/core.h"
// Function-entry profiler. LARECOMP_FNPROF=<from>:<to> counts guest function entries (every generated
// function calls FN_ENTER) between those presented frames, then writes build/fnprof.txt (addr count, most
// called first). Diffing two runs shows which game functions a change brings in; counts also show hot code.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "hle.h"
#include "platform.h"

volatile int g_fnprof;

#define NSLOT (1 << SF2C_FNPROF_N_18_4EC9)
static struct { _Atomic uint64_t addr; _Atomic uint64_t count; } slots[NSLOT];

// LARECOMP_FNPROF_BT=<hex addr>[,...]: also print the host backtrace (guest functions are f_<addr>) the first time
// each listed function is entered inside the window - who calls it
static uint64_t bt_addrs[SF2C_FNPROF_N_256_51E8];
static int bt_n = -1;
static _Atomic uint8_t bt_done[SF2C_FNPROF_N_256_51E8];

uint64_t g_bt_first_x0;

static void bt_check(uint64_t a, uint64_t lr, uint64_t x0) {
    if (bt_n < 0) {
        bt_n = 0;
        const char *e = getenv(SF2C_FNPROF_S_LARECOMP_FNPROF_BT_ACF6);
        for (const char *p = e; p && *p && bt_n < SF2C_FNPROF_N_256_51E8;) {
            bt_addrs[bt_n++] = strtoull(p, (char **)&p, SF2C_FNPROF_N_16_B17E);
            while (*p == SF2C_FNPROF_CH_EMPTY_EE1B || *p == SF2C_FNPROF_CH_EMPTY_730D) p++;
        }
    }
    for (int i = 0; i < bt_n; i++)
        if (bt_addrs[i] == a && !atomic_exchange(&bt_done[i], 1)) {
            if (i == 0) g_bt_first_x0 = x0;
            uint64_t vt = x0 >= SF2C_FNPROF_N_0X7000000000ULL_65B2 && x0 < SF2C_FNPROF_N_0X7800000000ULL_5ACA ? *(uint64_t *)(uintptr_t)x0 : 0;
            uint64_t v100 = vt >= SF2C_FNPROF_N_0X7000000000ULL_65B2 && vt < SF2C_FNPROF_N_0X7800000000ULL_5ACA ? *(uint64_t *)(uintptr_t)(vt + SF2C_FNPROF_N_0X100_BE6E) : 0;
            uint64_t p8 = x0 >= SF2C_FNPROF_N_0X7000000000ULL_65B2 && x0 < SF2C_FNPROF_N_0X7800000000ULL_5ACA ? *(uint64_t *)(uintptr_t)(x0 + SF2C_FNPROF_N_8_2C62) : 0;
            fprintf(stderr, SF2C_FNPROF_S_FNPROF_BT_LLX_ENTERED_FROM_G_B5C1,
                    (unsigned long long)a, (unsigned long long)lr, (unsigned long long)x0, (unsigned long long)vt,
                    (unsigned long long)v100, (unsigned long long)p8);
            plat_print_backtrace();  // (execinfo is macOS / glibc only)
        }
}

void fnprof_hit(uint64_t a, uint64_t lr, uint64_t x0) {
    if (bt_n) bt_check(a, lr, x0);
    uint32_t h = (uint32_t)((a >> SF2C_FNPROF_N_2_D473) * SF2C_FNPROF_N_0X9E3779B1U_BCDD) & (NSLOT - 1);
    for (int n = 0; n < NSLOT; n++, h = (h + 1) & (NSLOT - 1)) {
        uint64_t cur = atomic_load_explicit(&slots[h].addr, memory_order_relaxed);
        if (cur == a || (cur == 0 && atomic_compare_exchange_strong(&slots[h].addr, &cur, a)) || cur == a) {
            atomic_fetch_add_explicit(&slots[h].count, 1, memory_order_relaxed);
            return;
        }
    }
}

static int cmp(const void *x, const void *y) {
    uint64_t a = ((const uint64_t *)x)[1], b = ((const uint64_t *)y)[1];
    return a < b ? 1 : a > b ? -1 : 0;
}

void fnprof_frame(uint64_t frame) {
    static long from = -SF2C_FNPROF_N_2_D473, to;
    if (from == -SF2C_FNPROF_N_2_D473) {
        from = -1;
        const char *e = getenv(SF2C_FNPROF_S_LARECOMP_FNPROF_CC00);
        if (e && sscanf(e, SF2C_FNPROF_S_LD_LD_CF3A, &from, &to) != SF2C_FNPROF_N_2_D473) from = -1;
    }
    if (from < 0) return;
    if ((long)frame == from) { g_fnprof = 1; LOG(SF2C_FNPROF_S_FNPROF_ON_AT_FRAME_LLU_N_0098, (unsigned long long)frame); }
    if ((long)frame != to) return;
    g_fnprof = 0;
    uint64_t (*rows)[SF2C_FNPROF_N_2_D473] = malloc(sizeof(uint64_t[SF2C_FNPROF_N_2_D473]) * NSLOT);
    int n = 0;
    for (int i = 0; i < NSLOT; i++)
        if (slots[i].addr) { rows[n][0] = slots[i].addr; rows[n][1] = slots[i].count; n++; }
    qsort(rows, n, sizeof rows[0], cmp);
    const char *out = getenv(SF2C_FNPROF_S_LARECOMP_FNPROF_OUT_EA89) ? getenv(SF2C_FNPROF_S_LARECOMP_FNPROF_OUT_EA89) : SF2C_FNPROF_S_BUILD_FNPROF_TXT_A5F6;  // (parallel runs)
    FILE *f = fopen(out, SF2C_FNPROF_S_W_7265);
    for (int i = 0; f && i < n; i++) fprintf(f, SF2C_FNPROF_S_LLX_LLU_N_670A, (unsigned long long)rows[i][0], (unsigned long long)rows[i][1]);
    if (f) fclose(f);
    free(rows);
    LOG(SF2C_FNPROF_S_FNPROF_D_FUNCTIONS_WRITTEN_S_9D4B, n, out, from, to);
    from = -1;
}

// LARECOMP_FNPROF=0:<to>: profile from process start (threads the game starts before the first frame)
__attribute__((constructor)) static void fnprof_early(void) {
    const char *e = getenv(SF2C_FNPROF_S_LARECOMP_FNPROF_CC00);
    if (e && e[0] == SF2C_FNPROF_CH_0_59A9 && e[1] == SF2C_FNPROF_CH_EMPTY_4266) g_fnprof = 1;
}

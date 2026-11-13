#include "constants/core.h"
// Guest CPU state and helpers used by recompiled code.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>

typedef struct __attribute__((aligned(SF2C_CPU_N_16_B17E))) Cpu {
    uint64_t x[SF2C_CPU_N_32_E29C];          // x0-x30 (x[31] unused; sp is separate)
    uint64_t sp;             // 256
    uint64_t pc;             // 264: target for indirect dispatch
    uint64_t nzcv;           // 272: flags in bits 31..28
    uint64_t tpidr;          // 280
    uint64_t tpidrro;        // 288
    uint64_t excl_addr;      // 296
    uint64_t excl_val;       // 304
    void *thread;            // 312: runtime thread object
    uint64_t fpcr;           // 320: guest FPCR/FPSR (portable FP/SIMD lowering only; the asm path uses the host's)
    uint64_t fpsr;           // 328
    __uint128_t excl_val128; // 336
    uint8_t _pad[SF2C_CPU_N_512_94F8 - SF2C_CPU_N_352_9A72];
    __uint128_t v[SF2C_CPU_N_32_E29C];       // 512
} Cpu;

_Static_assert(offsetof(Cpu, sp) == SF2C_CPU_N_256_51E8, SF2C_CPU_S_SP_0674);
_Static_assert(offsetof(Cpu, pc) == SF2C_CPU_N_264_BBA5, SF2C_CPU_S_PC_F2CD);
_Static_assert(offsetof(Cpu, nzcv) == SF2C_CPU_N_272_1C6C, SF2C_CPU_S_NZCV_B3D5);
_Static_assert(offsetof(Cpu, fpcr) == SF2C_CPU_N_320_8882, SF2C_CPU_S_FPCR_9C54);
_Static_assert(offsetof(Cpu, fpsr) == SF2C_CPU_N_328_2452, SF2C_CPU_S_FPSR_DC79);
_Static_assert(offsetof(Cpu, v) == SF2C_CPU_N_512_94F8, SF2C_CPU_S_V_D1A4);

typedef void (*GuestFn)(Cpu *);

// ---- runtime services (runtime/*.c)
void rt_dispatch(Cpu *c);                 // call function at c->pc
bool rt_guest_try_call(Cpu *c, uint64_t site, uint64_t target, GuestFn function); // translated EH call
// hook site (tools/patches.py HOOKS, runtime/hle/hooks.c): 0 = run the instruction, 1 = the hook did it all,
// 2.. = jump to the site's first.. target
int rt_hook(Cpu *c, uint64_t site);
// Optional hook groups are configured before guest threads start. Generated
// sites bypass callbacks entirely when their group is disabled.
enum { RT_HOOK_MOBILE_UI = SF2C_CPU_N_1U_B8E6 << SF2C_CPU_N_3_4E07 };
extern uint32_t rt_hook_features;
void rt_hooks_configure(void);
void rt_svc(Cpu *c, uint32_t imm, uint64_t pc);
void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) __attribute__((noreturn));
void rt_udf(Cpu *c, uint64_t pc) __attribute__((noreturn));
void rt_stub(Cpu *c, uint64_t pc);
uint64_t rt_ticks(void);

#if defined(__aarch64__)
static inline uint64_t rt_get_fpcr(void) { uint64_t v; __asm__ volatile(SF2C_CPU_S_MRS_0_FPCR_1621 : SF2C_CPU_S_R_49F7(v)); return v; }
static inline uint64_t rt_get_fpsr(void) { uint64_t v; __asm__ volatile(SF2C_CPU_S_MRS_0_FPSR_8F99 : SF2C_CPU_S_R_49F7(v)); return v; }
static inline void rt_set_fpcr(uint64_t v) { __asm__ volatile(SF2C_CPU_S_MSR_FPCR_0_2CE9 :: SF2C_CPU_S_R_2659(v)); }
static inline void rt_set_fpsr(uint64_t v) { __asm__ volatile(SF2C_CPU_S_MSR_FPSR_0_4330 :: SF2C_CPU_S_R_2659(v)); }
#else
// Host FP state is not the guest's here: code lifted with --simd=c keeps FPCR/FPSR in Cpu (c->fpcr, zeroed
// with the Cpu), so the host-side reset in thread start-up has nothing to do.
static inline uint64_t rt_get_fpcr(void) { return 0; }
static inline uint64_t rt_get_fpsr(void) { return 0; }
static inline void rt_set_fpcr(uint64_t v) { (void)v; }
static inline void rt_set_fpsr(uint64_t v) { (void)v; }
#endif

// ---- memory: guest addresses are host addresses
typedef uint16_t __attribute__((aligned(1), may_alias)) u16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) u32u;
typedef uint64_t __attribute__((aligned(1), may_alias)) u64u;
typedef uint8_t __attribute__((may_alias)) u8a;

// Function-entry profiler (runtime/fnprof.c, SF2RECOMP_FNPROF): one predictable branch per call when off.
extern volatile int g_fnprof;
void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0);
#define FN_ENTER(a) do { if (__builtin_expect(g_fnprof, 0)) fnprof_hit(a, c->x[SF2C_CPU_N_30_624B], c->x[0]); } while (0)

#define LD8(a)  (*(const u8a *)(uintptr_t)(a))
#define LD16(a) (*(const u16u *)(uintptr_t)(a))
#define LD32(a) (*(const u32u *)(uintptr_t)(a))
#define LD64(a) (*(const u64u *)(uintptr_t)(a))
#define ST8(a, v)  (*(u8a *)(uintptr_t)(a) = (uint8_t)(v))
#define ST16(a, v) (*(u16u *)(uintptr_t)(a) = (uint16_t)(v))
#define ST32(a, v) (*(u32u *)(uintptr_t)(a) = (uint32_t)(v))
#define ST64(a, v) (*(u64u *)(uintptr_t)(a) = (uint64_t)(v))

// ARM LDAR/LDAXR and STLR provide RCsc ordering, including store-release ->
// later acquire-load to a different address. C acquire/release alone loses that
// store/load edge on x86 (and can select RCpc LDAPR on ARM hosts). Use host SC
// atomics conservatively so guest queue publications cannot miss wakeups.
// ---- exclusive monitor (emulated with compare-and-swap)
#define EXCL(N, T)                                                                         \
    static inline uint64_t ldx##N(Cpu *c, uint64_t a) {                                    \
        T v = __atomic_load_n((T *)(uintptr_t)a, __ATOMIC_SEQ_CST);                        \
        c->excl_addr = a;                                                                  \
        c->excl_val = v;                                                                   \
        return v;                                                                          \
    }                                                                                      \
    static inline uint32_t stx##N(Cpu *c, uint64_t a, uint64_t v) {                        \
        if (c->excl_addr != a) return 1;                                                   \
        c->excl_addr = ~SF2C_CPU_N_0ULL_DF1B;                                                              \
        T expected = (T)c->excl_val;                                                       \
        return !__atomic_compare_exchange_n((T *)(uintptr_t)a, &expected, (T)v, 0,         \
                                            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);           \
    }                                                                                      \
    static inline uint64_t lda##N(uint64_t a) {                                            \
        return __atomic_load_n((T *)(uintptr_t)a, __ATOMIC_SEQ_CST);                       \
    }                                                                                      \
    static inline void stl##N(uint64_t a, uint64_t v) {                                    \
        __atomic_store_n((T *)(uintptr_t)a, (T)v, __ATOMIC_SEQ_CST);                       \
    }
EXCL(8, uint8_t)
EXCL(16, uint16_t)
EXCL(32, uint32_t)
EXCL(64, uint64_t)

static inline void ldxp64(Cpu *c, uint64_t a, uint64_t *lo, uint64_t *hi) {
    c->excl_addr = a;
    __uint128_t val = 0;
    __atomic_load((__uint128_t *)(uintptr_t)a, &val, __ATOMIC_SEQ_CST);
    c->excl_val128 = val;
    *lo = (uint64_t)val;
    *hi = (uint64_t)(val >> SF2C_CPU_N_64_A68B);
}
static inline uint32_t stxp64(Cpu *c, uint64_t a, uint64_t lo, uint64_t hi) {
    if (c->excl_addr != a) return 1;
    c->excl_addr = ~SF2C_CPU_N_0ULL_DF1B;
    __uint128_t val = ((__uint128_t)hi << SF2C_CPU_N_64_A68B) | lo;
    __uint128_t expected = c->excl_val128;
    return !__atomic_compare_exchange((__uint128_t *)(uintptr_t)a, &expected, &val, 0,
                                      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline void ldxp32(Cpu *c, uint64_t a, uint64_t *lo, uint64_t *hi) {
    c->excl_addr = a;
    uint64_t val = __atomic_load_n((uint64_t *)(uintptr_t)a, __ATOMIC_SEQ_CST);
    *lo = (uint32_t)val;
    *hi = (uint32_t)(val >> SF2C_CPU_N_32_E29C);
}
static inline uint32_t stxp32(Cpu *c, uint64_t a, uint32_t lo, uint32_t hi) {
    if (c->excl_addr != a) return 1;
    c->excl_addr = ~SF2C_CPU_N_0ULL_DF1B;
    uint64_t val = ((uint64_t)hi << SF2C_CPU_N_32_E29C) | lo;
    uint64_t expected = __atomic_load_n((uint64_t *)(uintptr_t)a, __ATOMIC_RELAXED);
    return !__atomic_compare_exchange_n((uint64_t *)(uintptr_t)a, &expected, val, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}


// ---- flags (packed NZCV in bits 31..28)
#define FLAG_N(c) (((c)->nzcv >> SF2C_CPU_N_31_EB1E) & 1)
#define FLAG_Z(c) (((c)->nzcv >> SF2C_CPU_N_30_624B) & 1)
#define FLAG_C(c) (((c)->nzcv >> SF2C_CPU_N_29_3513) & 1)
#define FLAG_V(c) (((c)->nzcv >> SF2C_CPU_N_28_59E1) & 1)
#define COND_EQ(c) (FLAG_Z(c))
#define COND_NE(c) (!FLAG_Z(c))
#define COND_CS(c) (FLAG_C(c))
#define COND_CC(c) (!FLAG_C(c))
#define COND_MI(c) (FLAG_N(c))
#define COND_PL(c) (!FLAG_N(c))
#define COND_VS(c) (FLAG_V(c))
#define COND_VC(c) (!FLAG_V(c))
#define COND_HI(c) (FLAG_C(c) && !FLAG_Z(c))
#define COND_LS(c) (!(FLAG_C(c) && !FLAG_Z(c)))
#define COND_GE(c) (FLAG_N(c) == FLAG_V(c))
#define COND_LT(c) (FLAG_N(c) != FLAG_V(c))
#define COND_GT(c) (!FLAG_Z(c) && FLAG_N(c) == FLAG_V(c))
#define COND_LE(c) (!(!FLAG_Z(c) && FLAG_N(c) == FLAG_V(c)))

#define MKFLAGS(n, z, cy, v) \
    (((uint64_t)(n) << SF2C_CPU_N_31_EB1E) | ((uint64_t)(z) << SF2C_CPU_N_30_624B) | ((uint64_t)(cy) << SF2C_CPU_N_29_3513) | ((uint64_t)(v) << SF2C_CPU_N_28_59E1))

static inline uint64_t adds64(Cpu *c, uint64_t a, uint64_t b) {
    uint64_t r = a + b;
    c->nzcv = MKFLAGS(r >> SF2C_CPU_N_63_DA4E, r == 0, r < a, ((a ^ r) & (b ^ r)) >> SF2C_CPU_N_63_DA4E);
    return r;
}
static inline uint64_t subs64(Cpu *c, uint64_t a, uint64_t b) {
    uint64_t r = a - b;
    c->nzcv = MKFLAGS(r >> SF2C_CPU_N_63_DA4E, r == 0, a >= b, ((a ^ b) & (a ^ r)) >> SF2C_CPU_N_63_DA4E);
    return r;
}
static inline uint32_t adds32(Cpu *c, uint32_t a, uint32_t b) {
    uint32_t r = a + b;
    c->nzcv = MKFLAGS(r >> SF2C_CPU_N_31_EB1E, r == 0, r < a, ((a ^ r) & (b ^ r)) >> SF2C_CPU_N_31_EB1E);
    return r;
}
static inline uint32_t subs32(Cpu *c, uint32_t a, uint32_t b) {
    uint32_t r = a - b;
    c->nzcv = MKFLAGS(r >> SF2C_CPU_N_31_EB1E, r == 0, a >= b, ((a ^ b) & (a ^ r)) >> SF2C_CPU_N_31_EB1E);
    return r;
}
static inline uint64_t logic64(Cpu *c, uint64_t r) { c->nzcv = MKFLAGS(r >> SF2C_CPU_N_63_DA4E, r == 0, 0, 0); return r; }
static inline uint32_t logic32(Cpu *c, uint32_t r) { c->nzcv = MKFLAGS(r >> SF2C_CPU_N_31_EB1E, r == 0, 0, 0); return r; }

#define ADC_IMPL(N, S)                                                                     \
    static inline uint##N##_t adc##N(Cpu *c, uint##N##_t a, uint##N##_t b) {               \
        return a + b + (uint##N##_t)FLAG_C(c);                                             \
    }                                                                                      \
    static inline uint##N##_t sbc##N(Cpu *c, uint##N##_t a, uint##N##_t b) {               \
        return a - b - 1 + (uint##N##_t)FLAG_C(c);                                         \
    }                                                                                      \
    static inline uint##N##_t adcs##N(Cpu *c, uint##N##_t a, uint##N##_t b) {              \
        uint##N##_t cin = FLAG_C(c), r = a + b + cin;                                      \
        int carry = cin ? r <= a : r < a;                                                  \
        c->nzcv = MKFLAGS(r >> (N - 1), r == 0, carry, ((a ^ r) & (b ^ r)) >> (N - 1));    \
        return r;                                                                          \
    }                                                                                      \
    static inline uint##N##_t sbcs##N(Cpu *c, uint##N##_t a, uint##N##_t b) {              \
        return adcs##N(c, a, (uint##N##_t)~b);                                             \
    }
ADC_IMPL(64, )
ADC_IMPL(32, )

// ---- bit ops
#define ROR64(v, r) ((uint64_t)(v) >> ((r) & SF2C_CPU_N_63_DA4E) | (uint64_t)(v) << ((SF2C_CPU_N_64_A68B - ((r) & SF2C_CPU_N_63_DA4E)) & SF2C_CPU_N_63_DA4E))
#define ROR32(v, r) ((uint32_t)(v) >> ((r) & SF2C_CPU_N_31_EB1E) | (uint32_t)(v) << ((SF2C_CPU_N_32_E29C - ((r) & SF2C_CPU_N_31_EB1E)) & SF2C_CPU_N_31_EB1E))
static inline uint64_t clz64(uint64_t v) { return v ? __builtin_clzll(v) : SF2C_CPU_N_64_A68B; }
static inline uint32_t clz32(uint32_t v) { return v ? __builtin_clz(v) : SF2C_CPU_N_32_E29C; }
static inline uint64_t cls64(uint64_t v) { return clz64(v ^ (uint64_t)((int64_t)v >> 1)) - 1; }
static inline uint32_t cls32(uint32_t v) { return clz32(v ^ (uint32_t)((int32_t)v >> 1)) - 1; }
static inline uint64_t rbit64(uint64_t v) { return __builtin_bitreverse64(v); }
static inline uint32_t rbit32(uint32_t v) { return __builtin_bitreverse32(v); }
static inline uint32_t rev16_32(uint32_t v) { return ((v & SF2C_CPU_N_0X00FF00FFU_9E9C) << SF2C_CPU_N_8_2C62) | ((v >> SF2C_CPU_N_8_2C62) & SF2C_CPU_N_0X00FF00FFU_9E9C); }
static inline uint64_t rev16_64(uint64_t v) {
    return ((v & SF2C_CPU_N_0X00FF00FF00FF00FFULL_1ECA) << SF2C_CPU_N_8_2C62) | ((v >> SF2C_CPU_N_8_2C62) & SF2C_CPU_N_0X00FF00FF00FF00FFULL_1ECA);
}
static inline uint64_t rev32_64(uint64_t v) {
    return ((uint64_t)__builtin_bswap32((uint32_t)v)) | ((uint64_t)__builtin_bswap32((uint32_t)(v >> SF2C_CPU_N_32_E29C)) << SF2C_CPU_N_32_E29C);
}
static inline uint64_t udiv64(uint64_t a, uint64_t b) { return b ? a / b : 0; }
static inline uint32_t udiv32(uint32_t a, uint32_t b) { return b ? a / b : 0; }
static inline uint64_t sdiv64(uint64_t a, uint64_t b) {
    if (!b) return 0;
    if ((int64_t)a == INT64_MIN && (int64_t)b == -1) return a;
    return (uint64_t)((int64_t)a / (int64_t)b);
}
static inline uint32_t sdiv32(uint32_t a, uint32_t b) {
    if (!b) return 0;
    if ((int32_t)a == INT32_MIN && (int32_t)b == -1) return a;
    return (uint32_t)((int32_t)a / (int32_t)b);
}

// Portable FP/SIMD helpers for code lifted with --simd=c (default off AArch64; gen.py includes simd.h itself
// in that mode, so AArch64 hosts can build it too).
#if !defined(__cplusplus) && !defined(__aarch64__)
#include "simd.h"
#endif

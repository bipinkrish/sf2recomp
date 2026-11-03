#include "constants/core.h"
// FP helpers for one precision; included twice by simd.h (F = s/float, d/double). No include guard.
// Scalar entry points take/return T, vector ones VT (all lanes; callers zero the unused half for Q=0).

#define FN(x) SIMD_CAT(x, F)
#define SGN ((U)1 << (NB - 1))
#define EXPM ((((U)1 << EB) - 1) << MB)
#define QB ((U)1 << (MB - 1))
#define DNAN (EXPM | QB)
#define MINN ((U)1 << MB) // smallest normal
#define VK(v) ((VT){} + (T)(v))
#define K(v) ((T)(v))

SI U FN(fb)(T x) { U u; memcpy(&u, &x, sizeof u); return u; }
SI T FN(fv)(U u) { T x; memcpy(&x, &u, sizeof x); return x; }
SI int FN(isnan)(U u) { return (u & ~SGN) > EXPM; }
SI int FN(issnan)(U u) { return FN(isnan)(u) && !(u & QB); }
SI int FN(isqnan)(U u) { return FN(isnan)(u) && (u & QB); }
SI int FN(isinf)(U u) { return (u & ~SGN) == EXPM; }
SI int FN(iszero)(U u) { return !(u & ~SGN); }
SI int FN(isden)(U u) { return !(u & EXPM) && (u & ~SGN); }
SI T FN(flush)(T x) { U u = FN(fb)(x); return FN(isden)(u) ? FN(fv)(u & SGN) : x; }
SI T FN(fz)(Cpu *c, T x) { return __builtin_expect(c->fpcr & FPCR_FZ, 0) ? FN(flush)(x) : x; }
SI T FN(neg)(T x) { return FN(fv)(FN(fb)(x) ^ SGN); }

// FPProcessNaN(s): first signalling NaN (quietened), else first quiet NaN, else (invalid op) default NaN
SN T FN(nan1)(Cpu *c, T a) {
    U x = FN(fb)(a);
    if (!(c->fpcr & FPCR_DN) && FN(isnan)(x)) return FN(fv)(x | QB);
    return FN(fv)(DNAN);
}
SN T FN(nan2)(Cpu *c, T a, T b) {
    U x = FN(fb)(a), y = FN(fb)(b);
    if (!(c->fpcr & FPCR_DN)) {
        if (FN(issnan)(x)) return FN(fv)(x | QB);
        if (FN(issnan)(y)) return FN(fv)(y | QB);
        if (FN(isnan)(x)) return a;
        if (FN(isnan)(y)) return b;
    }
    return FN(fv)(DNAN);
}
SN T FN(nan3)(Cpu *c, T a, T b, T d) {
    U x = FN(fb)(a), y = FN(fb)(b), z = FN(fb)(d);
    if (!(c->fpcr & FPCR_DN)) {
        if (FN(issnan)(x)) return FN(fv)(x | QB);
        if (FN(issnan)(y)) return FN(fv)(y | QB);
        if (FN(issnan)(z)) return FN(fv)(z | QB);
        if (FN(isnan)(x)) return a;
        if (FN(isnan)(y)) return b;
        if (FN(isnan)(z)) return d;
    }
    return FN(fv)(DNAN);
}

// The arithmetic itself, for scalars (KK = K) and vectors (KK = VK). MADD: a + b*d (a = addend).
// RECPS/RSQRTS get their first operand already negated (FPNeg happens before NaN processing).
#define FCALC(op, a, b, d, KK)                                                                       \
    ((op) == FOP_ADD ? (a) + (b) : (op) == FOP_SUB ? (a) - (b)                                     \
   : (op) == FOP_MUL || (op) == FOP_MULX ? (a) * (b) : (op) == FOP_NMUL ? -((a) * (b))              \
   : (op) == FOP_DIV ? (a) / (b) : (op) == FOP_ABD ? __builtin_elementwise_abs((a) - (b))          \
   : (op) == FOP_SQRT ? __builtin_elementwise_sqrt(a)                                              \
   : (op) == FOP_MADD ? __builtin_elementwise_fma(b, d, a)                                         \
   : (op) == FOP_RECPS ? __builtin_elementwise_fma(a, b, KK(SF2C_SIMD_FP_N_2_D473))                                    \
   : (op) == FOP_RSQRTS ? __builtin_elementwise_fma(a, b, KK(SF2C_SIMD_FP_N_3_4E07)) * KK(SF2C_SIMD_FP_N_0_5_D2CB) : (a))

// Lanes whose fast result needs fixing: NaN results (NaN operand or invalid op), RSQRTS overflow.
#define FBAD(op, r) ((op) == FOP_RSQRTS ? ((r) != (r)) | (__builtin_elementwise_abs(r) == K(INFINITY)) : (r) != (r))

// (3 + a*b) / 2 with one rounding (a already negated): halve a normal operand first, so a product beyond
// the format's range (which the fast path rounds to inf, or to the largest normal when rounding down) is exact.
SN T FN(rsqrts)(T a, T b) {
    if (__builtin_elementwise_abs(a) >= FN(fv)(SF2C_SIMD_FP_N_2_D473 * MINN)) return __builtin_elementwise_fma(a * K(SF2C_SIMD_FP_N_0_5_D2CB), b, K(SF2C_SIMD_FP_N_1_5_9F29));
    if (__builtin_elementwise_abs(b) >= FN(fv)(SF2C_SIMD_FP_N_2_D473 * MINN)) return __builtin_elementwise_fma(a, b * K(SF2C_SIMD_FP_N_0_5_D2CB), K(SF2C_SIMD_FP_N_1_5_9F29));
    return __builtin_elementwise_fma(a, b, K(SF2C_SIMD_FP_N_3_4E07)) * K(SF2C_SIMD_FP_N_0_5_D2CB);
}

// ARM result for a lane flagged by FBAD (operands already flushed).
SN T FN(fix)(Cpu *c, int op, T a, T b, T d) {
    U x = FN(fb)(a), y = FN(fb)(b), z = FN(fb)(d);
    int nan = FN(isnan)(x) || FN(isnan)(y);
    switch (op) {
    case FOP_SQRT: return FN(nan1)(c, a); // NaN in, or negative -> default NaN
    case FOP_MADD:
        if (FN(isnan)(x) || FN(isnan)(y) || FN(isnan)(z)) {
            if (FN(isqnan)(x) && ((FN(isinf)(y) && FN(iszero)(z)) || (FN(iszero)(y) && FN(isinf)(z))))
                return FN(fv)(DNAN);
            return FN(nan3)(c, a, b, d);
        }
        return FN(fv)(DNAN);
    case FOP_MULX:
        if (!nan) return FN(fv)(((x ^ y) & SGN) | FN(fb)(K(SF2C_SIMD_FP_N_2_D473)));
        break;
    case FOP_RECPS:
        if (!nan) return K(SF2C_SIMD_FP_N_2_D473);
        break;
    case FOP_RSQRTS:
        if (!nan) {
            if ((FN(isinf)(x) && FN(iszero)(y)) || (FN(iszero)(x) && FN(isinf)(y))) return K(SF2C_SIMD_FP_N_1_5_9F29);
            return FN(rsqrts)(a, b);
        }
        break;
    case FOP_NMUL: return FN(neg)(FN(nan2)(c, a, b));
    case FOP_ABD: return FN(fv)(FN(fb)(FN(nan2)(c, a, b)) & ~SGN);
    }
    return FN(nan2)(c, a, b);
}

// FMAX/FMIN/FMAXNM/FMINNM (operands already flushed)
SI T FN(minmax)(Cpu *c, int op, T a, T b) {
    U x = FN(fb)(a), y = FN(fb)(b);
    if (__builtin_expect(FN(isnan)(x) || FN(isnan)(y), 0)) {
        if (op == FOP_MAXNM || op == FOP_MINNM) {
            if (FN(isqnan)(x) && !FN(isnan)(y)) return b;
            if (FN(isqnan)(y) && !FN(isnan)(x)) return a;
        }
        return FN(nan2)(c, a, b);
    }
    if (op == FOP_MAX || op == FOP_MAXNM) return a == b ? FN(fv)(x & y) : a > b ? a : b;
    return a == b ? FN(fv)(x | y) : a < b ? a : b;
}
#define FMINMAX(op) ((op) >= FOP_MAX && (op) <= FOP_MINNM)

// FPCR.FZ / RMode path: flush inputs, round in the FPCR mode, flush tiny results (tininess is detected
// before rounding on ARM: a result rounded up to the smallest normal is recomputed toward zero).
SN T FN(slow)(Cpu *c, int op, T a, T b, T d) {
#pragma STDC FENV_ACCESS ON
    uint64_t fpcr = c->fpcr;
    if (fpcr & FPCR_FZ) a = FN(flush)(a), b = FN(flush)(b), d = FN(flush)(d);
    if (FMINMAX(op)) return FN(minmax)(c, op, a, b);
    int old = simd_host_round();
    simd_set_host_round(simd_fe_mode(fpcr));
    volatile T va = a, vb = b, vd = d;
    volatile T vr = op == FOP_RSQRTS ? FN(rsqrts)(va, vb) : FCALC(op, va, vb, vd, K);
    T r = vr;
    if (FBAD(op, r)) vr = FN(fix)(c, op, a, b, d), r = vr;
    if ((fpcr & FPCR_FZ) && (FN(fb)(r) & ~SGN) <= MINN && !FN(iszero)(FN(fb)(r))) {
        if ((FN(fb)(r) & ~SGN) == MINN) {
            simd_set_host_round(FE_TOWARDZERO);
            volatile T vz = FCALC(op, va, vb, vd, K);
            if ((FN(fb)(vz) & ~SGN) < MINN) r = FN(fv)(FN(fb)(r) & SGN);
        } else {
            r = FN(fv)(FN(fb)(r) & SGN);
        }
    }
    simd_set_host_round(old);
    return r;
}

// Scalar FP op (constant op folds the selection).
SI T FN(op)(Cpu *c, int op, T a, T b, T d) {
    if (__builtin_expect(c->fpcr & FPCR_SLOW, 0)) return FN(slow)(c, op, a, b, d);
    if (FMINMAX(op)) return FN(minmax)(c, op, a, b);
    T r = FCALC(op, a, b, d, K);
    if (__builtin_expect(FBAD(op, r), 0)) r = FN(fix)(c, op, a, b, d);
    return r;
}

SN VT FN(vslow)(Cpu *c, int op, VT a, VT b, VT d) {
    VT r;
    for (int i = 0; i < NL; i++) r[i] = FN(op)(c, op, a[i], b[i], d[i]);
    return r;
}

SI VTU FN(vsel)(VTS m, VTU x, VTU y) { return (x & (VTU)m) | (y & ~(VTU)m); }

// Vector FP op: native lanes, any NaN (or FPCR.FZ/RMode) reruns all lanes through the scalar path.
SI VT FN(vop)(Cpu *c, int op, VT a, VT b, VT d) {
    if (__builtin_expect(c->fpcr & FPCR_SLOW, 0)) return FN(vslow)(c, op, a, b, d);
    VT r;
    if (FMINMAX(op)) {
        if (__builtin_expect(__builtin_reduce_or((a != a) | (b != b)) != 0, 0)) return FN(vslow)(c, op, a, b, d);
        VTU x = (VTU)a, y = (VTU)b;
        if (op == FOP_MAX || op == FOP_MAXNM)
            r = (VT)FN(vsel)(a > b, x, FN(vsel)(a == b, x & y, y));
        else
            r = (VT)FN(vsel)(a < b, x, FN(vsel)(a == b, x | y, y));
        return r;
    }
    r = FCALC(op, a, b, d, VK);
    if (__builtin_expect(__builtin_reduce_or(FBAD(op, r)) != 0, 0)) return FN(vslow)(c, op, a, b, d);
    return r;
}

SI VT FN(vfz)(Cpu *c, VT a) {
    if (__builtin_expect(c->fpcr & FPCR_FZ, 0)) {
        VTU u = (VTU)a, mag = u & ~((VTU){} + SGN);
        VTS den = (mag != 0) & ((u & EXPM) == 0);
        a = (VT)FN(vsel)(den, u & SGN, u);
    }
    return a;
}

// ---- compares
SI uint64_t FN(cmp)(Cpu *c, T a, T b) { // FCMP NZCV
    a = FN(fz)(c, a), b = FN(fz)(c, b);
    return a == b ? SF2C_SIMD_FP_N_0X60000000ULL_AC90 : a < b ? SF2C_SIMD_FP_N_0X80000000ULL_6840 : a > b ? SF2C_SIMD_FP_N_0X20000000ULL_ADEC : SF2C_SIMD_FP_N_0X30000000ULL_CD09;
}
// FCMEQ/FCMGE/FCMGT/FACGE/FACGT (op 0..4)
SI VTU FN(vcmp)(Cpu *c, int op, VT a, VT b) {
    a = FN(vfz)(c, a), b = FN(vfz)(c, b);
    if (op >= SF2C_SIMD_FP_N_3_4E07) a = __builtin_elementwise_abs(a), b = __builtin_elementwise_abs(b);
    switch (op) {
    case 0: return (VTU)(a == b);
    case 1: case SF2C_SIMD_FP_N_3_4E07: return (VTU)(a >= b);
    default: return (VTU)(a > b);
    }
}

// ---- rounding to integral (FRINT*): rm 0 N, 1 P, 2 M, 3 Z, 4 A; 5 X / 6 I = FPCR mode
// (rint = ties-to-even: the host stays in round-to-nearest outside the slow paths; no roundeven in MinGW's CRT)
SI T FN(rnd)(T x, int rm) {
    switch (rm) {
    case 0: return __builtin_elementwise_rint(x);
    case 1: return __builtin_elementwise_ceil(x);
    case SF2C_SIMD_FP_N_2_D473: return __builtin_elementwise_floor(x);
    case SF2C_SIMD_FP_N_3_4E07: return __builtin_elementwise_trunc(x);
    default: return __builtin_elementwise_round(x);
    }
}
SI T FN(frint)(Cpu *c, T x, int rm) {
    x = FN(fz)(c, x);
    if (rm >= SF2C_SIMD_FP_N_5_EF2D) rm = (int)((c->fpcr >> SF2C_SIMD_FP_N_22_785F) & SF2C_SIMD_FP_N_3_4E07);
    T r = FN(rnd)(x, rm);
    if (__builtin_expect(r != r, 0)) r = FN(nan1)(c, x);
    return r;
}
SI VT FN(vfrint)(Cpu *c, VT x, int rm) {
    x = FN(vfz)(c, x);
    if (rm >= SF2C_SIMD_FP_N_5_EF2D) rm = (int)((c->fpcr >> SF2C_SIMD_FP_N_22_785F) & SF2C_SIMD_FP_N_3_4E07);
    VT r;
    switch (rm) {
    case 0: r = __builtin_elementwise_rint(x); break;
    case 1: r = __builtin_elementwise_ceil(x); break;
    case SF2C_SIMD_FP_N_2_D473: r = __builtin_elementwise_floor(x); break;
    case SF2C_SIMD_FP_N_3_4E07: r = __builtin_elementwise_trunc(x); break;
    default: r = __builtin_elementwise_round(x); break;
    }
    if (__builtin_expect(__builtin_reduce_or(r != r) != 0, 0))
        for (int i = 0; i < NL; i++)
            if (r[i] != r[i]) r[i] = FN(nan1)(c, x[i]);
    return r;
}

// ---- FP -> integer (FCVT[NPMZA][SU], fixed point): saturating, NaN -> 0. ib = 32/64.
SI uint64_t FN(toint)(Cpu *c, T x, int rm, int fbits, int ib, int uns) {
    x = FN(fz)(c, x);
    if (fbits) x = x * (T)(SF2C_SIMD_FP_N_1ULL_622E << (fbits - 1)) * K(SF2C_SIMD_FP_N_2_D473);
    T r = FN(rnd)(x, rm);
    if (r != r) return 0;
    if (uns) {
        if (r <= K(0)) return 0;
        if (ib == SF2C_SIMD_FP_N_32_E29C) return r >= K(SF2C_SIMD_FP_N_4294967296_0_6B53) ? SF2C_SIMD_FP_N_0XFFFFFFFFULL_4EE8 : (uint32_t)r;
        return r >= K(SF2C_SIMD_FP_N_18446744073709551616_0_CCF7) ? ~SF2C_SIMD_FP_N_0ULL_DF1B : (uint64_t)r;
    }
    if (ib == SF2C_SIMD_FP_N_32_E29C) return r >= K(SF2C_SIMD_FP_N_2147483648_0_1E55) ? SF2C_SIMD_FP_N_0X7FFFFFFFULL_BD13 : r < K(-SF2C_SIMD_FP_N_2147483648_0_1E55) ? SF2C_SIMD_FP_N_0X80000000ULL_6840 : (uint32_t)(int32_t)r;
    return r >= K(SF2C_SIMD_FP_N_9223372036854775808_0_19A5) ? SF2C_SIMD_FP_N_0X7FFFFFFFFFFFFFFFULL_1E66 : r < K(-SF2C_SIMD_FP_N_9223372036854775808_0_19A5) ? SF2C_SIMD_FP_N_0X8000000000000000ULL_1BED
                                                                                              : (uint64_t)(int64_t)r;
}

// ---- integer -> FP (SCVTF/UCVTF, fixed point), rounded in the FPCR mode
SN T FN(fromint_slow)(Cpu *c, uint64_t v, int ib, int uns, int fbits) {
#pragma STDC FENV_ACCESS ON
    int old = simd_host_round();
    simd_set_host_round(simd_fe_mode(c->fpcr));
    volatile uint64_t vv = v;
    volatile T r;
    if (uns) r = ib == SF2C_SIMD_FP_N_32_E29C ? (T)(uint32_t)vv : (T)vv;
    else r = ib == SF2C_SIMD_FP_N_32_E29C ? (T)(int32_t)vv : (T)(int64_t)vv;
    simd_set_host_round(old);
    T x = r;
    if (fbits) x = x / (T)(SF2C_SIMD_FP_N_1ULL_622E << (fbits - 1)) / K(SF2C_SIMD_FP_N_2_D473);
    return x;
}
SI T FN(fromint)(Cpu *c, uint64_t v, int ib, int uns, int fbits) {
    if (__builtin_expect(c->fpcr & FPCR_RM, 0)) return FN(fromint_slow)(c, v, ib, uns, fbits);
    T r;
    if (uns) r = ib == SF2C_SIMD_FP_N_32_E29C ? (T)(uint32_t)v : (T)v;
    else r = ib == SF2C_SIMD_FP_N_32_E29C ? (T)(int32_t)v : (T)(int64_t)v;
    if (fbits) r = r / (T)(SF2C_SIMD_FP_N_1ULL_622E << (fbits - 1)) / K(SF2C_SIMD_FP_N_2_D473);
    return r;
}

// ---- reciprocal estimates (bit-exact ARMv8.0 pseudocode)
SI T FN(recpe)(Cpu *c, T a) {
    U x = FN(fb)(FN(fz)(c, a)), s = x & SGN;
    if (FN(isnan)(x)) return FN(nan1)(c, a);
    if (FN(isinf)(x)) return FN(fv)(s);
    if (FN(iszero)(x)) return FN(fv)(s | EXPM);
    U mag = x & ~SGN;
    if (mag < ((U)1 << (MB - SF2C_SIMD_FP_N_2_D473))) { // |x| < 2^(emin - 2): result overflows
        int rm = (c->fpcr >> SF2C_SIMD_FP_N_22_785F) & SF2C_SIMD_FP_N_3_4E07;
        int inf = rm == 0 || (rm == 1 && !s) || (rm == SF2C_SIMD_FP_N_2_D473 && s);
        return FN(fv)(s | (inf ? EXPM : EXPM - 1));
    }
    int64_t e = (int64_t)(mag >> MB);
    U frac = mag & (((U)1 << MB) - 1);
    if ((c->fpcr & FPCR_FZ) && e >= (int64_t)((1 << EB) - SF2C_SIMD_FP_N_3_4E07)) return FN(fv)(s); // result would be tiny
    if (e == 0) {
        if (!(frac >> (MB - 1))) e = -1, frac = (frac << SF2C_SIMD_FP_N_2_D473) & (((U)1 << MB) - 1);
        else frac = (frac << 1) & (((U)1 << MB) - 1);
    }
    int scaled = SF2C_SIMD_FP_N_256_51E8 | (int)(frac >> (MB - SF2C_SIMD_FP_N_8_2C62));
    int64_t re = (int64_t)((1 << EB) - SF2C_SIMD_FP_N_3_4E07) - e;
    U est = (U)simd_recip_tab[scaled - SF2C_SIMD_FP_N_256_51E8]; // 8 fraction bits
    U f = est << (MB - SF2C_SIMD_FP_N_8_2C62);
    if (re == 0) f = (f >> 1) | ((U)1 << (MB - 1));
    else if (re == -1) f = (f >> SF2C_SIMD_FP_N_2_D473) | ((U)1 << (MB - SF2C_SIMD_FP_N_2_D473)), re = 0;
    return FN(fv)(s | ((U)re << MB) | f);
}
SI T FN(rsqrte)(Cpu *c, T a) {
    U x = FN(fb)(FN(fz)(c, a)), s = x & SGN;
    if (FN(isnan)(x)) return FN(nan1)(c, a);
    if (FN(iszero)(x)) return FN(fv)(s | EXPM);
    if (s) return FN(fv)(DNAN);
    if (FN(isinf)(x)) return K(0);
    int64_t e = (int64_t)(x >> MB);
    U frac = x & (((U)1 << MB) - 1);
    if (e == 0) {
        while (!(frac >> (MB - 1))) frac <<= 1, e--;
        frac = (frac << 1) & (((U)1 << MB) - 1);
    }
    int scaled = (e & 1) ? SF2C_SIMD_FP_N_128_2747 | (int)(frac >> (MB - SF2C_SIMD_FP_N_7_7902)) : SF2C_SIMD_FP_N_256_51E8 | (int)(frac >> (MB - SF2C_SIMD_FP_N_8_2C62));
    int64_t re = ((int64_t)(SF2C_SIMD_FP_N_3_4E07 * ((1 << (EB - 1)) - 1) - 1) - e) / SF2C_SIMD_FP_N_2_D473; // (380 - e) / 2, (3068 - e) / 2
    U est = (U)simd_rsqrt_tab[scaled - SF2C_SIMD_FP_N_128_2747];
    return FN(fv)(((U)re << MB) | (est << (MB - SF2C_SIMD_FP_N_8_2C62)));
}
SI T FN(recpx)(Cpu *c, T a) {
    U x = FN(fb)(FN(fz)(c, a)), s = x & SGN;
    if (FN(isnan)(x)) return FN(nan1)(c, a);
    if (!(x & EXPM)) return FN(fv)(s | (EXPM - MINN));
    return FN(fv)(s | (~x & EXPM));
}

#undef FN
#undef SGN
#undef EXPM
#undef QB
#undef DNAN
#undef MINN
#undef VK
#undef K

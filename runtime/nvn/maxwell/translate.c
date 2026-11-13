#include "../../constants/nvn_maxwell.h"
// Maxwell (SM 5.x) shader binary -> Metal Shading Language, or Vulkan GLSL (key->glsl; contract in shader.h).
//
// The binary is an NVN header (0x30), the shader program header (SPH, 0x50; graphics stages), then
// instructions in groups of four 64-bit words (one scheduling word + three instructions).
// Registers are emitted as `uint` variables; float ops use as_type<> conversions. Shaders with
// branches become a `switch (pc)` state machine with explicit SSY/PBK/PCNT stacks.
//
// GLSL: the instruction body is generated in the MSL spelling and converted by glsl_rewrite() (a token-aware
// pass: as_type<T>() -> as_u/as_f overloads, float4 -> vec4, ulong -> uint64_t, char()/short() casts -> sx8/sx16,
// in.x / out.x stage I/O -> GLSL globals and built-ins), helpers with MSL names (saturate, fmin, clz, mulhi, ...)
// are defined in the GLSL prelude, and the structurally different constructs (texture lookups, global memory,
// half-float packing, subgroup shuffles, barriers, discard, declarations and entry point) are emitted per dialect.
// The MSL text is byte-identical to the MSL-only translator (tools/test_translate.c checks it).
#if defined(_WIN32)
#include <malloc.h>  // alloca
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shader.h"

uint64_t maxwell_gmem_base, maxwell_gmem_size;
uint32_t maxwell_gmem_chunk_shift;  // 0: one buffer for the whole heap (GLSL; shader.h)
bool maxwell_gmem_lazy;
bool maxwell_gmem_fs_miss;
const uint8_t *maxwell_dbg_code;
uint32_t maxwell_dbg_pc;
int maxwell_dbg_reg;
const uint8_t *maxwell_dbg_vs;

#define RZ SF2C_TRANSLATE_RZ
#define LMEM_WORDS SF2C_TRANSLATE_N_256_51E8  // local memory per invocation (1 KB)
#define PT SF2C_TRANSLATE_PT
typedef struct {
    char *buf;
    size_t len, cap;
} Str;

static void sp(Str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (s->len + n + 1 > s->cap) {
        s->cap = (s->len + n + 1) * SF2C_TRANSLATE_N_2_D473;
        s->buf = realloc(s->buf, s->cap);
    }
    vsnprintf(s->buf + s->len, n + 1, fmt, ap2);
    va_end(ap2);
    s->len += n;
}

typedef struct {
    const uint8_t *code;   // first instruction group
    size_t ninsts;         // number of 8-byte words to scan
    int stage;
    const ShaderKey *key;
    ShaderInfo *info;
    Str body;
    uint8_t reg_used[SF2C_TRANSLATE_N_256_51E8];
    uint8_t pred_used[SF2C_TRANSLATE_N_8_2C62];
    uint32_t in_attr_mask, out_attr_mask;
    uint32_t vs_in_comp[SF2C_TRANSLATE_N_32_E29C];
    bool uses_branches, uses_cc, uses_local, uses_vid, uses_iid, uses_frag_coord, uses_front_facing, uses_lane;
    bool uses_rscale;  // resolution-scale table (fragment/compute only)
    uint32_t flat_mask;           // fragment inputs using constant (flat) interpolation
    uint32_t noperspective_mask;  // fragment inputs using pass (linear) interpolation
    uint32_t omap_target, omap_depth;
    int tex_slot[SF2C_TRANSLATE_N_8192_864A];           // texture index -> Metal slot + 1
    bool tex_weak[SF2C_TRANSLATE_N_64_A68B];            // slot type only guessed (from a size query): the next sample decides
    bool fail;
    bool branchy;  // program uses the pc state machine
    int ipa_mode;  // interpolation mode of the IPA being translated (-1 for plain loads)
    int ipa_mul;   // its multiplier register (mode 1: the result is the "pass" value attr/w times this register)
    // GLSL (fields kept last: the MSL path's layout, including its out-of-range reg_used writes, is unchanged)
    bool glsl;               // emit Vulkan GLSL: dialect-specific constructs here, the rest via glsl_rewrite()
    bool uses_gmem_store;    // STG present: the gmem buffer is not readonly
    uint32_t cbuf_need[SF2C_TRANSLATE_N_18_4EC9];  // highest constant vec4 index + 1 per cbuf (array size of the GLSL block)
} Ctx;

static uint64_t bits(uint64_t w, int lo, int n) { return (w >> lo) & ((n == SF2C_TRANSLATE_N_64_A68B) ? ~SF2C_TRANSLATE_N_0ULL_DF1B : ((SF2C_TRANSLATE_N_1ULL_622E << n) - 1)); }
static int64_t sbits(uint64_t w, int lo, int n) {
    int64_t v = (int64_t)bits(w, lo, n);
    return (v & (SF2C_TRANSLATE_N_1LL_AD28 << (n - 1))) ? v - (SF2C_TRANSLATE_N_1LL_AD28 << n) : v;
}

static void fail(Ctx *c, const char *fmt, ...) {
    if (c->fail) return;
    c->fail = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->info->error, sizeof c->info->error, fmt, ap);
    va_end(ap);
}

// Branch targets may point at a scheduling word; execution continues at the next instruction.
static uint32_t norm_target(int64_t t) {
    if (t >= 0 && ((t / SF2C_TRANSLATE_N_8_2C62) % SF2C_TRANSLATE_N_4_4B22) == 0) t += SF2C_TRANSLATE_N_8_2C62;
    return (uint32_t)t;
}

// ------------------------------------------------------------------ operands
static const char *reg(Ctx *c, int r, char *buf) {
    if (r == RZ) return SF2C_TRANSLATE_S_0U_B642;
    c->reg_used[r] = 1;
    sprintf(buf, SF2C_TRANSLATE_S_R_D_5474, r);
    return buf;
}

#define R(r) reg(c, (r), alloca(SF2C_TRANSLATE_N_16_B17E))
#define F(expr) fmt_f(alloca(SF2C_TRANSLATE_N_512_94F8), (expr))
static char *fmt_f(char *b, const char *e) { sprintf(b, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_S_42B1, e); return b; }

static const char *pred(Ctx *c, int p, bool neg, char *buf) {
    if (p == PT) return neg ? SF2C_TRANSLATE_S_FALSE_076D : SF2C_TRANSLATE_S_TRUE_18D1;
    c->pred_used[p] = 1;
    sprintf(buf, SF2C_TRANSLATE_S_SP_D_5F16, neg ? SF2C_TRANSLATE_S_EMPTY_19D9 : SF2C_TRANSLATE_S_EMPTY_12AE, p);
    return buf;
}
#define P(p, n) pred(c, (p), (n), alloca(SF2C_TRANSLATE_N_16_B17E))

static const char *cbuf_ref(Ctx *c, int idx, int64_t off, const char *dyn, char *buf) {
    c->info->cbuf_mask |= SF2C_TRANSLATE_N_1U_B8E6 << idx;
    if (idx == 0) {  // a storage buffer binding: which slot (any, when indexed with a register)
        if (dyn) c->info->ssbo_read = SF2C_TRANSLATE_N_0XFFFF_F51F;
        else if (off >= SF2C_TRANSLATE_N_0X110_8C83 && off < SF2C_TRANSLATE_N_0X210_7579) c->info->ssbo_read |= (uint16_t)(SF2C_TRANSLATE_N_1U_B8E6 << ((off - SF2C_TRANSLATE_N_0X110_8C83) >> SF2C_TRANSLATE_N_4_4B22));
        else if (off >= SF2C_TRANSLATE_N_0X510_87F9 && off < SF2C_TRANSLATE_N_0X610_091E) c->info->ssbo_read |= (uint16_t)(SF2C_TRANSLATE_N_1U_B8E6 << ((off - SF2C_TRANSLATE_N_0X510_87F9) >> SF2C_TRANSLATE_N_4_4B22));
    }
    if (off + SF2C_TRANSLATE_N_16_B17E > (int64_t)c->info->cbuf_size[idx]) c->info->cbuf_size[idx] = (uint32_t)((off + SF2C_TRANSLATE_N_16_B17E + SF2C_TRANSLATE_N_255_9556) & ~SF2C_TRANSLATE_N_255_9556);
    if (dyn) c->info->cbuf_dynamic |= SF2C_TRANSLATE_N_1U_B8E6 << idx;
    if (!dyn && idx < SF2C_TRANSLATE_N_18_4EC9 && off >= 0 && (uint32_t)(off >> SF2C_TRANSLATE_N_4_4B22) + 1 > c->cbuf_need[idx]) c->cbuf_need[idx] = (uint32_t)(off >> SF2C_TRANSLATE_N_4_4B22) + 1;
    if (dyn) sprintf(buf, SF2C_TRANSLATE_S_CBR_CB_D_S_LLDU_A305, idx, dyn, (long long)off);
    else sprintf(buf, SF2C_TRANSLATE_S_CB_D_LLD_LLD_F058, idx, (long long)(off >> SF2C_TRANSLATE_N_4_4B22), (long long)((off >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07));
    return buf;
}

// Operand B in its register / constant buffer / immediate forms, selected by the opcode class.
typedef enum { B_REG, B_CBUF, B_IMM } BForm;

static BForm b_form(uint64_t w) {
    uint64_t top = w >> SF2C_TRANSLATE_N_60_39FA;
    if (top == SF2C_TRANSLATE_N_0X5_0BC3) return B_REG;
    if (top == SF2C_TRANSLATE_N_0X4_EB87) return B_CBUF;
    return B_IMM;
}

static const char *operand_b(Ctx *c, uint64_t w, bool float_imm, char *buf) {
    switch (b_form(w)) {
    case B_REG: return reg(c, (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62), buf);
    case B_CBUF: return cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, buf);
    default: {
        uint32_t v = (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_19_9400) | ((uint32_t)bits(w, SF2C_TRANSLATE_N_56_7688, 1) << SF2C_TRANSLATE_N_19_9400);
        if (float_imm) v <<= SF2C_TRANSLATE_N_12_6B51;
        else if (v & SF2C_TRANSLATE_N_0X80000U_F1FE) v |= SF2C_TRANSLATE_N_0XFFF00000U_B916;  // sign-extend 20 bits
        sprintf(buf, SF2C_TRANSLATE_S_0X_08XU_FED9, v);
        return buf;
    }
    }
}
#define B(fi) operand_b(c, w, (fi), alloca(SF2C_TRANSLATE_N_128_2747))

// The "C" operand of three-source ops: REG_39, or the cbuf/reg swap form (0x51/0x53/... classes).
static const char *operand_c_for_ffma(Ctx *c, uint64_t w, char *buf, const char **b_out, char *bbuf) {
    uint64_t op8 = w >> SF2C_TRANSLATE_N_56_7688;
    if ((op8 & SF2C_TRANSLATE_N_0XF8_EFC3) == SF2C_TRANSLATE_N_0X50_4171 && (op8 & SF2C_TRANSLATE_N_7_7902) != 0) {  // e.g. 0x51: B = REG_39, C = cbuf
        *b_out = reg(c, (int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62), bbuf);
        return cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, buf);
    }
    *b_out = operand_b(c, w, true, bbuf);
    return reg(c, (int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62), buf);
}

static void set_reg(Ctx *c, int r, const char *expr) {
    if (r == RZ) return;
    c->reg_used[r] = 1;
    sp(&c->body, SF2C_TRANSLATE_S_R_D_S_N_0D11, r, expr);
}

static const char *fneg_abs(const char *e, bool neg, bool abs_, char *buf) {
    if (abs_ && neg) sprintf(buf, SF2C_TRANSLATE_S_ABS_S_CACC, e);
    else if (abs_) sprintf(buf, SF2C_TRANSLATE_S_ABS_S_BCE4, e);
    else if (neg) sprintf(buf, SF2C_TRANSLATE_S_S_7BDD, e);
    else return e;
    return buf;
}
#define NA(e, n, a) fneg_abs((e), (n), (a), alloca(SF2C_TRANSLATE_N_600_284B))

static const char *fresult(const char *e, bool sat, char *buf) {
    if (sat) sprintf(buf, SF2C_TRANSLATE_S_AS_TYPE_UINT_SATURATE_S_310B, e);
    else sprintf(buf, SF2C_TRANSLATE_S_AS_TYPE_UINT_S_80EA, e);
    return buf;
}
#define FRES(e, sat) fresult((e), (sat), alloca(SF2C_TRANSLATE_N_700_99EE))

static const char *cmp_op(int op) {
    static const char *const ops[] = {SF2C_TRANSLATE_S_FALSE_076D, SF2C_TRANSLATE_S_EMPTY_53A5, SF2C_TRANSLATE_S_EMPTY_A90F, SF2C_TRANSLATE_S_EMPTY_57BA, SF2C_TRANSLATE_S_EMPTY_415D, SF2C_TRANSLATE_S_EMPTY_2002, SF2C_TRANSLATE_S_EMPTY_DDE4, SF2C_TRANSLATE_S_NUM_6509, SF2C_TRANSLATE_S_NAN_0E97,
                                      SF2C_TRANSLATE_S_EMPTY_53A5, SF2C_TRANSLATE_S_EMPTY_A90F, SF2C_TRANSLATE_S_EMPTY_57BA, SF2C_TRANSLATE_S_EMPTY_415D, SF2C_TRANSLATE_S_EMPTY_2002, SF2C_TRANSLATE_S_EMPTY_DDE4, SF2C_TRANSLATE_S_TRUE_18D1};
    return ops[op & SF2C_TRANSLATE_N_15_E629];
}

// float comparison including unordered variants
static void fcmp_expr(char *out, int op, const char *a, const char *b) {
    switch (op) {
    case 0: strcpy(out, SF2C_TRANSLATE_S_FALSE_076D); break;
    case SF2C_TRANSLATE_N_15_E629: strcpy(out, SF2C_TRANSLATE_S_TRUE_18D1); break;
    case SF2C_TRANSLATE_N_7_7902: sprintf(out, SF2C_TRANSLATE_S_ISNAN_S_ISNAN_S_4B5B, a, b); break;
    case SF2C_TRANSLATE_N_8_2C62: sprintf(out, SF2C_TRANSLATE_S_ISNAN_S_ISNAN_S_ED4D, a, b); break;
    default:
        if (op < SF2C_TRANSLATE_N_7_7902) sprintf(out, SF2C_TRANSLATE_S_S_S_S_A123, a, cmp_op(op), b);
        else sprintf(out, SF2C_TRANSLATE_S_S_S_S_ISNAN_S_ISNAN_S_29F6, a, cmp_op(op), b, a, b);
    }
}

static const char *bool_op(int op) { return op == 0 ? SF2C_TRANSLATE_S_EMPTY_880D : op == 1 ? SF2C_TRANSLATE_S_EMPTY_EFF6 : SF2C_TRANSLATE_S_EMPTY_2002; }

static void icmp_expr(char *out, int op, const char *a, const char *b, bool is_signed) {
    static const char *const ops[] = {SF2C_TRANSLATE_S_FALSE_076D, SF2C_TRANSLATE_S_EMPTY_53A5, SF2C_TRANSLATE_S_EMPTY_A90F, SF2C_TRANSLATE_S_EMPTY_57BA, SF2C_TRANSLATE_S_EMPTY_415D, SF2C_TRANSLATE_S_EMPTY_2002, SF2C_TRANSLATE_S_EMPTY_DDE4, SF2C_TRANSLATE_S_TRUE_18D1};
    if (op == 0) strcpy(out, SF2C_TRANSLATE_S_FALSE_076D);
    else if (op == SF2C_TRANSLATE_N_7_7902) strcpy(out, SF2C_TRANSLATE_S_TRUE_18D1);
    else if (is_signed) sprintf(out, SF2C_TRANSLATE_S_INT_S_S_INT_S_2423, a, ops[op], b);
    else sprintf(out, SF2C_TRANSLATE_S_S_S_S_A123, a, ops[op], b);
}

// ------------------------------------------------------------------ attributes
// Maxwell attribute space: 0x70 position, 0x80 + 16*i generic attribute i, 0x2F8 instance id,
// 0x2FC vertex id, 0x3FC front facing, 0x6C point size.
static void attr_load(Ctx *c, int dst, uint32_t addr, const char *idx) {
    char e[SF2C_TRANSLATE_N_256_51E8];
    if (idx) { fail(c, SF2C_TRANSLATE_S_INDEXED_ATTRIBUTE_LOAD_F8B3); return; }
    if (c->stage == STAGE_VERTEX) {
        if (addr >= SF2C_TRANSLATE_N_0X80_D41C && addr < SF2C_TRANSLATE_N_0X280_5907) {
            int a = (addr - SF2C_TRANSLATE_N_0X80_D41C) >> SF2C_TRANSLATE_N_4_4B22, comp = (addr >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07;
            c->in_attr_mask |= SF2C_TRANSLATE_N_1U_B8E6 << a;
            c->vs_in_comp[a] |= SF2C_TRANSLATE_N_1U_B8E6 << comp;
            int kind = c->key ? c->key->attr_kind[a] : 0;
            if (kind == 0) sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_A_D_D_3508, a, comp);
            else sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_A_D_D_3508, a, comp);
        } else if (addr == SF2C_TRANSLATE_N_0X2F8_DE8B) { c->uses_iid = true; strcpy(e, SF2C_TRANSLATE_S_IID_439D); }
        else if (addr == SF2C_TRANSLATE_N_0X2FC_B794) { c->uses_vid = true; strcpy(e, SF2C_TRANSLATE_S_VID_569D); }
        else { sprintf(e, SF2C_TRANSLATE_S_0U_ATTR_X_D469, addr); }
    } else if (c->stage == STAGE_FRAGMENT) {
        if (addr >= SF2C_TRANSLATE_N_0X70_B0F6 && addr < SF2C_TRANSLATE_N_0X80_D41C) {
            c->uses_frag_coord = true;
            int comp = (addr >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07;
            if (comp < SF2C_TRANSLATE_N_2_D473) {  // guest pixel units: the render target may be allocated larger
                c->uses_rscale = true;
                sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_POS_D_RSCALE_1E9E, comp, comp + SF2C_TRANSLATE_N_2_D473, comp);
            } else {
                sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_POS_D_6B6A, comp);
            }
        } else if (addr >= SF2C_TRANSLATE_N_0X80_D41C && addr < SF2C_TRANSLATE_N_0X280_5907) {
            int a = (addr - SF2C_TRANSLATE_N_0X80_D41C) >> SF2C_TRANSLATE_N_4_4B22, comp = (addr >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07;
            // (MSL: a mask of 0 means unknown. GLSL: the renderer always passes the vertex shader's outputs, and
            // Vulkan requires every fragment input to be a vertex output, so 0 means none.)
            if (c->key && (c->key->vs_out_mask || c->key->glsl) && !(c->key->vs_out_mask & (SF2C_TRANSLATE_N_1U_B8E6 << a))) {
                strcpy(e, comp == SF2C_TRANSLATE_N_3_4E07 ? SF2C_TRANSLATE_S_0X3F800000U_86AD : SF2C_TRANSLATE_S_0U_B642);  // not provided by the vertex shader
            } else {
                c->in_attr_mask |= SF2C_TRANSLATE_N_1U_B8E6 << a;
                // IPA "pass" yields attr/w (the shader multiplies by w itself); Metal gives attr. "multiply" is the
                // pass value times a register - usually w (from the pass of position w: Metal's attr again), but a
                // shader may pass its own (the Mysterious Woods mist: 1/(v4.w * pos.w), a projective screen lookup)
                if (c->ipa_mode == 0) sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_V_D_D_IN_POS_FE2F, a, comp);
                else if (c->ipa_mode == 1 && c->ipa_mul != RZ) {
                    char mb[SF2C_TRANSLATE_N_16_B17E];
                    sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_V_D_D_IN_POS_F2E6, a, comp, reg(c, c->ipa_mul, mb));
                } else sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_IN_V_D_D_E02A, a, comp);
            }
        } else if (addr == SF2C_TRANSLATE_N_0X3FC_F4C9) { c->uses_front_facing = true; strcpy(e, SF2C_TRANSLATE_S_FRONT_FACING_0XFFFFFFFFU_0U_AAB5); }
        else if (addr == SF2C_TRANSLATE_N_0X2E0_4FED || addr == SF2C_TRANSLATE_N_0X2E4_C5CE) { sprintf(e, SF2C_TRANSLATE_S_AS_TYPE_UINT_POINT_COORD_D_0A25, (addr >> SF2C_TRANSLATE_N_2_D473) & 1); }
        else { sprintf(e, SF2C_TRANSLATE_S_0U_ATTR_X_D469, addr); }
    } else {
        sprintf(e, SF2C_TRANSLATE_S_0U_ATTR_X_D469, addr);
    }
    set_reg(c, dst, e);
}

static void attr_store(Ctx *c, int src, uint32_t addr) {
    char b[SF2C_TRANSLATE_N_16_B17E];
    const char *v = reg(c, src, b);
    if (c->glsl && c->stage != STAGE_VERTEX) {  // no vertex outputs in this stage (MSL: undeclared `out`)
        if (addr >= SF2C_TRANSLATE_N_0X80_D41C && addr < SF2C_TRANSLATE_N_0X280_5907) c->out_attr_mask |= SF2C_TRANSLATE_N_1U_B8E6 << ((addr - SF2C_TRANSLATE_N_0X80_D41C) >> SF2C_TRANSLATE_N_4_4B22);
        if (addr == SF2C_TRANSLATE_N_0X6C_9944) c->info->writes_point_size = true;
        sp(&c->body, SF2C_TRANSLATE_S_STORE_ATTR_X_N_F531, addr);
        return;
    }
    if (addr >= SF2C_TRANSLATE_N_0X70_B0F6 && addr < SF2C_TRANSLATE_N_0X80_D41C) {
        sp(&c->body, SF2C_TRANSLATE_S_OUT_POS_D_AS_TYPE_FLOAT_S_N_CC58, (addr >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07, v);
    } else if (addr >= SF2C_TRANSLATE_N_0X80_D41C && addr < SF2C_TRANSLATE_N_0X280_5907) {
        int a = (addr - SF2C_TRANSLATE_N_0X80_D41C) >> SF2C_TRANSLATE_N_4_4B22;
        c->out_attr_mask |= SF2C_TRANSLATE_N_1U_B8E6 << a;
        sp(&c->body, SF2C_TRANSLATE_S_OUT_V_D_D_AS_TYPE_FLOAT_S_N_7B25, a, (addr >> SF2C_TRANSLATE_N_2_D473) & SF2C_TRANSLATE_N_3_4E07, v);
    } else if (addr == SF2C_TRANSLATE_N_0X6C_9944) {
        c->info->writes_point_size = true;
        sp(&c->body, SF2C_TRANSLATE_S_OUT_PSIZE_AS_TYPE_FLOAT_S_N_0CCC, v);
    } else {
        sp(&c->body, SF2C_TRANSLATE_S_STORE_ATTR_X_N_F531, addr);
    }
}


// ------------------------------------------------------------------ half precision (SM53)
// Swizzle: 0 H1_H0 (both halves), 1 F32 (one f32 used for both lanes), 2 H0_H0, 3 H1_H1.
// Merge: 0 H1_H0 (pack both), 1 F32 (lane 0 as f32), 2 MRG_H0 (replace low half), 3 MRG_H1 (replace high half).
static const char *hsrc(Ctx *c, const char *v, int swz, bool neg, bool abs_, char *buf) {
    (void)c;
    char t[SF2C_TRANSLATE_N_300_983B];
    sprintf(t, SF2C_TRANSLATE_S_HX_S_DU_92F3, v, swz);
    if (abs_ && neg) sprintf(buf, SF2C_TRANSLATE_S_ABS_S_CACC, t);
    else if (abs_) sprintf(buf, SF2C_TRANSLATE_S_ABS_S_BCE4, t);
    else if (neg) sprintf(buf, SF2C_TRANSLATE_S_S_7BDD, t);
    else strcpy(buf, t);
    return buf;
}
#define HS(v, sw, n, ab) hsrc(c, (v), (sw), (n), (ab), alloca(SF2C_TRANSLATE_N_400_26D2))

static void hstore(Ctx *c, int d, const char *expr, int merge, bool sat) {
    char t[SF2C_TRANSLATE_N_2048_BFA0], e[SF2C_TRANSLATE_N_1700_B97D];
    if (sat) sprintf(e, SF2C_TRANSLATE_S_SATURATE_S_5446, expr); else strcpy(e, expr);
    if (d == RZ) return;
    c->reg_used[d] = 1;
    sprintf(t, SF2C_TRANSLATE_S_HPACK_S_DU_R_D_FAA6, e, merge, d);
    set_reg(c, d, t);
}

// immediate halves: 9 bits of each half's top (the low 6 mantissa bits are zero) plus separate signs
static uint32_t himm(uint64_t w) {
    uint32_t lo = ((uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_9_1958) << SF2C_TRANSLATE_N_6_E7F6) | ((uint32_t)bits(w, SF2C_TRANSLATE_N_29_3513, 1) << SF2C_TRANSLATE_N_15_E629);
    uint32_t hi = ((uint32_t)bits(w, SF2C_TRANSLATE_N_30_624B, SF2C_TRANSLATE_N_9_1958) << SF2C_TRANSLATE_N_6_E7F6) | ((uint32_t)bits(w, SF2C_TRANSLATE_N_56_7688, 1) << SF2C_TRANSLATE_N_15_E629);
    return lo | (hi << SF2C_TRANSLATE_N_16_B17E);
}

static bool emit_half(Ctx *c, uint64_t w) {
    int d = (int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62), a = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62);
    char t[SF2C_TRANSLATE_N_1800_E49E], ib[SF2C_TRANSLATE_N_32_E29C], cb[SF2C_TRANSLATE_N_256_51E8];
    uint16_t op = (uint16_t)(w >> SF2C_TRANSLATE_N_48_9801);
    // ---- HADD2 / HMUL2 (reg 5d10/5d08, cbuf 7a8x/788x, imm 7a0x/780x, 32I 2c/2a)
    int kind = -1;  // 0 add, 1 mul
    int form = -1;  // 0 reg, 1 cbuf, 2 imm, 3 32i
    if ((op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D10_1B44) { kind = 0; form = 0; }
    else if ((op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D08_AA25) { kind = 1; form = 0; }
    else if ((op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7A80_E795) { kind = 0; form = 1; }
    else if ((op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7A00_FC49) { kind = 0; form = SF2C_TRANSLATE_N_2_D473; }
    else if ((op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7880_6E42) { kind = 1; form = 1; }
    else if ((op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7800_4600) { kind = 1; form = SF2C_TRANSLATE_N_2_D473; }
    else if ((op & SF2C_TRANSLATE_N_0XFE00_EBE7) == SF2C_TRANSLATE_N_0X2C00_302A) { kind = 0; form = SF2C_TRANSLATE_N_3_4E07; }
    else if ((op & SF2C_TRANSLATE_N_0XFE00_EBE7) == SF2C_TRANSLATE_N_0X2A00_F779) { kind = 1; form = SF2C_TRANSLATE_N_3_4E07; }
    if (kind >= 0) {
        int merge, swa, swb = 0;
        bool sat, nega, absa = false, negb = false, absb = false;
        const char *bv;
        if (form == SF2C_TRANSLATE_N_3_4E07) {
            merge = 0; sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); swa = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_2_D473); nega = kind == 0 ? bits(w, SF2C_TRANSLATE_N_56_7688, 1) : false;
            sprintf(ib, SF2C_TRANSLATE_S_0X_08XU_FED9, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C)); bv = ib;
        } else {
            // field layout per Ryujinx's InstDecoders (InstHadd2*/InstHmul2*)
            merge = (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_2_D473); swa = (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473); nega = bits(w, SF2C_TRANSLATE_N_43_44CB, 1); absa = bits(w, SF2C_TRANSLATE_N_44_71EE, 1);
            if (form == 0) {
                sat = bits(w, SF2C_TRANSLATE_N_32_E29C, 1); absb = bits(w, SF2C_TRANSLATE_N_30_624B, 1); swb = (int)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_2_D473);
                if (kind == 0) negb = bits(w, SF2C_TRANSLATE_N_31_EB1E, 1);
                else { nega = bits(w, SF2C_TRANSLATE_N_31_EB1E, 1); negb = false; }  // HMUL2 reg: the negate bit is A's at 31
                bv = R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62));
            } else if (form == 1) {
                sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); negb = kind == 0 ? bits(w, SF2C_TRANSLATE_N_56_7688, 1) : false; absb = bits(w, SF2C_TRANSLATE_N_54_2FCA, 1); swb = 1;
                bv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
            } else {
                sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); swb = 0;
                sprintf(ib, SF2C_TRANSLATE_S_0X_08XU_FED9, himm(w)); bv = ib;
            }
        }
        sprintf(t, kind == 0 ? SF2C_TRANSLATE_S_S_S_D630 : SF2C_TRANSLATE_S_S_S_EB58, HS(R(a), swa, nega, absa), HS(bv, swb, negb, absb));
        hstore(c, d, t, merge, sat);
        return true;
    }
    // ---- HFMA2 (reg 5d00, cr 70xx|0x80, rc 60xx|0x80, imm 70xx, 32I 28)
    form = -1;
    if ((op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D00_527C) form = 0;
    else if ((op & SF2C_TRANSLATE_N_0XF880_B680) == SF2C_TRANSLATE_N_0X7080_3A7A) form = 1;  // b = cbuf, c = reg
    else if ((op & SF2C_TRANSLATE_N_0XF880_B680) == SF2C_TRANSLATE_N_0X6080_E361) form = SF2C_TRANSLATE_N_2_D473;  // b = reg, c = cbuf
    else if ((op & SF2C_TRANSLATE_N_0XF880_B680) == SF2C_TRANSLATE_N_0X7000_CA34) form = SF2C_TRANSLATE_N_3_4E07;  // b = imm, c = reg
    else if ((op & SF2C_TRANSLATE_N_0XFE00_EBE7) == SF2C_TRANSLATE_N_0X2800_3E8F) form = SF2C_TRANSLATE_N_4_4B22;  // 32I: c = d
    if (form >= 0) {
        int merge, swa = (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473), swb = 0, swc = 0;
        bool sat, negb, negc;
        const char *bv, *cv;
        char cb2[SF2C_TRANSLATE_N_256_51E8];
        switch (form) {  // field layout per Ryujinx's InstDecoders (InstHfma2*); NegA is applied to B (same product)
        case 0:
            merge = (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_2_D473); sat = bits(w, SF2C_TRANSLATE_N_32_E29C, 1); negb = bits(w, SF2C_TRANSLATE_N_31_EB1E, 1); negc = bits(w, SF2C_TRANSLATE_N_30_624B, 1);
            swb = (int)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_2_D473); swc = (int)bits(w, SF2C_TRANSLATE_N_35_9F14, SF2C_TRANSLATE_N_2_D473);
            bv = R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62)); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            break;
        case 1:  // b = cbuf, c = reg
            merge = (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_2_D473); sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); negb = bits(w, SF2C_TRANSLATE_N_56_7688, 1); negc = bits(w, SF2C_TRANSLATE_N_51_031B, 1);
            swb = 1; swc = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_2_D473);
            bv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            break;
        case SF2C_TRANSLATE_N_2_D473:  // b = reg (field at 39), c = cbuf
            merge = (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_2_D473); sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); negb = bits(w, SF2C_TRANSLATE_N_56_7688, 1); negc = bits(w, SF2C_TRANSLATE_N_51_031B, 1);
            swb = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_2_D473); swc = 1;
            bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb2);
            break;
        case SF2C_TRANSLATE_N_3_4E07:  // b = imm, c = reg
            merge = (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_2_D473); sat = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); negb = false; negc = bits(w, SF2C_TRANSLATE_N_51_031B, 1);
            swb = 0; swc = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_2_D473);
            sprintf(ib, SF2C_TRANSLATE_S_0X_08XU_FED9, himm(w)); bv = ib; cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            break;
        default:  // 32I: c = d
            merge = 0; sat = false; negb = false; negc = bits(w, SF2C_TRANSLATE_N_51_031B, 1);
            swa = (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473); swb = 0; swc = 0;
            sprintf(ib, SF2C_TRANSLATE_S_0X_08XU_FED9, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C)); bv = ib; cv = R(d);
            break;
        }
        sprintf(t, SF2C_TRANSLATE_S_FMA_S_S_S_85B2, HS(R(a), swa, false, false), HS(bv, swb, negb, false), HS(cv, swc, negc, false));
        hstore(c, d, t, merge, sat);
        return true;
    }
    // ---- HSETP2 / HSET2 (register, constant buffer, immediate)
    // Constant/immediate forms move the comparison and H_AND/BF fields to
    // bits 49 and 53; the constant operand is one f32 broadcast to both lanes.
    // Encoding reference: envytools/envydis/gm107.c (7e80/7e00/7c80/7c00).
    bool cmp_reg = (op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D20_02E8 || (op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D18_A949;
    bool cmp_cbuf = (op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7E80_6033 || (op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7C80_7DB6;
    bool cmp_imm = (op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7E00_C39E || (op & SF2C_TRANSLATE_N_0XFE80_2984) == SF2C_TRANSLATE_N_0X7C00_B0FD;
    if (cmp_reg || cmp_cbuf || cmp_imm) {
        bool setp = cmp_reg ? (op & SF2C_TRANSLATE_N_0XFFF8_FFDB) == SF2C_TRANSLATE_N_0X5D20_02E8 : (op & SF2C_TRANSLATE_N_0X0200_13AD) != 0;
        const char *x = HS(R(a), (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473), bits(w, SF2C_TRANSLATE_N_43_44CB, 1), bits(w, SF2C_TRANSLATE_N_44_71EE, 1));
        const char *y;
        if (cmp_reg) {
            y = HS(R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62)), (int)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_2_D473), bits(w, SF2C_TRANSLATE_N_31_EB1E, 1), bits(w, SF2C_TRANSLATE_N_30_624B, 1));
        } else if (cmp_cbuf) {
            const char *bv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
            y = HS(bv, 1, bits(w, SF2C_TRANSLATE_N_56_7688, 1), bits(w, SF2C_TRANSLATE_N_54_2FCA, 1));
        } else {
            sprintf(ib, SF2C_TRANSLATE_S_0X_08XU_FED9, himm(w));
            y = HS(ib, 0, false, false);
        }
        int cop = (int)bits(w, cmp_reg ? SF2C_TRANSLATE_N_35_9F14 : SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_4_4B22), bop = (int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473);
        bool combine = bits(w, cmp_reg ? SF2C_TRANSLATE_N_49_0E17 : SF2C_TRANSLATE_N_53_2858, 1);
        const char *pp = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        char lx[SF2C_TRANSLATE_N_512_94F8], ly[SF2C_TRANSLATE_N_512_94F8];
        sp(&c->body, SF2C_TRANSLATE_S_FLOAT2_HX_S_HY_S_BOOL_PP_S_N_BA8C, x, y, pp);
        fcmp_expr(lx, cop, SF2C_TRANSLATE_S_HX_X_96C4, SF2C_TRANSLATE_S_HY_X_C990);
        fcmp_expr(ly, cop, SF2C_TRANSLATE_S_HX_Y_ACC5, SF2C_TRANSLATE_S_HY_Y_CDE0);
        sp(&c->body, SF2C_TRANSLATE_S_BOOL_L_S_H_S_N_9FC4, lx, ly);
        if (setp) {
            int pa = (int)bits(w, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_3_4E07), pb = (int)bits(w, 0, SF2C_TRANSLATE_N_3_4E07);
            if (combine) {  // .H_AND: each half is combined with the predicate first, then they're ANDed
                sp(&c->body, SF2C_TRANSLATE_S_L_L_S_PP_H_S_PP_H_L_N_D5CB, bool_op(bop), bool_op(bop));
                if (pa != PT) { c->pred_used[pa] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_L_N_B745, pa); }
                if (pb != PT) { c->pred_used[pb] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_H_N_43F7, pb); }
            } else {
                if (pa != PT) { c->pred_used[pa] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_L_S_PP_N_4D86, pa, bool_op(bop)); }
                if (pb != PT) { c->pred_used[pb] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_H_S_PP_N_6FCA, pb, bool_op(bop)); }
            }
        } else {
            bool bf = combine;
            if (d != RZ) {
                c->reg_used[d] = 1;
                sp(&c->body, SF2C_TRANSLATE_S_L_L_S_PP_H_H_S_PP_N_BFAD, bool_op(bop), bool_op(bop));
                sp(&c->body, bf ? SF2C_TRANSLATE_S_R_D_L_0X3C00U_0U_H_0X3C00000_BAE5
                                : SF2C_TRANSLATE_S_R_D_L_0XFFFFU_0U_H_0XFFFF000_9B9A, d);
            }
        }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ textures
static int tex_slot(Ctx *c, uint32_t index, int type, bool shadow, bool array) {
    if (index >= SF2C_TRANSLATE_N_8192_864A) return 0;
    if (c->tex_slot[index]) {
        int s = c->tex_slot[index] - 1;
        if (type >= 0 && c->tex_weak[s]) {
            ShaderTexture *t = &c->info->tex[s];
            t->type = (uint8_t)type;
            t->shadow = shadow;
            t->is_array = array;
            c->tex_weak[s] = false;
        } else if (c->glsl && type >= 0 && (c->info->tex[s].type != type || c->info->tex[s].shadow != shadow)) {
            fail(c, SF2C_TRANSLATE_S_TEXTURE_U_USED_AS_TWO_TYPES_09AD, index);  // one GLSL sampler type per binding
        }
        return s;
    }
    bool weak = type < 0;
    if (weak) type = TEXTYPE_2D;
    int s = c->info->ntex++;
    if (s >= SF2C_TRANSLATE_N_31_EB1E) { fail(c, SF2C_TRANSLATE_S_TOO_MANY_TEXTURES_299C); return 0; }
    c->tex_slot[index] = s + 1;
    c->tex_weak[s] = weak;
    ShaderTexture *t = &c->info->tex[s];
    t->handle_offset = index * SF2C_TRANSLATE_N_4_4B22;
    t->type = (uint8_t)type;
    t->shadow = shadow;
    t->is_array = array;
    return s;
}

static const char *tex_type_name(int t) {
    switch (t) {
    case TEXTYPE_1D: return SF2C_TRANSLATE_S_1D_7949;
    case TEXTYPE_3D: return SF2C_TRANSLATE_S_3D_39B4;
    case TEXTYPE_CUBE: return SF2C_TRANSLATE_S_CUBE_9C6C;
    case TEXTYPE_2D_ARRAY: return SF2C_TRANSLATE_S_2D_ARRAY_7942;
    case TEXTYPE_CUBE_ARRAY: return SF2C_TRANSLATE_S_CUBE_ARRAY_2479;
    case TEXTYPE_1D_ARRAY: return SF2C_TRANSLATE_S_1D_ARRAY_1EA5;
    default: return SF2C_TRANSLATE_S_2D_F95D;
    }
}

// Write texture results: component mask selects which of r,g,b,a go to consecutive registers.
static void write_tex_result(Ctx *c, int slot, const char *call, uint32_t mask, int d0, int d1, bool texs, bool f16) {
    sp(&c->body, SF2C_TRANSLATE_S_FLOAT4_T_S_N_17EE, call);
    (void)slot;
    if (f16) {
        // TEXS/TLDS .F16: components (in mask order) packed as half pairs into d0 (lo, hi) and d1 (lo, hi)
        int comps[SF2C_TRANSLATE_N_4_4B22], n = 0;
        for (int comp = 0; comp < SF2C_TRANSLATE_N_4_4B22; comp++) if (mask & (SF2C_TRANSLATE_N_1U_B8E6 << comp)) comps[n++] = comp;
        char v[SF2C_TRANSLATE_N_4_4B22][SF2C_TRANSLATE_N_16_B17E];
        for (int i = 0; i < SF2C_TRANSLATE_N_4_4B22; i++) { if (i < n) sprintf(v[i], SF2C_TRANSLATE_S_T_D_0021, comps[i]); else strcpy(v[i], SF2C_TRANSLATE_S_0_0F_3781); }
        const char *pk = c->glsl ? SF2C_TRANSLATE_S_R_D_PACKHALF2X16_VEC2_S_S_N_7951 : SF2C_TRANSLATE_S_R_D_AS_TYPE_UINT_HALF2_HALF__9657;
        if (d0 != RZ) { c->reg_used[d0] = 1; sp(&c->body, pk, d0, v[0], v[1]); }
        if (d1 != RZ) { c->reg_used[d1] = 1; sp(&c->body, pk, d1, v[SF2C_TRANSLATE_N_2_D473], v[SF2C_TRANSLATE_N_3_4E07]); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        return;
    }
    int regs[SF2C_TRANSLATE_N_4_4B22], n = 0;
    if (texs) {
        // TEXS: up to two components in d0/d0+1, two more in d1/d1+1
        int out[SF2C_TRANSLATE_N_4_4B22] = {d0, d0 == RZ ? RZ : d0 + 1, d1, d1 == RZ ? RZ : d1 + 1};
        for (int i = 0; i < SF2C_TRANSLATE_N_4_4B22; i++) regs[i] = out[i];
    } else {
        for (int i = 0; i < SF2C_TRANSLATE_N_4_4B22; i++) regs[i] = d0 == RZ ? RZ : d0 + i;
    }
    for (int comp = 0; comp < SF2C_TRANSLATE_N_4_4B22; comp++) {
        if (!(mask & (SF2C_TRANSLATE_N_1U_B8E6 << comp))) continue;
        int r = regs[n++];
        if (r != RZ) { c->reg_used[r] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_AS_TYPE_UINT_T_D_N_3581, r, comp); }
    }
    sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
}

// Integer texel coordinates in guest units -> the (possibly larger) allocation of texture slot `slot`.
static const char *RSC2(Ctx *c, int slot, const char *fmt, ...) {
    static char bufs[SF2C_TRANSLATE_N_4_4B22][SF2C_TRANSLATE_N_1024_E39E];
    static int bi;
    char *o = bufs[bi++ & SF2C_TRANSLATE_N_3_4E07];
    char inner[SF2C_TRANSLATE_N_700_99EE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(inner, sizeof inner, fmt, ap);
    va_end(ap);
    if (c->stage == STAGE_VERTEX) { snprintf(o, SF2C_TRANSLATE_N_1024_E39E, SF2C_TRANSLATE_S_S_0030, inner); return o; }
    c->uses_rscale = true;
    snprintf(o, SF2C_TRANSLATE_N_1024_E39E, SF2C_TRANSLATE_S_RSC_S_RSCALE_D_B878, inner, slot + 1);
    return o;
}

// TEXS / TLDS: texture_info selects target and coordinate layout
static const char *alayer(Ctx *c, const char *r, int slot, char *buf) {
    if (c->glsl) sprintf(buf, SF2C_TRANSLATE_S_MIN_S_UINT_TSIZE_D_0_Z_1U_16E6, r, slot);
    else sprintf(buf, SF2C_TRANSLATE_S_MIN_S_T_D_GET_ARRAY_SIZE_1U_0BB4, r, slot);
    return buf;
}
#define ALAYER(r, slot) alayer(c, (r), (slot), alloca(SF2C_TRANSLATE_N_96_7B1A))

// 2D normalized coordinates: a scaled texture whose contents occupy its top-left part (Dynamic resolution) is sampled
// there (prelude uvs(); rscale[1 + slot].zw). Vertex shaders have no rscale table: their lookups stay as they are.
static const char *uv2(Ctx *c, int slot, const char *x, const char *y, char *buf) {
    if (c->stage == STAGE_VERTEX) { snprintf(buf, SF2C_TRANSLATE_N_700_99EE, SF2C_TRANSLATE_S_S2_S_S_D3D6, c->glsl ? SF2C_TRANSLATE_S_VEC_A25E : SF2C_TRANSLATE_S_FLOAT_1E71, x, y); return buf; }
    c->uses_rscale = true;
    snprintf(buf, SF2C_TRANSLATE_N_700_99EE, SF2C_TRANSLATE_S_UVS_S2_S_S_RSCALE_D_DEA2, c->glsl ? SF2C_TRANSLATE_S_VEC_A25E : SF2C_TRANSLATE_S_FLOAT_1E71, x, y, slot + 1);
    return buf;
}
#define UV2(slot, x, y) uv2(c, (slot), (x), (y), alloca(SF2C_TRANSLATE_N_700_99EE))

// GLSL texture lookup on slot `slot` (its type is final here: a conflicting use fails in tex_slot).
// co: coordinate expressions (1 for 1D, 2 for 2D, 3 for 3D / cube), layer: uint expression or NULL, dref: depth
// reference or NULL, lod_kind 0 implicit / 1 level / 2 bias (lod), off: ivec2 texel offset or NULL.
// GLSL offsets must be constants, so register offsets move the coordinates by offset / size of the level
// (level 0 for implicit-LOD lookups). Non-fragment stages have no implicit LOD: level 0 (+ bias).
static void glsl_sample(Ctx *c, char *out, int slot, const char *const *co, const char *layer, const char *dref,
                        int lod_kind, const char *lod, const char *off) {
    if (c->fail) { strcpy(out, SF2C_TRANSLATE_S_VEC4_0_0F_F172); return; }  // e.g. a type conflict: tex[slot] is not this lookup's type
    const ShaderTexture *t = &c->info->tex[slot];
    int type = t->type;
    bool cube = type == TEXTYPE_CUBE || type == TEXTYPE_CUBE_ARRAY;
    int n = (type == TEXTYPE_1D || type == TEXTYPE_1D_ARRAY) ? 1 : (type == TEXTYPE_2D || type == TEXTYPE_2D_ARRAY) ? SF2C_TRANSLATE_N_2_D473 : SF2C_TRANSLATE_N_3_4E07;
    if (c->stage != STAGE_FRAGMENT && lod_kind != 1) {
        if (lod_kind == 0) lod = SF2C_TRANSLATE_S_0_0F_3781;
        lod_kind = 1;
    }
    if (t->shadow && type == TEXTYPE_3D) { fail(c, SF2C_TRANSLATE_S_3D_SHADOW_TEXTURE_6F49); strcpy(out, SF2C_TRANSLATE_S_VEC4_0_0F_F172); return; }
    if (layer && type != TEXTYPE_1D_ARRAY && type != TEXTYPE_2D_ARRAY && type != TEXTYPE_CUBE_ARRAY) {
        fail(c, SF2C_TRANSLATE_S_ARRAY_LOOKUP_ON_A_3D_TEXTURE_7E3F);  // TEX .3D with the array bit
        strcpy(out, SF2C_TRANSLATE_S_VEC4_0_0F_F172);
        return;
    }
    char L[SF2C_TRANSLATE_N_600_284B], xs[SF2C_TRANSLATE_N_1600_B458], P[SF2C_TRANSLATE_N_2000_81A8];
    if (lod_kind == 1) snprintf(L, sizeof L, SF2C_TRANSLATE_S_INT_S_C689, lod); else strcpy(L, SF2C_TRANSLATE_S_0_9808);
    if (n == 1) {
        if (off) snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_S_FLOAT_S_X_FLOAT_TSIZE_D_S__DD8B, co[0], off, slot, L);
        else snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_S_0030, co[0]);
    } else if (n == SF2C_TRANSLATE_N_2_D473) {
        if (off) snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_VEC2_S_S_VEC2_S_VEC2_TSIZE_D_7C88, co[0], co[1], off, slot, L);
        else snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_VEC2_S_S_8EDF, co[0], co[1]);
        if (c->stage != STAGE_VERTEX) { char w[SF2C_TRANSLATE_N_1700_B97D]; snprintf(w, sizeof w, SF2C_TRANSLATE_S_UVS_S_RSCALE_D_9FEA, xs, slot + 1); strcpy(xs, w); c->uses_rscale = true; }
    } else {
        if (off && !cube) snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_VEC3_VEC2_S_S_VEC2_S_VEC2_TS_104A, co[0], co[1], off, slot, L, co[SF2C_TRANSLATE_N_2_D473]);
        else snprintf(xs, sizeof xs, SF2C_TRANSLATE_S_VEC3_S_S_S_1451, co[0], co[1], co[SF2C_TRANSLATE_N_2_D473]);  // cube lookups take no offset
    }
    const char *extra = NULL;  // separate compare argument (cube array shadow)
    if (!t->shadow) {
        if (layer) snprintf(P, sizeof P, SF2C_TRANSLATE_S_VEC_D_S_FLOAT_S_A3E8, n + 1, xs, layer);
        else snprintf(P, sizeof P, SF2C_TRANSLATE_S_S_0030, xs);
    } else if (type == TEXTYPE_CUBE_ARRAY) {
        snprintf(P, sizeof P, SF2C_TRANSLATE_S_VEC4_S_FLOAT_S_E52D, xs, layer ? layer : SF2C_TRANSLATE_S_0U_B642);
        extra = dref;
    } else if (type == TEXTYPE_1D) {
        snprintf(P, sizeof P, SF2C_TRANSLATE_S_VEC3_S_0_0F_S_C2D9, xs, dref);
    } else if (layer) {
        snprintf(P, sizeof P, SF2C_TRANSLATE_S_VEC_D_S_FLOAT_S_S_5AC6, n + SF2C_TRANSLATE_N_2_D473, xs, layer, dref);
    } else {
        snprintf(P, sizeof P, SF2C_TRANSLATE_S_VEC_D_S_S_760C, n + 1, xs, dref);
    }
    char e[SF2C_TRANSLATE_N_2000_81A8], ex[SF2C_TRANSLATE_N_300_983B] = SF2C_TRANSLATE_S_EMPTY_12AE;
    if (extra) snprintf(ex, sizeof ex, SF2C_TRANSLATE_S_S_F2F7, extra);
    if (lod_kind == 0) snprintf(e, sizeof e, SF2C_TRANSLATE_S_TEXTURE_T_D_S_S_C055, slot, P, ex);
    else if (lod_kind == 1) snprintf(e, sizeof e, SF2C_TRANSLATE_S_TEXTURELOD_T_D_S_S_S_B409, slot, P, ex, lod);
    else snprintf(e, sizeof e, SF2C_TRANSLATE_S_TEXTURE_T_D_S_S_S_34AF, slot, P, ex, lod);
    if (t->shadow) sprintf(out, SF2C_TRANSLATE_S_VEC4_S_34F7, e);
    else strcpy(out, e);
}

// TEXS / TLDS in GLSL (same slot allocation order and result layout as the MSL switch in emit_texs)
static bool glsl_texs(Ctx *c, uint64_t w, bool fetch, int *slot_out, char *call, const char *A, const char *A1,
                      const char *Bv, const char *B1) {
    uint32_t index = (uint32_t)bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_13_3FDB);
    int info = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_4_4B22);
    int slot;
    const char *co[SF2C_TRANSLATE_N_3_4E07];
    char fa[SF2C_TRANSLATE_N_64_A68B], fa1[SF2C_TRANSLATE_N_64_A68B], fb[SF2C_TRANSLATE_N_64_A68B], fb1[SF2C_TRANSLATE_N_64_A68B];
    sprintf(fa, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_S_42B1, A); sprintf(fa1, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_S_42B1, A1);
    sprintf(fb, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_S_42B1, Bv); sprintf(fb1, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_S_42B1, B1);
    if (!fetch) {
        switch (info) {
        case 0: slot = tex_slot(c, index, TEXTYPE_1D, false, false); co[0] = fa;
            glsl_sample(c, call, slot, co, NULL, NULL, 0, NULL, NULL); break;
        case 1: slot = tex_slot(c, index, TEXTYPE_2D, false, false); co[0] = fa; co[1] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 0, NULL, NULL); break;
        case SF2C_TRANSLATE_N_2_D473: slot = tex_slot(c, index, TEXTYPE_2D, false, false); co[0] = fa; co[1] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 1, SF2C_TRANSLATE_S_0_0F_3781, NULL); break;
        case SF2C_TRANSLATE_N_3_4E07: slot = tex_slot(c, index, TEXTYPE_2D, false, false); co[0] = fa; co[1] = fa1;
            glsl_sample(c, call, slot, co, NULL, NULL, 1, fb, NULL); break;
        case SF2C_TRANSLATE_N_4_4B22: slot = tex_slot(c, index, TEXTYPE_2D, true, false); co[0] = fa; co[1] = fa1;
            glsl_sample(c, call, slot, co, NULL, fb, 0, NULL, NULL); break;
        case SF2C_TRANSLATE_N_5_EF2D: slot = tex_slot(c, index, TEXTYPE_2D, true, false); co[0] = fa; co[1] = fa1;
            glsl_sample(c, call, slot, co, NULL, fb1, 1, fb, NULL); break;
        case SF2C_TRANSLATE_N_6_E7F6: slot = tex_slot(c, index, TEXTYPE_2D, true, false); co[0] = fa; co[1] = fa1;
            glsl_sample(c, call, slot, co, NULL, fb, 1, SF2C_TRANSLATE_S_0_0F_3781, NULL); break;
        case SF2C_TRANSLATE_N_7_7902: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true); co[0] = fa1; co[1] = fb;
            glsl_sample(c, call, slot, co, ALAYER(A, slot), NULL, 0, NULL, NULL); break;
        case SF2C_TRANSLATE_N_8_2C62: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true); co[0] = fa1; co[1] = fb;
            glsl_sample(c, call, slot, co, ALAYER(A, slot), NULL, 1, SF2C_TRANSLATE_S_0_0F_3781, NULL); break;
        case SF2C_TRANSLATE_N_9_1958: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, true, true); co[0] = fa1; co[1] = fb;
            glsl_sample(c, call, slot, co, A, fb1, 1, SF2C_TRANSLATE_S_0_0F_3781, NULL); break;
        case SF2C_TRANSLATE_N_10_4A44: slot = tex_slot(c, index, TEXTYPE_3D, false, false); co[0] = fa; co[1] = fa1; co[SF2C_TRANSLATE_N_2_D473] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 0, NULL, NULL); break;
        case SF2C_TRANSLATE_N_11_4FC8: slot = tex_slot(c, index, TEXTYPE_3D, false, false); co[0] = fa; co[1] = fa1; co[SF2C_TRANSLATE_N_2_D473] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 1, SF2C_TRANSLATE_S_0_0F_3781, NULL); break;
        case SF2C_TRANSLATE_N_12_6B51: slot = tex_slot(c, index, TEXTYPE_CUBE, false, false); co[0] = fa; co[1] = fa1; co[SF2C_TRANSLATE_N_2_D473] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 0, NULL, NULL); break;
        case SF2C_TRANSLATE_N_13_3FDB: slot = tex_slot(c, index, TEXTYPE_CUBE, false, false); co[0] = fa; co[1] = fa1; co[SF2C_TRANSLATE_N_2_D473] = fb;
            glsl_sample(c, call, slot, co, NULL, NULL, 1, fb1, NULL); break;
        default: fail(c, SF2C_TRANSLATE_S_TEXS_INFO_D_B75C, info); return false;
        }
    } else {
        switch (info) {
        case 0: slot = tex_slot(c, index, TEXTYPE_1D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_INT_S_0_EF65, slot, A); break;
        case 1: slot = tex_slot(c, index, TEXTYPE_1D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_INT_S_INT_S_98A3, slot, A, Bv); break;
        case SF2C_TRANSLATE_N_2_D473: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC2_S_0_667B, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A, Bv)); break;
        case SF2C_TRANSLATE_N_4_4B22: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC2_S_0_667B, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_INT2_S_S_INT2_INT_S_28_748E, A, A1, Bv, Bv)); break;
        case SF2C_TRANSLATE_N_5_EF2D: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC2_S_INT_S_0DD0, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A, A1), Bv); break;
        case SF2C_TRANSLATE_N_7_7902: slot = tex_slot(c, index, TEXTYPE_3D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC3_S_S_S_0_AE90, slot, A, A1, Bv); break;
        case SF2C_TRANSLATE_N_8_2C62: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC3_IVEC2_S_DD4D, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A1, Bv), A); break;
        case SF2C_TRANSLATE_N_12_6B51: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_TEXELFETCH_T_D_IVEC2_S_INT_S_0DD0, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_INT2_S_S_INT2_INT_S_28_748E, A, A1, B1, B1), Bv); break;
        default: fail(c, SF2C_TRANSLATE_S_TLDS_INFO_D_723F, info); return false;
        }
    }
    *slot_out = slot;
    return true;
}

static void emit_texs(Ctx *c, uint64_t w, bool fetch) {
    int d0 = (int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62), d1 = (int)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_8_2C62), a = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62), b = (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62);
    uint32_t index = (uint32_t)bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_13_3FDB);
    int info = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_4_4B22);
    int msel = (int)bits(w, SF2C_TRANSLATE_N_50_1A65, SF2C_TRANSLATE_N_3_4E07);
    uint32_t mask;
    if (d1 == RZ) {
        static const uint32_t m2[] = {1, SF2C_TRANSLATE_N_2_D473, SF2C_TRANSLATE_N_4_4B22, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_9_1958, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_12_6B51};
        mask = m2[msel];
    } else {
        static const uint32_t m4[] = {SF2C_TRANSLATE_N_7_7902, SF2C_TRANSLATE_N_11_4FC8, SF2C_TRANSLATE_N_13_3FDB, SF2C_TRANSLATE_N_14_8527, SF2C_TRANSLATE_N_15_E629, SF2C_TRANSLATE_N_15_E629, SF2C_TRANSLATE_N_15_E629, SF2C_TRANSLATE_N_15_E629};
        mask = m4[msel];
    }
    char ra[SF2C_TRANSLATE_N_16_B17E], ra1[SF2C_TRANSLATE_N_16_B17E], rb[SF2C_TRANSLATE_N_16_B17E], rb1[SF2C_TRANSLATE_N_16_B17E];
    const char *A = reg(c, a, ra), *A1 = reg(c, a == RZ ? RZ : a + 1, ra1), *Bv = reg(c, b, rb),
               *B1 = reg(c, b == RZ ? RZ : b + 1, rb1);
    char call[SF2C_TRANSLATE_N_1024_E39E];
    int slot;
    if (c->glsl) {
        char gcall[SF2C_TRANSLATE_N_4096_8B92];
        if (!glsl_texs(c, w, fetch, &slot, gcall, A, A1, Bv, B1)) return;
        write_tex_result(c, slot, gcall, mask, d0, d1, true, !bits(w, SF2C_TRANSLATE_N_59_3E1E, 1));
        return;
    }
    if (!fetch) {
        switch (info) {
        case 0: slot = tex_slot(c, index, TEXTYPE_1D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_F4A5, slot, slot, F(A)); break;
        case 1: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_F4A5, slot, slot, UV2(slot, F(A), F(Bv))); break;
        case SF2C_TRANSLATE_N_2_D473: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_LEVEL_0_AE42, slot, slot, UV2(slot, F(A), F(Bv))); break;
        case SF2C_TRANSLATE_N_3_4E07: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_LEVEL_S_70D1, slot, slot, UV2(slot, F(A), F(A1)), F(Bv)); break;
        case SF2C_TRANSLATE_N_4_4B22: slot = tex_slot(c, index, TEXTYPE_2D, true, false);
            sprintf(call, SF2C_TRANSLATE_S_FLOAT4_T_D_SAMPLE_COMPARE_S__20E2, slot, slot, UV2(slot, F(A), F(A1)), F(Bv)); break;
        case SF2C_TRANSLATE_N_5_EF2D: slot = tex_slot(c, index, TEXTYPE_2D, true, false);
            sprintf(call, SF2C_TRANSLATE_S_FLOAT4_T_D_SAMPLE_COMPARE_S__841F, slot, slot, UV2(slot, F(A), F(A1)), F(B1), F(Bv)); break;
        case SF2C_TRANSLATE_N_6_E7F6: slot = tex_slot(c, index, TEXTYPE_2D, true, false);
            sprintf(call, SF2C_TRANSLATE_S_FLOAT4_T_D_SAMPLE_COMPARE_S__44A0, slot, slot, UV2(slot, F(A), F(A1)), F(Bv)); break;
        case SF2C_TRANSLATE_N_7_7902: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_S_3CB2, slot, slot, UV2(slot, F(A1), F(Bv)), ALAYER(A, slot)); break;
        case SF2C_TRANSLATE_N_8_2C62: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_S_LEVEL_0_E472, slot, slot, UV2(slot, F(A1), F(Bv)), ALAYER(A, slot)); break;
        case SF2C_TRANSLATE_N_9_1958: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, true, true);
            sprintf(call, SF2C_TRANSLATE_S_FLOAT4_T_D_SAMPLE_COMPARE_S__DF46, slot, slot, UV2(slot, F(A1), F(Bv)), A, F(B1)); break;
        case SF2C_TRANSLATE_N_10_4A44: slot = tex_slot(c, index, TEXTYPE_3D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_FLOAT3_S_S_S_62FC, slot, slot, F(A), F(A1), F(Bv)); break;
        case SF2C_TRANSLATE_N_11_4FC8: slot = tex_slot(c, index, TEXTYPE_3D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_FLOAT3_S_S_S__D259, slot, slot, F(A), F(A1), F(Bv)); break;
        case SF2C_TRANSLATE_N_12_6B51: slot = tex_slot(c, index, TEXTYPE_CUBE, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_FLOAT3_S_S_S_62FC, slot, slot, F(A), F(A1), F(Bv)); break;
        case SF2C_TRANSLATE_N_13_3FDB: slot = tex_slot(c, index, TEXTYPE_CUBE, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_FLOAT3_S_S_S__EAA4, slot, slot, F(A), F(A1), F(Bv), F(B1)); break;
        default: fail(c, SF2C_TRANSLATE_S_TEXS_INFO_D_B75C, info); return;
        }
    } else {
        switch (info) {
        case 0: slot = tex_slot(c, index, TEXTYPE_1D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_UINT_S_2C64, slot, A); break;
        case 1: slot = tex_slot(c, index, TEXTYPE_1D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_UINT_S_UINT_S_AB13, slot, A, Bv); break;
        case SF2C_TRANSLATE_N_2_D473: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_S_CCB6, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A, Bv)); break;
        case SF2C_TRANSLATE_N_4_4B22: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_S_CCB6, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_INT2_S_S_INT2_INT_S_28_748E, A, A1, Bv, Bv)); break;
        case SF2C_TRANSLATE_N_5_EF2D: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_S_UINT_S_9A7C, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A, A1), Bv); break;
        case SF2C_TRANSLATE_N_7_7902: slot = tex_slot(c, index, TEXTYPE_3D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_UINT3_S_S_S_0FC7, slot, A, A1, Bv); break;
        case SF2C_TRANSLATE_N_8_2C62: slot = tex_slot(c, index, TEXTYPE_2D_ARRAY, false, true);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_S_UINT_S_9A7C, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_S_S_5276, A1, Bv), A); break;
        case SF2C_TRANSLATE_N_12_6B51: slot = tex_slot(c, index, TEXTYPE_2D, false, false);
            sprintf(call, SF2C_TRANSLATE_S_T_D_READ_S_UINT_S_9A7C, slot, RSC2(c, slot, SF2C_TRANSLATE_S_UINT2_INT2_S_S_INT2_INT_S_28_748E, A, A1, B1, B1), Bv); break;
        default: fail(c, SF2C_TRANSLATE_S_TLDS_INFO_D_723F, info); return;
        }
    }
    write_tex_result(c, slot, call, mask, d0, d1, true, !bits(w, SF2C_TRANSLATE_N_59_3E1E, 1));  // 0xd0/0xd2: .F16 results
}

// TEX (full form): coordinates from consecutive registers at gpr8, extra parameters at gpr20.
static void emit_tex(Ctx *c, uint64_t w, bool bindless) {
    int d = (int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62), a = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62), b = (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62);
    uint32_t index = bindless ? 0 : (uint32_t)bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_13_3FDB);
    int target = (int)bits(w, SF2C_TRANSLATE_N_29_3513, SF2C_TRANSLATE_N_2_D473);
    bool array = bits(w, SF2C_TRANSLATE_N_28_59E1, 1), dc = bits(w, SF2C_TRANSLATE_N_50_1A65, 1), aoffi = bindless ? bits(w, SF2C_TRANSLATE_N_36_76A5, 1) : bits(w, SF2C_TRANSLATE_N_54_2FCA, 1);
    uint32_t mask = (uint32_t)bits(w, SF2C_TRANSLATE_N_31_EB1E, SF2C_TRANSLATE_N_4_4B22);
    int lod_mode = bindless ? (int)bits(w, SF2C_TRANSLATE_N_37_7A61, SF2C_TRANSLATE_N_3_4E07) : (int)bits(w, SF2C_TRANSLATE_N_55_02D2, SF2C_TRANSLATE_N_3_4E07);  // 0 none, 1 lz, 2 lb, 3 ll, 6 lba, 7 lla
    if (bindless) { fail(c, SF2C_TRANSLATE_S_BINDLESS_TEX_3302); return; }
    int type = target == 0 ? (array ? TEXTYPE_1D_ARRAY : TEXTYPE_1D)
             : target == 1 ? (array ? TEXTYPE_2D_ARRAY : TEXTYPE_2D)
             : target == SF2C_TRANSLATE_N_2_D473 ? TEXTYPE_3D
                           : (array ? TEXTYPE_CUBE_ARRAY : TEXTYPE_CUBE);
    int ncoord = target == 0 ? 1 : target == 1 ? SF2C_TRANSLATE_N_2_D473 : SF2C_TRANSLATE_N_3_4E07;
    int slot = tex_slot(c, index, type, dc, array);
    if (c->glsl) {  // same register layout as below
        char cs[SF2C_TRANSLATE_N_3_4E07][SF2C_TRANSLATE_N_40_D59E], lay[SF2C_TRANSLATE_N_96_7B1A], lodv[SF2C_TRANSLATE_N_48_9801], offv[SF2C_TRANSLATE_N_96_7B1A], drv[SF2C_TRANSLATE_N_48_9801], gcall[SF2C_TRANSLATE_N_4096_8B92];
        const char *co[SF2C_TRANSLATE_N_3_4E07], *layp = NULL, *lodp = NULL, *offp = NULL, *drp = NULL;
        int ar2 = a, p2 = b, lk = 0;
        if (array) { sprintf(lay, SF2C_TRANSLATE_S_MIN_UINT_R_D_UINT_TSIZE_D_0__E86B, a, slot); c->reg_used[a] = 1; ar2 = a + 1; layp = lay; }
        for (int i = 0; i < ncoord; i++) { sprintf(cs[i], SF2C_TRANSLATE_S_AS_TYPE_FLOAT_R_D_FB23, ar2 + i); c->reg_used[ar2 + i] = 1; co[i] = cs[i]; }
        if (lod_mode == SF2C_TRANSLATE_N_2_D473 || lod_mode == SF2C_TRANSLATE_N_3_4E07 || lod_mode == SF2C_TRANSLATE_N_6_E7F6 || lod_mode == SF2C_TRANSLATE_N_7_7902) {
            c->reg_used[p2] = 1;
            sprintf(lodv, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_R_D_FB23, p2);
            lodp = lodv;
            lk = lod_mode == SF2C_TRANSLATE_N_2_D473 || lod_mode == SF2C_TRANSLATE_N_6_E7F6 ? SF2C_TRANSLATE_N_2_D473 : 1;
            p2++;
        } else if (lod_mode == 1) {
            lk = 1;
            lodp = SF2C_TRANSLATE_S_0_0F_3781;
        }
        if (aoffi) { c->reg_used[p2] = 1; sprintf(offv, SF2C_TRANSLATE_S_INT2_INT_R_D_28_28_INT_R_D_2_284D, p2, p2); offp = offv; p2++; }
        if (dc) { c->reg_used[p2] = 1; sprintf(drv, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_R_D_FB23, p2); drp = drv; p2++; }
        glsl_sample(c, gcall, slot, co, layp, drp, lk, lodp, offp);
        write_tex_result(c, slot, gcall, mask, d, RZ, false, false);
        return;
    }
    char coords[SF2C_TRANSLATE_N_256_51E8] = SF2C_TRANSLATE_S_EMPTY_12AE, layer[SF2C_TRANSLATE_N_64_A68B] = SF2C_TRANSLATE_S_EMPTY_12AE, tmp[SF2C_TRANSLATE_N_64_A68B];
    int ar = a;
    // the hardware clamps the array index to the layer count
    if (array) { sprintf(layer, SF2C_TRANSLATE_S_MIN_UINT_R_D_T_D_GET_ARRAY_S_2C49, a, slot); c->reg_used[a] = 1; ar = a + 1; }
    for (int i = 0; i < ncoord; i++) {
        sprintf(tmp, SF2C_TRANSLATE_S_SAS_TYPE_FLOAT_R_D_E69A, i ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE, ar + i);
        c->reg_used[ar + i] = 1;
        strcat(coords, tmp);
    }
    int pr = b;
    char lod[SF2C_TRANSLATE_N_96_7B1A] = SF2C_TRANSLATE_S_EMPTY_12AE, dref[SF2C_TRANSLATE_N_64_A68B] = SF2C_TRANSLATE_S_EMPTY_12AE, off[SF2C_TRANSLATE_N_128_2747] = SF2C_TRANSLATE_S_EMPTY_12AE;
    if (lod_mode == SF2C_TRANSLATE_N_2_D473 || lod_mode == SF2C_TRANSLATE_N_3_4E07 || lod_mode == SF2C_TRANSLATE_N_6_E7F6 || lod_mode == SF2C_TRANSLATE_N_7_7902) {
        c->reg_used[pr] = 1;
        sprintf(lod, lod_mode == SF2C_TRANSLATE_N_2_D473 || lod_mode == SF2C_TRANSLATE_N_6_E7F6 ? SF2C_TRANSLATE_S_BIAS_AS_TYPE_FLOAT_R_D_BA65 : SF2C_TRANSLATE_S_LEVEL_AS_TYPE_FLOAT_R_D_F7A6, pr);
        pr++;
    } else if (lod_mode == 1) {
        strcpy(lod, SF2C_TRANSLATE_S_LEVEL_0_E147);
    }
    if (aoffi) {
        c->reg_used[pr] = 1;
        sprintf(off, SF2C_TRANSLATE_S_INT2_INT_R_D_28_28_INT_R_D_2_42BD, pr, pr);
        pr++;
    }
    if (dc) { c->reg_used[pr] = 1; sprintf(dref, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_R_D_FB23, pr); pr++; }
    const char *vec = ncoord == 1 ? SF2C_TRANSLATE_S_EMPTY_12AE : ncoord == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_FLOAT2_05A7 : SF2C_TRANSLATE_S_FLOAT3_5F8D;
    char cexpr[SF2C_TRANSLATE_N_420_DB55], call[SF2C_TRANSLATE_N_1024_E39E];
    if (ncoord == SF2C_TRANSLATE_N_2_D473 && c->stage != STAGE_VERTEX) { snprintf(cexpr, sizeof cexpr, SF2C_TRANSLATE_S_UVS_FLOAT2_S_RSCALE_D_2D7A, coords, slot + 1); c->uses_rscale = true; }
    else snprintf(cexpr, sizeof cexpr, SF2C_TRANSLATE_S_S_S_DC79, vec, coords);
    if (dc)
        sprintf(call, SF2C_TRANSLATE_S_FLOAT4_T_D_SAMPLE_COMPARE_S__6D7C, slot, slot, cexpr, array ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE,
                layer, dref, lod, off);
    else
        sprintf(call, SF2C_TRANSLATE_S_T_D_SAMPLE_S_D_S_S_S_S_S_2872, slot, slot, cexpr, array ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE, layer, lod, off);
    write_tex_result(c, slot, call, mask, d, RZ, false, false);
}

// ------------------------------------------------------------------ instruction translation
static void emit_insn(Ctx *c, uint64_t w, uint32_t pc) {
    char t[SF2C_TRANSLATE_N_2048_BFA0], u[SF2C_TRANSLATE_N_2048_BFA0];
    int d = (int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62), a = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62);
    int pr = (int)bits(w, SF2C_TRANSLATE_N_16_B17E, SF2C_TRANSLATE_N_3_4E07);
    bool prn = bits(w, SF2C_TRANSLATE_N_19_9400, 1);
    uint16_t op13 = (uint16_t)(w >> SF2C_TRANSLATE_N_51_031B);  // top 13 bits
    // predicate guard. SSY/PBK/PCNT/PRET take no predicate: those bits are zero in the encoding,
    // which would otherwise read as @P0.
    uint64_t top12 = w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D;
    if (top12 == SF2C_TRANSLATE_N_0XE290000000000000ULL_9928 || top12 == SF2C_TRANSLATE_N_0XE2A0000000000000ULL_101C || top12 == SF2C_TRANSLATE_N_0XE2B0000000000000ULL_4E82 ||
        top12 == SF2C_TRANSLATE_N_0XE270000000000000ULL_FF14) {
        pr = PT;
        prn = false;
    }
    bool guarded = !(pr == PT && !prn);
    if (pr == PT && prn) return;  // @!pt: never executes
    if (guarded) sp(&c->body, SF2C_TRANSLATE_S_IF_S_N_4BC5, P(pr, prn));

#define OPM(val, mask) ((w & (uint64_t)(mask)) == (uint64_t)(val))
    if (OPM(SF2C_TRANSLATE_N_0XE300000000000000ULL_BA38, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // EXIT
        sp(&c->body, c->branchy ? SF2C_TRANSLATE_S_EXIT_NOW_TRUE_BREAK_N_75A2 : SF2C_TRANSLATE_S_BREAK_N_EED1);
    } else if (OPM(SF2C_TRANSLATE_N_0XE330000000000000ULL_622B, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // KIL
        c->info->uses_discard = true;
        if (getenv(SF2C_TRANSLATE_S_SF2RECOMP_NO_DISCARD_5FB6)) {
        } else if (!c->glsl) sp(&c->body, SF2C_TRANSLATE_S_DISCARD_FRAGMENT_N_C2EB);
        else if (c->stage == STAGE_FRAGMENT) sp(&c->body, SF2C_TRANSLATE_S_DISCARD_N_A332);
        else sp(&c->body, SF2C_TRANSLATE_S_KIL_N_ADC3);  // GLSL has discard in fragment shaders only
    } else if (OPM(SF2C_TRANSLATE_N_0XE240000000000000ULL_BCB5, SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B)) {  // BRA
        uint32_t target = norm_target((int64_t)pc + SF2C_TRANSLATE_N_8_2C62 + sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235));
        c->uses_branches = true;
        sp(&c->body, SF2C_TRANSLATE_S_PC_UU_CONTINUE_N_28EF, target);
    } else if (OPM(SF2C_TRANSLATE_N_0XE290000000000000ULL_9928, SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B)) {  // SSY
        c->uses_branches = true;
        sp(&c->body, SF2C_TRANSLATE_S_SSY_SSY_N_UU_N_3FAC, norm_target((int64_t)pc + SF2C_TRANSLATE_N_8_2C62 + sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235)));
    } else if (OPM(SF2C_TRANSLATE_N_0XE2A0000000000000ULL_101C, SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B)) {  // PBK
        c->uses_branches = true;
        sp(&c->body, SF2C_TRANSLATE_S_PBK_PBK_N_UU_N_8975, norm_target((int64_t)pc + SF2C_TRANSLATE_N_8_2C62 + sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235)));
    } else if (OPM(SF2C_TRANSLATE_N_0XE2B0000000000000ULL_4E82, SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B)) {  // PCNT
        c->uses_branches = true;
        sp(&c->body, SF2C_TRANSLATE_S_PCNT_PCNT_N_UU_N_E966, norm_target((int64_t)pc + SF2C_TRANSLATE_N_8_2C62 + sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235)));
    } else if (OPM(SF2C_TRANSLATE_N_0XF0F8000000000000ULL_12C2, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // SYNC
        sp(&c->body, SF2C_TRANSLATE_S_PC_SSY_SSY_N_CONTINUE_N_E922);
    } else if (OPM(SF2C_TRANSLATE_N_0XE340000000000000ULL_9D8A, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // BRK
        sp(&c->body, SF2C_TRANSLATE_S_PC_PBK_PBK_N_CONTINUE_N_24D5);
    } else if (OPM(SF2C_TRANSLATE_N_0XE350000000000000ULL_C06F, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // CONT
        sp(&c->body, SF2C_TRANSLATE_S_PC_PCNT_PCNT_N_1_CONTINUE_N_6510);
    } else if (OPM(SF2C_TRANSLATE_N_0XF0F0000000000000ULL_8E3D, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X50B0000000000000ULL_49F5, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0XEF98000000000000ULL_91D3, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {
        // DEPBAR / NOP / MEMBAR
    } else if (OPM(SF2C_TRANSLATE_N_0XE00000000000FF00ULL_CA6D, SF2C_TRANSLATE_N_0XFF0000400000FF00ULL_0CCA) || OPM(SF2C_TRANSLATE_N_0XE000004000000000ULL_7917, SF2C_TRANSLATE_N_0XFF00004000000000ULL_E05D)) {
        // IPA: interpolate fragment input
        bool idx = bits(w, SF2C_TRANSLATE_N_38_AEA9, 1);
        uint32_t addr = (uint32_t)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_10_4A44);
        int mode = (int)bits(w, SF2C_TRANSLATE_N_54_2FCA, SF2C_TRANSLATE_N_2_D473);  // 0 pass, 1 multiply, 2 constant, 3 sc
        if (idx) { fail(c, SF2C_TRANSLATE_S_IPA_IDX_F921); }
        if (addr >= SF2C_TRANSLATE_N_0X80_D41C && addr < SF2C_TRANSLATE_N_0X280_5907) {
            int at = (addr - SF2C_TRANSLATE_N_0X80_D41C) >> SF2C_TRANSLATE_N_4_4B22;
            if (mode == SF2C_TRANSLATE_N_2_D473) c->flat_mask |= SF2C_TRANSLATE_N_1U_B8E6 << at;
        }
        c->ipa_mode = mode;
        c->ipa_mul = (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62);
        attr_load(c, d, addr, NULL);
        c->ipa_mode = -1;
        if (bits(w, SF2C_TRANSLATE_N_51_031B, 1) && d != RZ) sp(&c->body, SF2C_TRANSLATE_S_R_D_AS_TYPE_UINT_SATURATE_AS_9A41, d, d);
    } else if (OPM(SF2C_TRANSLATE_N_0XEFD8000000000000ULL_B113, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // LD attribute
        int n = 1 + (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473);
        uint32_t addr = (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_10_4A44);
        int ia = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62);
        if (ia != RZ) { fail(c, SF2C_TRANSLATE_S_INDEXED_LD_A_F8A8); }
        for (int i = 0; i < n; i++) attr_load(c, d + i, addr + SF2C_TRANSLATE_N_4_4B22 * i, NULL);
    } else if (OPM(SF2C_TRANSLATE_N_0XEFF0000000000000ULL_5B3E, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // ST attribute
        int n = 1 + (int)bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_2_D473);
        uint32_t addr = (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_10_4A44);
        for (int i = 0; i < n; i++) attr_store(c, d + i, addr + SF2C_TRANSLATE_N_4_4B22 * i);
    } else if (OPM(SF2C_TRANSLATE_N_0XEF90000000000000ULL_E5EB, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // LD c[idx][reg + imm]
        int idx = (int)bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_5_EF2D);
        int64_t off = (int64_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_16_B17E);  // unsigned (Ryujinx InstLdc.CbufOffset)
        int sz = (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07);
        int n = sz == SF2C_TRANSLATE_N_5_EF2D ? SF2C_TRANSLATE_N_2_D473 : 1;
        char rb[SF2C_TRANSLATE_N_16_B17E];
        const char *dyn = a == RZ ? NULL : reg(c, a, rb);
        // LDC.64 R4, c[i][R4 + x]: the address register is also a destination. Read it once before the writes, or
        // the second word comes from c[i][<first word> + x + 4] (fix from letsgo-recomp)
        bool overlap = dyn && n > 1 && a >= d && a < d + n;
        if (overlap) { sp(&c->body, SF2C_TRANSLATE_S_UINT_LDC_ADDR_S_N_FEA8, dyn); dyn = SF2C_TRANSLATE_S_LDC_ADDR_53BA; }
        for (int i = 0; i < n; i++) {
            char cb[SF2C_TRANSLATE_N_256_51E8];
            cbuf_ref(c, idx, off + SF2C_TRANSLATE_N_4_4B22 * i, dyn, cb);
            if (dyn) c->info->cbuf_size[idx] = SF2C_TRANSLATE_N_0X10000_4939;
            set_reg(c, d + i, cb);
        }
        if (overlap) sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0XD000000000000000ULL_6DA6, SF2C_TRANSLATE_N_0XF600000000000000ULL_3576)) {  // TEXS
        emit_texs(c, w, false);
    } else if (OPM(SF2C_TRANSLATE_N_0XD200000000000000ULL_84D2, SF2C_TRANSLATE_N_0XF600000000000000ULL_3576)) {  // TLDS
        emit_texs(c, w, true);
    } else if (OPM(SF2C_TRANSLATE_N_0XC038000000000000ULL_EA73, SF2C_TRANSLATE_N_0XFC38000000000000ULL_FA4B)) {  // TEX
        emit_tex(c, w, false);
    } else if (OPM(SF2C_TRANSLATE_N_0XDEB8000000000000ULL_03BC, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // TEX.B (bindless)
        emit_tex(c, w, true);
    } else if (OPM(SF2C_TRANSLATE_N_0XDF48000000000000ULL_3817, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // TXQ
        uint32_t index = (uint32_t)bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_13_3FDB);
        int qt = (int)bits(w, SF2C_TRANSLATE_N_22_785F, SF2C_TRANSLATE_N_6_E7F6);
        uint32_t mask = (uint32_t)bits(w, SF2C_TRANSLATE_N_31_EB1E, SF2C_TRANSLATE_N_4_4B22);
        int slot = tex_slot(c, index, -1, false, false);  // the type comes from a sample, if any
        if (qt == 1 && c->glsl) {  // tsize<slot>() is emitted with the slot's final type
            if (c->stage != STAGE_VERTEX) {
                c->uses_rscale = true;
                sp(&c->body, SF2C_TRANSLATE_S_UINT_LV_S_UVEC4_Q_UVEC4_UINT_C989
                   SF2C_TRANSLATE_S_UINT_FLOOR_FLOAT_TSIZE_D_INT_E652,
                   R(a), slot, slot + 1, slot + 1, slot, slot + 1, slot + 1, slot);
            } else {
                sp(&c->body, SF2C_TRANSLATE_S_UINT_LV_S_UVEC4_Q_UVEC4_UINT_2D7F,
                   R(a), slot, slot, slot);
            }
            int n = 0;
            for (int i = 0; i < SF2C_TRANSLATE_N_4_4B22; i++)
                if (mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) { c->reg_used[d + n] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_Q_D_N_D87D, d + n, i); n++; }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        } else if (qt == 1) {  // dimension
            if (c->stage != STAGE_VERTEX) {  // report the guest size of scaled allocations
                c->uses_rscale = true;
                sp(&c->body, SF2C_TRANSLATE_S_UINT_LV_S_UINT4_Q_UINT4_UINT_89D5
                   SF2C_TRANSLATE_S_UINT_ROUND_FLOAT_T_D_GET_HEI_2F67,
                   R(a), slot, slot + 1, slot + 1, slot, slot + 1, slot + 1, slot);
            } else {
                sp(&c->body, SF2C_TRANSLATE_S_UINT_LV_S_UINT4_Q_UINT4_T_D__31BF,
                   R(a), slot, slot, slot);
            }
            int n = 0;
            for (int i = 0; i < SF2C_TRANSLATE_N_4_4B22; i++)
                if (mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) { c->reg_used[d + n] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_Q_D_N_D87D, d + n, i); n++; }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        } else {
            fail(c, SF2C_TRANSLATE_S_TXQ_TYPE_D_21B2, qt);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5080000000000000ULL_62B2, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // MUFU
        int fn = (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_4_4B22);
        const char *x = NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_48_9801, 1), bits(w, SF2C_TRANSLATE_N_46_25FC, 1));
        switch (fn) {
        case 0: sprintf(t, SF2C_TRANSLATE_S_COS_S_6996, x); break;
        case 1: sprintf(t, SF2C_TRANSLATE_S_SIN_S_F2C1, x); break;
        case SF2C_TRANSLATE_N_2_D473: sprintf(t, SF2C_TRANSLATE_S_EXP2_S_D5A2, x); break;
        case SF2C_TRANSLATE_N_3_4E07: sprintf(t, SF2C_TRANSLATE_S_LOG2_S_CCAD, x); break;
        case SF2C_TRANSLATE_N_4_4B22: sprintf(t, SF2C_TRANSLATE_S_1_0F_S_F122, x); break;
        case SF2C_TRANSLATE_N_5_EF2D: sprintf(t, SF2C_TRANSLATE_S_RSQRT_S_7623, x); break;
        case SF2C_TRANSLATE_N_8_2C62: sprintf(t, SF2C_TRANSLATE_S_SQRT_S_2C7C, x); break;
        default: fail(c, SF2C_TRANSLATE_S_MUFU_D_51D6, fn); return;
        }
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_50_1A65, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X5C90000000000000ULL_2082, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C90000000000000ULL_4E9D, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3890000000000000ULL_E2F2, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // RRO: range reduction (no-op for us)
        const char *x = NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1));
        set_reg(c, d, FRES(x, false));
    } else if (OPM(SF2C_TRANSLATE_N_0X5C58000000000000ULL_F4E9, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C58000000000000ULL_20B6, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3858000000000000ULL_AB0D, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // FADD
        sprintf(t, SF2C_TRANSLATE_S_S_S_D630, NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_48_9801, 1), bits(w, SF2C_TRANSLATE_N_46_25FC, 1)), NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1)));
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_50_1A65, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X0800000000000000ULL_3345, SF2C_TRANSLATE_N_0XFC00000000000000ULL_7D02)) {  // FADD32I
        sprintf(u, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_0X_08XU_87EC, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        sprintf(t, SF2C_TRANSLATE_S_S_S_D630, NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_56_7688, 1), bits(w, SF2C_TRANSLATE_N_54_2FCA, 1)), NA(u, bits(w, SF2C_TRANSLATE_N_53_2858, 1), bits(w, SF2C_TRANSLATE_N_57_C837, 1)));
        set_reg(c, d, FRES(t, false));
    } else if (OPM(SF2C_TRANSLATE_N_0X5C68000000000000ULL_FEB6, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C68000000000000ULL_1B9F, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3868000000000000ULL_D8F6, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // FMUL
        static const char *const scale[] = {SF2C_TRANSLATE_S_EMPTY_12AE, SF2C_TRANSLATE_S_0_5F_2BF4, SF2C_TRANSLATE_S_0_25F_3204, SF2C_TRANSLATE_S_0_125F_CF67, SF2C_TRANSLATE_S_8_0F_2837, SF2C_TRANSLATE_S_4_0F_FA58, SF2C_TRANSLATE_S_2_0F_6354, SF2C_TRANSLATE_S_EMPTY_12AE};
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_5122, F(R(a)), NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_48_9801, 1), false), scale[bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_3_4E07)]);
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_50_1A65, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X1E00000000000000ULL_7895, SF2C_TRANSLATE_N_0XFF00000000000000ULL_323F)) {  // FMUL32I
        sprintf(t, SF2C_TRANSLATE_S_S_AS_TYPE_FLOAT_0X_08XU_C083, F(R(a)), (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_55_02D2, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X5980000000000000ULL_A7E0, SF2C_TRANSLATE_N_0XFF80000000000000ULL_60B3) || OPM(SF2C_TRANSLATE_N_0X4980000000000000ULL_C4EB, SF2C_TRANSLATE_N_0XFF80000000000000ULL_60B3) ||
               OPM(SF2C_TRANSLATE_N_0X5180000000000000ULL_A033, SF2C_TRANSLATE_N_0XFF80000000000000ULL_60B3) || OPM(SF2C_TRANSLATE_N_0X3280000000000000ULL_925B, SF2C_TRANSLATE_N_0XFE80000000000000ULL_1CA3)) {  // FFMA
        const char *bv;
        char cbuf_c[SF2C_TRANSLATE_N_256_51E8], bbuf[SF2C_TRANSLATE_N_256_51E8];
        const char *cv = operand_c_for_ffma(c, w, cbuf_c, &bv, bbuf);
        sprintf(t, SF2C_TRANSLATE_S_FMA_S_S_S_85B2, F(R(a)), NA(F(bv), bits(w, SF2C_TRANSLATE_N_48_9801, 1), false), NA(F(cv), bits(w, SF2C_TRANSLATE_N_49_0E17, 1), false));
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_50_1A65, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X0C00000000000000ULL_B03D, SF2C_TRANSLATE_N_0XFC00000000000000ULL_7D02)) {  // FFMA32I
        sprintf(u, SF2C_TRANSLATE_S_AS_TYPE_FLOAT_0X_08XU_87EC, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        sprintf(t, SF2C_TRANSLATE_S_FMA_S_S_S_85B2, NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_56_7688, 1), false), u, NA(F(R(d)), bits(w, SF2C_TRANSLATE_N_57_C837, 1), false));
        set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_55_02D2, 1)));
    } else if (OPM(SF2C_TRANSLATE_N_0X5C60000000000000ULL_BE39, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C60000000000000ULL_0C42, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3860000000000000ULL_E954, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // FMNMX
        const char *x = NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_48_9801, 1), bits(w, SF2C_TRANSLATE_N_46_25FC, 1)), *y = NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1));
        sprintf(t, SF2C_TRANSLATE_S_S_FMIN_S_S_FMAX_S_S_9DB9, P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1)), x, y, x, y);
        set_reg(c, d, FRES(t, false));
    } else if (OPM(SF2C_TRANSLATE_N_0X5BB0000000000000ULL_CE18, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4BB0000000000000ULL_B8D0, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X36B0000000000000ULL_B41E, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292)) {  // FSETP
        const char *x = NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_43_44CB, 1), bits(w, SF2C_TRANSLATE_N_7_7902, 1)), *y = NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_6_E7F6, 1), bits(w, SF2C_TRANSLATE_N_44_71EE, 1));
        char cmp[SF2C_TRANSLATE_N_512_94F8];
        fcmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_4_4B22), x, y);
        int p = (int)bits(w, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_3_4E07), q = (int)bits(w, 0, SF2C_TRANSLATE_N_3_4E07), bo = (int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473);
        const char *pc_ = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        if (p != PT) { c->pred_used[p] = 1; sp(&c->body, SF2C_TRANSLATE_S_BOOL_CMP_S_BOOL_PP_S_N_P_D_C_A4A9, cmp, pc_, p, bool_op(bo)); }
        else sp(&c->body, SF2C_TRANSLATE_S_BOOL_CMP_S_BOOL_PP_S_N_7DC5, cmp, pc_);
        if (q != PT) { c->pred_used[q] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_CMP_S_PP_N_0DD5, q, bool_op(bo)); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5800000000000000ULL_145D, SF2C_TRANSLATE_N_0XFF00000000000000ULL_323F) || OPM(SF2C_TRANSLATE_N_0X4800000000000000ULL_5C9C, SF2C_TRANSLATE_N_0XFE00000000000000ULL_F24F) ||
               OPM(SF2C_TRANSLATE_N_0X3000000000000000ULL_84BB, SF2C_TRANSLATE_N_0XFE00000000000000ULL_F24F)) {  // FSET
        const char *x = NA(F(R(a)), bits(w, SF2C_TRANSLATE_N_43_44CB, 1), bits(w, SF2C_TRANSLATE_N_54_2FCA, 1)), *y = NA(F(B(true)), bits(w, SF2C_TRANSLATE_N_53_2858, 1), bits(w, SF2C_TRANSLATE_N_44_71EE, 1));
        char cmp[SF2C_TRANSLATE_N_512_94F8];
        fcmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_4_4B22), x, y);
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_7431, cmp, bool_op((int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473)), P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1)));
        sprintf(u, bits(w, SF2C_TRANSLATE_N_52_41CF, 1) ? SF2C_TRANSLATE_S_S_0X3F800000U_0U_B6E9 : SF2C_TRANSLATE_S_S_0XFFFFFFFFU_0U_2807, t);
        set_reg(c, d, u);
    } else if (OPM(SF2C_TRANSLATE_N_0X5BA0000000000000ULL_1399, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4BA0000000000000ULL_A1C3, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X36A0000000000000ULL_6B9F, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292) || OPM(SF2C_TRANSLATE_N_0X53A0000000000000ULL_E51D, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // FCMP
        char cmp[SF2C_TRANSLATE_N_512_94F8], cb[SF2C_TRANSLATE_N_256_51E8], bb[SF2C_TRANSLATE_N_256_51E8];
        const char *bv, *cv;
        if (OPM(SF2C_TRANSLATE_N_0X53A0000000000000ULL_E51D, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {
            bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
        } else {
            bv = operand_b(c, w, true, bb);
            cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
        }
        fcmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_4_4B22), F(cv), SF2C_TRANSLATE_S_0_0F_3781);
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_D317, cmp, R(a), bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C98000000000000ULL_5BDD, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C98000000000000ULL_C0E6, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3898000000000000ULL_2E3D, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // MOV
        set_reg(c, d, B(false));
    } else if (OPM(SF2C_TRANSLATE_N_0X0100000000000000ULL_7325, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // MOV32I
        sprintf(t, SF2C_TRANSLATE_S_0X_08XU_FED9, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CA0000000000000ULL_EF7F, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4CA0000000000000ULL_FDBE, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X38A0000000000000ULL_1C0A, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // SEL
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_D317, P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1)), R(a), B(false));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CB0000000000000ULL_D087, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4CB0000000000000ULL_DC8B, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X38B0000000000000ULL_36D4, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // F2I (src fmt bits 10-11: 1 F16; dst size bits 8-9)
        char srcv[SF2C_TRANSLATE_N_512_94F8];
        if (bits(w, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_2_D473) == 1 && c->glsl) sprintf(srcv, SF2C_TRANSLATE_S_H2F_S_DU_0XFFFFU_F640, B(false), SF2C_TRANSLATE_N_16_B17E * (int)bits(w, SF2C_TRANSLATE_N_41_3D91, 1));
        else if (bits(w, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_2_D473) == 1) sprintf(srcv, SF2C_TRANSLATE_S_FLOAT_AS_TYPE_HALF2_S_D_66E2, B(false), (int)bits(w, SF2C_TRANSLATE_N_41_3D91, 1));
        else strcpy(srcv, F(B(true)));
        const char *x = NA(srcv, bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1));
        int rnd = (int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_2_D473);
        bool sgn = bits(w, SF2C_TRANSLATE_N_12_6B51, 1), i16 = bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_2_D473) == 1;
        static const char *const rf[] = {SF2C_TRANSLATE_S_RINT_E6CF, SF2C_TRANSLATE_S_FLOOR_2CEF, SF2C_TRANSLATE_S_CEIL_B752, SF2C_TRANSLATE_S_TRUNC_DBDF};
        if (i16)
            sprintf(t, sgn ? SF2C_TRANSLATE_S_UINT_INT_CLAMP_S_S_32768_0F__D722 : SF2C_TRANSLATE_S_UINT_CLAMP_S_S_0_0F_65535_0F_BDB2, rf[rnd], x);
        else
            sprintf(t, sgn ? SF2C_TRANSLATE_S_UINT_INT_CLAMP_S_S_214748364_A2D2 : SF2C_TRANSLATE_S_UINT_CLAMP_S_S_0_0F_42949670_7AD8, rf[rnd], x);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CB8000000000000ULL_0DFB, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4CB8000000000000ULL_3061, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X38B8000000000000ULL_5172, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // I2F
        bool sgn = bits(w, SF2C_TRANSLATE_N_13_3FDB, 1);
        int sz = (int)bits(w, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_2_D473), sel = (int)bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_2_D473);
        char src[SF2C_TRANSLATE_N_256_51E8];
        const char *bv = B(false);
        if (sz == 0) sprintf(src, sgn ? SF2C_TRANSLATE_S_INT_CHAR_S_D_0XFF_D3A0 : SF2C_TRANSLATE_S_S_D_0XFFU_9A54, bv, sel * SF2C_TRANSLATE_N_8_2C62);
        else if (sz == 1) sprintf(src, sgn ? SF2C_TRANSLATE_S_INT_SHORT_S_D_0XFFFF_E382 : SF2C_TRANSLATE_S_S_D_0XFFFFU_35EF, bv, sel * SF2C_TRANSLATE_N_8_2C62);
        else sprintf(src, sgn ? SF2C_TRANSLATE_S_INT_S_C689 : SF2C_TRANSLATE_S_S_0030, bv);
        sprintf(u, SF2C_TRANSLATE_S_FLOAT_S_6576, src);
        if (bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_2_D473) == 1) {  // F16 result in the low half
            char h[SF2C_TRANSLATE_N_700_99EE];
            snprintf(h, sizeof h, c->glsl ? SF2C_TRANSLATE_S_PACKHALF2X16_VEC2_S_0_0F_C508 : SF2C_TRANSLATE_S_UINT_AS_TYPE_USHORT_HALF_S_8A48,
                     NA(u, bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1)));
            set_reg(c, d, h);
        } else {
            set_reg(c, d, FRES(NA(u, bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1)), false));
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5CA8000000000000ULL_ECB5, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4CA8000000000000ULL_5412, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X38A8000000000000ULL_A05D, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // F2F (formats: 1 F16, 2 F32; bit 41 = high half)
        int dfmt = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_2_D473), sfmt = (int)bits(w, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_2_D473);
        char srcv[SF2C_TRANSLATE_N_512_94F8];
        if (sfmt == 1 && c->glsl) sprintf(srcv, SF2C_TRANSLATE_S_H2F_S_DU_0XFFFFU_F640, B(false), SF2C_TRANSLATE_N_16_B17E * (int)bits(w, SF2C_TRANSLATE_N_41_3D91, 1));
        else if (sfmt == 1) sprintf(srcv, SF2C_TRANSLATE_S_FLOAT_AS_TYPE_HALF2_S_D_66E2, B(false), (int)bits(w, SF2C_TRANSLATE_N_41_3D91, 1));
        else strcpy(srcv, F(B(true)));
        const char *x = NA(srcv, bits(w, SF2C_TRANSLATE_N_45_8117, 1), bits(w, SF2C_TRANSLATE_N_49_0E17, 1));
        int mode = (int)((bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_2_D473)) | (bits(w, SF2C_TRANSLATE_N_42_7347, 1) << SF2C_TRANSLATE_N_2_D473));
        switch (mode) {
        case SF2C_TRANSLATE_N_4_4B22: sprintf(t, SF2C_TRANSLATE_S_RINT_S_54F2, x); break;
        case SF2C_TRANSLATE_N_5_EF2D: sprintf(t, SF2C_TRANSLATE_S_FLOOR_S_BE4E, x); break;
        case SF2C_TRANSLATE_N_6_E7F6: sprintf(t, SF2C_TRANSLATE_S_CEIL_S_1105, x); break;
        case SF2C_TRANSLATE_N_7_7902: sprintf(t, SF2C_TRANSLATE_S_TRUNC_S_A6D8, x); break;
        default: sprintf(t, SF2C_TRANSLATE_S_S_0030, x);
        }
        if (dfmt == 1) {  // F16 result in the low half, high half zero
            char h[SF2C_TRANSLATE_N_1100_AB98];
            snprintf(h, sizeof h, c->glsl ? SF2C_TRANSLATE_S_PACKHALF2X16_VEC2_S_0_0F_C508 : SF2C_TRANSLATE_S_UINT_AS_TYPE_USHORT_HALF_S_8A48,
                     bits(w, SF2C_TRANSLATE_N_50_1A65, 1) ? (sprintf(u, SF2C_TRANSLATE_S_SATURATE_S_5446, t), u) : t);
            set_reg(c, d, h);
        } else {
            set_reg(c, d, FRES(t, bits(w, SF2C_TRANSLATE_N_50_1A65, 1)));
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5C10000000000000ULL_571D, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C10000000000000ULL_83FF, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3810000000000000ULL_4D52, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // IADD
        if (bits(w, SF2C_TRANSLATE_N_43_44CB, 1) || bits(w, SF2C_TRANSLATE_N_47_3148, 1)) c->uses_cc = true;
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_S_S_56F2, bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a), bits(w, SF2C_TRANSLATE_N_48_9801, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, B(false),
                bits(w, SF2C_TRANSLATE_N_43_44CB, 1) ? SF2C_TRANSLATE_S_CC_615E : SF2C_TRANSLATE_S_EMPTY_12AE);
        if (bits(w, SF2C_TRANSLATE_N_47_3148, 1)) {
            sp(&c->body, SF2C_TRANSLATE_S_UINT_S_S_S_UINT_T2_S_S_UINT__57D3, bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE,
               R(a), bits(w, SF2C_TRANSLATE_N_48_9801, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, B(false), bits(w, SF2C_TRANSLATE_N_43_44CB, 1) ? SF2C_TRANSLATE_S_CC_615E : SF2C_TRANSLATE_S_EMPTY_12AE);
            if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_RES_N_70D0, d); }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        } else {
            set_reg(c, d, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X1C00000000000000ULL_1A97, SF2C_TRANSLATE_N_0XFE00000000000000ULL_F24F)) {  // IADD32I (CC bit 52, X bit 53)
        bool wcc = bits(w, SF2C_TRANSLATE_N_52_41CF, 1), x = bits(w, SF2C_TRANSLATE_N_53_2858, 1);
        if (wcc || x) c->uses_cc = true;
        sp(&c->body, SF2C_TRANSLATE_S_ULONG_SUM_ULONG_S_S_ULONG_0X_97B3, bits(w, SF2C_TRANSLATE_N_56_7688, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a), (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C),
           x ? SF2C_TRANSLATE_S_ULONG_CC_88BC : SF2C_TRANSLATE_S_EMPTY_12AE, wcc ? SF2C_TRANSLATE_S_CC_UINT_SUM_32_1U_9CBC : SF2C_TRANSLATE_S_EMPTY_12AE);
        if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_UINT_SUM_N_2121, d); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CC0000000000000ULL_CF3A, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4CC0000000000000ULL_C934, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X38C0000000000000ULL_CE6D, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292)) {  // IADD3
        bool wcc3 = bits(w, SF2C_TRANSLATE_N_47_3148, 1), x3 = bits(w, SF2C_TRANSLATE_N_48_9801, 1);
        if (wcc3 || x3) {  // carry in/out (64-bit address math)
            c->uses_cc = true;
            sp(&c->body, SF2C_TRANSLATE_S_ULONG_SUM_ULONG_S_S_ULONG_S__15C0, bits(w, SF2C_TRANSLATE_N_51_031B, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a),
               bits(w, SF2C_TRANSLATE_N_50_1A65, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, B(false), bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)), x3 ? SF2C_TRANSLATE_S_ULONG_CC_88BC : SF2C_TRANSLATE_S_EMPTY_12AE);
            if (wcc3) sp(&c->body, SF2C_TRANSLATE_S_CC_UINT_SUM_32_1U_N_0CB1);
            if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_UINT_SUM_N_2121, d); }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        } else {
            sprintf(t, SF2C_TRANSLATE_S_S_S_S_S_S_S_ADE8, bits(w, SF2C_TRANSLATE_N_51_031B, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a), bits(w, SF2C_TRANSLATE_N_50_1A65, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, B(false),
                    bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)));
            set_reg(c, d, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5C18000000000000ULL_CC85, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C18000000000000ULL_873E, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3818000000000000ULL_357A, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // ISCADD
        sprintf(t, SF2C_TRANSLATE_S_S_S_DU_S_S_06F5, bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a), (int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_48_9801, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE,
                B(false));
        if (bits(w, SF2C_TRANSLATE_N_47_3148, 1)) {  // .CC: carry out for a following IADD.X (64-bit address math)
            c->uses_cc = true;
            sp(&c->body, SF2C_TRANSLATE_S_UINT_S_S_S_DU_UINT_T2_S_S_UI_82FA,
               bits(w, SF2C_TRANSLATE_N_49_0E17, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, R(a), (int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_48_9801, 1) ? SF2C_TRANSLATE_S_EMPTY_0933 : SF2C_TRANSLATE_S_EMPTY_12AE, B(false));
            if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_RES_N_70D0, d); }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
        } else {
            set_reg(c, d, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X1400000000000000ULL_C21C, SF2C_TRANSLATE_N_0XFC00000000000000ULL_7D02)) {  // ISCADD32I
        sprintf(t, SF2C_TRANSLATE_S_S_DU_0X_08XU_0B4B, R(a), (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_5_EF2D), (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5B60000000000000ULL_7F3D, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4B60000000000000ULL_69E0, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X3660000000000000ULL_4F58, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292)) {  // ISETP
        char cmp[SF2C_TRANSLATE_N_512_94F8];
        icmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_3_4E07), R(a), B(false), bits(w, SF2C_TRANSLATE_N_48_9801, 1));
        int p = (int)bits(w, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_3_4E07), q = (int)bits(w, 0, SF2C_TRANSLATE_N_3_4E07), bo = (int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473);
        const char *pp = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        sp(&c->body, SF2C_TRANSLATE_S_BOOL_CMP_S_BOOL_PP_S_N_7DC5, cmp, pp);
        if (p != PT) { c->pred_used[p] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_CMP_S_PP_N_78C5, p, bool_op(bo)); }
        if (q != PT) { c->pred_used[q] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_CMP_S_PP_N_0DD5, q, bool_op(bo)); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5B50000000000000ULL_9B22, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4B50000000000000ULL_E413, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X3650000000000000ULL_2494, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292)) {  // ISET
        char cmp[SF2C_TRANSLATE_N_512_94F8];
        icmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_3_4E07), R(a), B(false), bits(w, SF2C_TRANSLATE_N_48_9801, 1));
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_7431, cmp, bool_op((int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473)), P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1)));
        sprintf(u, bits(w, SF2C_TRANSLATE_N_44_71EE, 1) ? SF2C_TRANSLATE_S_S_0X3F800000U_0U_B6E9 : SF2C_TRANSLATE_S_S_0XFFFFFFFFU_0U_2807, t);
        set_reg(c, d, u);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C20000000000000ULL_E137, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C20000000000000ULL_D1D1, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3820000000000000ULL_44DA, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // IMNMX
        bool sgn = bits(w, SF2C_TRANSLATE_N_48_9801, 1);
        const char *x = R(a), *y = B(false), *p = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        if (sgn) sprintf(t, SF2C_TRANSLATE_S_UINT_S_MIN_INT_S_INT_S_MAX_I_EC59, p, x, y, x, y);
        else sprintf(t, SF2C_TRANSLATE_S_S_MIN_S_S_MAX_S_S_89DB, p, x, y, x, y);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C48000000000000ULL_DEC4, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C48000000000000ULL_666A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3848000000000000ULL_BCC6, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // SHL
        const char *bv = B(false);
        if (bits(w, SF2C_TRANSLATE_N_39_0B91, 1)) sprintf(t, SF2C_TRANSLATE_S_S_S_31U_C933, R(a), bv);
        else sprintf(t, SF2C_TRANSLATE_S_S_32U_0U_S_S_E4D2, bv, R(a), bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C28000000000000ULL_E267, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C28000000000000ULL_66C5, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3828000000000000ULL_16E0, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // SHR
        const char *bv = B(false);
        bool sgn = bits(w, SF2C_TRANSLATE_N_48_9801, 1);
        if (sgn) sprintf(t, SF2C_TRANSLATE_S_UINT_INT_S_MIN_S_31U_F727, R(a), bv);
        else sprintf(t, SF2C_TRANSLATE_S_S_32U_0U_S_S_97D4, bv, R(a), bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C40000000000000ULL_21A7, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C40000000000000ULL_D98F, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3840000000000000ULL_E13B, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // LOP
        char x[SF2C_TRANSLATE_N_64_A68B], y[SF2C_TRANSLATE_N_256_51E8];
        sprintf(x, bits(w, SF2C_TRANSLATE_N_39_0B91, 1) ? SF2C_TRANSLATE_S_S_BA74 : SF2C_TRANSLATE_S_S_0030, R(a));
        sprintf(y, bits(w, SF2C_TRANSLATE_N_40_D59E, 1) ? SF2C_TRANSLATE_S_S_BA74 : SF2C_TRANSLATE_S_S_0030, B(false));
        static const char *const ops[] = {SF2C_TRANSLATE_S_EMPTY_7F62, SF2C_TRANSLATE_S_EMPTY_72A2, SF2C_TRANSLATE_S_EMPTY_06AC, SF2C_TRANSLATE_S_EMPTY_12AE};
        int op = (int)bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_2_D473);
        if (op == SF2C_TRANSLATE_N_3_4E07) sprintf(t, SF2C_TRANSLATE_S_S_0030, y);
        else sprintf(t, SF2C_TRANSLATE_S_S_S_S_A123, x, ops[op], y);
        int pd = (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07);
        if (pd != PT) {
            c->pred_used[pd] = 1;
            int pop = (int)bits(w, SF2C_TRANSLATE_N_44_71EE, SF2C_TRANSLATE_N_2_D473);
            sp(&c->body, SF2C_TRANSLATE_S_UINT_RES_S_N_E092, t);
            if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_RES_N_70D0, d); }
            sp(&c->body, SF2C_TRANSLATE_S_P_D_S_N_AFC1, pd, pop == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_RES_0U_D2C9 : pop == SF2C_TRANSLATE_N_3_4E07 ? SF2C_TRANSLATE_S_RES_0U_B4FA : pop == 1 ? SF2C_TRANSLATE_S_TRUE_18D1 : SF2C_TRANSLATE_S_FALSE_076D);
        } else {
            set_reg(c, d, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X0400000000000000ULL_6B94, SF2C_TRANSLATE_N_0XFC00000000000000ULL_7D02)) {  // LOP32I
        char x[SF2C_TRANSLATE_N_64_A68B], y[SF2C_TRANSLATE_N_64_A68B];
        sprintf(x, bits(w, SF2C_TRANSLATE_N_55_02D2, 1) ? SF2C_TRANSLATE_S_S_BA74 : SF2C_TRANSLATE_S_S_0030, R(a));
        sprintf(y, bits(w, SF2C_TRANSLATE_N_56_7688, 1) ? SF2C_TRANSLATE_S_0X_08XU_AD8D : SF2C_TRANSLATE_S_0X_08XU_FED9, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        static const char *const ops[] = {SF2C_TRANSLATE_S_EMPTY_7F62, SF2C_TRANSLATE_S_EMPTY_72A2, SF2C_TRANSLATE_S_EMPTY_06AC, SF2C_TRANSLATE_S_EMPTY_12AE};
        int op = (int)bits(w, SF2C_TRANSLATE_N_53_2858, SF2C_TRANSLATE_N_2_D473);
        if (op == SF2C_TRANSLATE_N_3_4E07) sprintf(t, SF2C_TRANSLATE_S_S_0030, y);
        else sprintf(t, SF2C_TRANSLATE_S_S_S_S_A123, x, ops[op], y);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5BE0000000000000ULL_3EB5, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X3C00000000000000ULL_CE63, SF2C_TRANSLATE_N_0XFC00000000000000ULL_7D02) ||
               OPM(SF2C_TRANSLATE_N_0X0200000000000000ULL_2B53, SF2C_TRANSLATE_N_0XFE00000000000000ULL_F24F)) {  // LOP3
        uint32_t lut;
        const char *bv, *cv;
        char bb[SF2C_TRANSLATE_N_256_51E8];
        if ((w >> SF2C_TRANSLATE_N_56_7688) == SF2C_TRANSLATE_N_0X5B_5E87) { lut = (uint32_t)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_8_2C62); bv = R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62)); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); }
        else if ((w >> SF2C_TRANSLATE_N_58_6208) == SF2C_TRANSLATE_N_0XF_93CC) { lut = (uint32_t)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_8_2C62); sprintf(bb, SF2C_TRANSLATE_S_0X_08XU_FED9, (uint32_t)((uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_19_9400) | ((uint32_t)bits(w, SF2C_TRANSLATE_N_56_7688, 1) << SF2C_TRANSLATE_N_19_9400) | (bits(w, SF2C_TRANSLATE_N_56_7688, 1) ? SF2C_TRANSLATE_N_0XFFF00000U_B916 : 0))); bv = bb; cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); }
        else { lut = (uint32_t)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_8_2C62); bv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, bb); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); }
        char e[SF2C_TRANSLATE_N_1024_E39E] = SF2C_TRANSLATE_S_0U_B642;
        // sum of minterms
        int first = 1;
        for (int i = 0; i < SF2C_TRANSLATE_N_8_2C62; i++) {
            if (!(lut & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
            char term[SF2C_TRANSLATE_N_256_51E8];
            sprintf(term, SF2C_TRANSLATE_S_S_S_S_S_S_S_80D7, (i & SF2C_TRANSLATE_N_4_4B22) ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_EMPTY_16EB, R(a), (i & SF2C_TRANSLATE_N_2_D473) ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_EMPTY_16EB, bv, (i & 1) ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_EMPTY_16EB, cv);
            if (first) { strcpy(e, term); first = 0; }
            else { strcat(e, SF2C_TRANSLATE_S_EMPTY_658F); strcat(e, term); }
        }
        sprintf(t, SF2C_TRANSLATE_S_S_D5A7, e);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C00000000000000ULL_4D7C, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C00000000000000ULL_0816, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3800000000000000ULL_907B, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // BFE
        const char *bv = B(false);
        bool sgn = bits(w, SF2C_TRANSLATE_N_48_9801, 1);
        if (bits(w, SF2C_TRANSLATE_N_40_D59E, 1)) fail(c, SF2C_TRANSLATE_S_BFE_BREV_AC58);
        sprintf(t, sgn ? SF2C_TRANSLATE_S_BFE_S_S_S_C8E1 : SF2C_TRANSLATE_S_BFE_U_S_S_D3CB, R(a), bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5BF0000000000000ULL_39DB, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4BF0000000000000ULL_553D, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X36F0000000000000ULL_1E17, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217) || OPM(SF2C_TRANSLATE_N_0X53F0000000000000ULL_683F, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // BFI
        const char *bv, *cv;
        char cb[SF2C_TRANSLATE_N_256_51E8];
        if (OPM(SF2C_TRANSLATE_N_0X53F0000000000000ULL_683F, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {
            bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
            sprintf(t, SF2C_TRANSLATE_S_BFI_S_S_S_44EF, R(a), cv, bv);  // insert a into base (cbuf) with b = position/length
            // bfi with cbuf form: B = REG_39 as position/length, C = cbuf as base
            sprintf(t, SF2C_TRANSLATE_S_BFI_S_S_S_44EF, R(a), bv, cv);
        } else {
            bv = B(false); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            sprintf(t, SF2C_TRANSLATE_S_BFI_S_S_S_44EF, R(a), bv, cv);
        }
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5B00000000000000ULL_5DDE, SF2C_TRANSLATE_N_0XFFC0000000000000ULL_6E68) || OPM(SF2C_TRANSLATE_N_0X4E00000000000000ULL_3F4B, SF2C_TRANSLATE_N_0XFE00000000000000ULL_F24F) ||
               OPM(SF2C_TRANSLATE_N_0X3600000000000000ULL_1713, SF2C_TRANSLATE_N_0XFEC0000000000000ULL_1128) || OPM(SF2C_TRANSLATE_N_0X5100000000000000ULL_908E, SF2C_TRANSLATE_N_0XFF80000000000000ULL_60B3)) {  // XMAD
        // per Ryujinx's InstXmad* decoders and EmitXmad
        const char *bv, *cv;
        char cb[SF2C_TRANSLATE_N_256_51E8], bb[SF2C_TRANSLATE_N_256_51E8];
        bool hb, psl, mrg, ext, ha = bits(w, SF2C_TRANSLATE_N_53_2858, 1);
        int cmode;
        uint64_t top = w >> SF2C_TRANSLATE_N_56_7688;
        if (top == SF2C_TRANSLATE_N_0X5B_5E87) {  // reg, reg
            bv = R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62)); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            hb = bits(w, SF2C_TRANSLATE_N_35_9F14, 1); psl = bits(w, SF2C_TRANSLATE_N_36_76A5, 1); mrg = bits(w, SF2C_TRANSLATE_N_37_7A61, 1); ext = bits(w, SF2C_TRANSLATE_N_38_AEA9, 1); cmode = (int)bits(w, SF2C_TRANSLATE_N_50_1A65, SF2C_TRANSLATE_N_3_4E07);
        } else if ((top & SF2C_TRANSLATE_N_0XFE_C772) == SF2C_TRANSLATE_N_0X4E_2C8D) {  // cbuf B
            bv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            hb = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); psl = bits(w, SF2C_TRANSLATE_N_55_02D2, 1); mrg = bits(w, SF2C_TRANSLATE_N_56_7688, 1); ext = bits(w, SF2C_TRANSLATE_N_54_2FCA, 1); cmode = (int)bits(w, SF2C_TRANSLATE_N_50_1A65, SF2C_TRANSLATE_N_2_D473);
        } else if ((top & SF2C_TRANSLATE_N_0XFE_C772) == SF2C_TRANSLATE_N_0X36_BBAE) {  // imm16 B
            sprintf(bb, SF2C_TRANSLATE_S_0X_XU_F48D, (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_16_B17E)); bv = bb; cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            hb = false; psl = bits(w, SF2C_TRANSLATE_N_36_76A5, 1); mrg = bits(w, SF2C_TRANSLATE_N_37_7A61, 1); ext = bits(w, SF2C_TRANSLATE_N_38_AEA9, 1); cmode = (int)bits(w, SF2C_TRANSLATE_N_50_1A65, SF2C_TRANSLATE_N_3_4E07);
        } else {  // 0x51: B = reg39, C = cbuf
            bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
            hb = bits(w, SF2C_TRANSLATE_N_52_41CF, 1); psl = false; mrg = false; ext = bits(w, SF2C_TRANSLATE_N_54_2FCA, 1); cmode = (int)bits(w, SF2C_TRANSLATE_N_50_1A65, SF2C_TRANSLATE_N_2_D473);
        }
        bool sa = bits(w, SF2C_TRANSLATE_N_48_9801, 1), sb = bits(w, SF2C_TRANSLATE_N_49_0E17, 1), wcc = bits(w, SF2C_TRANSLATE_N_47_3148, 1);
        if (ext || wcc) c->uses_cc = true;
        sp(&c->body, SF2C_TRANSLATE_S_UINT_XA_S_D_0XFFFFU_UINT_XB__0FF6, R(a), ha ? SF2C_TRANSLATE_N_16_B17E : 0, bv, hb ? SF2C_TRANSLATE_N_16_B17E : 0, bv);
        if (sa) sp(&c->body, SF2C_TRANSLATE_S_XA_UINT_INT_SHORT_XA_N_BA2A);
        if (sb) sp(&c->body, SF2C_TRANSLATE_S_XB_UINT_INT_SHORT_XB_N_9293);
        sp(&c->body, SF2C_TRANSLATE_S_UINT_PROD_XA_XB_S_N_0A91, psl ? SF2C_TRANSLATE_S_PROD_16_4B63 : SF2C_TRANSLATE_S_EMPTY_12AE);
        switch (cmode) {
        case 1: sp(&c->body, SF2C_TRANSLATE_S_UINT_CC_S_0XFFFFU_N_544E, cv); break;                                  // clo
        case SF2C_TRANSLATE_N_2_D473: sp(&c->body, SF2C_TRANSLATE_S_UINT_CC_S_16_N_8E3E, cv); break;                                      // chi
        case SF2C_TRANSLATE_N_3_4E07: sp(&c->body, SF2C_TRANSLATE_S_UINT_CC_S_XA_31_16_XB_31_16__5451, cv); break;  // csfu
        case SF2C_TRANSLATE_N_4_4B22: sp(&c->body, SF2C_TRANSLATE_S_UINT_CC_S_XBU_16_N_69B3, cv); break;                              // cbcc
        default: sp(&c->body, SF2C_TRANSLATE_S_UINT_CC_S_N_5D26, cv);                                                  // cfull
        }
        if (ext) sp(&c->body, SF2C_TRANSLATE_S_UINT_RES_PROD_CC_1U_N_CD8E);
        else sp(&c->body, SF2C_TRANSLATE_S_UINT_RES_PROD_CC_N_EC43);
        if (wcc) sp(&c->body, SF2C_TRANSLATE_S_CC_RES_PROD_1U_0U_N_E03F);
        if (mrg) sp(&c->body, SF2C_TRANSLATE_S_RES_RES_0XFFFFU_XBU_16_N_E5E3);
        if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_RES_N_70D0, d); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5090000000000000ULL_5576, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // PSETP
        const char *pa = P((int)bits(w, SF2C_TRANSLATE_N_12_6B51, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_15_E629, 1)), *pb = P((int)bits(w, SF2C_TRANSLATE_N_29_3513, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_32_E29C, 1));
        const char *pcc = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        int bo1 = (int)bits(w, SF2C_TRANSLATE_N_24_C235, SF2C_TRANSLATE_N_2_D473), bo2 = (int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473);
        int p = (int)bits(w, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_3_4E07), q = (int)bits(w, 0, SF2C_TRANSLATE_N_3_4E07);
        sp(&c->body, SF2C_TRANSLATE_S_BOOL_V_S_S_S_BOOL_PP_S_N_5402, pa, bool_op(bo1), pb, pcc);
        if (p != PT) { c->pred_used[p] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_V_S_PP_N_9B7E, p, bool_op(bo2)); }
        if (q != PT) { c->pred_used[q] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_V_S_PP_N_D30A, q, bool_op(bo2)); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5088000000000000ULL_F5B1, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // PSET
        const char *pa = P((int)bits(w, SF2C_TRANSLATE_N_12_6B51, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_15_E629, 1)), *pb = P((int)bits(w, SF2C_TRANSLATE_N_29_3513, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_32_E29C, 1));
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_S_S_58C7, pa, bool_op((int)bits(w, SF2C_TRANSLATE_N_24_C235, SF2C_TRANSLATE_N_2_D473)), pb, bool_op((int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473)),
                P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1)));
        sprintf(u, bits(w, SF2C_TRANSLATE_N_44_71EE, 1) ? SF2C_TRANSLATE_S_S_0X3F800000U_0U_B6E9 : SF2C_TRANSLATE_S_S_0XFFFFFFFFU_0U_2807, t);
        set_reg(c, d, u);
    } else if (OPM(SF2C_TRANSLATE_N_0XF0C8000000000000ULL_0FE8, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // S2R
        int sr = (int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62);
        switch (sr) {
        case SF2C_TRANSLATE_N_0X12_9AE3: set_reg(c, d, SF2C_TRANSLATE_S_0XBF800000U_6907); break;  // y_direction: -1 (lower-left)
        case SF2C_TRANSLATE_N_0X21_A102: set_reg(c, d, SF2C_TRANSLATE_S_TID_X_CB10); break;
        case SF2C_TRANSLATE_N_0X22_9F8A: set_reg(c, d, SF2C_TRANSLATE_S_TID_Y_1254); break;
        case SF2C_TRANSLATE_N_0X23_A029: set_reg(c, d, SF2C_TRANSLATE_S_TID_Z_1415); break;
        case SF2C_TRANSLATE_N_0X25_2611: set_reg(c, d, SF2C_TRANSLATE_S_CTAID_X_B8E4); break;
        case SF2C_TRANSLATE_N_0X26_50D1: set_reg(c, d, SF2C_TRANSLATE_S_CTAID_Y_8CE8); break;
        case SF2C_TRANSLATE_N_0X27_D7EC: set_reg(c, d, SF2C_TRANSLATE_S_CTAID_Z_AD22); break;
        case SF2C_TRANSLATE_N_0X00_C4DD: set_reg(c, d, SF2C_TRANSLATE_S_0U_B642); break;
        default: sprintf(t, SF2C_TRANSLATE_S_0U_SR_X_AFE6, sr); set_reg(c, d, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5C30000000000000ULL_F87D, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C30000000000000ULL_AFA1, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3830000000000000ULL_9E4C, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // FLO
        const char *bv = B(false);
        sprintf(t, bits(w, SF2C_TRANSLATE_N_48_9801, 1) ? SF2C_TRANSLATE_S_INT_S_0_31U_CLZ_S_31U_CLZ_S_D221 : SF2C_TRANSLATE_S_31U_CLZ_S_19B8, bv, bv, bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C08000000000000ULL_419B, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C08000000000000ULL_4012, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3808000000000000ULL_9615, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // POPC
        sprintf(t, bits(w, SF2C_TRANSLATE_N_40_D59E, 1) ? SF2C_TRANSLATE_S_POPCOUNT_S_E4DF : SF2C_TRANSLATE_S_POPCOUNT_S_96C7, B(false));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5C38000000000000ULL_7014, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4C38000000000000ULL_8B5A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X3838000000000000ULL_68EA, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // IMUL
        if (bits(w, SF2C_TRANSLATE_N_39_0B91, 1)) sprintf(t, bits(w, SF2C_TRANSLATE_N_40_D59E, 1) ? SF2C_TRANSLATE_S_UINT_MULHI_INT_S_INT_S_CB81 : SF2C_TRANSLATE_S_MULHI_S_S_0F40, R(a), B(false));
        else sprintf(t, SF2C_TRANSLATE_S_S_S_EB58, R(a), B(false));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X1F00000000000000ULL_470A, SF2C_TRANSLATE_N_0XFF00000000000000ULL_323F)) {  // IMUL32I
        sprintf(t, SF2C_TRANSLATE_S_S_0X_08XU_59B4, R(a), (uint32_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_32_E29C));
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CE0000000000000ULL_9C43, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X4CE0000000000000ULL_B58E, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X38E0000000000000ULL_D851, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // I2I
        int dt = (int)((bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_2_D473)) | (bits(w, SF2C_TRANSLATE_N_12_6B51, 1) << SF2C_TRANSLATE_N_2_D473)), st = (int)((bits(w, SF2C_TRANSLATE_N_10_4A44, SF2C_TRANSLATE_N_2_D473)) | (bits(w, SF2C_TRANSLATE_N_13_3FDB, 1) << SF2C_TRANSLATE_N_2_D473));
        int sel = (int)bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_2_D473);
        char src[SF2C_TRANSLATE_N_256_51E8];
        const char *bv = B(false);
        int ssz = st & SF2C_TRANSLATE_N_3_4E07;
        bool ssg = st & SF2C_TRANSLATE_N_4_4B22;
        if (ssz == 0) sprintf(src, ssg ? SF2C_TRANSLATE_S_INT_CHAR_S_D_0XFFU_C08C : SF2C_TRANSLATE_S_INT_S_D_0XFFU_E10D, bv, sel * SF2C_TRANSLATE_N_8_2C62);
        else if (ssz == 1) sprintf(src, ssg ? SF2C_TRANSLATE_S_INT_SHORT_S_D_0XFFFFU_6230 : SF2C_TRANSLATE_S_INT_S_D_0XFFFFU_DE66, bv, sel * SF2C_TRANSLATE_N_8_2C62);
        else sprintf(src, SF2C_TRANSLATE_S_INT_S_C689, bv);
        if (bits(w, SF2C_TRANSLATE_N_49_0E17, 1)) { sprintf(u, SF2C_TRANSLATE_S_ABS_S_BCE4, src); strcpy(src, u); }
        if (bits(w, SF2C_TRANSLATE_N_45_8117, 1)) { sprintf(u, SF2C_TRANSLATE_S_S_7BDD, src); strcpy(src, u); }
        int dsz = dt & SF2C_TRANSLATE_N_3_4E07;
        bool dsg = dt & SF2C_TRANSLATE_N_4_4B22, sat = bits(w, SF2C_TRANSLATE_N_50_1A65, 1);
        if (sat && !ssg) {  // .SAT from an unsigned source: compare unsigned (a U32 value above 0x7FFFFFFF isn't negative)
            static const char *const hi[SF2C_TRANSLATE_N_2_D473][SF2C_TRANSLATE_N_3_4E07] = {{SF2C_TRANSLATE_S_0XFFU_4D92, SF2C_TRANSLATE_S_0XFFFFU_1FC3, SF2C_TRANSLATE_S_EMPTY_12AE}, {SF2C_TRANSLATE_S_0X7FU_6F23, SF2C_TRANSLATE_S_0X7FFFU_4622, SF2C_TRANSLATE_S_0X7FFFFFFFU_0CBA}};
            if (dsz < SF2C_TRANSLATE_N_2_D473 || dsg) sprintf(t, SF2C_TRANSLATE_S_MIN_UINT_S_S_45E0, src, hi[dsg][dsz < SF2C_TRANSLATE_N_2_D473 ? dsz : SF2C_TRANSLATE_N_2_D473]);
            else sprintf(t, SF2C_TRANSLATE_S_UINT_S_5E45, src);
        } else if (dsz == 0) sprintf(t, sat ? (dsg ? SF2C_TRANSLATE_S_UINT_CLAMP_S_128_127_A8DE : SF2C_TRANSLATE_S_UINT_CLAMP_S_0_255_A07A) : SF2C_TRANSLATE_S_UINT_S_0XFFU_4D56, src);
        else if (dsz == 1) sprintf(t, sat ? (dsg ? SF2C_TRANSLATE_S_UINT_CLAMP_S_32768_32767_2DFE : SF2C_TRANSLATE_S_UINT_CLAMP_S_0_65535_CD4F) : SF2C_TRANSLATE_S_UINT_S_0XFFFFU_F16A, src);
        else if (sat && !dsg) sprintf(t, SF2C_TRANSLATE_S_UINT_MAX_S_0_A9A9, src);  // S32 -> U32 .SAT: negatives become 0
        else sprintf(t, SF2C_TRANSLATE_S_UINT_S_5E45, src);
        set_reg(c, d, t);
        if (bits(w, SF2C_TRANSLATE_N_47_3148, 1)) {  // .CC: zero/sign flags of the result
            c->uses_cc = true;
            sp(&c->body, SF2C_TRANSLATE_S_UINT_CCV_S_CCZ_CCV_0U_CCS_IN_B3C0, t);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X5BC0000000000000ULL_2243, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4BC0000000000000ULL_1622, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X36C0000000000000ULL_58A6, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292) || OPM(SF2C_TRANSLATE_N_0X53C0000000000000ULL_E72F, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // PRMT
        const char *bv, *cv;
        char cb[SF2C_TRANSLATE_N_256_51E8];
        if (OPM(SF2C_TRANSLATE_N_0X53C0000000000000ULL_E72F, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) { bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb); }
        else { bv = B(false); cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62)); }
        if (bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_4_4B22)) fail(c, SF2C_TRANSLATE_S_PRMT_MODE_09E5);
        sprintf(t, SF2C_TRANSLATE_S_PRMT_S_S_S_01C8, R(a), cv, bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5CF8000000000000ULL_239B, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X38F8000000000000ULL_E0BF, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217) ||
               OPM(SF2C_TRANSLATE_N_0X5BF8000000000000ULL_31EB, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0X36F8000000000000ULL_A2E8, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217)) {  // SHF
        // funnel shift of hi:lo (C:A). .W (bit 50) wraps the shift amount, otherwise it's clamped to the width;
        // bits 37-38: 2 .U64 / 3 .S64 shift the full 64 bits (width 64), else 32. The immediate is 6 bits (20-25:
        // bits 37-38 of the 19-bit B immediate are the width field). .CC / .X / .HI (47-49) aren't modelled.
        bool right = (w >> SF2C_TRANSLATE_N_56_7688) == SF2C_TRANSLATE_N_0X5C_F9CC || (w >> SF2C_TRANSLATE_N_57_C837) == (SF2C_TRANSLATE_N_0X38_AE4F >> 1);
        if (bits(w, SF2C_TRANSLATE_N_47_3148, SF2C_TRANSLATE_N_3_4E07)) { fail(c, SF2C_TRANSLATE_S_SHF_CC_X_HI_3EA0); return; }
        char shb[SF2C_TRANSLATE_N_16_B17E];
        const char *sh = ((w >> SF2C_TRANSLATE_N_60_39FA) == SF2C_TRANSLATE_N_3_4E07) ? (sprintf(shb, SF2C_TRANSLATE_S_UU_1382, (unsigned)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_6_E7F6)), shb) : R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62));
        const char *hi = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
        int width = bits(w, SF2C_TRANSLATE_N_38_AEA9, 1) ? SF2C_TRANSLATE_N_64_A68B : SF2C_TRANSLATE_N_32_E29C;
        bool s64 = bits(w, SF2C_TRANSLATE_N_37_7A61, SF2C_TRANSLATE_N_2_D473) == SF2C_TRANSLATE_N_3_4E07, wrap = bits(w, SF2C_TRANSLATE_N_50_1A65, 1);
        sp(&c->body, wrap ? SF2C_TRANSLATE_S_UINT_S_S_DU_ULONG_V_ULONG_S__2125
                          : SF2C_TRANSLATE_S_UINT_S_MIN_S_DU_ULONG_V_ULON_1194,
           sh, wrap ? width - 1 : width, hi, R(a));
        if (right && width == SF2C_TRANSLATE_N_64_A68B)
            sprintf(t, SF2C_TRANSLATE_S_S_64U_S_UINT_S_S_B3C8, s64 ? SF2C_TRANSLATE_S_UINT_INT_V_32_31_BF4E : SF2C_TRANSLATE_S_0U_B642, s64 ? SF2C_TRANSLATE_S_LONG_V_BB84 : SF2C_TRANSLATE_S_V_D1A4);
        else if (right) sprintf(t, SF2C_TRANSLATE_S_UINT_V_S_C5E6);
        else if (width == SF2C_TRANSLATE_N_64_A68B) sprintf(t, SF2C_TRANSLATE_S_S_64U_0U_UINT_V_S_32_C5FB);
        else sprintf(t, SF2C_TRANSLATE_S_UINT_V_S_32_A1A2);
        if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_S_N_1DFA, d, t); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0XEED0000000000000ULL_7246, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0XEED8000000000000ULL_519A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // LDG / STG
        bool store = bits(w, SF2C_TRANSLATE_N_51_031B, 1);
        bool wide = bits(w, SF2C_TRANSLATE_N_45_8117, 1);
        int size = (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07);
        int64_t off = sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235);
        c->info->uses_gmem = true;
        char addr[SF2C_TRANSLATE_N_256_51E8];
        if (a == RZ) sprintf(addr, SF2C_TRANSLATE_S_ULONG_LLD_17A1, (long long)off);
        else if (wide) sprintf(addr, SF2C_TRANSLATE_S_ULONG_S_32_ULONG_S_ULONG_LLD_9D38, R(a + 1), R(a), (long long)off);
        else sprintf(addr, SF2C_TRANSLATE_S_ULONG_S_ULONG_LLD_137B, R(a), (long long)off);
        sp(&c->body, SF2C_TRANSLATE_S_ULONG_GA_S_0X_LLXUL_BOOL_OK__5A8A, addr,
           (unsigned long long)maxwell_gmem_base, (unsigned long long)maxwell_gmem_size);
        static const char *const ld_small[] = {SF2C_TRANSLATE_S_UINT_GMEM_GA_9924, SF2C_TRANSLATE_S_UINT_INT_CHAR_GMEM_GA_5F4C,
                                               SF2C_TRANSLATE_S_UINT_DEVICE_USHORT_GMEM_GA_3DBA, SF2C_TRANSLATE_S_UINT_INT_DEVICE_SHORT_GMEM_G_E0F6};
        int nw = size == SF2C_TRANSLATE_N_5_EF2D ? SF2C_TRANSLATE_N_2_D473 : size >= SF2C_TRANSLATE_N_6_E7F6 ? SF2C_TRANSLATE_N_4_4B22 : 1;
        bool lazy_wide = c->glsl && maxwell_gmem_chunk_shift && maxwell_gmem_lazy && nw > 1;  // (one chunk check per access)
        if (c->glsl && c->stage == STAGE_COMPUTE) {
            // Compute (Vulkan): the guest heap is host memory across PCIe on a discrete GPU, and a shader storing to
            // it in a loop crawls (the little chain chomps in Madam MeowMeow's house: ~4 ms a dispatch, ~0.07 ms without
            // the heap stores). The dispatch's storage buffers are copied to a device-local buffer (gfast_, address
            // table gmap: exec.c gfast_prepare) and back after it: loads and stores inside them use that copy.
            // Anything outside the table goes to the heap as before.
            c->info->uses_gmem_map = true;
            static const char *const gl_small[] = {
                SF2C_TRANSLATE_S_S_UINT_GA_3UL_8U_0XFFU_0EE5, SF2C_TRANSLATE_S_UINT_SX8_S_UINT_GA_3UL_8U_D8C2,
                SF2C_TRANSLATE_S_S_UINT_GA_2UL_8U_0XFFFFU_F019, SF2C_TRANSLATE_S_UINT_SX16_S_UINT_GA_2UL_8U_D1E1};
            sp(&c->body, SF2C_TRANSLATE_S_UINT_FW_OK_GFIND_UINT_GA_0XF_6455);
            if (!store) {
                if (size < SF2C_TRANSLATE_N_4_4B22) {
                    if (d != RZ) {
                        char fast[SF2C_TRANSLATE_N_160_A512], slow[SF2C_TRANSLATE_N_160_A512];
                        snprintf(fast, sizeof fast, gl_small[size], SF2C_TRANSLATE_S_GFAST_W_FW_DD6E);
                        snprintf(slow, sizeof slow, gl_small[size], SF2C_TRANSLATE_S_GMR_UINT_GA_2_1CE2);
                        c->reg_used[d] = 1;
                        sp(&c->body, SF2C_TRANSLATE_S_R_D_FW_0XFFFFFFFFU_S_OK_S_0U_B2B6, d, fast, slow);
                    }
                } else if (lazy_wide) {
                    sp(&c->body, SF2C_TRANSLATE_S_UVEC_D_T_FW_0XFFFFFFFFU_UVEC_3A1B, nw, nw);
                    for (int k = 0; k < nw; k++) sp(&c->body, SF2C_TRANSLATE_S_SGFAST_W_FW_DU_155F, k ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE, k);
                    sp(&c->body, SF2C_TRANSLATE_S_OK_GMR_D_UINT_GA_2_UVEC_D_0U_4189, nw, nw);
                    for (int k = 0; k < nw; k++)
                        if (d != RZ && d + k < RZ) { c->reg_used[d + k] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_T_D_N_6804, d + k, k); }
                    sp(&c->body, SF2C_TRANSLATE_S_N_D2D5);
                } else {
                    for (int k = 0; k < nw; k++)
                        if (d != RZ && d + k < RZ) {
                            c->reg_used[d + k] = 1;
                            sp(&c->body, SF2C_TRANSLATE_S_R_D_FW_0XFFFFFFFFU_GFAST_W_F_0041,
                               d + k, k, k);
                        }
                }
            } else {
                c->uses_gmem_store = true;
                c->info->uses_gmem_store = true;
                if (size < SF2C_TRANSLATE_N_4_4B22) {
                    uint32_t m = size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_N_0XFFU_4861 : SF2C_TRANSLATE_N_0XFFFFU_EC9C;
                    const char *bm = size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_3UL_DF5B : SF2C_TRANSLATE_S_2UL_32C8;
                    sp(&c->body, SF2C_TRANSLATE_S_IF_OK_UINT_SH_UINT_GA_S_8U_U_54BB, bm, R(d), m);
                    sp(&c->body, SF2C_TRANSLATE_S_IF_FW_0XFFFFFFFFU_ATOMICAND__24CC, m);
                    sp(&c->body, SF2C_TRANSLATE_S_ELSE_GMAND_UINT_GA_2_0X_XU_S_DF03, m);
                } else if (lazy_wide) {
                    for (int k = 0; k < nw; k++) sp(&c->body, SF2C_TRANSLATE_S_IF_FW_0XFFFFFFFFU_GFAST_W_FW_B0DA, k, R(d == RZ ? RZ : d + k));
                    sp(&c->body, SF2C_TRANSLATE_S_IF_FW_0XFFFFFFFFU_OK_GMW_D_U_6ED3, nw, nw);
                    for (int k = 0; k < nw; k++) sp(&c->body, SF2C_TRANSLATE_S_S_S_72BD, k ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE, R(d == RZ ? RZ : d + k));
                    sp(&c->body, SF2C_TRANSLATE_S_N_10D1);
                } else {
                    for (int k = 0; k < nw; k++)
                        sp(&c->body, SF2C_TRANSLATE_S_IF_FW_0XFFFFFFFFU_GFAST_W_FW_2D4E, k,
                           R(d == RZ ? RZ : d + k), k, R(d == RZ ? RZ : d + k));
                }
            }
        } else if (c->glsl) {  // 32-bit words (GMR / GMW / GMAND / GMOR: the prelude); sub-word accesses within the containing word
            static const char *const gl_small[] = {
                SF2C_TRANSLATE_S_GMR_UINT_GA_2_UINT_GA_3UL_8U_A8DF,
                SF2C_TRANSLATE_S_UINT_SX8_GMR_UINT_GA_2_UINT__20B9,
                SF2C_TRANSLATE_S_GMR_UINT_GA_2_UINT_GA_2UL_8U_F231,
                SF2C_TRANSLATE_S_UINT_SX16_GMR_UINT_GA_2_UINT_FC55};
            if (!store) {
                if (size < SF2C_TRANSLATE_N_4_4B22) {
                    if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_IF_OK_R_D_S_ELSE_R_D_0U_N_C9C9, d, gl_small[size], d); }
                } else if (lazy_wide) {  // (one bounds check and address for the whole aligned access)
                    sp(&c->body, SF2C_TRANSLATE_S_UVEC_D_T_OK_GMR_D_UINT_GA_2__AB88, nw, nw, nw);
                    for (int k = 0; k < nw; k++)
                        if (d != RZ && d + k < RZ) { c->reg_used[d + k] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_T_D_N_6804, d + k, k); }
                    sp(&c->body, SF2C_TRANSLATE_S_N_D2D5);
                } else {
                    for (int k = 0; k < nw; k++)
                        if (d != RZ && d + k < RZ) {
                            c->reg_used[d + k] = 1;
                            sp(&c->body, SF2C_TRANSLATE_S_IF_OK_R_D_GMR_UINT_GA_2_DU_E_824F, d + k, k, d + k);
                        }
                }
            } else {
                c->uses_gmem_store = true;
                c->info->uses_gmem_store = true;  // (the draw marks the pages it stores to: vertex cache copies go stale)
                if (size < SF2C_TRANSLATE_N_4_4B22)
                    sp(&c->body, SF2C_TRANSLATE_S_IF_OK_UINT_WI_UINT_GA_2_UINT_9EC8
                       SF2C_TRANSLATE_S_GMOR_WI_S_0X_XU_SH_N_1DEC, size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_3UL_DF5B : SF2C_TRANSLATE_S_2UL_32C8, size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_N_0XFFU_4861 : SF2C_TRANSLATE_N_0XFFFFU_EC9C, R(d),
                       size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_N_0XFFU_4861 : SF2C_TRANSLATE_N_0XFFFFU_EC9C);
                else if (lazy_wide) {
                    sp(&c->body, SF2C_TRANSLATE_S_IF_OK_GMW_D_UINT_GA_2_UVEC_D_3CD3, nw, nw);
                    for (int k = 0; k < nw; k++) sp(&c->body, SF2C_TRANSLATE_S_S_S_72BD, k ? SF2C_TRANSLATE_S_EMPTY_F240 : SF2C_TRANSLATE_S_EMPTY_12AE, R(d == RZ ? RZ : d + k));
                    sp(&c->body, SF2C_TRANSLATE_S_N_10D1);
                } else
                    for (int k = 0; k < nw; k++)
                        sp(&c->body, SF2C_TRANSLATE_S_IF_OK_GMW_UINT_GA_2_DU_S_N_E25C, k, R(d == RZ ? RZ : d + k));
            }
        } else if (!store) {
            if (size < SF2C_TRANSLATE_N_4_4B22) {
                if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_OK_S_0U_N_C5B4, d, ld_small[size]); }
            } else {
                for (int k = 0; k < nw; k++)
                    if (d != RZ && d + k < RZ) { c->reg_used[d + k] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_OK_DEVICE_UINT_GMEM_GA_D_D063, d + k, k * SF2C_TRANSLATE_N_4_4B22); }
            }
        } else {
            if (size == 0 || size == 1) sp(&c->body, SF2C_TRANSLATE_S_IF_OK_GMEM_GA_UCHAR_S_N_40F5, R(d));
            else if (size == SF2C_TRANSLATE_N_2_D473 || size == SF2C_TRANSLATE_N_3_4E07) sp(&c->body, SF2C_TRANSLATE_S_IF_OK_DEVICE_USHORT_GMEM_GA__EBF4, R(d));
            else
                for (int k = 0; k < nw; k++)
                    sp(&c->body, SF2C_TRANSLATE_S_IF_OK_DEVICE_UINT_GMEM_GA_DU_DB0D, k * SF2C_TRANSLATE_N_4_4B22, R(d == RZ ? RZ : d + k));
        }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (emit_half(c, w)) {
    } else if (OPM(SF2C_TRANSLATE_N_0X50F8000000000000ULL_2E6A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // FSWZADD
        // per quad lane (laneid & 3), 2 bits of the swizzle pick: a+b, b-a, a-b, b
        uint32_t swz = (uint32_t)bits(w, SF2C_TRANSLATE_N_28_59E1, SF2C_TRANSLATE_N_8_2C62);
        if (c->stage != STAGE_FRAGMENT) {
            sprintf(t, SF2C_TRANSLATE_S_AS_TYPE_UINT_S_S_E7F2, F(R(a)), F(R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62))));
        } else {
            c->uses_lane = true;
            sprintf(t, SF2C_TRANSLATE_S_AS_TYPE_UINT_FSWZADD_S_S_0X__EDAE, F(R(a)), F(R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62))), swz);
        }
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0X5B40000000000000ULL_FC5F, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) || OPM(SF2C_TRANSLATE_N_0X4B40000000000000ULL_7CF2, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) ||
               OPM(SF2C_TRANSLATE_N_0X3640000000000000ULL_E2DA, SF2C_TRANSLATE_N_0XFEF0000000000000ULL_9292) || OPM(SF2C_TRANSLATE_N_0X5340000000000000ULL_38D9, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {  // ICMP
        char cmp[SF2C_TRANSLATE_N_512_94F8], cb[SF2C_TRANSLATE_N_256_51E8], bb[SF2C_TRANSLATE_N_256_51E8];
        const char *bv, *cv;
        if (OPM(SF2C_TRANSLATE_N_0X5340000000000000ULL_38D9, SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D)) {
            bv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
            cv = cbuf_ref(c, (int)bits(w, SF2C_TRANSLATE_N_34_86E5, SF2C_TRANSLATE_N_5_EF2D), bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_14_8527) * SF2C_TRANSLATE_N_4_4B22, NULL, cb);
        } else {
            bv = operand_b(c, w, false, bb);
            cv = R((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_8_2C62));
        }
        icmp_expr(cmp, (int)bits(w, SF2C_TRANSLATE_N_49_0E17, SF2C_TRANSLATE_N_3_4E07), cv, SF2C_TRANSLATE_S_0U_B642, bits(w, SF2C_TRANSLATE_N_48_9801, 1));
        sprintf(t, SF2C_TRANSLATE_S_S_S_S_D317, cmp, R(a), bv);
        set_reg(c, d, t);
    } else if (OPM(SF2C_TRANSLATE_N_0XEF40000000000000ULL_7D7B, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) || OPM(SF2C_TRANSLATE_N_0XEF50000000000000ULL_EF18, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // LDL / STL
        // thread-private local memory, modelled as a word array (byte/short accesses within a word)
        bool store = bits(w, SF2C_TRANSLATE_N_52_41CF, 1);
        int size = (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07);
        int32_t off = (int32_t)sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235);
        c->uses_local = true;
        char addr[SF2C_TRANSLATE_N_128_2747];
        if (a == RZ) sprintf(addr, SF2C_TRANSLATE_S_DU_2FE0, off);
        else sprintf(addr, SF2C_TRANSLATE_S_S_DU_9D8D, R(a), (uint32_t)off);
        int nw = size == SF2C_TRANSLATE_N_5_EF2D ? SF2C_TRANSLATE_N_2_D473 : size >= SF2C_TRANSLATE_N_6_E7F6 ? SF2C_TRANSLATE_N_4_4B22 : 1;
        sp(&c->body, SF2C_TRANSLATE_S_UINT_LA_S_N_B63B, addr);
        if (!store) {
            if (size < SF2C_TRANSLATE_N_4_4B22) {
                static const char *const ext[] = {SF2C_TRANSLATE_S_V_SH_0XFFU_5DA1, SF2C_TRANSLATE_S_UINT_INT_CHAR_V_SH_0XFFU_A4EB, SF2C_TRANSLATE_S_V_SH_0XFFFFU_8B2C,
                                                  SF2C_TRANSLATE_S_UINT_INT_SHORT_V_SH_0XFFFFU_DB08};
                if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_UINT_V_LMEM_LA_2_DU_UINT_SH__000D, LMEM_WORDS - 1, d, ext[size]); }
            } else {
                for (int k = 0; k < nw; k++)
                    if (d != RZ && d + k < RZ) { c->reg_used[d + k] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_LMEM_LA_2_DU_DU_N_5061, d + k, k, LMEM_WORDS - 1); }
            }
        } else {
            if (size < SF2C_TRANSLATE_N_4_4B22) {
                uint32_t m = size < SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_N_0XFFU_4861 : SF2C_TRANSLATE_N_0XFFFFU_EC9C;
                sp(&c->body, SF2C_TRANSLATE_S_UINT_SH_LA_3U_8U_UINT_I_LA_2_3F62,
                   LMEM_WORDS - 1, m, R(d), m);
            } else {
                for (int k = 0; k < nw; k++)
                    sp(&c->body, SF2C_TRANSLATE_S_LMEM_LA_2_DU_DU_S_N_859D, k, LMEM_WORDS - 1, R(d == RZ ? RZ : d + k));
            }
        }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0XEF10000000000000ULL_F45A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // SHFL
        int mode = (int)bits(w, SF2C_TRANSLATE_N_30_624B, SF2C_TRANSLATE_N_2_D473);
        char bb[SF2C_TRANSLATE_N_32_E29C];
        const char *bv = bits(w, SF2C_TRANSLATE_N_28_59E1, 1) ? (sprintf(bb, SF2C_TRANSLATE_S_UU_1382, (unsigned)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_5_EF2D)), bb) : R((int)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_8_2C62));
        int pd = (int)bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07);
        if (c->stage == STAGE_VERTEX) {
            set_reg(c, d, R(a));  // no SIMD-group functions in vertex functions: lane-local value
        } else if (c->glsl) {  // a quad xor (lane mask < 4) is the subgroup xor within the quad
            static const char *const fn[] = {SF2C_TRANSLATE_S_SUBGROUPSHUFFLE_29A0, SF2C_TRANSLATE_S_SUBGROUPSHUFFLEUP_A249, SF2C_TRANSLATE_S_SUBGROUPSHUFFLEDOWN_24FD, SF2C_TRANSLATE_S_SUBGROUPSHUFFLEXOR_992E};
            sprintf(t, SF2C_TRANSLATE_S_S_S_UINT_S_4BD4, fn[mode], R(a), bv);
            set_reg(c, d, t);
        } else if (mode == SF2C_TRANSLATE_N_3_4E07) {
            bool quad = bits(w, SF2C_TRANSLATE_N_28_59E1, 1) && bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_5_EF2D) < SF2C_TRANSLATE_N_4_4B22;
            sprintf(t, quad ? SF2C_TRANSLATE_S_QUAD_SHUFFLE_XOR_S_USHORT_S_690A : SF2C_TRANSLATE_S_SIMD_SHUFFLE_XOR_S_USHORT_S_95CE, R(a), bv);
            set_reg(c, d, t);
        } else {
            static const char *const fn[] = {SF2C_TRANSLATE_S_SIMD_SHUFFLE_BD96, SF2C_TRANSLATE_S_SIMD_SHUFFLE_UP_98F7, SF2C_TRANSLATE_S_SIMD_SHUFFLE_DOWN_BF26};
            sprintf(t, SF2C_TRANSLATE_S_S_S_USHORT_S_25FA, fn[mode], R(a), bv);
            set_reg(c, d, t);
        }
        if (pd != PT) { c->pred_used[pd] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_TRUE_N_8924, pd); }
    } else if (OPM(SF2C_TRANSLATE_N_0X38F0000000000000ULL_AE5A, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217) || OPM(SF2C_TRANSLATE_N_0X5CF0000000000000ULL_E051, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X4CF0000000000000ULL_6CCC, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // R2P
        if (bits(w, SF2C_TRANSLATE_N_40_D59E, 1)) { fail(c, SF2C_TRANSLATE_S_R2P_CC_D922); }
        int shift = (int)bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_2_D473) * SF2C_TRANSLATE_N_8_2C62;
        const char *mask = B(false);
        for (int i = 0; i < SF2C_TRANSLATE_N_7_7902; i++) {
            c->pred_used[i] = 1;
            sp(&c->body, c->glsl ? SF2C_TRANSLATE_S_IF_S_DU_1U_0U_P_D_S_DU_1U_0U_89E9
                                 : SF2C_TRANSLATE_S_IF_S_DU_1U_P_D_S_DU_1U_0U_N_1E62, mask, i, i, R(a), shift + i);
        }
    } else if (OPM(SF2C_TRANSLATE_N_0X38E8000000000000ULL_3508, SF2C_TRANSLATE_N_0XFEF8000000000000ULL_6217) || OPM(SF2C_TRANSLATE_N_0X5CE8000000000000ULL_A9AA, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) ||
               OPM(SF2C_TRANSLATE_N_0X4CE8000000000000ULL_BBCC, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // P2R
        int shift = (int)bits(w, SF2C_TRANSLATE_N_41_3D91, SF2C_TRANSLATE_N_2_D473) * SF2C_TRANSLATE_N_8_2C62;
        const char *mask = B(false);
        if (bits(w, SF2C_TRANSLATE_N_40_D59E, 1)) {  // .CC: the flags Z, S, C, O as bits 0-3 (overflow isn't tracked: 0)
            c->uses_cc = true;
            sp(&c->body, SF2C_TRANSLATE_S_UINT_PR_CCZ_1U_0U_CCS_2U_0U__35CD);
            sp(&c->body, SF2C_TRANSLATE_S_UINT_M_S_0XFU_DU_N_81EB, mask, shift);
            if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_S_M_PR_DU_M_N_C594, d, R(a), shift); }
            sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
            goto p2r_done;
        }
        sp(&c->body, SF2C_TRANSLATE_S_UINT_PR_0U_N_7478);
        for (int i = 0; i < SF2C_TRANSLATE_N_7_7902; i++) { c->pred_used[i] = 1; sp(&c->body, SF2C_TRANSLATE_S_PR_P_D_1U_0U_DU_N_E442, i, i); }
        sp(&c->body, SF2C_TRANSLATE_S_UINT_M_S_0X7FU_DU_N_8633, mask, shift);
        if (d != RZ) { c->reg_used[d] = 1; sp(&c->body, SF2C_TRANSLATE_S_R_D_S_M_PR_DU_M_N_C594, d, R(a), shift); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    p2r_done:;
    } else if (OPM(SF2C_TRANSLATE_N_0XE250000000000000ULL_2683, SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B)) {  // BRX reg + imm (relative)
        // computed targets can name a scheduling word; execution continues at the next slot (as for BRA)
        sp(&c->body, SF2C_TRANSLATE_S_UINT_T_UU_S_0X_08XU_IF_T_3_3_5E30, pc + SF2C_TRANSLATE_N_8_2C62, R(a),
           (uint32_t)sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235));
    } else if (OPM(SF2C_TRANSLATE_N_0X50A0000000000000ULL_BA9A, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // CSETP (zero/sign flags only)
        int test = (int)bits(w, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_5_EF2D);
        const char *cond;
        switch (test) {
        case 0: cond = SF2C_TRANSLATE_S_FALSE_076D; break;
        case SF2C_TRANSLATE_N_15_E629: cond = SF2C_TRANSLATE_S_TRUE_18D1; break;
        case SF2C_TRANSLATE_N_2_D473: case SF2C_TRANSLATE_N_10_4A44: cond = SF2C_TRANSLATE_S_CCZ_5100; break;
        case SF2C_TRANSLATE_N_5_EF2D: case SF2C_TRANSLATE_N_13_3FDB: cond = SF2C_TRANSLATE_S_CCZ_AF3D; break;
        case 1: case SF2C_TRANSLATE_N_9_1958: cond = SF2C_TRANSLATE_S_CCS_BB46; break;
        case SF2C_TRANSLATE_N_6_E7F6: case SF2C_TRANSLATE_N_14_8527: cond = SF2C_TRANSLATE_S_CCS_B019; break;
        case SF2C_TRANSLATE_N_4_4B22: case SF2C_TRANSLATE_N_12_6B51: cond = SF2C_TRANSLATE_S_CCZ_CCS_4E8D; break;
        case SF2C_TRANSLATE_N_3_4E07: case SF2C_TRANSLATE_N_11_4FC8: cond = SF2C_TRANSLATE_S_CCZ_CCS_02D2; break;
        default: cond = SF2C_TRANSLATE_S_FALSE_076D; fail(c, SF2C_TRANSLATE_S_CSETP_TEST_D_E7B6, test);
        }
        c->uses_cc = true;
        int p = (int)bits(w, SF2C_TRANSLATE_N_3_4E07, SF2C_TRANSLATE_N_3_4E07), q = (int)bits(w, 0, SF2C_TRANSLATE_N_3_4E07), bo = (int)bits(w, SF2C_TRANSLATE_N_45_8117, SF2C_TRANSLATE_N_2_D473);
        const char *pc_ = P((int)bits(w, SF2C_TRANSLATE_N_39_0B91, SF2C_TRANSLATE_N_3_4E07), bits(w, SF2C_TRANSLATE_N_42_7347, 1));
        sp(&c->body, SF2C_TRANSLATE_S_BOOL_CMP_S_BOOL_PP_S_N_7DC5, cond, pc_);
        if (p != PT) { c->pred_used[p] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_CMP_S_PP_N_78C5, p, bool_op(bo)); }
        if (q != PT) { c->pred_used[q] = 1; sp(&c->body, SF2C_TRANSLATE_S_P_D_CMP_S_PP_N_0DD5, q, bool_op(bo)); }
        sp(&c->body, SF2C_TRANSLATE_S_N_3BF2);
    } else if (OPM(SF2C_TRANSLATE_N_0X5098000000000000ULL_984B, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // CSET
        fail(c, SF2C_TRANSLATE_S_CSET_9C40);
    } else if (OPM(SF2C_TRANSLATE_N_0XF0A8000000000000ULL_6C3C, SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50)) {  // BAR
        if (!c->glsl) sp(&c->body, SF2C_TRANSLATE_S_THREADGROUP_BARRIER_MEM_FLAG_CA84);
        else if (c->stage == STAGE_COMPUTE) sp(&c->body, SF2C_TRANSLATE_S_BARRIER_N_1DE9);
        else sp(&c->body, SF2C_TRANSLATE_S_BAR_N_08F3);  // GLSL barrier() is compute / tessellation only
    } else {
        fail(c, SF2C_TRANSLATE_S_UNKNOWN_INSTRUCTION_016LLX_A_73E7, (unsigned long long)w, pc);
    }
    (void)op13;
    if (guarded) sp(&c->body, SF2C_TRANSLATE_S_N_583B);
}

// ------------------------------------------------------------------ program assembly
static bool is_branch_target_op(uint64_t w) {
    return ((w & SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B) == SF2C_TRANSLATE_N_0XE240000000000000ULL_BCB5);
}

// ------------------------------------------------------------------ GLSL output
static bool id_start(char ch) { return (ch >= SF2C_TRANSLATE_CH_A_D749 && ch <= SF2C_TRANSLATE_CH_Z_0DF4) || (ch >= SF2C_TRANSLATE_CH_A_341F && ch <= SF2C_TRANSLATE_CH_Z_2E9F) || ch == SF2C_TRANSLATE_CH_EMPTY_BDC4; }
static bool id_char(char ch) { return id_start(ch) || (ch >= SF2C_TRANSLATE_CH_0_59A9 && ch <= SF2C_TRANSLATE_CH_9_E453); }

// MSL-spelled instruction body -> GLSL (token-aware; comments and numbers are copied as they are).
static void glsl_rewrite(Ctx *c, Str *o, const char *src) {
    static const char *const map[][SF2C_TRANSLATE_N_2_D473] = {
        {SF2C_TRANSLATE_S_FLOAT2_05A7, SF2C_TRANSLATE_S_VEC2_F583}, {SF2C_TRANSLATE_S_FLOAT3_5F8D, SF2C_TRANSLATE_S_VEC3_1818}, {SF2C_TRANSLATE_S_FLOAT4_2B2C, SF2C_TRANSLATE_S_VEC4_B997}, {SF2C_TRANSLATE_S_INT2_3C19, SF2C_TRANSLATE_S_IVEC2_BAE2}, {SF2C_TRANSLATE_S_INT3_99E3, SF2C_TRANSLATE_S_IVEC3_E082},
        {SF2C_TRANSLATE_S_INT4_E360, SF2C_TRANSLATE_S_IVEC4_F360}, {SF2C_TRANSLATE_S_UINT2_A1BA, SF2C_TRANSLATE_S_UVEC2_762C}, {SF2C_TRANSLATE_S_UINT3_47FA, SF2C_TRANSLATE_S_UVEC3_ED74}, {SF2C_TRANSLATE_S_UINT4_3D2D, SF2C_TRANSLATE_S_UVEC4_1DCC}, {SF2C_TRANSLATE_S_BOOL2_6EB7, SF2C_TRANSLATE_S_BVEC2_FB7D},
        {SF2C_TRANSLATE_S_ULONG_2E1D, SF2C_TRANSLATE_S_UINT64_T_568B}, {SF2C_TRANSLATE_S_LONG_C32C, SF2C_TRANSLATE_S_INT64_T_5B64}, {SF2C_TRANSLATE_S_RSQRT_73CF, SF2C_TRANSLATE_S_INVERSESQRT_25EF}, {SF2C_TRANSLATE_S_RINT_E6CF, SF2C_TRANSLATE_S_ROUNDEVEN_38D0},
        {SF2C_TRANSLATE_S_FRONT_FACING_F828, SF2C_TRANSLATE_S_GL_FRONTFACING_0CEA}, {SF2C_TRANSLATE_S_POINT_COORD_4E50, SF2C_TRANSLATE_S_GL_POINTCOORD_8BA4}, {SF2C_TRANSLATE_S_LANE_1FA5, SF2C_TRANSLATE_S_GL_SUBGROUPINVOCATIONID_DAFA},
        {SF2C_TRANSLATE_S_VID_569D, SF2C_TRANSLATE_S_UINT_GL_VERTEXINDEX_BD3E}, {SF2C_TRANSLATE_S_IID_439D, SF2C_TRANSLATE_S_UINT_GL_INSTANCEINDEX_776C},
    };
    const char *p = src;
    while (*p) {
        if (p[0] == SF2C_TRANSLATE_CH_EMPTY_0825 && p[1] == SF2C_TRANSLATE_CH_EMPTY_15A7) {
            const char *e = strstr(p + SF2C_TRANSLATE_N_2_D473, SF2C_TRANSLATE_S_EMPTY_CC5E);
            e = e ? e + SF2C_TRANSLATE_N_2_D473 : p + strlen(p);
            sp(o, SF2C_TRANSLATE_S_S_F03C, (int)(e - p), p);
            p = e;
            continue;
        }
        if (p[0] == SF2C_TRANSLATE_CH_EMPTY_0825 && p[1] == SF2C_TRANSLATE_CH_EMPTY_0825) {
            const char *e = strchr(p, SF2C_TRANSLATE_CH_N_AD65);
            if (!e) e = p + strlen(p);
            sp(o, SF2C_TRANSLATE_S_S_F03C, (int)(e - p), p);
            p = e;
            continue;
        }
        if (*p >= SF2C_TRANSLATE_CH_0_59A9 && *p <= SF2C_TRANSLATE_CH_9_E453) {  // number (0x..u, 1.0f, ...)
            const char *e = p;
            while (id_char(*e) || *e == SF2C_TRANSLATE_CH_EMPTY_277A) e++;
            sp(o, SF2C_TRANSLATE_S_S_F03C, (int)(e - p), p);
            p = e;
            continue;
        }
        if (!id_start(*p)) { sp(o, SF2C_TRANSLATE_S_C_7A1C, *p); p++; continue; }
        const char *e = p;
        while (id_char(*e)) e++;
        int n = (int)(e - p);
        char id[SF2C_TRANSLATE_N_64_A68B];
        if (n >= (int)sizeof id || (p > src && p[-1] == SF2C_TRANSLATE_CH_EMPTY_277A)) {  // member / swizzle
            sp(o, SF2C_TRANSLATE_S_S_F03C, n, p);
            p = e;
            continue;
        }
        memcpy(id, p, n);
        id[n] = 0;
        p = e;
        if (!strcmp(id, SF2C_TRANSLATE_S_AS_TYPE_2562) && *p == SF2C_TRANSLATE_CH_EMPTY_D13B) {
            const char *gt = strchr(p, SF2C_TRANSLATE_CH_EMPTY_4DE9);
            if (!gt) { fail(c, SF2C_TRANSLATE_S_GLSL_BAD_AS_TYPE_003C); return; }
            int tn = (int)(gt - p - 1);
            if (tn == SF2C_TRANSLATE_N_5_EF2D && !strncmp(p + 1, SF2C_TRANSLATE_S_FLOAT_1E71, SF2C_TRANSLATE_N_5_EF2D)) sp(o, SF2C_TRANSLATE_S_AS_F_408D);
            else if (tn == SF2C_TRANSLATE_N_4_4B22 && !strncmp(p + 1, SF2C_TRANSLATE_S_UINT_AB5D, SF2C_TRANSLATE_N_4_4B22)) sp(o, SF2C_TRANSLATE_S_AS_U_5B6B);
            else if (tn == SF2C_TRANSLATE_N_3_4E07 && !strncmp(p + 1, SF2C_TRANSLATE_S_INT_3F2B, SF2C_TRANSLATE_N_3_4E07)) sp(o, SF2C_TRANSLATE_S_AS_I_F7AA);
            else if (tn == SF2C_TRANSLATE_N_5_EF2D && !strncmp(p + 1, SF2C_TRANSLATE_S_HALF2_8DBE, SF2C_TRANSLATE_N_5_EF2D)) sp(o, SF2C_TRANSLATE_S_UNPACK_H2_DFAF);
            else { fail(c, SF2C_TRANSLATE_S_GLSL_AS_TYPE_S_9EE6, tn, p + 1); return; }
            p = gt + 1;
            continue;
        }
        if ((!strcmp(id, SF2C_TRANSLATE_S_IN_AB12) || !strcmp(id, SF2C_TRANSLATE_S_OUT_1F30)) && *p == SF2C_TRANSLATE_CH_EMPTY_277A) {  // stage I/O struct members
            const char *m = p + 1, *me = m;
            while (id_char(*me)) me++;
            char mem[SF2C_TRANSLATE_N_32_E29C];
            snprintf(mem, sizeof mem, SF2C_TRANSLATE_S_S_F03C, (int)(me - m), m);
            if (!strcmp(mem, SF2C_TRANSLATE_S_POS_28FE)) sp(o, c->stage == STAGE_FRAGMENT ? SF2C_TRANSLATE_S_GL_FRAGCOORD_11DE : SF2C_TRANSLATE_S_GL_POSITION_D637);
            else if (!strcmp(mem, SF2C_TRANSLATE_S_PSIZE_B259)) sp(o, SF2C_TRANSLATE_S_GL_POINTSIZE_89B7);
            else sp(o, SF2C_TRANSLATE_S_S_0030, mem);  // a<i> (vertex input), v<i> (varying)
            p = me;
            continue;
        }
        if ((!strcmp(id, SF2C_TRANSLATE_S_CHAR_2B21) || !strcmp(id, SF2C_TRANSLATE_S_SHORT_45C6)) && *p == SF2C_TRANSLATE_CH_EMPTY_ACE5) { sp(o, id[0] == SF2C_TRANSLATE_CH_C_0E09 ? SF2C_TRANSLATE_S_SX8_FAF1 : SF2C_TRANSLATE_S_SX16_2603); continue; }
        if (!strcmp(id, SF2C_TRANSLATE_S_TID_5B2E) || !strcmp(id, SF2C_TRANSLATE_S_CTAID_4282)) {
            sp(o, c->stage != STAGE_COMPUTE ? SF2C_TRANSLATE_S_UVEC3_0U_F540 : id[0] == SF2C_TRANSLATE_CH_T_2AD4 ? SF2C_TRANSLATE_S_GL_LOCALINVOCATIONID_39AD : SF2C_TRANSLATE_S_GL_WORKGROUPID_34D8);
            continue;
        }
        if (id[0] == SF2C_TRANSLATE_CH_R_5BDE && n > 1 && n < SF2C_TRANSLATE_N_6_E7F6) {  // registers beyond r255 (RZ) come from out-of-range encodings
            bool num = true;
            for (int i = 1; i < n; i++) num = num && id[i] >= SF2C_TRANSLATE_CH_0_59A9 && id[i] <= SF2C_TRANSLATE_CH_9_E453;
            if (num && atoi(id + 1) > SF2C_TRANSLATE_N_255_9556) { fail(c, SF2C_TRANSLATE_S_REGISTER_S_OUT_OF_RANGE_4A99, id); return; }
        }
        const char *rep = id;
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
            if (!strcmp(id, map[i][0])) { rep = map[i][1]; break; }
        sp(o, SF2C_TRANSLATE_S_S_0030, rep);
    }
}

static const char glsl_prelude[] =
    SF2C_TRANSLATE_S_VERSION_460_N_855F
    SF2C_TRANSLATE_S_EXTENSION_GL_EXT_SHADER_EXPL_7FF6
    SF2C_TRANSLATE_S_EXTENSION_GL_KHR_SHADER_SUBG_395F
    SF2C_TRANSLATE_S_EXTENSION_GL_KHR_SHADER_SUBG_1A24
    SF2C_TRANSLATE_S_EXTENSION_GL_KHR_SHADER_SUBG_A464
    SF2C_TRANSLATE_S_EXTENSION_GL_EXT_TEXTURE_SHA_86FF
    SF2C_TRANSLATE_S_DEFINE_CBR_CB_OFF_CB_MIN_OFF_58C3
    SF2C_TRANSLATE_S_UINT_AS_U_FLOAT_X_RETURN_FLO_451C
    SF2C_TRANSLATE_S_UINT_AS_U_UINT_X_RETURN_X_N_DB46
    SF2C_TRANSLATE_S_UINT_AS_U_INT_X_RETURN_UINT__F47F
    SF2C_TRANSLATE_S_FLOAT_AS_F_UINT_X_RETURN_UIN_1E49
    SF2C_TRANSLATE_S_FLOAT_AS_F_INT_X_RETURN_INTB_C511
    SF2C_TRANSLATE_S_FLOAT_AS_F_FLOAT_X_RETURN_X__1BED
    SF2C_TRANSLATE_S_INT_AS_I_UINT_X_RETURN_INT_X_B408
    SF2C_TRANSLATE_S_INT_AS_I_FLOAT_X_RETURN_FLOA_770A
    SF2C_TRANSLATE_S_INT_SX8_UINT_V_RETURN_BITFIE_11DB
    SF2C_TRANSLATE_S_INT_SX8_INT_V_RETURN_BITFIEL_32D1
    SF2C_TRANSLATE_S_INT_SX16_UINT_V_RETURN_BITFI_8190
    SF2C_TRANSLATE_S_INT_SX16_INT_V_RETURN_BITFIE_E887
    // MSL semantics: saturate(NaN) = 0, fmin/fmax return the non-NaN operand
    SF2C_TRANSLATE_S_FLOAT_SATURATE_FLOAT_X_RETUR_7320
    SF2C_TRANSLATE_S_VEC2_SATURATE_VEC2_X_RETURN__8AC8
    SF2C_TRANSLATE_S_FLOAT_FMIN_FLOAT_A_FLOAT_B_R_F244
    SF2C_TRANSLATE_S_FLOAT_FMAX_FLOAT_A_FLOAT_B_R_5176
    SF2C_TRANSLATE_S_UINT_CLZ_UINT_V_RETURN_V_0U__749D
    SF2C_TRANSLATE_S_UINT_POPCOUNT_UINT_V_RETURN__9BBB
    SF2C_TRANSLATE_S_UINT_MULHI_UINT_A_UINT_B_UIN_797B
    SF2C_TRANSLATE_S_INT_MULHI_INT_A_INT_B_INT_HI_5038
    SF2C_TRANSLATE_S_UINT_BFE_U_UINT_V_UINT_CTL_U_EB04
    SF2C_TRANSLATE_S_UINT_BFE_S_UINT_V_UINT_CTL_U_11D3
    SF2C_TRANSLATE_S_UINT_BFI_UINT_INS_UINT_CTL_U_4CB5
    SF2C_TRANSLATE_S_FLOAT_FSWZADD_FLOAT_A_FLOAT__5155
    SF2C_TRANSLATE_S_UVEC2_RSC_UVEC2_C_VEC4_S_RET_D262
    // a scaled texture whose contents occupy its top-left part (Dynamic resolution): sample that part
    SF2C_TRANSLATE_S_VEC2_UVS_VEC2_UV_VEC4_S_RETU_2941;
// The half <-> float helpers (glsl_finish: glsl_h2f_exact or glsl_h2f_native, then glsl_prelude_half).
// Half -> float in vertex (and other pre-raster) shaders: by bits, not unpackHalf2x16. NVIDIA's driver recognises
// unpacked halves and runs some of the fp32 math after them in native fp16, differently per shader, so a depth
// pre-pass and its colour pass (EQUAL test) placed wind-animated foliage apart: black slashes / flicker (notes/PLAN.md
// 2026-10-08).
static const char glsl_h2f_exact[] =
    SF2C_TRANSLATE_S_FLOAT_H2F_UINT_H_UINT_S_H_0X_7921
    SF2C_TRANSLATE_S_IF_E_0U_FLOAT_D_FLOAT_M_5_96_3E4F
    SF2C_TRANSLATE_S_IF_E_31U_RETURN_UINTBITSTOFL_1CD7
    SF2C_TRANSLATE_S_RETURN_UINTBITSTOFLOAT_S_E_1_AD76
    SF2C_TRANSLATE_S_VEC2_UNPACK_H2_UINT_V_RETURN_B80A;
// ... and in fragment / compute shaders the native conversion: positions don't depend on them, and the by-bits
// version (~12 integer ops and two branches per half) was half of the main lighting pass's GPU time on an Intel UHD
// 770 and a third on an RTX 3080 Ti (the game's shaders use packed halves heavily). SF2RECOMP_VK_EXACT_H2F=1: by bits
// in every stage.
static const char glsl_h2f_native[] =
    SF2C_TRANSLATE_S_FLOAT_H2F_UINT_H_RETURN_UNPA_408E
    SF2C_TRANSLATE_S_VEC2_UNPACK_H2_UINT_V_RETURN_3787;
static const char glsl_prelude_half[] =
    SF2C_TRANSLATE_S_VEC2_HX_UINT_V_UINT_SWZ_IF_S_F36D
    SF2C_TRANSLATE_S_UINT_HPACK_VEC2_R_UINT_MERGE_2B49
    SF2C_TRANSLATE_S_UINT_PRMT_UINT_A_UINT_B_UINT_4BBB;

static const char *glsl_sampler_type(const ShaderTexture *t) {
    static const char *const names[][SF2C_TRANSLATE_N_2_D473] = {
        {SF2C_TRANSLATE_S_SAMPLER1D_D9D2, SF2C_TRANSLATE_S_SAMPLER1DSHADOW_80C5}, {SF2C_TRANSLATE_S_SAMPLER2D_AFC2, SF2C_TRANSLATE_S_SAMPLER2DSHADOW_E125}, {SF2C_TRANSLATE_S_SAMPLER3D_F7F5, SF2C_TRANSLATE_S_SAMPLER3D_F7F5},
        {SF2C_TRANSLATE_S_SAMPLERCUBE_5FE2, SF2C_TRANSLATE_S_SAMPLERCUBESHADOW_D7E6}, {SF2C_TRANSLATE_S_SAMPLER2DARRAY_8CF8, SF2C_TRANSLATE_S_SAMPLER2DARRAYSHADOW_4E10},
        {SF2C_TRANSLATE_S_SAMPLERCUBEARRAY_E652, SF2C_TRANSLATE_S_SAMPLERCUBEARRAYSHADOW_C2FC}, {SF2C_TRANSLATE_S_SAMPLER1DARRAY_5C2E, SF2C_TRANSLATE_S_SAMPLER1DARRAYSHADOW_5074}};
    int ty = t->type <= TEXTYPE_1D_ARRAY ? t->type : TEXTYPE_2D;
    return names[ty][t->shadow ? 1 : 0];
}

// (GLSL fragment, lazy heap chunks) Early depth / stencil tests for a shader that counts chunk misses (GMiss atomics):
// only where the tests' outcome can't depend on the shader - no discard, no depth output (a sample mask output
// isn't translated) - and the shader has no side effect of its own (stores), so whether a hidden fragment runs it is
// not observable. (Alpha to
// coverage with early tests would write depth for samples it drops: the renderer uses this shader's module without
// the execution mode for those pipelines, vulkan/exec.c fs_late.)
static bool glsl_early_tests(const Ctx *c) {
    return c->glsl && c->stage == STAGE_FRAGMENT && c->info->uses_gmem && maxwell_gmem_chunk_shift && maxwell_gmem_lazy &&
           !maxwell_gmem_fs_miss && !c->info->uses_discard && !c->omap_depth && !c->uses_gmem_store;
}

// Declarations + entry point around the (rewritten) body; mirrors the MSL assembly in maxwell_translate.
static ShaderInfo *glsl_finish(Ctx *c, bool any_branch, bool dbg) {
    ShaderInfo *info = c->info;
    const ShaderKey *key = c->key;
    int stage = c->stage;
    Str body = {0};
    glsl_rewrite(c, &body, c->body.buf ? c->body.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
    if (info->uses_gmem && (info->cbuf_mask & (SF2C_TRANSLATE_N_1U_B8E6 << GMEM_BUFFER_INDEX))) fail(c, SF2C_TRANSLATE_S_CBUF_D_CLASHES_WITH_GMEM_B11B, GMEM_BUFFER_INDEX);
    if (info->cbuf_mask >> SF2C_TRANSLATE_N_18_4EC9) fail(c, SF2C_TRANSLATE_S_CBUF_INDEX_OUT_OF_RANGE_0481);
    for (int i = 0; i < info->ntex; i++)
        if (info->tex[i].shadow && info->tex[i].type == TEXTYPE_3D) fail(c, SF2C_TRANSLATE_S_3D_SHADOW_TEXTURE_6F49);
    if (c->fail) { free(body.buf); free(c->body.buf); free(c); return info; }
    int set = stage == STAGE_FRAGMENT ? 1 : 0;
    Str s = {0};
    sp(&s, SF2C_TRANSLATE_S_VERSION_460_N_855F);  // (the prelude's first line; an extension directive has to follow it)
    if (info->uses_gmem && maxwell_gmem_chunk_shift && maxwell_gmem_lazy) sp(&s, SF2C_TRANSLATE_S_EXTENSION_GL_EXT_BUFFER_REFE_B65B);
    sp(&s, SF2C_TRANSLATE_S_S_0030, glsl_prelude + sizeof SF2C_TRANSLATE_S_VERSION_460_N_855F - 1);
    static int exact = -1;
    if (exact < 0) exact = getenv(SF2C_TRANSLATE_S_SF2RECOMP_VK_EXACT_H2F_F30C) && atoi(getenv(SF2C_TRANSLATE_S_SF2RECOMP_VK_EXACT_H2F_F30C)) != 0;
    sp(&s, SF2C_TRANSLATE_S_S_S_72BD, !exact && (stage == STAGE_FRAGMENT || stage == STAGE_COMPUTE) ? glsl_h2f_native : glsl_h2f_exact,
       glsl_prelude_half);
    for (int i = 0; i < SF2C_TRANSLATE_N_18_4EC9; i++) {
        if (!(info->cbuf_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
        uint32_t n = info->cbuf_size[i] / SF2C_TRANSLATE_N_16_B17E;
        if (c->cbuf_need[i] > n) n = c->cbuf_need[i];
        if ((info->cbuf_dynamic & (SF2C_TRANSLATE_N_1U_B8E6 << i)) && n < SF2C_TRANSLATE_N_4096_8B92) n = SF2C_TRANSLATE_N_4096_8B92;
        if (!n) n = 1;
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD140_SET_D_BINDING__583C, set, i, i, i, n);
    }
    if (info->uses_gmem && !maxwell_gmem_chunk_shift) {
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD430_SET_D_BINDING__14F9, set, GMEM_BUFFER_INDEX,
           c->uses_gmem_store ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_READONLY_A6FF);
        sp(&s, SF2C_TRANSLATE_S_DEFINE_GMR_I_GMEM_W_I_N_7F86);
        if (c->uses_gmem_store)
            sp(&s, SF2C_TRANSLATE_S_DEFINE_GMW_I_V_GMEM_W_I_V_N__4B0C
                   SF2C_TRANSLATE_S_DEFINE_GMOR_I_M_ATOMICOR_GME_2797);
    } else if (info->uses_gmem && !maxwell_gmem_lazy) {  // the heap in windows, one binding each, plain branches (shader.h)
        int n = maxwell_gmem_chunks(), ws = (int)maxwell_gmem_chunk_shift - SF2C_TRANSLATE_N_2_D473, cs = GMEM_CHUNK_SET(stage);
        for (int k = 0; k < n; k++)
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD430_SET_D_BINDING__2055, cs, GMEM_CHUNK_BINDING + k,
               c->uses_gmem_store ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_READONLY_A6FF, k, k);
        sp(&s, SF2C_TRANSLATE_S_UINT_GMR_UINT_I_UINT_C_I_DU__89AB, ws, (SF2C_TRANSLATE_N_1U_B8E6 << ws) - 1);
        for (int k = 0; k < n; k++) sp(&s, SF2C_TRANSLATE_S_IF_C_DU_RETURN_GMEM_D_W_O_N_8571, k, k);
        sp(&s, SF2C_TRANSLATE_S_RETURN_0U_N_DEFINE_GMR_I_GMR_DDF0);
        if (c->uses_gmem_store) {
            static const char *const ops[SF2C_TRANSLATE_N_3_4E07][SF2C_TRANSLATE_N_2_D473] = {{SF2C_TRANSLATE_S_GMW_3134, SF2C_TRANSLATE_S_GMEM_D_W_O_V_0531}, {SF2C_TRANSLATE_S_GMAND_8A5F, SF2C_TRANSLATE_S_ATOMICAND_GMEM_D_W_O_V_2FC7},
                                                  {SF2C_TRANSLATE_S_GMOR_70FD, SF2C_TRANSLATE_S_ATOMICOR_GMEM_D_W_O_V_159C}};
            for (int op = 0; op < SF2C_TRANSLATE_N_3_4E07; op++) {
                sp(&s, SF2C_TRANSLATE_S_VOID_S_UINT_I_UINT_V_UINT_C__1942, ops[op][0], ws, (SF2C_TRANSLATE_N_1U_B8E6 << ws) - 1);
                for (int k = 0; k < n; k++) {
                    char body[SF2C_TRANSLATE_N_64_A68B];
                    snprintf(body, sizeof body, ops[op][1], k);
                    sp(&s, SF2C_TRANSLATE_S_IF_C_DU_S_RETURN_N_5996, k, body);
                }
                sp(&s, SF2C_TRANSLATE_S_N_E09D);
            }
            sp(&s, SF2C_TRANSLATE_S_DEFINE_GMW_I_V_GMW_I_V_N_DEF_B302);
        }
    } else if (info->uses_gmem) {  // chunks imported lazily: a table of device addresses, buffer references (shader.h)
        int ws = (int)maxwell_gmem_chunk_shift - SF2C_TRANSLATE_N_2_D473, cs = GMEM_CHUNK_SET(stage);
        bool direct = stage != STAGE_COMPUTE;  // (compute: its storage buffers are in gfast_ already)
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD140_SET_D_BINDING__28E1, cs, GMEM_CHUNK_BINDING, GMEM_CHUNKS_MAX);
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_BUFFER_REFERENCE_STD4_818B, c->uses_gmem_store ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_READONLY_A6FF);
        // Misses are counted with atomics on GMiss: a side effect, which in a fragment shader turns early depth / stencil
        // tests off - every overdrawn fragment ran in full (UHD 770 main pass 13.3 ms vs 8.4). So a fragment shader
        // whose own result can't depend on late tests (no discard or depth output, no stores) declares
        // early tests (glsl_early_tests) and keeps its counter; one that can't (discard / depth, no stores)
        // counts nothing - it would cost those tests - and relies on the runtime bringing its chunks in before the draw:
        // the bound storage buffers' chunks (gmem_chunk_bind), pointers in uniform buffers, the chunks other shaders
        // missed (cache/gmem_chunks.txt). SF2RECOMP_VK_GMEM_FS_MISS=1 (maxwell_gmem_fs_miss): every shader counts, no
        // early tests (as before; to find a chunk only such a shader reaches).
        bool count = stage != STAGE_FRAGMENT || maxwell_gmem_fs_miss || c->uses_gmem_store || glsl_early_tests(c);
        if (count) {
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD430_SET_D_BINDING__E525, cs, GMEM_MISS_BINDING,
               GMEM_CHUNKS_MAX, GMEM_CHUNKS_MAX);
            sp(&s, SF2C_TRANSLATE_S_VOID_GMISS_UINT_C_UINT_I_ATO_55FF);
        } else {
            sp(&s, SF2C_TRANSLATE_S_VOID_GMISS_UINT_C_UINT_I_N_CA51);
        }
        // (gchunk[c]: x, y = device address, z = words; the first word's chunk holds the whole aligned access)
        sp(&s, SF2C_TRANSLATE_S_DEFINE_GMC_I_I_DU_N_DEFINE_G_1231, ws, (SF2C_TRANSLATE_N_1U_B8E6 << ws) - 1);
        if (direct) {  // (gd[k]: x = the chunk's first word, y = its words (0: unused), z, w = its device address; GDir<k>: it)
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_PUSH_CONSTANT_UNIFORM_595E, GMEM_DIRECT_MAX);
            for (int k = 0; k < GMEM_DIRECT_MAX; k++)
                sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD430_SET_D_BINDING__C19B, cs, GMEM_DIRECT_BINDING + k,
                   c->uses_gmem_store ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_READONLY_A6FF, k, k);
        }
        // Each access: the draw's storage buffer chunks first (gd, uniform: no table read, no miss path), then the table.
        static const struct { const char *head, *act, *miss; int last; bool store; } fn[] = {
            {SF2C_TRANSLATE_S_UINT_GMR1_UINT_I_4501, SF2C_TRANSLATE_S_RETURN_P_W_O_7524, SF2C_TRANSLATE_S_RETURN_0U_7301, 0, false},
            {SF2C_TRANSLATE_S_UVEC2_GMR2_UINT_I_0F66, SF2C_TRANSLATE_S_RETURN_UVEC2_P_W_O_P_W_O_1U_970D, SF2C_TRANSLATE_S_RETURN_UVEC2_0U_ACB7, 1, false},
            {SF2C_TRANSLATE_S_UVEC4_GMR4_UINT_I_ACC8, SF2C_TRANSLATE_S_RETURN_UVEC4_P_W_O_P_W_O_1U__E38F, SF2C_TRANSLATE_S_RETURN_UVEC4_0U_2FF6, SF2C_TRANSLATE_N_3_4E07, false},
            {SF2C_TRANSLATE_S_VOID_GMW1_UINT_I_UINT_V_034A, SF2C_TRANSLATE_S_P_W_O_V_RETURN_A1D2, SF2C_TRANSLATE_S_EMPTY_12AE, 0, true},
            {SF2C_TRANSLATE_S_VOID_GMW2_UINT_I_UVEC2_V_B7AA, SF2C_TRANSLATE_S_P_W_O_V_X_P_W_O_1U_V_Y_RETUR_120B, SF2C_TRANSLATE_S_EMPTY_12AE, 1, true},
            {SF2C_TRANSLATE_S_VOID_GMW4_UINT_I_UVEC4_V_37AC, SF2C_TRANSLATE_S_P_W_O_V_X_P_W_O_1U_V_Y_P_W_O_81AA, SF2C_TRANSLATE_S_EMPTY_12AE, SF2C_TRANSLATE_N_3_4E07, true},
            {SF2C_TRANSLATE_S_VOID_GMAND1_UINT_I_UINT_V_37A0, SF2C_TRANSLATE_S_ATOMICAND_P_W_O_V_RETURN_2541, SF2C_TRANSLATE_S_EMPTY_12AE, 0, true},
            {SF2C_TRANSLATE_S_VOID_GMOR1_UINT_I_UINT_V_3F2A, SF2C_TRANSLATE_S_ATOMICOR_P_W_O_V_RETURN_9FB1, SF2C_TRANSLATE_S_EMPTY_12AE, 0, true},
        };
        for (int f = 0; f < (int)(sizeof fn / sizeof fn[0]); f++) {
            if (fn[f].store && !c->uses_gmem_store) continue;
            sp(&s, SF2C_TRANSLATE_S_S_UINT_O_39C3, fn[f].head);
            if (direct)  // (whole chunks: an aligned access in one never leaves it; the storage buffer, not the address)
                for (int k = 0; k < GMEM_DIRECT_MAX; k++) {
                    char act[SF2C_TRANSLATE_N_192_EB3B], *d = act;
                    for (const char *a = fn[f].act; *a && d < act + sizeof act - SF2C_TRANSLATE_N_8_2C62; a++) {
                        if (a[0] == SF2C_TRANSLATE_CH_P_8463 && a[1] == SF2C_TRANSLATE_CH_EMPTY_277A) d += snprintf(d, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_S_GDIR_D_9CDD, k);
                        else *d++ = *a;
                    }
                    *d = 0;
                    sp(&s, SF2C_TRANSLATE_S_O_I_GD_D_X_IF_O_GD_D_Y_S_F136, k, k, act);
                }
            char lim[SF2C_TRANSLATE_N_16_B17E];
            if (fn[f].last) snprintf(lim, sizeof lim, SF2C_TRANSLATE_S_O_DU_9646, fn[f].last); else snprintf(lim, sizeof lim, SF2C_TRANSLATE_S_O_F83C);
            sp(&s, SF2C_TRANSLATE_S_UINT_C_GMC_I_O_GMO_I_UVEC4_E_381A
                       SF2C_TRANSLATE_S_GMISS_C_I_S_N_052B, lim, fn[f].act, fn[f].miss);
        }
        sp(&s, SF2C_TRANSLATE_S_DEFINE_GMR_I_GMR1_I_N_CAE6);
        if (c->uses_gmem_store) sp(&s, SF2C_TRANSLATE_S_DEFINE_GMW_I_V_GMW1_I_V_N_DE_A336);
    }
    if (info->uses_gmem_map) {  // (compute) the dispatch's storage buffers, device-local: gmap[i] = heap offset, size, word
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD140_SET_D_BINDING__6E71, set, GMAP_BUFFER_INDEX, GMAP_MAX + 1);
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD430_SET_D_BINDING__B9F4, set, GFAST_BUFFER_INDEX,
           c->uses_gmem_store ? SF2C_TRANSLATE_S_EMPTY_12AE : SF2C_TRANSLATE_S_READONLY_A6FF);
        sp(&s, SF2C_TRANSLATE_S_UINT_GFIND_UINT_G_UINT_N_GMA_F976
               SF2C_TRANSLATE_S_IF_G_GMAP_I_X_O_GMAP_I_Y_RET_5F4C, GMAP_MAX);
    }
    if (c->uses_rscale && stage != STAGE_VERTEX) {
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_STD140_SET_D_BINDING__0F00, set);
        info->uses_rscale = true;
    }
    for (int i = 0; i < info->ntex; i++) {
        ShaderTexture *t = &info->tex[i];
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_SET_D_BINDING_D_UNIFO_E426, set, SF2C_TRANSLATE_N_32_E29C + i, glsl_sampler_type(t), i);
        // size of level l as (width, height, depth or layers)
        switch (t->type) {
        case TEXTYPE_1D: sp(&s, SF2C_TRANSLATE_S_IVEC3_TSIZE_D_INT_L_RETURN_I_CD26, i, i); break;
        case TEXTYPE_1D_ARRAY: sp(&s, SF2C_TRANSLATE_S_IVEC3_TSIZE_D_INT_L_IVEC2_S__5320, i, i); break;
        case TEXTYPE_3D: case TEXTYPE_2D_ARRAY: case TEXTYPE_CUBE_ARRAY:
            sp(&s, SF2C_TRANSLATE_S_IVEC3_TSIZE_D_INT_L_RETURN_T_22D7, i, i); break;
        default: sp(&s, SF2C_TRANSLATE_S_IVEC3_TSIZE_D_INT_L_RETURN_I_403D, i, i);
        }
    }
    if (stage == STAGE_VERTEX) {
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++) {
            if (!(c->in_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
            int kind = key ? key->attr_kind[i] : 0;
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_LOCATION_D_IN_S_A_D_N_17D0, i, kind == 1 ? SF2C_TRANSLATE_S_IVEC4_F360 : kind == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_UVEC4_1DCC : SF2C_TRANSLATE_S_VEC4_B997, i);
        }
        // depth pre-pass + EQUAL test needs identical positions from different shaders: invariant, and precise so no
        // compiler fuses or reorders the math feeding it (MSL gets this from preserveInvariance; without it foliage
        // drawn twice flickered black)
        sp(&s, SF2C_TRANSLATE_S_INVARIANT_GL_POSITION_NPRECI_5DF6);
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++)
            if (c->out_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) sp(&s, SF2C_TRANSLATE_S_LAYOUT_LOCATION_D_OUT_VEC4_V_D77B, i, i);
    } else if (stage == STAGE_FRAGMENT) {
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++) {
            if (!(c->in_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
            const char *interp = (c->flat_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) ? SF2C_TRANSLATE_S_FLAT_0B45 : (c->noperspective_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) ? SF2C_TRANSLATE_S_NOPERSPECTIVE_7719 : SF2C_TRANSLATE_S_EMPTY_12AE;
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_LOCATION_D_SIN_VEC4_V_9A5E, i, interp, i);
        }
        for (int rt = 0; rt < SF2C_TRANSLATE_N_8_2C62; rt++) {
            if (!((c->omap_target >> (rt * SF2C_TRANSLATE_N_4_4B22)) & SF2C_TRANSLATE_N_0XF_7419)) continue;
            int kind = key ? key->rt_kind[rt] : 0;
            sp(&s, SF2C_TRANSLATE_S_LAYOUT_LOCATION_D_OUT_S_C_D__AE48, rt, kind == 1 ? SF2C_TRANSLATE_S_IVEC4_F360 : kind == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_UVEC4_1DCC : SF2C_TRANSLATE_S_VEC4_B997, rt);
        }
        if (c->omap_depth) info->writes_depth = true;
        if (glsl_early_tests(c)) sp(&s, SF2C_TRANSLATE_S_LAYOUT_EARLY_FRAGMENT_TESTS__9439);
    } else {
        sp(&s, SF2C_TRANSLATE_S_LAYOUT_LOCAL_SIZE_X_ID_0_LOC_E1FE);
    }
    sp(&s, SF2C_TRANSLATE_S_VOID_MAIN_N_6682);
    if (stage == STAGE_VERTEX) {
        sp(&s, SF2C_TRANSLATE_S_GL_POSITION_VEC4_0_0_0_1_N_2CEE);
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++)
            if (c->out_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) sp(&s, SF2C_TRANSLATE_S_V_D_VEC4_0_0_0_1_N_21D2, i);
        if (info->writes_point_size) sp(&s, SF2C_TRANSLATE_S_GL_POINTSIZE_0_0F_N_2296);
    }
    for (int i = 0; i < SF2C_TRANSLATE_N_256_51E8; i++)
        if (c->reg_used[i]) sp(&s, SF2C_TRANSLATE_S_UINT_R_D_0U_N_AD6E, i);
    for (int i = 0; i < SF2C_TRANSLATE_N_7_7902; i++)
        if (c->pred_used[i]) sp(&s, SF2C_TRANSLATE_S_BOOL_P_D_FALSE_N_E8FB, i);
    if (c->uses_cc) sp(&s, SF2C_TRANSLATE_S_UINT_CC_0U_BOOL_CCZ_FALSE_CC_B7E2);
    if (c->uses_local) sp(&s, SF2C_TRANSLATE_S_UINT_LMEM_D_FOR_INT_LI_0_LI__2F01, LMEM_WORDS, LMEM_WORDS);
    if (dbg) sp(&s, maxwell_dbg_pc == SF2C_TRANSLATE_N_0XFFFFFFFFU_7BAF ? SF2C_TRANSLATE_S_FLOAT_DBGV_1_0F_N_408F : SF2C_TRANSLATE_S_FLOAT_DBGV_1_0F_N_D6D5);
    if (any_branch) {
        sp(&s, SF2C_TRANSLATE_S_UINT_PC_8U_UINT_SSY_16_UINT__AB65);
        sp(&s, SF2C_TRANSLATE_S_BOOL_EXIT_NOW_FALSE_N_B527);
        sp(&s, SF2C_TRANSLATE_S_FOR_UINT_GUARD_0U_GUARD_1000_3290);
        sp(&s, SF2C_TRANSLATE_S_S_0030, body.buf ? body.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
        sp(&s, SF2C_TRANSLATE_S_EXIT_NOW_TRUE_BREAK_N_DEFAUL_FA01);
    } else {
        sp(&s, SF2C_TRANSLATE_S_DO_N_S_WHILE_FALSE_N_C821, body.buf ? body.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
    }
    free(body.buf);
    if (stage == STAGE_VERTEX) {
        if (key && key->flip_y) sp(&s, SF2C_TRANSLATE_S_GL_POSITION_Y_GL_POSITION_Y__B224);
        if (key && key->depth_minus_one) sp(&s, SF2C_TRANSLATE_S_GL_POSITION_Z_GL_POSITION_Z__945F);
    } else if (stage == STAGE_FRAGMENT) {
        int r = 0;
        for (int rt = 0; rt < SF2C_TRANSLATE_N_8_2C62; rt++) {
            uint32_t m = (c->omap_target >> (rt * SF2C_TRANSLATE_N_4_4B22)) & SF2C_TRANSLATE_N_0XF_7419;
            if (!m) continue;
            int kind = key ? key->rt_kind[rt] : 0;
            const char *cv = kind == 1 ? SF2C_TRANSLATE_S_INT_3F2B : kind == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_UINT_AB5D : SF2C_TRANSLATE_S_EMPTY_12AE;
            for (int comp = 0; comp < SF2C_TRANSLATE_N_4_4B22; comp++) {
                if (!(m & (SF2C_TRANSLATE_N_1U_B8E6 << comp))) continue;
                if (dbg) {
                    if (comp == SF2C_TRANSLATE_N_3_4E07) sp(&s, SF2C_TRANSLATE_S_C_D_D_S_1_0F_N_427B, rt, comp, cv);
                    else sp(&s, SF2C_TRANSLATE_S_C_D_D_S_DBGV_N_644D, rt, comp, cv);
                } else if (r < SF2C_TRANSLATE_N_256_51E8 && c->reg_used[r]) {
                    if (kind == 0) sp(&s, SF2C_TRANSLATE_S_C_D_D_AS_F_R_D_N_4422, rt, comp, r);
                    else sp(&s, SF2C_TRANSLATE_S_C_D_D_S_R_D_N_E766, rt, comp, cv, r);
                } else {
                    sp(&s, SF2C_TRANSLATE_S_C_D_D_0_N_141C, rt, comp);
                }
                r++;
            }
        }
        if (c->omap_depth) {
            if (c->reg_used[r + 1]) sp(&s, SF2C_TRANSLATE_S_GL_FRAGDEPTH_AS_F_R_D_N_9A26, r + 1);
            else sp(&s, SF2C_TRANSLATE_S_GL_FRAGDEPTH_GL_FRAGCOORD_Z__0F18);
        }
    }
    sp(&s, SF2C_TRANSLATE_S_N_E09D);
    info->msl = s.buf;
    info->attr_mask = c->in_attr_mask;
    info->out_mask = stage == STAGE_FRAGMENT ? c->omap_target : c->out_attr_mask;
    free(c->body.buf);
    free(c);
    return info;
}

// BRX (indirect branch, Vulkan / GLSL only): label just the targets its jump table can name. A BRX used to make every
// instruction a case of the dispatcher switch (any one may be a target): hundreds of cases falling into each other
// inside the dispatcher loop. Suspected of what AMD's Windows driver never finishes building (issue #237: the dialogue
// body text's vertex shader stayed "building", its draws skipped - Tarin's name showed, the message stayed blank).
// The compiler puts the table in the program's own constants (cb1, after the code: blob + const_offset), loaded into
// the BRX register by an LDC c[0x1][Rx + table] shortly before: BRX Ra, imm goes to pc + 8 + table[k] + imm. Entries
// are read until one isn't an instruction of this shader (the end of the table; a word past it that still looks like
// one only adds a harmless case). False: not that shape (the register set some other way, no constants, nothing
// valid) - the caller labels every instruction as before. SF2RECOMP_BRX_ALL_LABELS=1: always every instruction.
static bool brx_table_labels(const uint8_t *code, size_t i, size_t end, const uint8_t *blob, size_t const_offset, uint8_t *label) {
    static int all = -1;
    if (all < 0) { const char *e = getenv(SF2C_TRANSLATE_S_SF2RECOMP_BRX_ALL_LABELS_13D9); all = e && atoi(e) != 0; }
    if (all || const_offset >= SF2C_TRANSLATE_N_0X10000_4939 || const_offset < SF2C_TRANSLATE_N_0X80_D41C) return false;  // (0x10000: no constants known)
    uint64_t brx = ((const uint64_t *)code)[i];
    int ra = (int)bits(brx, SF2C_TRANSLATE_N_8_2C62, SF2C_TRANSLATE_N_8_2C62);
    int64_t imm = sbits(brx, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235);
    if (ra == RZ) return false;
    // the instruction that set ra: an LDC from cb1 within the few before, nothing else writing ra (or a branch) between
    int64_t table = -1;
    for (size_t k = i, seen = 0; k-- > 0 && seen < SF2C_TRANSLATE_N_32_E29C;) {
        if (k % SF2C_TRANSLATE_N_4_4B22 == 0) continue;  // (scheduling words)
        seen++;
        uint64_t w = ((const uint64_t *)code)[k];
        if ((w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) >= SF2C_TRANSLATE_N_0XE200000000000000ULL_E437 && (w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) <= SF2C_TRANSLATE_N_0XE350000000000000ULL_C06F)
            return false;  // flow control between: not straight-line
        if ((w & SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) == SF2C_TRANSLATE_N_0XEF90000000000000ULL_E5EB && (int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62) == ra) {  // LDC ra, c[idx][Rx + off]
            if (bits(w, SF2C_TRANSLATE_N_36_76A5, SF2C_TRANSLATE_N_5_EF2D) != 1 || bits(w, SF2C_TRANSLATE_N_48_9801, SF2C_TRANSLATE_N_3_4E07) != SF2C_TRANSLATE_N_4_4B22) return false;  // (cb1, 32-bit)
            table = (int64_t)bits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_16_B17E);
            break;
        }
        if ((int)bits(w, 0, SF2C_TRANSLATE_N_8_2C62) == ra) return false;  // (something else may write ra: unknown)
    }
    if (table < 0) return false;
    const uint8_t *tab = blob + const_offset + table;
    uint8_t *mark = calloc(end + SF2C_TRANSLATE_N_4_4B22, 1);
    int n = 0;
    for (int k = 0; k < SF2C_TRANSLATE_N_256_51E8; k++) {
        int32_t v;
        memcpy(&v, tab + SF2C_TRANSLATE_N_4_4B22 * k, SF2C_TRANSLATE_N_4_4B22);
        int64_t t = (int64_t)i * SF2C_TRANSLATE_N_8_2C62 + SF2C_TRANSLATE_N_8_2C62 + v + imm;
        if (t < SF2C_TRANSLATE_N_8_2C62 || (t & SF2C_TRANSLATE_N_7_7902) || (size_t)(t / SF2C_TRANSLATE_N_8_2C62) >= end) break;
        t = norm_target(t);  // (as the BRX itself does at run time)
        if ((size_t)(t / SF2C_TRANSLATE_N_8_2C62) >= end) break;
        mark[t / SF2C_TRANSLATE_N_8_2C62] = 1;
        n++;
    }
    if (n) for (size_t k = 0; k < end; k++) label[k] |= mark[k];
    free(mark);
    return n > 0;
}

ShaderInfo *maxwell_translate(const uint8_t *blob, size_t max_size, int stage, const ShaderKey *key) {
    ShaderInfo *info = calloc(1, sizeof *info);
    info->stage = stage;
    Ctx *c = calloc(1, sizeof *c);
    c->info = info;
    c->key = key;
    c->stage = stage;
    c->ipa_mode = -1;
    c->glsl = key && key->glsl;
    const uint8_t *sph = blob + SF2C_TRANSLATE_N_0X30_8BAD;
    const uint8_t *code = blob + SF2C_TRANSLATE_N_0X80_D41C;
    // compute programs have neither the 0x30-byte NVN header nor an SPH: code starts at the blob
    if (stage == STAGE_COMPUTE) code = blob;
    if (stage == STAGE_FRAGMENT) {
        c->omap_target = *(const uint32_t *)(sph + SF2C_TRANSLATE_N_0X48_0EF0);
        c->omap_depth = (*(const uint32_t *)(sph + SF2C_TRANSLATE_N_0X4C_CEDB) >> 1) & 1;
    }
    // find the end: the last EXIT followed by self-branch padding, or max_size
    size_t n = (max_size - (code - blob)) / SF2C_TRANSLATE_N_8_2C62;
    if (n > SF2C_TRANSLATE_N_65536_0F25) n = SF2C_TRANSLATE_N_65536_0F25;
    size_t end = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t w = ((const uint64_t *)code)[i];
        if (i % SF2C_TRANSLATE_N_4_4B22 == 0) continue;
        if ((w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) == SF2C_TRANSLATE_N_0XE240000000000000ULL_BCB5 && sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235) == -SF2C_TRANSLATE_N_8_2C62 && end) break;  // bra self
        if (w == 0 && i > SF2C_TRANSLATE_N_4_4B22) break;
        end = i + 1;
    }
    // collect branch targets (labels)
    uint8_t *label = calloc(end + SF2C_TRANSLATE_N_4_4B22, 1);
    label[1] = 1;
    bool any_branch = false;
    for (size_t i = 0; i < end; i++) {
        if (i % SF2C_TRANSLATE_N_4_4B22 == 0) continue;
        uint64_t w = ((const uint64_t *)code)[i];
        uint32_t pc = (uint32_t)(i * SF2C_TRANSLATE_N_8_2C62);
        bool is_flow = (w & SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B) == SF2C_TRANSLATE_N_0XE240000000000000ULL_BCB5 ||
                       (w & SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B) == SF2C_TRANSLATE_N_0XE290000000000000ULL_9928 ||
                       (w & SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B) == SF2C_TRANSLATE_N_0XE2A0000000000000ULL_101C ||
                       (w & SF2C_TRANSLATE_N_0XFFF0000000000020ULL_950B) == SF2C_TRANSLATE_N_0XE2B0000000000000ULL_4E82;
        if (is_flow) {
            int64_t t = norm_target((int64_t)pc + SF2C_TRANSLATE_N_8_2C62 + sbits(w, SF2C_TRANSLATE_N_20_F5CA, SF2C_TRANSLATE_N_24_C235));
            if (t >= 0 && (size_t)(t / SF2C_TRANSLATE_N_8_2C62) < end) label[t / SF2C_TRANSLATE_N_8_2C62] = 1;
            any_branch = true;
        }
        if ((w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) == SF2C_TRANSLATE_N_0XE250000000000000ULL_2683) {  // BRX: its jump table's targets, else any instruction
            any_branch = true;
            if (!c->glsl || !brx_table_labels(code, i, end, blob, max_size, label)) memset(label, 1, end);
        }
        if ((w & SF2C_TRANSLATE_N_0XFFF8000000000000ULL_9A50) == SF2C_TRANSLATE_N_0XF0F8000000000000ULL_12C2 || (w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) == SF2C_TRANSLATE_N_0XE340000000000000ULL_9D8A ||
            (w & SF2C_TRANSLATE_N_0XFFF0000000000000ULL_8D1D) == SF2C_TRANSLATE_N_0XE350000000000000ULL_C06F)
            any_branch = true;
        (void)is_branch_target_op;
    }
    // translate
    bool dbg = maxwell_dbg_code && blob == maxwell_dbg_code && stage == STAGE_FRAGMENT;
    static int annotate = -1;
    if (annotate < 0) annotate = getenv(SF2C_TRANSLATE_S_SF2RECOMP_SAVE_MSL_AF49) != NULL;
    c->branchy = any_branch;
    for (size_t i = 0; i < end && !c->fail; i++) {
        if (i % SF2C_TRANSLATE_N_4_4B22 == 0) continue;
        uint32_t pc = (uint32_t)(i * SF2C_TRANSLATE_N_8_2C62);
        if (any_branch && label[i]) sp(&c->body, SF2C_TRANSLATE_S_CASE_UU_N_6520, pc);
        if (annotate) sp(&c->body, SF2C_TRANSLATE_S_PC_X_016LLX_N_EAB4, pc, (unsigned long long)((const uint64_t *)code)[i]);
        emit_insn(c, ((const uint64_t *)code)[i], pc);
        if (dbg && pc == maxwell_dbg_pc) {
            int rr = maxwell_dbg_reg % SF2C_TRANSLATE_N_1000_4051, half = maxwell_dbg_reg / SF2C_TRANSLATE_N_1000_4051;  // +1000: low f16, +2000: high f16
            c->reg_used[rr] = 1;
            if (half) sp(&c->body, c->glsl ? SF2C_TRANSLATE_S_DBGV_UNPACKHALF2X16_R_D_D_N_E3AA : SF2C_TRANSLATE_S_DBGV_FLOAT_AS_TYPE_HALF2_R_D_075C, rr, half - 1);
            else sp(&c->body, SF2C_TRANSLATE_S_DBGV_AS_TYPE_FLOAT_R_D_N_F34A, rr);
        }
        info->num_insts++;
    }
    free(label);
    // (MSL: more textures than sampler slots take a slot table, at a buffer index the vertex stage's streams use)
    if (!c->glsl && stage == STAGE_VERTEX && info->ntex > MSL_MAX_SAMPLERS)
        fail(c, SF2C_TRANSLATE_S_D_TEXTURES_IN_A_VERTEX_SHADE_F0BA, info->ntex, MSL_MAX_SAMPLERS);
    if (c->fail) { free(c->body.buf); free(c); return info; }
    if (c->glsl) return glsl_finish(c, any_branch, dbg);

    // ---------------------------------------------------------- MSL source
    Str s = {0};
    sp(&s, SF2C_TRANSLATE_S_INCLUDE_METAL_STDLIB_NUSING__7047);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_CBR_CONST_FEE3);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_BFE_U_UIN_2A77);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_BFE_S_UIN_8C14);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_BFI_UINT__8E7E);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_FLOAT_FSWZADD__BB40);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT2_RSC_UINT_0A18);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_FLOAT2_UVS_FLO_1721);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_FLOAT2_HX_UINT_0A2F);
    // merges on half2 lanes rather than with integer masks: on an M1 (macOS 15.0.1) the Metal compiler miscompiled
    // (old & 0xFFFF0000u) | lo in Link's body shader - the merged low half read back as a constant 512.0 and Link
    // rendered white. Same bits either way.
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_HPACK_FLO_5EEA);
    sp(&s, SF2C_TRANSLATE_S_STATIC_INLINE_UINT_PRMT_UINT_F4CE);

    // inputs/outputs
    if (stage == STAGE_VERTEX) {
        sp(&s, SF2C_TRANSLATE_S_STRUCT_VIN_N_4DA0);
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++) {
            if (!(c->in_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
            int kind = key ? key->attr_kind[i] : 0;
            sp(&s, SF2C_TRANSLATE_S_S4_A_D_ATTRIBUTE_D_N_BF25, kind == 1 ? SF2C_TRANSLATE_S_INT_3F2B : kind == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_UINT_AB5D : SF2C_TRANSLATE_S_FLOAT_1E71, i, i);
        }
        sp(&s, SF2C_TRANSLATE_S_N_B7AE);
        sp(&s, SF2C_TRANSLATE_S_STRUCT_VOUT_N_FLOAT4_POS_POS_4BDB);  // depth pre-pass + EQUAL test needs identical positions
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++)
            if (c->out_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) sp(&s, SF2C_TRANSLATE_S_FLOAT4_V_D_USER_LOCN_D_N_C020, i, i);
        if (info->writes_point_size) sp(&s, SF2C_TRANSLATE_S_FLOAT_PSIZE_POINT_SIZE_N_EB62);
        sp(&s, SF2C_TRANSLATE_S_N_B7AE);
    } else if (stage == STAGE_FRAGMENT) {
        sp(&s, SF2C_TRANSLATE_S_STRUCT_FIN_N_FLOAT4_POS_POSI_1C3B);
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++) {
            if (!(c->in_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i))) continue;
            const char *interp = (c->flat_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) ? SF2C_TRANSLATE_S_FLAT_9BC1 : (c->noperspective_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) ? SF2C_TRANSLATE_S_CENTER_NO_PERSPECTIVE_6845 : SF2C_TRANSLATE_S_EMPTY_12AE;
            sp(&s, SF2C_TRANSLATE_S_FLOAT4_V_D_USER_LOCN_D_S_N_E052, i, i, interp);
        }
        sp(&s, SF2C_TRANSLATE_S_N_B7AE);
        sp(&s, SF2C_TRANSLATE_S_STRUCT_FOUT_N_AA83);
        for (int rt = 0; rt < SF2C_TRANSLATE_N_8_2C62; rt++) {
            if (!((c->omap_target >> (rt * SF2C_TRANSLATE_N_4_4B22)) & SF2C_TRANSLATE_N_0XF_7419)) continue;
            int kind = key ? key->rt_kind[rt] : 0;
            sp(&s, SF2C_TRANSLATE_S_S4_C_D_COLOR_D_N_C9D1, kind == 1 ? SF2C_TRANSLATE_S_INT_3F2B : kind == SF2C_TRANSLATE_N_2_D473 ? SF2C_TRANSLATE_S_UINT_AB5D : SF2C_TRANSLATE_S_FLOAT_1E71, rt, rt);
        }
        if (c->omap_depth) { sp(&s, SF2C_TRANSLATE_S_FLOAT_DEPTH_DEPTH_ANY_N_45AE); info->writes_depth = true; }
        sp(&s, SF2C_TRANSLATE_S_N_B7AE);
    }

    // entry point
    Str args = {0};
    for (int i = 0; i < SF2C_TRANSLATE_N_18_4EC9; i++)
        if (info->cbuf_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) sp(&args, SF2C_TRANSLATE_S_CONSTANT_UINT4_CB_D_BUFFER_D_DCF0, i, i);
    if (info->uses_gmem) {
        if (info->cbuf_mask & (SF2C_TRANSLATE_N_1U_B8E6 << GMEM_BUFFER_INDEX)) {
            free(args.buf);
            args = (Str){0};  // (was a use after free + double free of args.buf)
            fail(c, SF2C_TRANSLATE_S_CBUF_D_CLASHES_WITH_GMEM_B11B, GMEM_BUFFER_INDEX);
        }
        sp(&args, SF2C_TRANSLATE_S_DEVICE_UCHAR_GMEM_BUFFER_D_48BC, GMEM_BUFFER_INDEX);
    }
    if (c->uses_rscale && stage != STAGE_VERTEX) {
        sp(&args, SF2C_TRANSLATE_S_CONSTANT_FLOAT4_RSCALE_BUFFE_6BC6);
        info->uses_rscale = true;
    }
    // Metal has 16 sampler slots a stage (textures: 31). Up to 16 textures, texture i samples with sampler i. With more
    // (issue #215: a cave floor's fragment shader with 17), the samplers come as one array of the 16 slots and a table
    // (buffer SMAP_BUFFER_INDEX) names each texture's slot: the renderer puts each distinct sampler state the draw uses
    // in a slot of its own, so every texture keeps its own filtering and wrapping (renderer.m sampler_slots).
    bool smap = info->ntex > MSL_MAX_SAMPLERS;  // (not in vertex shaders: refused above)
    if (smap) sp(&args, SF2C_TRANSLATE_S_CONSTANT_UINT_SMAP_BUFFER_D__97F5, SMAP_BUFFER_INDEX,
                 MSL_MAX_SAMPLERS);
    for (int i = 0; i < info->ntex; i++) {
        ShaderTexture *t = &info->tex[i];
        const char *tn = tex_type_name(t->type);
        sp(&args, SF2C_TRANSLATE_S_S_S_FLOAT_T_D_TEXTURE_D_9A60, t->shadow ? SF2C_TRANSLATE_S_DEPTH_2B15 : SF2C_TRANSLATE_S_TEXTURE_8BB2, tn, i, i);
        if (!smap) sp(&args, SF2C_TRANSLATE_S_SAMPLER_S_D_SAMPLER_D_1F3D, i, i);
    }
    if (stage == STAGE_VERTEX) {
        sp(&s, SF2C_TRANSLATE_S_VERTEX_VOUT_MAIN0_VIN_IN_STA_16DB,
           (maxwell_dbg_vs && blob == maxwell_dbg_vs) ? SF2C_TRANSLATE_S_DEVICE_FLOAT4_DBGOUT_BUFFER__7A02 : SF2C_TRANSLATE_S_EMPTY_12AE,
           args.buf ? args.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
        sp(&s, SF2C_TRANSLATE_S_VOUT_OUT_N_OUT_POS_FLOAT4_0__7810);
        for (int i = 0; i < SF2C_TRANSLATE_N_32_E29C; i++)
            if (c->out_attr_mask & (SF2C_TRANSLATE_N_1U_B8E6 << i)) sp(&s, SF2C_TRANSLATE_S_OUT_V_D_FLOAT4_0_0_0_1_N_BE57, i);
    } else if (stage == STAGE_FRAGMENT) {
        sp(&s, SF2C_TRANSLATE_S_FRAGMENT_FOUT_MAIN0_FIN_IN_S_52C5,
           c->uses_lane ? SF2C_TRANSLATE_S_UINT_LANE_THREAD_INDEX_IN_SI_9F88 : SF2C_TRANSLATE_S_EMPTY_12AE, args.buf ? args.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
    } else {
        sp(&s, SF2C_TRANSLATE_S_KERNEL_VOID_MAIN0_UINT3_TID__DE59,
           args.buf ? args.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
    }
    free(args.buf);
    if (smap)
        for (int i = 0; i < info->ntex; i++) sp(&s, SF2C_TRANSLATE_S_SAMPLER_S_D_SS_SMAP_D_N_19C3, i, i);
    for (int i = 0; i < SF2C_TRANSLATE_N_256_51E8; i++)
        if (c->reg_used[i]) sp(&s, SF2C_TRANSLATE_S_UINT_R_D_0_N_B3A2, i);
    for (int i = 0; i < SF2C_TRANSLATE_N_7_7902; i++)
        if (c->pred_used[i]) sp(&s, SF2C_TRANSLATE_S_BOOL_P_D_FALSE_N_E8FB, i);
    if (c->uses_cc) sp(&s, SF2C_TRANSLATE_S_UINT_CC_0_BOOL_CCZ_FALSE_CCS_9822);
    if (c->uses_local) sp(&s, SF2C_TRANSLATE_S_UINT_LMEM_D_N_C023, LMEM_WORDS);
    if (dbg) sp(&s, maxwell_dbg_pc == SF2C_TRANSLATE_N_0XFFFFFFFFU_7BAF ? SF2C_TRANSLATE_S_FLOAT_DBGV_1_0F_N_408F : SF2C_TRANSLATE_S_FLOAT_DBGV_1_0F_N_D6D5);
    if (any_branch) {
        sp(&s, SF2C_TRANSLATE_S_UINT_PC_8U_UINT_SSY_16_UINT__0910);
        sp(&s, SF2C_TRANSLATE_S_BOOL_EXIT_NOW_FALSE_N_B527);
        sp(&s, SF2C_TRANSLATE_S_FOR_UINT_GUARD_0_GUARD_10000_40F8);
        sp(&s, SF2C_TRANSLATE_S_S_0030, c->body.buf ? c->body.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
        sp(&s, SF2C_TRANSLATE_S_EXIT_NOW_TRUE_BREAK_N_DEFAUL_FA01);
    } else {
        sp(&s, SF2C_TRANSLATE_S_DO_N_S_WHILE_FALSE_N_C821, c->body.buf ? c->body.buf : SF2C_TRANSLATE_S_EMPTY_12AE);
    }
    if (stage == STAGE_VERTEX) {
        if (key && key->flip_y) sp(&s, SF2C_TRANSLATE_S_OUT_POS_Y_OUT_POS_Y_N_D333);
        if (key && key->depth_minus_one) sp(&s, SF2C_TRANSLATE_S_OUT_POS_Z_OUT_POS_Z_OUT_POS__2739);
        if (maxwell_dbg_vs && blob == maxwell_dbg_vs) sp(&s, SF2C_TRANSLATE_S_IF_IID_64U_VID_8U_DBGOUT_IID_288D, (c->out_attr_mask & SF2C_TRANSLATE_N_2U_8C70) ? SF2C_TRANSLATE_S_OUT_V1_E751 : SF2C_TRANSLATE_S_FLOAT4_0_A9E5);
        sp(&s, SF2C_TRANSLATE_S_RETURN_OUT_N_N_DB2C);
    } else if (stage == STAGE_FRAGMENT) {
        sp(&s, SF2C_TRANSLATE_S_FOUT_O_N_9F3E);
        int r = 0;
        for (int rt = 0; rt < SF2C_TRANSLATE_N_8_2C62; rt++) {
            uint32_t m = (c->omap_target >> (rt * SF2C_TRANSLATE_N_4_4B22)) & SF2C_TRANSLATE_N_0XF_7419;
            if (!m) continue;
            int kind = key ? key->rt_kind[rt] : 0;
            for (int comp = 0; comp < SF2C_TRANSLATE_N_4_4B22; comp++) {
                if (!(m & (SF2C_TRANSLATE_N_1U_B8E6 << comp))) continue;
                if (dbg) {
                    sp(&s, comp == SF2C_TRANSLATE_N_3_4E07 ? SF2C_TRANSLATE_S_O_C_D_D_1_0F_N_FC02 : SF2C_TRANSLATE_S_O_C_D_D_DBGV_N_05D7, rt, comp);
                } else if (r < SF2C_TRANSLATE_N_256_51E8 && c->reg_used[r]) {
                    if (kind == 0) sp(&s, SF2C_TRANSLATE_S_O_C_D_D_AS_TYPE_FLOAT_R_D_N_A050, rt, comp, r);
                    else sp(&s, SF2C_TRANSLATE_S_O_C_D_D_R_D_N_65F4, rt, comp, r);
                } else {
                    sp(&s, SF2C_TRANSLATE_S_O_C_D_D_0_N_60DB, rt, comp);
                }
                r++;
            }
        }
        if (c->omap_depth) {
            if (c->reg_used[r + 1]) sp(&s, SF2C_TRANSLATE_S_O_DEPTH_AS_TYPE_FLOAT_R_D_N_95E7, r + 1);
            else sp(&s, SF2C_TRANSLATE_S_O_DEPTH_IN_POS_Z_N_C7F1);
        }
        sp(&s, SF2C_TRANSLATE_S_RETURN_O_N_N_1E6D);
    } else {
        sp(&s, SF2C_TRANSLATE_S_RETURN_N_N_B7DA);
    }
    info->msl = s.buf;
    info->attr_mask = c->in_attr_mask;
    info->out_mask = stage == STAGE_FRAGMENT ? c->omap_target : c->out_attr_mask;
    free(c->body.buf);
    free(c);
    return info;
}

void maxwell_free(ShaderInfo *s) {
    if (!s) return;
    free(s->msl);
    free(s);
}

void maxwell_disassemble(const uint8_t *code, size_t max_size, int stage, void *file) {
    (void)code; (void)max_size; (void)stage; (void)file;
}

"""Portable C lowering of AArch64 FP / AdvSIMD / crypto / CRC32 instructions (lifter --simd=c).

Every encoding is decoded here at lift time and turned into C over the Cpu struct, calling the static
inline helpers in runtime/simd.h with constant fields (sizes, lanes, ops), so clang folds the helpers'
switches. Covers ARMv8.0-A + Crypto + CRC32 (the Switch's Cortex-A57); unallocated or later-architecture
encodings (FP16 arithmetic, RDM, dot product, FCMA, JSCVT, ...) raise Unsupported.
"""
from _sf2_recompiler_constants import SIMD_C as K
from lifter import COND_NAMES, Unsupported, W, X, XS, bits, extend_expr, hx, set_reg, sext


def lop(name, u=0):
    """C name of a lane op (enum in runtime/simd.h), with the unsigned flag."""
    return f'LOP_{name}' + (K.S_LOP_U_4364 if u else K.S_EMPTY_6F49)


def vu(sz):
    return (K.S_VU8_4458, K.S_VU16_45C2, K.S_VU32_8CE2, K.S_VU64_EED5)[sz]


def vs(sz):
    return (K.S_VS8_16DF, K.S_VS16_6AB4, K.S_VS32_8097, K.S_VS64_C017)[sz]


def ut(sz):
    return (K.S_UINT8_T_85DD, K.S_UINT16_T_A730, K.S_UINT32_T_595B, K.S_UINT64_T_0BAB)[sz]


def VR(T, n):
    return f'VREG({T}, {n})'


def VQ(T, Q, n):
    return f'VREG({T}, {n})' if Q else f'VQ({T}, 0, {n})'


def VWr(T, Q, d, e):
    return f'VW({T}, {Q}, {d}, {e});'


def cond(c):
    return f'COND_{COND_NAMES[c].upper()}(c)' if c < K.N_14_8527 else K.S_1_9A76


def gen(op, sz, ne, d, szd, offd, keep, n, szn, offn, m=-1, szm=0, offm=0, bidx=-1, bimm=0):
    return (f'sl_gen(c, {op}, {sz}, {ne}, {d}, {szd}, {offd}, {keep}, {n}, {szn}, {offn}, {m}, {szm}, {offm}, '
            f'{bidx}, {hx(bimm)});')


def same(op, sz, ne, d, n, m=-1, bimm=0, bidx=-1):
    """Lane-wise op, all operands the same size."""
    return gen(op, sz, ne, d, sz, 0, 0, n, sz, 0, m, sz, 0, bidx, bimm)


def lanes(Q, sz):
    return (K.N_8_2C62 << Q) >> sz


def shuf(T, a, b, idx):
    return f'SHUF({a}, {b}, {K.S_EMPTY_3C01.join(str(i) for i in idx)})'


def u128(v):
    lo, hi = v & ((1 << K.N_64_A68B) - 1), v >> K.N_64_A68B
    return f'((u128){hx(hi)} << 64 | {hx(lo)})' if hi else hx(lo)


FT = {0: (K.S_S_ECE4, K.S_VF32_58A3, K.S_VU32_8CE2, K.S_FLOAT_D741, K.S_VSF32_617E, K.S_VS32_F929, K.S_FP_SETS_30DD),
      1: (K.S_D_8F5F, K.S_VF64_36EA, K.S_VU64_EED5, K.S_DOUBLE_EC41, K.S_VSF64_4E0B, K.S_VS64_147B, K.S_FP_SETD_EBD7)}

FOP3 = {  # vector three-same FP: (U, a, opcode) -> FOP
    (0, 0, K.N_24_C235): K.S_FOP_MAXNM_3B83, (0, 0, K.N_26_5F9C): K.S_FOP_ADD_468A, (0, 0, K.N_27_6706): K.S_FOP_MULX_B0F6,
    (0, 0, K.N_30_624B): K.S_FOP_MAX_1A5E, (0, 0, K.N_31_EB1E): K.S_FOP_RECPS_5F92,
    (0, 1, K.N_24_C235): K.S_FOP_MINNM_047C, (0, 1, K.N_26_5F9C): K.S_FOP_SUB_DB54, (0, 1, K.N_30_624B): K.S_FOP_MIN_EC9D, (0, 1, K.N_31_EB1E): K.S_FOP_RSQRTS_5F26,
    (1, 0, K.N_27_6706): K.S_FOP_MUL_8D62, (1, 0, K.N_31_EB1E): K.S_FOP_DIV_2A67, (1, 1, K.N_26_5F9C): K.S_FOP_ABD_C27B,
}
FPAIRV = {(1, 0, K.N_24_C235): K.S_FOP_MAXNM_3B83, (1, 1, K.N_24_C235): K.S_FOP_MINNM_047C, (1, 0, K.N_26_5F9C): K.S_FOP_ADD_468A,
          (1, 0, K.N_30_624B): K.S_FOP_MAX_1A5E, (1, 1, K.N_30_624B): K.S_FOP_MIN_EC9D}
FCMP3 = {(0, 0, K.N_28_59E1): 0, (1, 0, K.N_28_59E1): 1, (1, 1, K.N_28_59E1): K.N_2_D473, (1, 0, K.N_29_3513): K.N_3_4E07, (1, 1, K.N_29_3513): K.N_4_4B22}


def vfp_expand_imm(imm8, N):
    E = K.N_8_2C62 if N == K.N_32_E29C else K.N_11_4FC8
    F = N - E - 1
    sign = imm8 >> K.N_7_7902
    b6 = (imm8 >> K.N_6_E7F6) & 1
    exp = ((b6 ^ 1) << (E - 1)) | ((((1 << (E - K.N_3_4E07)) - 1) if b6 else 0) << K.N_2_D473) | ((imm8 >> K.N_4_4B22) & K.N_3_4E07)
    frac = (imm8 & K.N_15_E629) << (F - K.N_4_4B22)
    return (sign << (N - 1)) | (exp << F) | frac


def simd_expand_imm(op, cmode, imm8):
    c = cmode >> 1
    if c == 0:
        imm = (imm8 << K.N_32_E29C) | imm8
    elif c == 1:
        imm = ((imm8 << K.N_8_2C62) << K.N_32_E29C) | (imm8 << K.N_8_2C62)
    elif c == K.N_2_D473:
        imm = ((imm8 << K.N_16_B17E) << K.N_32_E29C) | (imm8 << K.N_16_B17E)
    elif c == K.N_3_4E07:
        imm = ((imm8 << K.N_24_C235) << K.N_32_E29C) | (imm8 << K.N_24_C235)
    elif c == K.N_4_4B22:
        imm = sum(imm8 << s for s in range(0, K.N_64_A68B, K.N_16_B17E))
    elif c == K.N_5_EF2D:
        imm = sum((imm8 << K.N_8_2C62) << s for s in range(0, K.N_64_A68B, K.N_16_B17E))
    elif c == K.N_6_E7F6:
        v = ((imm8 << K.N_8_2C62) | K.N_255_9556) if not (cmode & 1) else ((imm8 << K.N_16_B17E) | K.N_65535_F2F8)
        imm = (v << K.N_32_E29C) | v
    else:
        if not (cmode & 1) and not op:
            imm = sum(imm8 << s for s in range(0, K.N_64_A68B, K.N_8_2C62))
        elif not (cmode & 1) and op:
            imm = 0
            for i in range(K.N_8_2C62):
                if (imm8 >> i) & 1:
                    imm |= K.N_255_9556 << (K.N_8_2C62 * i)
        elif (cmode & 1) and not op:
            v = vfp_expand_imm(imm8, K.N_32_E29C)
            imm = (v << K.N_32_E29C) | v
        else:
            imm = vfp_expand_imm(imm8, K.N_64_A68B)
    return imm


class SimdC:
    """FP/SIMD decoder emitting portable C. Methods return C statements or raise Unsupported."""

    # ------------------------------------------------------------ top level

    def dp(self, w, pc):
        op0, op1, op2, op3 = bits(w, K.N_28_59E1, K.N_4_4B22), bits(w, K.N_23_535F, K.N_2_D473), bits(w, K.N_19_9400, K.N_4_4B22), bits(w, K.N_10_4A44, K.N_9_1958)
        if op0 == K.N_4_4B22 and not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_5_EF2D and (op3 & K.N_387_25DA) == K.N_2_D473:
            return self.crypto_aes(w)
        if op0 == K.N_5_EF2D and not (op1 & K.N_2_D473) and not (op2 & K.N_4_4B22) and (op3 & K.N_35_9F14) == 0:
            return self.crypto_sha3(w)
        if op0 == K.N_5_EF2D and not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_5_EF2D and (op3 & K.N_387_25DA) == K.N_2_D473:
            return self.crypto_sha2(w)
        if (op0 & K.N_13_3FDB) == K.N_5_EF2D:  # AdvSIMD scalar
            if op1 == 0 and not (op2 & K.N_12_6B51) and (op3 & K.N_33_C6F3) == 1:
                return self.scalar_copy(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_4_4B22 and (op3 & K.N_387_25DA) == K.N_2_D473:
                return self.misc2(w, scalar=True)
            if not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_6_E7F6 and (op3 & K.N_387_25DA) == K.N_2_D473:
                return self.scalar_pairwise(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_4_4B22) and (op3 & K.N_3_4E07) == 0:
                return self.three_diff(w, scalar=True)
            if not (op1 & K.N_2_D473) and (op2 & K.N_4_4B22) and (op3 & 1):
                return self.three_same(w, scalar=True)
            if op1 == K.N_2_D473 and (op3 & 1):
                return self.shift_imm(w, scalar=True)
            if (op1 & K.N_2_D473) and not (op3 & 1):
                return self.indexed(w, scalar=True)
            raise Unsupported(f'simd scalar {w:08x}')
        if (op0 & K.N_11_4FC8) == 0 and not (op1 & K.N_2_D473) and not (op2 & K.N_4_4B22) and (op3 & K.N_35_9F14) == 0:
            return self.tbl(w)
        if (op0 & K.N_11_4FC8) == 0 and not (op1 & K.N_2_D473) and not (op2 & K.N_4_4B22) and (op3 & K.N_35_9F14) == K.N_2_D473:
            return self.permute(w)
        if (op0 & K.N_11_4FC8) == K.N_2_D473 and not (op1 & K.N_2_D473) and not (op2 & K.N_4_4B22) and (op3 & K.N_33_C6F3) == 0:
            return self.ext(w)
        if (op0 & K.N_9_1958) == 0:  # AdvSIMD vector
            if op1 == 0 and not (op2 & K.N_12_6B51) and (op3 & K.N_33_C6F3) == 1:
                return self.copy(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_4_4B22) and (op3 & 1):
                return self.three_same(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_4_4B22) and (op3 & K.N_3_4E07) == 0:
                return self.three_diff(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_4_4B22 and (op3 & K.N_387_25DA) == K.N_2_D473:
                return self.misc2(w)
            if not (op1 & K.N_2_D473) and (op2 & K.N_7_7902) == K.N_6_E7F6 and (op3 & K.N_387_25DA) == K.N_2_D473:
                return self.across(w)
            if op1 == K.N_2_D473 and op2 == 0 and (op3 & 1):
                return self.modimm(w)
            if op1 == K.N_2_D473 and op2 and (op3 & 1):
                return self.shift_imm(w)
            if (op1 & K.N_2_D473) and not (op3 & 1):
                return self.indexed(w)
            raise Unsupported(f'simd vector {w:08x}')
        if (op0 & K.N_5_EF2D) == 0b0001:  # scalar FP
            if bits(w, K.N_29_3513, 1) or (bits(w, K.N_31_EB1E, 1) and (op1 & K.N_2_D473 or (op2 & K.N_4_4B22 and op3 & K.N_63_DA4E))):
                raise Unsupported(K.S_FP_M_S_D243)
            if op1 & K.N_2_D473:
                return self.fp_dp3(w)
            if not (op2 & K.N_4_4B22):
                return self.fp_fixed(w)
            if (op3 & K.N_63_DA4E) == 0:
                return self.fp_int(w)
            if (op3 & K.N_31_EB1E) == K.N_16_B17E:
                return self.fp_dp1(w)
            if (op3 & K.N_15_E629) == K.N_8_2C62:
                return self.fp_cmp(w)
            if (op3 & K.N_7_7902) == K.N_4_4B22:
                return self.fp_imm(w)
            if (op3 & K.N_3_4E07) == 1:
                return self.fp_ccmp(w)
            if (op3 & K.N_3_4E07) == K.N_2_D473:
                return self.fp_dp2(w)
            if (op3 & K.N_3_4E07) == K.N_3_4E07:
                return self.fp_csel(w)
            raise Unsupported(K.S_FP_OP3_9B4E)
        raise Unsupported(f'fp/simd {w:08x}')

    # ------------------------------------------------------------ crypto

    def crypto_aes(self, w):
        if bits(w, K.N_22_785F, K.N_2_D473):
            raise Unsupported(K.S_AES_SIZE_F7F8)
        opc = bits(w, K.N_12_6B51, K.N_5_EF2D)
        if opc not in (K.N_4_4B22, K.N_5_EF2D, K.N_6_E7F6, K.N_7_7902):
            raise Unsupported(K.S_AES_OPCODE_C3D3)
        return f'sv_aes(c, {opc - K.N_4_4B22}, {bits(w, 0, K.N_5_EF2D)}, {bits(w, K.N_5_EF2D, K.N_5_EF2D)});'

    def crypto_sha3(self, w):
        if bits(w, K.N_22_785F, K.N_2_D473):
            raise Unsupported(K.S_SHA_SIZE_C671)
        opc, d, n, m = bits(w, K.N_12_6B51, K.N_3_4E07), bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D)
        if opc < K.N_3_4E07:
            return f'sv_sha1hash(c, {opc}, {d}, {n}, {m});'
        if opc == K.N_3_4E07:
            return f'sv_sha_misc(c, 0, {d}, {n}, {m});'
        if opc in (K.N_4_4B22, K.N_5_EF2D):
            return f'sv_sha256hash(c, {int(opc == K.N_4_4B22)}, {d}, {n}, {m});'
        if opc == K.N_6_E7F6:
            return f'sv_sha_misc(c, 3, {d}, {n}, {m});'
        raise Unsupported(K.S_SHA3_OPCODE_2F73)

    def crypto_sha2(self, w):
        if bits(w, K.N_22_785F, K.N_2_D473):
            raise Unsupported(K.S_SHA_SIZE_C671)
        opc, d, n = bits(w, K.N_12_6B51, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D)
        op = {0: K.N_4_4B22, 1: 1, K.N_2_D473: K.N_2_D473}.get(opc)
        if op is None:
            raise Unsupported(K.S_SHA2_OPCODE_0576)
        return f'sv_sha_misc(c, {op}, {d}, {n}, 0);'

    # ------------------------------------------------------------ copy / permute / table

    @staticmethod
    def _imm5(imm5):
        if not imm5 & K.N_15_E629:
            raise Unsupported(K.S_COPY_IMM5_3B0C)
        sz = (imm5 & -imm5).bit_length() - 1
        return sz, imm5 >> (sz + 1)

    def scalar_copy(self, w):
        if bits(w, K.N_29_3513, 1) or bits(w, K.N_11_4FC8, K.N_4_4B22):
            raise Unsupported(K.S_SCALAR_COPY_CA7F)
        sz, idx = self._imm5(bits(w, K.N_16_B17E, K.N_5_EF2D))
        if sz > K.N_3_4E07:
            raise Unsupported(K.S_SCALAR_DUP_SIZE_154B)
        d, n = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D)
        return f'c->v[{d}] = (({(K.S_UINT8_T_85DD, K.S_SA16_F4CF, K.S_SA32_A846, K.S_SA64_21BC)[sz]} *)&c->v[{n}])[{idx}];'

    def copy(self, w):
        Q, op, imm5, imm4 = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_11_4FC8, K.N_4_4B22)
        d, n = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D)
        sz, idx = self._imm5(imm5)
        if sz > K.N_3_4E07:
            raise Unsupported(K.S_COPY_SIZE_6B47)
        P = (K.S_UINT8_T_85DD, K.S_SA16_F4CF, K.S_SA32_A846, K.S_SA64_21BC)[sz]
        if op:
            if not Q:
                raise Unsupported(K.S_INS_ELEM_Q_C0BD)
            idx2 = imm4 >> sz
            return f'(({P} *)&c->v[{d}])[{idx}] = (({P} *)&c->v[{n}])[{idx2}];'
        if imm4 == 0:  # DUP (element)
            if sz == K.N_3_4E07 and not Q:
                raise Unsupported(K.S_DUP_1D_9B59)
            nl = K.N_16_B17E >> sz
            return VWr(vu(sz), Q, d, shuf(vu(sz), VR(vu(sz), n), VR(vu(sz), n), [idx] * nl))
        if imm4 == 1:  # DUP (general)
            if sz == K.N_3_4E07 and not Q:
                raise Unsupported(K.S_DUP_1D_9B59)
            return VWr(vu(sz), Q, d, f'({vu(sz)}){{}} + ({ut(sz)}){X(n)}')
        if imm4 == K.N_3_4E07:  # INS (general)
            if not Q:
                raise Unsupported(K.S_INS_Q_3BF6)
            return f'(({P} *)&c->v[{d}])[{idx}] = ({ut(sz)}){X(n)};'
        if imm4 == K.N_5_EF2D:  # SMOV
            if sz >= K.N_2_D473 + Q:
                raise Unsupported(K.S_SMOV_SIZE_E3D5)
            st = (K.S_INT8_T_A582, K.S_INT16_T_6870, K.S_INT32_T_BA71)[sz]
            return set_reg(Q, d, f'(uint64_t)(int64_t)({st})(({P} *)&c->v[{n}])[{idx}]')
        if imm4 == K.N_7_7902:  # UMOV
            if (Q and sz != K.N_3_4E07) or (not Q and sz == K.N_3_4E07):
                raise Unsupported(K.S_UMOV_SIZE_B773)
            return set_reg(Q, d, f'(({P} *)&c->v[{n}])[{idx}]')
        raise Unsupported(K.S_COPY_IMM4_3734)

    def permute(self, w):
        Q, sz, m, opc, n, d = bits(w, K.N_30_624B, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_12_6B51, K.N_3_4E07), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc in (0, K.N_4_4B22) or (sz == K.N_3_4E07 and not Q):
            raise Unsupported(K.S_PERMUTE_5207)
        ne, NL = lanes(Q, sz), K.N_16_B17E >> sz
        part = opc >> K.N_2_D473
        kind = opc & K.N_3_4E07
        idx = []
        for i in range(ne):
            if kind == 1:  # UZP
                j = K.N_2_D473 * i + part
                idx.append(j if j < ne else NL + j - ne)
            elif kind == K.N_2_D473:  # TRN
                p = i & ~1
                idx.append(p + part if not (i & 1) else NL + p + part)
            else:  # ZIP
                base = part * ne // K.N_2_D473
                idx.append(base + i // K.N_2_D473 if not (i & 1) else NL + base + i // K.N_2_D473)
        idx += [0] * (NL - ne)
        return VWr(vu(sz), Q, d, shuf(vu(sz), VR(vu(sz), n), VR(vu(sz), m), idx))

    def ext(self, w):
        Q, m, imm4, n, d = bits(w, K.N_30_624B, 1), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_11_4FC8, K.N_4_4B22), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if bits(w, K.N_22_785F, K.N_2_D473) or (not Q and imm4 >= K.N_8_2C62):
            raise Unsupported(K.S_EXT_B350)
        if Q:
            idx = [imm4 + i for i in range(K.N_16_B17E)]
        else:
            idx = [(imm4 + i) if imm4 + i < K.N_8_2C62 else K.N_16_B17E + imm4 + i - K.N_8_2C62 for i in range(K.N_8_2C62)] + [0] * K.N_8_2C62
        return VWr(K.S_VU8_4458, Q, d, shuf(K.S_VU8_4458, VR(K.S_VU8_4458, n), VR(K.S_VU8_4458, m), idx))

    def tbl(self, w):
        Q, m, ln, op, n, d = bits(w, K.N_30_624B, 1), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_13_3FDB, K.N_2_D473), bits(w, K.N_12_6B51, 1), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if bits(w, K.N_22_785F, K.N_2_D473):
            raise Unsupported(K.S_TBL_OP2_544A)
        return f'sv_tbl(c, {Q}, {ln}, {op}, {d}, {n}, {m});'

    # ------------------------------------------------------------ three same

    def three_same(self, w, scalar=False):
        Q, U, sz, m, opc, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_11_4FC8, K.N_5_EF2D),
                                  bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        if opc >= K.N_24_C235:
            return self.three_same_fp(w, scalar)
        if scalar:
            return self.three_same_scalar(U, sz, opc, d, n, m)
        if sz == K.N_3_4E07 and not Q and opc != K.N_3_4E07:
            raise Unsupported(K.S_3SAME_1D_A105)
        ne = lanes(Q, sz)
        T, S = vu(sz), vs(sz)
        a, b, o = VR(T, n), VR(T, m), VR(T, d)
        sa, sb = VR(S, n), VR(S, m)
        x, y = (a, b) if U else (sa, sb)
        if opc in (0, K.N_2_D473, K.N_4_4B22, K.N_14_8527, K.N_15_E629, K.N_12_6B51, K.N_13_3FDB) and sz == K.N_3_4E07:
            raise Unsupported(K.S_3SAME_SIZE_D6CB)
        if opc == 0:
            return same(lop(K.S_HADD_5179, U), sz, ne, d, n, m)
        if opc == 1:
            return VWr(T, Q, d, f'({T})__builtin_elementwise_{K.S_ADD_7411}_sat({x}, {y})')
        if opc == K.N_2_D473:
            return same(lop(K.S_RHADD_74AF, U), sz, ne, d, n, m)
        if opc == K.N_3_4E07:
            a64, b64, o64 = VR(K.S_VU64_EED5, n), VR(K.S_VU64_EED5, m), VR(K.S_VU64_EED5, d)
            e = [[f'{a64} & {b64}', f'{a64} & ~{b64}', f'{a64} | {b64}', f'{a64} | ~{b64}'],
                 [f'{a64} ^ {b64}', f'({o64} & {a64}) | (~{o64} & {b64})', f'({o64} & ~{b64}) | ({a64} & {b64})',
                  f'({o64} & {b64}) | ({a64} & ~{b64})']][U][sz]
            return VWr(K.S_VU64_EED5, Q, d, e)
        if opc == K.N_4_4B22:
            return same(lop(K.S_HSUB_850F, U), sz, ne, d, n, m)
        if opc == K.N_5_EF2D:
            return VWr(T, Q, d, f'({T})__builtin_elementwise_sub_sat({x}, {y})')
        if opc == K.N_6_E7F6:
            return VWr(T, Q, d, f'({T})({x} > {y})')
        if opc == K.N_7_7902:
            return VWr(T, Q, d, f'({T})({x} >= {y})')
        if K.N_8_2C62 <= opc <= K.N_11_4FC8:
            return same(lop((K.S_SHL_62FC, K.S_QSHL_F2FD, K.S_RSHL_96D6, K.S_QRSHL_531E)[opc - K.N_8_2C62], U), sz, ne, d, n, m)
        if opc == K.N_12_6B51:
            return VWr(T, Q, d, f'({T})__builtin_elementwise_max({x}, {y})')
        if opc == K.N_13_3FDB:
            return VWr(T, Q, d, f'({T})__builtin_elementwise_min({x}, {y})')
        if opc in (K.N_14_8527, K.N_15_E629):
            e = f'({T})__builtin_elementwise_max({x}, {y}) - ({T})__builtin_elementwise_min({x}, {y})'
            return VWr(T, Q, d, f'{o} + ({e})' if opc == K.N_15_E629 else e)
        if opc == K.N_16_B17E:
            return VWr(T, Q, d, f'{a} {K.S_EMPTY_01F8 if U else K.S_EMPTY_3604} {b}')
        if opc == K.N_17_4523:
            return VWr(T, Q, d, f'({T})({a} == {b})' if U else f'({T})(({a} & {b}) != 0)')
        if sz == K.N_3_4E07 and opc != K.N_23_535F:
            raise Unsupported(K.S_3SAME_SIZE_D6CB)
        if opc == K.N_18_4EC9:
            return VWr(T, Q, d, f'{o} {K.S_EMPTY_01F8 if U else K.S_EMPTY_3604} {a} * {b}')
        if opc == K.N_19_9400:
            if U:
                if sz:
                    raise Unsupported(K.S_PMUL_SIZE_D125)
                return same(lop(K.S_PMUL_7322), 0, ne, d, n, m)
            return VWr(T, Q, d, f'{a} * {b}')
        if opc in (K.N_20_F5CA, K.N_21_6F4B, K.N_23_535F):
            if opc == K.N_23_535F and U:
                raise Unsupported(K.S_ADDP_U_6F6E)
            ev, od = self._pair_idx(Q, sz)
            A, B = (a, b) if (U or opc == K.N_23_535F) else (sa, sb)
            L, R = shuf(T, A, B, ev), shuf(T, A, B, od)
            if opc == K.N_23_535F:
                return VWr(T, Q, d, f'({T})({L} + {R})')
            return VWr(T, Q, d, f'({T})__builtin_elementwise_{K.S_MAX_3378 if opc == K.N_20_F5CA else K.S_MIN_16B4}({L}, {R})')
        if opc == K.N_22_785F:
            if sz not in (1, K.N_2_D473):
                raise Unsupported(K.S_SQDMULH_SIZE_E64B)
            return same(lop(K.S_SQRDMULH_7E3C if U else K.S_SQDMULH_2CA7), sz, ne, d, n, m)
        raise Unsupported(K.S_3SAME_OPCODE_7760)

    @staticmethod
    def _pair_idx(Q, sz):
        """Lane indices (into SHUF(a, b)) of the left / right operands of a pairwise op; for Q=0 the
        padding lanes point at a's upper half."""
        ne, NL = lanes(Q, sz), K.N_16_B17E >> sz
        src = list(range(ne)) + [NL + i for i in range(ne)]
        ev = [src[K.N_2_D473 * k] for k in range(ne)] + [ne % NL] * (NL - ne)
        od = [src[K.N_2_D473 * k + 1] for k in range(ne)] + [ne % NL] * (NL - ne)
        return ev, od

    def three_same_scalar(self, U, sz, opc, d, n, m):
        if opc in (1, K.N_5_EF2D):
            return same(lop(K.S_UQADD_7ADA if U else K.S_SQADD_E1C8) if opc == 1 else lop(K.S_UQSUB_8BEC if U else K.S_SQSUB_A1BE), sz, 1, d, n, m)
        if opc in (K.N_9_1958, K.N_11_4FC8):
            return same(lop(K.S_QSHL_F2FD if opc == K.N_9_1958 else K.S_QRSHL_531E, U), sz, 1, d, n, m)
        if opc == K.N_22_785F:
            if sz not in (1, K.N_2_D473):
                raise Unsupported(K.S_SQDMULH_SIZE_E64B)
            return same(lop(K.S_SQRDMULH_7E3C if U else K.S_SQDMULH_2CA7), sz, 1, d, n, m)
        if sz != K.N_3_4E07:
            raise Unsupported(K.S_SCALAR_3SAME_SIZE_A1A4)
        a, b = f'VS64({n})', f'VS64({m})'
        sa, sb = f'(int64_t)VS64({n})', f'(int64_t)VS64({m})'
        x, y = (a, b) if U else (sa, sb)
        if opc == K.N_6_E7F6:
            return f'c->v[{d}] = ({x} > {y}) ? ~0ull : 0;'
        if opc == K.N_7_7902:
            return f'c->v[{d}] = ({x} >= {y}) ? ~0ull : 0;'
        if opc in (K.N_8_2C62, K.N_10_4A44):
            return same(lop(K.S_SHL_62FC if opc == K.N_8_2C62 else K.S_RSHL_96D6, U), K.N_3_4E07, 1, d, n, m)
        if opc == K.N_16_B17E:
            return f'c->v[{d}] = (uint64_t)({a} {K.S_EMPTY_01F8 if U else K.S_EMPTY_3604} {b});'
        if opc == K.N_17_4523:
            return f'c->v[{d}] = ({a} == {b}) ? ~0ull : 0;' if U else f'c->v[{d}] = ({a} & {b}) ? ~0ull : 0;'
        raise Unsupported(K.S_SCALAR_3SAME_OPCODE_1312)

    def three_same_fp(self, w, scalar):
        Q, U, a_, sz, m, opc, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_23_535F, 1), bits(w, K.N_22_785F, 1), bits(w, K.N_16_B17E, K.N_5_EF2D),
                                      bits(w, K.N_11_4FC8, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        if not scalar and sz and not Q:
            raise Unsupported(K.S_FP_2D_Q_0E48)
        F, VT, VU, T, SF, SU, SET = FT[sz]
        key = (U, a_, opc)
        if key in FCMP3:
            cop = FCMP3[key]
            if scalar:
                return (f'c->v[{d}] = ({ut(K.N_2_D473 + sz)})vcmp_{F}(c, {cop}, ({VT}){{{SF}({n})}}, '
                        f'({VT}){{{SF}({m})}})[0];')
            return VWr(VU, Q, d, f'vcmp_{F}(c, {cop}, {VQ(VT, Q, n)}, {VQ(VT, Q, m)})')
        if scalar:
            if key not in ((0, 0, K.N_27_6706), (0, 0, K.N_31_EB1E), (0, 1, K.N_31_EB1E), (1, 1, K.N_26_5F9C)):
                raise Unsupported(K.S_SCALAR_FP_3SAME_D7A4)
            fop = FOP3[key]
            x = f'neg_{F}({SF}({n}))' if fop in (K.S_FOP_RECPS_5F92, K.S_FOP_RSQRTS_5F26) else f'{SF}({n})'
            return f'{SET}({d}, op_{F}(c, {fop}, {x}, {SF}({m}), 0));'
        A, B = VQ(VT, Q, n), VQ(VT, Q, m)
        if key in FPAIRV:
            ev, od = self._pair_idx(Q, K.N_2_D473 + sz)
            return f'SV_FOP_{F.upper()}({FPAIRV[key]}, {Q}, {d}, {shuf(VT, A, B, ev)}, {shuf(VT, A, B, od)}, ({VT}){{}});'
        if U == 0 and opc == K.N_25_B7A5:  # FMLA / FMLS: the multiplicand is negated for FMLS
            return f'SV_FOP_{F.upper()}(FOP_MADD, {Q}, {d}, {VQ(VT, Q, d)}, {f"vneg_{F}({A})" if a_ else A}, {B});'
        fop = FOP3.get(key)
        if fop is None:
            raise Unsupported(K.S_FP_3SAME_OPCODE_21E2)
        if fop in (K.S_FOP_RECPS_5F92, K.S_FOP_RSQRTS_5F26):
            A = f'vneg_{F}({A})'
        return f'SV_FOP_{F.upper()}({fop}, {Q}, {d}, {A}, {B}, ({VT}){{}});'

    # ------------------------------------------------------------ three different

    def three_diff(self, w, scalar=False):
        Q, U, sz, m, opc, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_12_6B51, K.N_4_4B22),
                                  bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        if scalar:
            if U or opc not in (K.N_9_1958, K.N_11_4FC8, K.N_13_3FDB) or sz not in (1, K.N_2_D473):
                raise Unsupported(K.S_SCALAR_3DIFF_3633)
            op = {K.N_9_1958: K.S_SQDMLAL_0671, K.N_11_4FC8: K.S_SQDMLSL_18A0, K.N_13_3FDB: K.S_SQDMULL_8E4C}[opc]
            return gen(lop(op), sz, 1, d, sz + 1, 0, 0, n, sz, 0, m, sz, 0)
        if opc == K.N_14_8527:  # PMULL
            if U:
                raise Unsupported(K.S_PMULL_U_5332)
            if sz == K.N_3_4E07:
                return f'sl_pmull64(c, {Q}, {d}, {n}, {m});'
            if sz:
                raise Unsupported(K.S_PMULL_SIZE_EFB8)
        if sz == K.N_3_4E07 or opc == K.N_15_E629:
            raise Unsupported(K.S_3DIFF_SIZE_7A00)
        ne = K.N_8_2C62 >> sz
        off = Q * ne
        if opc in (K.N_4_4B22, K.N_6_E7F6):  # ADDHN/RADDHN/SUBHN/RSUBHN: narrow into the low or high half
            op = (K.S_ADDHN_91F5, K.S_RADDHN_3125, K.S_SUBHN_C09F, K.S_RSUBHN_C588)[(opc == K.N_6_E7F6) * K.N_2_D473 + U]
            return gen(lop(op), sz, ne, d, sz, off, Q, n, sz + 1, 0, m, sz + 1, 0)
        if opc in (1, K.N_3_4E07):  # wide
            op = K.S_ADDW_9DD0 if opc == 1 else K.S_SUBW_714A
            return gen(lop(op, U), sz, ne, d, sz + 1, 0, 0, n, sz + 1, 0, m, sz, off)
        op = {0: K.S_ADDL_C01A, K.N_2_D473: K.S_SUBL_0866, K.N_5_EF2D: K.S_ABAL_A58D, K.N_7_7902: K.S_ABDL_43FE, K.N_8_2C62: K.S_MLAL_EB76, K.N_10_4A44: K.S_MLSL_D7D8, K.N_12_6B51: K.S_MULL_5D5C,
              K.N_9_1958: K.S_SQDMLAL_0671, K.N_11_4FC8: K.S_SQDMLSL_18A0, K.N_13_3FDB: K.S_SQDMULL_8E4C, K.N_14_8527: K.S_PMULL_3A56}[opc]
        if op.startswith(K.S_SQD_34A2):
            if U or sz not in (1, K.N_2_D473):
                raise Unsupported(K.S_SQDMULL_FD64)
        return gen(lop(op, U if not op.startswith((K.S_SQD_34A2, K.S_PM_E1DC)) else 0), sz, ne, d, sz + 1, 0, 0, n, sz, off,
                   m, sz, off)

    # ------------------------------------------------------------ two-register misc

    def misc2(self, w, scalar=False):
        Q, U, sz, opc, n, d = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_12_6B51, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc >= K.N_24_C235 or opc in (K.N_22_785F, K.N_23_535F) or K.N_12_6B51 <= opc <= K.N_15_E629:
            return self.misc2_fp(w, scalar)
        if scalar:
            return self.misc2_scalar(U, sz, opc, d, n)
        ne = lanes(Q, sz)
        T, S = vu(sz), vs(sz)
        if opc == 0 or (opc == 1 and not U):  # REV64 / REV32 / REV16
            gsz = {(0, 0): K.N_3_4E07, (1, 0): K.N_2_D473, (0, 1): 1}.get((U, opc))
            if gsz is None or sz >= gsz:
                raise Unsupported(K.S_REV_75A4)
            es = 1 << sz
            gb = 1 << gsz
            idx = []
            for i in range(K.N_16_B17E):
                g, o = i // gb * gb, i % gb
                e, b = o // es, o % es
                idx.append(g + (gb // es - 1 - e) * es + b)
            return VWr(K.S_VU8_4458, Q, d, shuf(K.S_VU8_4458, VR(K.S_VU8_4458, n), VR(K.S_VU8_4458, n), idx))
        if sz == K.N_3_4E07 and not Q and opc not in (K.N_18_4EC9, K.N_20_F5CA, K.N_19_9400):
            raise Unsupported(K.S_MISC_1D_9055)
        if opc in (K.N_2_D473, K.N_6_E7F6):
            if sz == K.N_3_4E07:
                raise Unsupported(K.S_ADDLP_SIZE_F07D)
            return f'sl_addlp(c, {U}, {int(opc == K.N_6_E7F6)}, {Q}, {sz}, {d}, {n});'
        if opc == K.N_3_4E07:
            return same(lop(K.S_USQADD_7454 if U else K.S_SUQADD_FABB), sz, ne, d, n)
        if opc == K.N_4_4B22:
            if sz == K.N_3_4E07:
                raise Unsupported(K.S_CLZ_SIZE_2B2A)
            return same(lop(K.S_CLZ_C925 if U else K.S_CLS_E55E), sz, ne, d, n)
        if opc == K.N_5_EF2D:
            if not U:
                if sz:
                    raise Unsupported(K.S_CNT_SIZE_FF91)
                return same(lop(K.S_CNT_2AEA), 0, ne, d, n)
            if sz == 0:
                return VWr(K.S_VU64_EED5, Q, d, f'~{VR(K.S_VU64_EED5, n)}')
            if sz == 1:
                return same(lop(K.S_RBIT_54D4), 0, lanes(Q, 0), d, n)
            raise Unsupported(K.S_NOT_RBIT_SIZE_3BF3)
        if opc == K.N_7_7902:
            return same(lop(K.S_SQNEG_16A3 if U else K.S_SQABS_0272), sz, ne, d, n)
        a, sa = VR(T, n), VR(S, n)
        if opc == K.N_8_2C62:
            return VWr(T, Q, d, f'({T})({sa} {K.S_EMPTY_B554 if U else K.S_EMPTY_4DE9} 0)')
        if opc == K.N_9_1958:
            return VWr(T, Q, d, f'({T})({sa} {K.S_EMPTY_0306 if U else K.S_EMPTY_A0B1} 0)')
        if opc == K.N_10_4A44 and not U:
            return VWr(T, Q, d, f'({T})({sa} < 0)')
        if opc == K.N_11_4FC8:
            if U:
                return VWr(T, Q, d, f'-{a}')
            return VWr(T, Q, d, f'(({T}){sa} ^ ({T})({sa} >> {(K.N_8_2C62 << sz) - 1})) - ({T})({sa} >> {(K.N_8_2C62 << sz) - 1})')
        if opc in (K.N_18_4EC9, K.N_20_F5CA):  # XTN / SQXTUN / SQXTN / UQXTN
            if sz == K.N_3_4E07:
                raise Unsupported(K.S_XTN_SIZE_A01A)
            op = {(K.N_18_4EC9, 0): K.S_XTN_5D84, (K.N_18_4EC9, 1): K.S_SQXTUN_90E9, (K.N_20_F5CA, 0): K.S_SQXTN_62CD, (K.N_20_F5CA, 1): K.S_UQXTN_030B}[(opc, U)]
            ne = K.N_8_2C62 >> sz
            return gen(lop(op), sz, ne, d, sz, Q * ne, Q, n, sz + 1, 0)
        if opc == K.N_19_9400 and U:  # SHLL
            if sz == K.N_3_4E07:
                raise Unsupported(K.S_SHLL_SIZE_2932)
            ne = K.N_8_2C62 >> sz
            return gen(lop(K.S_SHLL_DE1B), sz, ne, d, sz + 1, 0, 0, n, sz, Q * ne)
        raise Unsupported(K.S_MISC2_OPCODE_BDDE)

    def misc2_scalar(self, U, sz, opc, d, n):
        if opc == K.N_3_4E07:
            return same(lop(K.S_USQADD_7454 if U else K.S_SUQADD_FABB), sz, 1, d, n)
        if opc == K.N_7_7902:
            return same(lop(K.S_SQNEG_16A3 if U else K.S_SQABS_0272), sz, 1, d, n)
        if opc in (K.N_18_4EC9, K.N_20_F5CA):
            if sz == K.N_3_4E07 or (opc == K.N_18_4EC9 and not U):
                raise Unsupported(K.S_SCALAR_XTN_D107)
            op = {(K.N_18_4EC9, 1): K.S_SQXTUN_90E9, (K.N_20_F5CA, 0): K.S_SQXTN_62CD, (K.N_20_F5CA, 1): K.S_UQXTN_030B}[(opc, U)]
            return gen(lop(op), sz, 1, d, sz, 0, 0, n, sz + 1, 0)
        if sz != K.N_3_4E07:
            raise Unsupported(K.S_SCALAR_MISC_SIZE_A7F3)
        a, sa = f'VS64({n})', f'(int64_t)VS64({n})'
        if opc == K.N_8_2C62:
            return f'c->v[{d}] = ({sa} {K.S_EMPTY_B554 if U else K.S_EMPTY_4DE9} 0) ? ~0ull : 0;'
        if opc == K.N_9_1958:
            return f'c->v[{d}] = ({sa} {K.S_EMPTY_0306 if U else K.S_EMPTY_A0B1} 0) ? ~0ull : 0;'
        if opc == K.N_10_4A44 and not U:
            return f'c->v[{d}] = ({sa} < 0) ? ~0ull : 0;'
        if opc == K.N_11_4FC8:
            return f'c->v[{d}] = (uint64_t)-{a};' if U else f'c->v[{d}] = ({sa} < 0) ? (uint64_t)-{a} : {a};'
        raise Unsupported(K.S_SCALAR_MISC_OPCODE_BD3E)

    def misc2_fp(self, w, scalar):
        """FP two-register misc, vector and scalar."""
        Q, U, a_, sz, opc, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_23_535F, 1), bits(w, K.N_22_785F, 1), bits(w, K.N_12_6B51, K.N_5_EF2D),
                                   bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        F, VT, VU, T, SF, SU, SET = FT[sz]
        if opc in (K.N_22_785F, K.N_23_535F):  # FCVTN / FCVTL / FCVTXN
            if a_:
                raise Unsupported(K.S_FCVTN_A_9DA2)
            if opc == K.N_22_785F and U:
                if not sz:
                    raise Unsupported(K.S_FCVTXN_SIZE_8D24)
                if scalar:
                    return f'FP_SETS({d}, simd_d2s_odd(c, VSF64({n})));'
                return f'sv_fcvtn(c, 1, {Q}, 1, {d}, {n});'
            if scalar or U:
                raise Unsupported(K.S_FCVTN_L_B2C5)
            if opc == K.N_22_785F:
                return f'sv_fcvtn(c, {sz}, {Q}, 0, {d}, {n});'
            return f'sv_fcvtl(c, {sz}, {Q}, {d}, {n});'
        if not scalar and sz and not Q:
            raise Unsupported(K.S_FP_MISC_2D_Q_C28A)
        x = VQ(VT, Q, n)
        if K.N_12_6B51 <= opc <= K.N_15_E629:
            if not a_:
                raise Unsupported(K.S_FP16_MISC_34CA)
            if opc == K.N_15_E629:  # FABS / FNEG
                if scalar:
                    raise Unsupported(K.S_SCALAR_FABS_109E)
                mask = K.S_0X80000000U_927B if not sz else K.S_0X8000000000000000ULL_0F7B
                return VWr(VU, Q, d, f'{VR(VU, n)} ^ {mask}' if U else f'{VR(VU, n)} & ~{mask}')
            cop = {(K.N_12_6B51, 0): (K.N_2_D473, 0), (K.N_12_6B51, 1): (1, 0), (K.N_13_3FDB, 0): (0, 0), (K.N_13_3FDB, 1): (1, 1),
                   (K.N_14_8527, 0): (K.N_2_D473, 1)}.get((opc, U))
            if cop is None:
                raise Unsupported(K.S_FCM_ZERO_4FF4)
            op, swap = cop
            if scalar:
                a, b = f'({VT}){{{SF}({n})}}', f'({VT}){{}}'
                if swap:
                    a, b = b, a
                return f'c->v[{d}] = ({ut(K.N_2_D473 + sz)})vcmp_{F}(c, {op}, {a}, {b})[0];'
            a, b = x, f'({VT}){{}}'
            if swap:
                a, b = b, a
            return VWr(VU, Q, d, f'vcmp_{F}(c, {op}, {a}, {b})')
        if opc == K.N_31_EB1E:
            if not a_:
                raise Unsupported(K.S_FP_MISC_11111_B9B6)
            if U:
                if scalar:
                    raise Unsupported(K.S_SCALAR_FSQRT_8D44)
                return f'SV_FOP_{F.upper()}(FOP_SQRT, {Q}, {d}, {x}, {x}, ({VT}){{}});'
            if not scalar:
                raise Unsupported(K.S_VECTOR_FRECPX_536A)
            return f'{SET}({d}, recpx_{F}(c, {SF}({n})));'
        if opc in (K.N_24_C235, K.N_25_B7A5):  # FRINT*
            if scalar:
                raise Unsupported(K.S_SCALAR_FRINT_6DA4)
            rm = {(0, 0, 0): 0, (0, 0, 1): K.N_2_D473, (0, 1, 0): 1, (0, 1, 1): K.N_3_4E07, (1, 0, 0): K.N_4_4B22, (1, 0, 1): K.N_5_EF2D, (1, 1, 1): K.N_6_E7F6}.get(
                (U, a_, opc & 1))
            if rm is None:
                raise Unsupported(K.S_FRINT_78B6)
            return VWr(VT, Q, d, f'vfrint_{F}(c, {x}, {rm})')
        if opc in (K.N_26_5F9C, K.N_27_6706) or (opc == K.N_28_59E1 and not a_):  # FCVT[NPMZA][SU]
            rm = {(K.N_26_5F9C, 0): 0, (K.N_26_5F9C, 1): 1, (K.N_27_6706, 0): K.N_2_D473, (K.N_27_6706, 1): K.N_3_4E07, (K.N_28_59E1, 0): K.N_4_4B22}[(opc, a_)]
            if scalar:
                return f'c->v[{d}] = ({ut(K.N_2_D473 + sz)})toint_{F}(c, {SF}({n}), {rm}, 0, {K.N_32_E29C << sz}, {U});'
            return VWr(VU, Q, d, f'vtoint_{F}(c, {x}, {rm}, 0, {U})')
        if opc == K.N_28_59E1:  # URECPE / URSQRTE
            if scalar or sz:
                raise Unsupported(K.S_URECPE_8DF7)
            return same(lop(K.S_URSQRTE_BDC4 if U else K.S_URECPE_9550), K.N_2_D473, lanes(Q, K.N_2_D473), d, n)
        if opc == K.N_29_3513:
            if not a_:  # SCVTF / UCVTF
                if scalar:
                    return f'{SET}({d}, fromint_{F}(c, {SU}({n}), {K.N_32_E29C << sz}, {U}, 0));'
                return VWr(VT, Q, d, f'vfromint_{F}(c, {VR(VU, n)}, 0, {U})')
            if scalar:  # FRECPE / FRSQRTE
                return f'{SET}({d}, {K.S_RSQRTE_E362 if U else K.S_RECPE_5D5F}_{F}(c, {SF}({n})));'
            return VWr(VT, Q, d, f'v{K.S_RSQRTE_E362 if U else K.S_RECPE_5D5F}_{F}(c, {x})')
        raise Unsupported(K.S_FP_MISC_OPCODE_65C8)

    # ------------------------------------------------------------ across lanes / pairwise

    def across(self, w):
        Q, U, sz, opc, n, d = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_12_6B51, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc in (K.N_12_6B51, K.N_15_E629):
            if not U:
                raise Unsupported(K.S_FP16_ACROSS_3CFD)
            if not Q or (sz & 1):
                raise Unsupported(K.S_FP_ACROSS_SIZE_DB56)
            fop = {(K.N_12_6B51, 0): K.S_FOP_MAXNM_3B83, (K.N_12_6B51, 1): K.S_FOP_MINNM_047C, (K.N_15_E629, 0): K.S_FOP_MAX_1A5E,
                   (K.N_15_E629, 1): K.S_FOP_MIN_EC9D}[(opc, sz >> 1)]
            return f'sv_fred_s(c, {fop}, {d}, {n});'
        op = {K.N_3_4E07: 1, K.N_10_4A44: K.N_2_D473, K.N_26_5F9C: K.N_3_4E07, K.N_27_6706: 0}.get(opc)
        if op is None or (op == 0 and U) or sz == K.N_3_4E07 or (sz == K.N_2_D473 and not Q):
            raise Unsupported(K.S_ACROSS_0443)
        return f'sl_across(c, {op}, {U}, {Q}, {sz}, {d}, {n});'

    def scalar_pairwise(self, w):
        U, sz, opc, n, d = bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_12_6B51, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if not U:
            if opc == K.N_27_6706 and sz == K.N_3_4E07:
                return f'c->v[{d}] = (uint64_t)(VS64({n}) + VHI64({n}));'
            raise Unsupported(K.S_SCALAR_PAIRWISE_6399)
        fop = {(K.N_12_6B51, 0): K.S_FOP_MAXNM_3B83, (K.N_12_6B51, 1): K.S_FOP_MINNM_047C, (K.N_13_3FDB, 0): K.S_FOP_ADD_468A,
               (K.N_15_E629, 0): K.S_FOP_MAX_1A5E, (K.N_15_E629, 1): K.S_FOP_MIN_EC9D}.get((opc, sz >> 1))
        if fop is None:
            raise Unsupported(K.S_SCALAR_FP_PAIRWISE_877A)
        F, VT, VU, T, SF, SU, SET = FT[sz & 1]
        return f'{SET}({d}, pair_{F}(c, {fop}, {VR(VT, n)}[0], {VR(VT, n)}[1]));'

    # ------------------------------------------------------------ modified immediate

    def modimm(self, w):
        Q, op, cmode, o2, d = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_12_6B51, K.N_4_4B22), bits(w, K.N_11_4FC8, 1), bits(w, 0, K.N_5_EF2D)
        imm8 = (bits(w, K.N_16_B17E, K.N_3_4E07) << K.N_5_EF2D) | bits(w, K.N_5_EF2D, K.N_5_EF2D)
        if o2:
            raise Unsupported(K.S_FMOV_HALF_IMM_ECAD)
        if cmode == K.N_15_E629 and op and not Q:
            raise Unsupported(K.S_FMOV_1D_D4EF)
        imm = simd_expand_imm(op, cmode, imm8)
        c4 = cmode
        if (c4 < K.N_8_2C62 and c4 & 1) or (K.N_8_2C62 <= c4 < K.N_12_6B51 and c4 & 1):  # ORR / BIC
            if op:
                imm = ~imm & ((1 << K.N_64_A68B) - 1)
                return VWr(K.S_VU64_EED5, Q, d, f'{VR(K.S_VU64_EED5, d)} & {hx(imm)}')
            return VWr(K.S_VU64_EED5, Q, d, f'{VR(K.S_VU64_EED5, d)} | {hx(imm)}')
        if op and not (c4 == K.N_14_8527 or c4 == K.N_15_E629):  # MVNI
            imm = ~imm & ((1 << K.N_64_A68B) - 1)
        v = imm | (imm << K.N_64_A68B) if Q else imm
        return f'c->v[{d}] = {u128(v)};'

    # ------------------------------------------------------------ shift by immediate

    def shift_imm(self, w, scalar=False):
        Q, U, immh, immb, opc, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_19_9400, K.N_4_4B22), bits(w, K.N_16_B17E, K.N_3_4E07), bits(w, K.N_11_4FC8, K.N_5_EF2D),
                                       bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        if not immh:
            raise Unsupported(K.S_SHIFT_IMMH_BFA6)
        sz = immh.bit_length() - 1
        es = K.N_8_2C62 << sz
        ih = (immh << K.N_3_4E07) | immb
        rsh, lsh = K.N_2_D473 * es - ih, ih - es
        if scalar:
            Q = 1
        ne = 1 if scalar else lanes(Q, sz)
        narrow = K.N_16_B17E <= opc <= K.N_19_9400
        if opc in (K.N_28_59E1, K.N_31_EB1E):  # fixed-point conversions
            if sz < K.N_2_D473:
                raise Unsupported(K.S_FP16_FIXED_A9FD)
            if sz == K.N_3_4E07 and not Q:
                raise Unsupported(K.S_FIXED_1D_EBF0)
            F, VT, VU, T, SF, SU, SET = FT[sz - K.N_2_D473]
            fb = rsh
            if opc == K.N_28_59E1:
                if scalar:
                    return f'{SET}({d}, fromint_{F}(c, {SU}({n}), {es}, {U}, {fb}));'
                return VWr(VT, Q, d, f'vfromint_{F}(c, {VR(VU, n)}, {fb}, {U})')
            if scalar:
                return f'c->v[{d}] = ({ut(sz)})toint_{F}(c, {SF}({n}), 3, {fb}, {es}, {U});'
            return VWr(VU, Q, d, f'vtoint_{F}(c, {VQ(VT, Q, n)}, 3, {fb}, {U})')
        if narrow:
            if sz == K.N_3_4E07:
                raise Unsupported(K.S_NARROW_SHIFT_SIZE_17BD)
            ne = 1 if scalar else K.N_8_2C62 >> sz
            op = {(K.N_16_B17E, 0): (K.S_SHRN_4327, 0), (K.N_17_4523, 0): (K.S_RSHRN_5ED7, 0), (K.N_16_B17E, 1): (K.S_QSHRUN_A42F, 0),
                  (K.N_17_4523, 1): (K.S_QRSHRUN_A229, 0), (K.N_18_4EC9, 0): (K.S_QSHRN_CA00, 0), (K.N_18_4EC9, 1): (K.S_QSHRN_CA00, 1),
                  (K.N_19_9400, 0): (K.S_QRSHRN_7DC2, 0), (K.N_19_9400, 1): (K.S_QRSHRN_7DC2, 1)}[(opc, U)]
            if scalar and op[0] in (K.S_SHRN_4327, K.S_RSHRN_5ED7):
                raise Unsupported(K.S_SCALAR_SHRN_E2D0)
            part = 0 if scalar else Q
            return gen(lop(*op), sz, ne, d, sz, part * ne, part, n, sz + 1, 0, -1, 0, 0, -1, rsh)
        if opc == K.N_20_F5CA:  # SSHLL / USHLL
            if scalar or sz == K.N_3_4E07:
                raise Unsupported(K.S_SSHLL_AC9B)
            ne = K.N_8_2C62 >> sz
            return gen(lop(K.S_SHLLI_7BBE, U), sz, ne, d, sz + 1, 0, 0, n, sz, Q * ne, -1, 0, 0, -1, lsh)
        if sz == K.N_3_4E07 and not Q:
            raise Unsupported(K.S_SHIFT_1D_3550)
        if scalar and sz != K.N_3_4E07 and opc not in (K.N_12_6B51, K.N_14_8527):
            raise Unsupported(K.S_SCALAR_SHIFT_SIZE_B0EA)
        T, S = vu(sz), vs(sz)
        if opc in (0, K.N_2_D473):  # SSHR/USHR, SSRA/USRA
            if U:
                e = f'({T}){{}}' if rsh >= es else f'({VR(T, n)} >> {rsh})'
            else:
                e = f'({T})({VR(S, n)} >> {min(rsh, es - 1)})'
            if opc == K.N_2_D473:
                e = f'{VR(T, d)} + {e}'
            return VWr(T, Q, d, e) if not scalar else f'c->v[{d}] = (uint64_t)(({e})[0]);'
        if opc in (K.N_4_4B22, K.N_6_E7F6):
            return same(lop(K.S_RSHRI_9F97 if opc == K.N_4_4B22 else K.S_RSRAI_9223, U), sz, ne, d, n, -1, rsh)
        if opc == K.N_8_2C62:
            if not U:
                raise Unsupported(K.S_SRI_U_9C64)
            return same(lop(K.S_SRI_ED66), sz, ne, d, n, -1, rsh)
        if opc == K.N_10_4A44:
            if U:
                return same(lop(K.S_SLI_A7E5), sz, ne, d, n, -1, lsh)
            e = f'{VR(T, n)} << {lsh}'
            return VWr(T, Q, d, e) if not scalar else f'c->v[{d}] = (uint64_t)(({e})[0]);'
        if opc == K.N_12_6B51:
            if not U:
                raise Unsupported(K.S_SQSHLU_U_B9C4)
            return same(lop(K.S_QSHLUI_3449), sz, ne, d, n, -1, lsh)
        if opc == K.N_14_8527:
            return same(lop(K.S_QSHLI_6FED, U), sz, ne, d, n, -1, lsh)
        raise Unsupported(K.S_SHIFT_IMM_OPCODE_2539)

    # ------------------------------------------------------------ indexed element

    def indexed(self, w, scalar=False):
        Q, U, sz, L, M, rm, opc, H, n, d = (bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_21_6F4B, 1),
                                           bits(w, K.N_20_F5CA, 1), bits(w, K.N_16_B17E, K.N_4_4B22), bits(w, K.N_12_6B51, K.N_4_4B22), bits(w, K.N_11_4FC8, 1),
                                           bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        fp = opc in (1, K.N_5_EF2D, K.N_9_1958) and not (U and opc != K.N_9_1958)
        if fp:
            if sz < K.N_2_D473:
                raise Unsupported(K.S_FP16_INDEXED_EED1)
            fsz = sz & 1
            if fsz:
                if L:
                    raise Unsupported(K.S_INDEXED_L_6F40)
                idx = H
            else:
                idx = (H << 1) | L
            m = (M << K.N_4_4B22) | rm
            if not scalar and fsz and not Q:
                raise Unsupported(K.S_INDEXED_2D_Q_5017)
            F, VT, VU, T, SF, SU, SET = FT[fsz]
            if scalar:
                el = f'{VR(VT, m)}[{idx}]'
                if opc == K.N_9_1958:
                    return f'{SET}({d}, op_{F}(c, {K.S_FOP_MULX_B0F6 if U else K.S_FOP_MUL_8D62}, {SF}({n}), {el}, 0));'
                x = f'{SF}({n})' if opc == 1 else f'neg_{F}({SF}({n}))'
                return f'{SET}({d}, op_{F}(c, FOP_MADD, {SF}({d}), {x}, {el}));'
            el = f'vsplat_{F}({VR(VT, m)}, {idx}, {Q})'
            if opc == K.N_9_1958:
                return f'SV_FOP_{F.upper()}({K.S_FOP_MULX_B0F6 if U else K.S_FOP_MUL_8D62}, {Q}, {d}, {VQ(VT, Q, n)}, {el}, ({VT}){{}});'
            x = VQ(VT, Q, n) if opc == 1 else f'vneg_{F}({VQ(VT, Q, n)})'
            return f'SV_FOP_{F.upper()}(FOP_MADD, {Q}, {d}, {VQ(VT, Q, d)}, {x}, {el});'
        if sz == 1:
            idx, m = (H << K.N_2_D473) | (L << 1) | M, rm
        elif sz == K.N_2_D473:
            idx, m = (H << 1) | L, (M << K.N_4_4B22) | rm
        else:
            raise Unsupported(K.S_INDEXED_SIZE_F269)
        T = vu(sz)
        if scalar:
            op = {(0, K.N_3_4E07): K.S_SQDMLAL_0671, (0, K.N_7_7902): K.S_SQDMLSL_18A0, (0, K.N_11_4FC8): K.S_SQDMULL_8E4C, (0, K.N_12_6B51): K.S_SQDMULH_2CA7,
                  (0, K.N_13_3FDB): K.S_SQRDMULH_7E3C}.get((U, opc))
            if op is None:
                raise Unsupported(K.S_SCALAR_INDEXED_9C50)
            if op in (K.S_SQDMULH_2CA7, K.S_SQRDMULH_7E3C):
                return gen(lop(op), sz, 1, d, sz, 0, 0, n, sz, 0, m, sz, 0, idx)
            return gen(lop(op), sz, 1, d, sz + 1, 0, 0, n, sz, 0, m, sz, 0, idx)
        ne = lanes(Q, sz)
        el = shuf(T, VR(T, m), VR(T, m), [idx] * (K.N_16_B17E >> sz))
        if (U, opc) == (0, K.N_8_2C62):
            return VWr(T, Q, d, f'{VR(T, n)} * {el}')
        if (U, opc) in ((1, 0), (1, K.N_4_4B22)):
            return VWr(T, Q, d, f'{VR(T, d)} {K.S_EMPTY_3604 if opc == 0 else K.S_EMPTY_01F8} {VR(T, n)} * {el}')
        if (U, opc) in ((0, K.N_12_6B51), (0, K.N_13_3FDB)):
            return same(lop(K.S_SQDMULH_2CA7 if opc == K.N_12_6B51 else K.S_SQRDMULH_7E3C), sz, ne, d, n, m, 0, idx)
        lng = {K.N_2_D473: K.S_MLAL_EB76, K.N_6_E7F6: K.S_MLSL_D7D8, K.N_10_4A44: K.S_MULL_5D5C, K.N_3_4E07: K.S_SQDMLAL_0671, K.N_7_7902: K.S_SQDMLSL_18A0, K.N_11_4FC8: K.S_SQDMULL_8E4C}.get(opc)
        if lng is None or (U and lng.startswith(K.S_SQ_38B7)):
            raise Unsupported(K.S_INDEXED_OPCODE_37B6)
        ne = K.N_8_2C62 >> sz
        return gen(lop(lng, U if not lng.startswith(K.S_SQ_38B7) else 0), sz, ne, d, sz + 1, 0, 0, n, sz, Q * ne, m, sz, 0, idx)

    # ------------------------------------------------------------ scalar FP

    @staticmethod
    def _ftype(w):
        t = bits(w, K.N_22_785F, K.N_2_D473)
        if t > 1:
            raise Unsupported(K.S_FP_TYPE_2061)
        return t

    def fp_dp3(self, w):
        t = self._ftype(w)
        o1, o0 = bits(w, K.N_21_6F4B, 1), bits(w, K.N_15_E629, 1)
        d, n, m, a = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_10_4A44, K.N_5_EF2D)
        F = FT[t][0]
        na, nn = o1, o1 ^ o0  # FMADD 00, FMSUB 01 (-n), FNMADD 10 (-a, -n), FNMSUB 11 (-a)
        return f'fp_fma_{F}(c, {na}, {nn}, {d}, {n}, {m}, {a});'

    def fp_dp2(self, w):
        t = self._ftype(w)
        opc, d, n, m = bits(w, K.N_12_6B51, K.N_4_4B22), bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D)
        fop = (K.S_FOP_MUL_8D62, K.S_FOP_DIV_2A67, K.S_FOP_ADD_468A, K.S_FOP_SUB_DB54, K.S_FOP_MAX_1A5E, K.S_FOP_MIN_EC9D, K.S_FOP_MAXNM_3B83, K.S_FOP_MINNM_047C,
               K.S_FOP_NMUL_A07C)[opc] if opc <= K.N_8_2C62 else None
        if fop is None:
            raise Unsupported(K.S_FP_DP2_3857)
        return f'fp_op_{FT[t][0]}(c, {fop}, {d}, {n}, {m});'

    def fp_dp1(self, w):
        t = bits(w, K.N_22_785F, K.N_2_D473)
        opc, d, n = bits(w, K.N_15_E629, K.N_6_E7F6), bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D)
        if t == K.N_3_4E07:  # half source: only FCVT
            if opc == K.N_4_4B22:
                return f'FP_SETS({d}, simd_h2s(c, VS16({n})));'
            if opc == K.N_5_EF2D:
                return f'FP_SETD({d}, simd_h2d(c, VS16({n})));'
            raise Unsupported(K.S_FP16_DP1_3262)
        if t == K.N_2_D473:
            raise Unsupported(K.S_FP_TYPE_2061)
        F, VT, VU, T, SF, SU, SET = FT[t]
        sign = K.S_0X80000000U_927B if not t else K.S_0X8000000000000000ULL_0F7B
        if opc == 0:
            return f'c->v[{d}] = {SU}({n});'
        if opc == 1:
            return f'c->v[{d}] = {SU}({n}) & ~{sign};'
        if opc == K.N_2_D473:
            return f'c->v[{d}] = {SU}({n}) ^ {sign};'
        if opc == K.N_3_4E07:
            return f'fp_op_{F}(c, FOP_SQRT, {d}, {n}, {n});'
        if opc in (K.N_4_4B22, K.N_5_EF2D, K.N_7_7902):
            to = {K.N_4_4B22: 0, K.N_5_EF2D: 1, K.N_7_7902: K.N_3_4E07}[opc]
            if to == t:
                raise Unsupported(K.S_FCVT_SAME_B288)
            if to == K.N_3_4E07:
                return f'c->v[{d}] = {K.S_SIMD_S2H_8914 if not t else K.S_SIMD_DD2H_5720}(c, {SF}({n}));'
            return f'FP_SETD({d}, simd_s2d(c, VSF32({n})));' if to else f'FP_SETS({d}, simd_d2s(c, VSF64({n})));'
        rm = {K.N_8_2C62: 0, K.N_9_1958: 1, K.N_10_4A44: K.N_2_D473, K.N_11_4FC8: K.N_3_4E07, K.N_12_6B51: K.N_4_4B22, K.N_14_8527: K.N_5_EF2D, K.N_15_E629: K.N_6_E7F6}.get(opc)
        if rm is None:
            raise Unsupported(K.S_FP_DP1_OPCODE_6E4C)
        return f'{SET}({d}, frint_{F}(c, {SF}({n}), {rm}));'

    def fp_cmp(self, w):
        t = self._ftype(w)
        if bits(w, K.N_14_8527, K.N_2_D473) or bits(w, 0, K.N_3_4E07):
            raise Unsupported(K.S_FCMP_OP_6528)
        F, VT, VU, T, SF, SU, SET = FT[t]
        n, m, zero = bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_3_4E07, 1)
        b = K.S_0_59A9 if zero else f'{SF}({m})'
        return f'c->nzcv = cmp_{F}(c, {SF}({n}), {b});'

    def fp_ccmp(self, w):
        t = self._ftype(w)
        F, VT, VU, T, SF, SU, SET = FT[t]
        n, m, cnd, nzcv = bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_12_6B51, K.N_4_4B22), bits(w, 0, K.N_4_4B22)
        return f'c->nzcv = {cond(cnd)} ? cmp_{F}(c, {SF}({n}), {SF}({m})) : {hx(nzcv << K.N_28_59E1)};'

    def fp_csel(self, w):
        t = self._ftype(w)
        SU = FT[t][K.N_5_EF2D]
        d, n, m, cnd = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_12_6B51, K.N_4_4B22)
        return f'c->v[{d}] = {cond(cnd)} ? {SU}({n}) : {SU}({m});'

    def fp_imm(self, w):
        t = self._ftype(w)
        if bits(w, K.N_5_EF2D, K.N_5_EF2D):
            raise Unsupported(K.S_FMOV_IMM5_48E0)
        imm8, d = bits(w, K.N_13_3FDB, K.N_8_2C62), bits(w, 0, K.N_5_EF2D)
        return f'c->v[{d}] = {hx(vfp_expand_imm(imm8, K.N_32_E29C << t))};'

    def fp_fixed(self, w):
        sf, t, rmode, opc, scale, n, d = (bits(w, K.N_31_EB1E, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_19_9400, K.N_2_D473), bits(w, K.N_16_B17E, K.N_3_4E07), bits(w, K.N_10_4A44, K.N_6_E7F6),
                                          bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        if t > 1:
            raise Unsupported(K.S_FIXED_TYPE_768A)
        if not sf and scale < K.N_32_E29C:
            raise Unsupported(K.S_FIXED_SCALE_A7E5)
        F, VT, VU, T, SF, SU, SET = FT[t]
        fb, ib = K.N_64_A68B - scale, K.N_64_A68B if sf else K.N_32_E29C
        if rmode == 0 and opc in (K.N_2_D473, K.N_3_4E07):
            src = X(n) if sf else W(n)
            return f'{SET}({d}, fromint_{F}(c, {src}, {ib}, {opc & 1}, {fb}));'
        if rmode == K.N_3_4E07 and opc in (0, 1):
            return set_reg(sf, d, f'toint_{F}(c, {SF}({n}), 3, {fb}, {ib}, {opc})')
        raise Unsupported(K.S_FIXED_OPCODE_19C1)

    def fp_int(self, w):
        sf, t, rmode, opc, n, d = bits(w, K.N_31_EB1E, 1), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_19_9400, K.N_2_D473), bits(w, K.N_16_B17E, K.N_3_4E07), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc in (K.N_6_E7F6, K.N_7_7902):  # FMOV
            if t == K.N_2_D473 and sf and rmode == 1:
                return set_reg(1, d, f'VHI64({n})') if opc == K.N_6_E7F6 else f'VHI64({d}) = {X(n)};'
            if rmode or t != sf or t > 1:
                raise Unsupported(K.S_FMOV_GENERAL_A7B5)
            if opc == K.N_6_E7F6:
                return set_reg(sf, d, f'VS64({n})' if sf else f'VS32({n})')
            return f'c->v[{d}] = {X(n) if sf else W(n)};'
        if t > 1:
            raise Unsupported(K.S_FP_INT_TYPE_1BC0)
        F, VT, VU, T, SF, SU, SET = FT[t]
        ib = K.N_64_A68B if sf else K.N_32_E29C
        if opc in (K.N_2_D473, K.N_3_4E07):
            if rmode:
                raise Unsupported(K.S_CVTF_RMODE_6D0C)
            return f'{SET}({d}, fromint_{F}(c, {X(n) if sf else W(n)}, {ib}, {opc & 1}, 0));'
        if opc in (K.N_4_4B22, K.N_5_EF2D):
            if rmode:
                raise Unsupported(K.S_FCVTA_RMODE_39CE)
            rm = K.N_4_4B22
        else:
            rm = (0, 1, K.N_2_D473, K.N_3_4E07)[rmode]
        return set_reg(sf, d, f'toint_{F}(c, {SF}({n}), {rm}, 0, {ib}, {opc & 1})')

    # ------------------------------------------------------------ loads / stores

    def ldst(self, w, pc):
        rt = bits(w, 0, K.N_5_EF2D)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_3_4E07 and bits(w, K.N_24_C235, K.N_2_D473) == 0:  # literal
            opc = bits(w, K.N_30_624B, K.N_2_D473)
            if opc == K.N_3_4E07:
                raise Unsupported(K.S_LDR_LITERAL_OPC_5C26)
            addr = (pc + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22) & ((1 << K.N_64_A68B) - 1)
            return f'c->v[{rt}] = {(K.S_LD32_318D, K.S_LD64_7930, K.S_LD128_9549)[opc]}({hx(addr)});'
        if bits(w, K.N_31_EB1E, 1) == 0 and bits(w, K.N_24_C235, K.N_6_E7F6) in (K.N_12_6B51, K.N_13_3FDB):
            return self.ldst_struct(w)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_5_EF2D:
            return self.ldst_pair(w)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_7_7902:
            return self.ldst_reg(w)
        raise Unsupported(f'ldst simd {w:08x}')

    def ldst_pair(self, w):
        opc, mode, L = bits(w, K.N_30_624B, K.N_2_D473), bits(w, K.N_23_535F, K.N_2_D473), bits(w, K.N_22_785F, 1)
        imm7, rt2, rn, rt = sext(bits(w, K.N_15_E629, K.N_7_7902), K.N_7_7902), bits(w, K.N_10_4A44, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc == K.N_3_4E07:
            raise Unsupported(K.S_PAIR_OPC_3_D6DA)
        scale = K.N_2_D473 + opc
        off = imm7 << scale
        base = XS(rn)
        post, wb = mode == 1, mode in (1, K.N_3_4E07)
        ea = base if post else (f'{base} + {off}' if off >= 0 else f'{base} - {-off}')
        ld, st = {K.N_2_D473: (K.S_LD32_318D, K.S_ST32_8225), K.N_3_4E07: (K.S_LD64_7930, K.S_ST64_0347), K.N_4_4B22: (K.S_LD128_9549, K.S_ST128_C9F6)}[scale]
        sz = 1 << scale
        lines = [f'uint64_t ea = {ea};']
        if L:
            lines.append(f'u128 a = {ld}(ea), b = {ld}(ea + {sz});')
            lines.append(f'c->v[{rt}] = a; c->v[{rt2}] = b;')
        else:
            cast = {K.N_2_D473: K.S_VS32_F929, K.N_3_4E07: K.S_VS64_147B, K.N_4_4B22: K.S_C_V_EDC1}[scale]
            v1, v2 = (f'c->v[{rt}]', f'c->v[{rt2}]') if scale == K.N_4_4B22 else (f'{cast}({rt})', f'{cast}({rt2})')
            lines.append(f'{st}(ea, {v1}); {st}(ea + {sz}, {v2});')
        if wb:
            dst = K.S_C_SP_4C74 if rn == K.N_31_EB1E else f'c->x[{rn}]'
            if not post:
                lines.append(f'{dst} = ea;')
            else:
                lines.append(f'{dst} = ea + {off};' if off >= 0 else f'{dst} = ea - {-off};')
        return K.S_EMPTY_DCF7 + K.S_EMPTY_730D.join(lines) + K.S_EMPTY_1498

    def ldst_reg(self, w):
        size, opc, rn, rt = bits(w, K.N_30_624B, K.N_2_D473), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc & K.N_2_D473:
            if size:
                raise Unsupported(K.S_LDST_SIMD_SIZE_20AD)
            scale = K.N_4_4B22
        else:
            scale = size
        L = opc & 1
        base = XS(rn)
        wb = None
        if bits(w, K.N_24_C235, K.N_2_D473) == 1:
            ea = f'{base} + {hx(bits(w, K.N_10_4A44, K.N_12_6B51) << scale)}'
        elif bits(w, K.N_24_C235, K.N_2_D473) == 0:
            kind = bits(w, K.N_10_4A44, K.N_2_D473)
            if bits(w, K.N_21_6F4B, 1):
                if kind != K.N_2_D473:
                    raise Unsupported(K.S_LDST_SIMD_REG_KIND_AFBA)
                rm, option, S = bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_13_3FDB, K.N_3_4E07), bits(w, K.N_12_6B51, 1)
                if option not in (K.N_2_D473, K.N_3_4E07, K.N_6_E7F6, K.N_7_7902):
                    raise Unsupported(K.S_LDST_SIMD_REG_OPTION_27F8)
                ea = f'{base} + {extend_expr(rm, option, scale if S else 0)}'
            else:
                imm9 = sext(bits(w, K.N_12_6B51, K.N_9_1958), K.N_9_1958)
                si = f'+ {imm9}' if imm9 >= 0 else f'- {-imm9}'
                if kind == 0:
                    ea = f'{base} {si}'
                elif kind == 1:
                    ea, wb = base, f'{base} {si}'
                elif kind == K.N_3_4E07:
                    ea, wb = f'{base} {si}', K.S_EA_337F
                else:
                    raise Unsupported(K.S_LDST_SIMD_UNPRIVILEGED_9B7E)
        else:
            raise Unsupported(K.S_LDST_SIMD_938E)
        parts = [f'uint64_t ea = {ea};']
        if L:
            parts.append(f'c->v[{rt}] = {(K.S_LD8_8132, K.S_LD16_A122, K.S_LD32_318D, K.S_LD64_7930, K.S_LD128_9549)[scale]}(ea);')
        else:
            src = (f'VS8({rt})', f'VS16({rt})', f'VS32({rt})', f'VS64({rt})', f'c->v[{rt}]')[scale]
            parts.append(f'{(K.S_ST8_664A, K.S_ST16_FBE6, K.S_ST32_8225, K.S_ST64_0347, K.S_ST128_C9F6)[scale]}(ea, {src});')
        if wb:
            parts.append(f'{K.S_C_SP_4C74 if rn == K.N_31_EB1E else f"c->x[{rn}]"} = {wb};')
        return K.S_EMPTY_DCF7 + K.S_EMPTY_730D.join(parts) + K.S_EMPTY_1498

    def ldst_struct(self, w):
        Q, L, post, rm, rn, rt, sz = (bits(w, K.N_30_624B, 1), bits(w, K.N_22_785F, 1), bits(w, K.N_23_535F, 1), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D),
                                      bits(w, 0, K.N_5_EF2D), bits(w, K.N_10_4A44, K.N_2_D473))
        if not post and rm:
            raise Unsupported(K.S_LDST_STRUCT_RM_977A)
        base = XS(rn)
        if bits(w, K.N_24_C235, 1) == 0:  # multiple structures
            if bits(w, K.N_21_6F4B, 1):
                raise Unsupported(K.S_LDST_MULTIPLE_BIT21_65E9)
            opc = bits(w, K.N_12_6B51, K.N_4_4B22)
            sel = {0: (K.N_4_4B22, 1), K.N_2_D473: (1, K.N_4_4B22), K.N_4_4B22: (K.N_3_4E07, 1), K.N_6_E7F6: (1, K.N_3_4E07), K.N_7_7902: (1, 1), K.N_8_2C62: (K.N_2_D473, 1), K.N_10_4A44: (1, K.N_2_D473)}.get(opc)
            if sel is None:
                raise Unsupported(K.S_LDST_MULTIPLE_OPCODE_FF73)
            selem, rpt = sel
            if sz == K.N_3_4E07 and not Q and selem > 1:
                raise Unsupported(K.S_LDST_MULTIPLE_1D_7B61)
            total = (K.N_8_2C62 << Q) * selem * rpt
            if selem == 1:  # LD1/ST1: whole registers
                regs = [(rt + i) % K.N_32_E29C for i in range(rpt)]
                step = K.N_16_B17E if Q else K.N_8_2C62
                if L:
                    acc = K.S_EMPTY_730D.join(f'c->v[{r}] = {K.S_LD128_9549 if Q else K.S_LD64_7930}(ea + {i * step});' for i, r in enumerate(regs))
                else:
                    acc = K.S_EMPTY_730D.join(f'{K.S_ST128_C9F6 if Q else K.S_ST64_0347}(ea + {i * step}, {K.S_C_V_D_75F5 % r if Q else K.S_VS64_D_2B48 % r});'
                                   for i, r in enumerate(regs))
            else:
                acc = f'sl_{K.S_LDM_2178 if L else K.S_STM_7CCD}(c, {Q}, {sz}, {selem}, {rpt}, {rt}, ea);'
        else:  # single structure
            opc, S, R = bits(w, K.N_13_3FDB, K.N_3_4E07), bits(w, K.N_12_6B51, 1), bits(w, K.N_21_6F4B, 1)
            scale = opc >> 1
            selem = ((opc & 1) << 1 | R) + 1
            if scale == K.N_3_4E07:
                if not L or S:
                    raise Unsupported(K.S_LDNR_1AEB)
                total = selem << sz
                acc = f'sl_ldr(c, {Q}, {sz}, {selem}, {rt}, ea);'
            else:
                if scale == 0:
                    idx = (Q << K.N_3_4E07) | (S << K.N_2_D473) | sz
                elif scale == 1:
                    if sz & 1:
                        raise Unsupported(K.S_LD1_H_SIZE_A329)
                    idx = (Q << K.N_2_D473) | (S << 1) | (sz >> 1)
                else:
                    if sz & K.N_2_D473:
                        raise Unsupported(K.S_LD1_S_SIZE_18F3)
                    if sz & 1:
                        if S:
                            raise Unsupported(K.S_LD1_D_S_A09A)
                        idx, scale = Q, K.N_3_4E07
                    else:
                        idx = (Q << 1) | S
                total = selem << scale
                acc = f'sl_{K.S_LDS_F5D2 if L else K.S_STS_1DF0}(c, {scale}, {selem}, {idx}, {rt}, ea);'
        parts = [f'uint64_t ea = {base};', acc]
        if post:
            dst = K.S_C_SP_4C74 if rn == K.N_31_EB1E else f'c->x[{rn}]'
            parts.append(f'{dst} = ea + {total};' if rm == K.N_31_EB1E else f'{dst} = ea + {X(rm)};')
        return K.S_EMPTY_DCF7 + K.S_EMPTY_730D.join(parts) + K.S_EMPTY_1498

    # ------------------------------------------------------------ CRC32

    @staticmethod
    def crc(w):
        sf, rm, C, sz, rn, rd = bits(w, K.N_31_EB1E, 1), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_12_6B51, 1), bits(w, K.N_10_4A44, K.N_2_D473), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if (sz == K.N_3_4E07) != bool(sf):
            raise Unsupported(K.S_CRC_SIZE_D585)
        val = X(rm) if sz == K.N_3_4E07 else W(rm)
        return set_reg(0, rd, f'{K.S_CRC32C_6D63 if C else K.S_CRC32_229F}({W(rn)}, {val}, {1 << sz})')


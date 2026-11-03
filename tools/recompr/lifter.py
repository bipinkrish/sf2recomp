"""AArch64 -> C static recompiler.

Integer, memory, and control flow are translated to C. FP/SIMD instructions (and CRC32) are
either re-emitted as inline assembly (simd='asm', AArch64 hosts), with guest V registers
loaded from / stored to the Cpu struct around each batch, so their semantics are
bit-exact by construction, or lowered to portable C (simd='c', tools/simd_c.py + runtime/simd.h).
"""
from _sf2_recompiler_constants import LIFTER as K
import struct

# Cpu struct offsets (must match runtime/cpu.h)
OFF_X = 0
OFF_SP = K.OFF_SP
OFF_PC = K.OFF_PC
OFF_NZCV = K.OFF_NZCV
OFF_V = K.OFF_V

COND_NAMES = [K.S_EQ_3E2E, K.S_NE_81F8, K.S_CS_30F3, K.S_CC_C2C9, K.S_MI_E7D6, K.S_PL_6BA0, K.S_VS_013A, K.S_VC_B2C8, K.S_HI_C455, K.S_LS_C412, K.S_GE_9AD6, K.S_LT_139F, K.S_GT_866E, K.S_LE_F391, K.S_AL_61A4, K.S_NV_E001]
SCRATCH = [K.N_9_1958, K.N_10_4A44, K.N_11_4FC8, K.N_12_6B51, K.N_13_3FDB, K.N_14_8527, K.N_15_E629, K.N_16_B17E, K.N_17_4523]


class Unsupported(Exception):
    pass


def bits(w, lo, n):
    return (w >> lo) & ((1 << n) - 1)


def sext(v, n):
    return v - (1 << n) if v & (1 << (n - 1)) else v


def ones(n):
    return (1 << n) - 1


def ror(v, r, size):
    r %= size
    return ((v >> r) | (v << (size - r))) & ones(size) if r else v


def replicate(v, esize, size):
    out = 0
    for i in range(0, size, esize):
        out |= v << i
    return out


def decode_bit_masks(N, imms, immr, size, immediate):
    x = (N << K.N_6_E7F6) | (~imms & K.N_63_DA4E)
    if x == 0:
        raise Unsupported(K.S_RESERVED_BITMASK_7BE5)
    length = x.bit_length() - 1
    if length < 1:
        raise Unsupported(K.S_RESERVED_BITMASK_7BE5)
    levels = ones(length)
    if immediate and (imms & levels) == levels:
        raise Unsupported(K.S_RESERVED_BITMASK_7BE5)
    S = imms & levels
    R = immr & levels
    diff = (S - R) & K.N_63_DA4E
    esize = 1 << length
    d = diff & ones(length)
    welem = ones(S + 1)
    telem = ones(d + 1)
    wmask = replicate(ror(welem, R, esize), esize, size)
    tmask = replicate(telem, esize, size)
    return wmask, tmask


def hx(v):
    return f'0x{v:x}ull'


# ---------------------------------------------------------------- register helpers

def X(r):
    return K.S_0ULL_8C6F if r == K.N_31_EB1E else f'c->x[{r}]'


def XS(r):
    return K.S_C_SP_4C74 if r == K.N_31_EB1E else f'c->x[{r}]'


def W(r):
    return K.S_0U_4780 if r == K.N_31_EB1E else f'(uint32_t)c->x[{r}]'


def WS(r):
    return K.S_UINT32_T_C_SP_B11B if r == K.N_31_EB1E else f'(uint32_t)c->x[{r}]'


def R(sf, r, sp=False):
    if sf:
        return XS(r) if sp else X(r)
    return WS(r) if sp else W(r)


def set_reg(sf, r, expr, sp=False):
    """Statement writing expr to Rd (zero-extending 32-bit results)."""
    if r == K.N_31_EB1E and not sp:
        return f'(void)({expr});'
    dst = K.S_C_SP_4C74 if r == K.N_31_EB1E else f'c->x[{r}]'
    if sf:
        return f'{dst} = (uint64_t)({expr});'
    return f'{dst} = (uint32_t)({expr});'


def shift_expr(sf, val, typ, amt):
    if amt == 0 and typ != K.N_3_4E07:
        return val
    if typ == 0:
        return f'({val} << {amt})'
    if typ == 1:
        return f'({val} >> {amt})'
    if typ == K.N_2_D473:
        return f'(uint64_t)((int64_t){val} >> {amt})' if sf else f'(uint32_t)((int32_t){val} >> {amt})'
    return f'ROR{K.N_64_A68B if sf else K.N_32_E29C}({val}, {amt})'


def extend_expr(r, option, shift):
    base = X(r)
    e = {
        0: f'(uint64_t)(uint8_t){base}',
        1: f'(uint64_t)(uint16_t){base}',
        K.N_2_D473: f'(uint64_t)(uint32_t){base}',
        K.N_3_4E07: base,
        K.N_4_4B22: f'(uint64_t)(int64_t)(int8_t){base}',
        K.N_5_EF2D: f'(uint64_t)(int64_t)(int16_t){base}',
        K.N_6_E7F6: f'(uint64_t)(int64_t)(int32_t){base}',
        K.N_7_7902: base,
    }[option]
    return f'({e} << {shift})' if shift else e


# ---------------------------------------------------------------- instruction classes

class Insn:
    """Result of lifting one instruction."""
    __slots__ = (K.S_KIND_395E, K.S_CODE_2188, K.S_TARGET_0B9D, K.S_COND_6BDA, K.S_ASM_3C46)

    def __init__(self, kind, code=K.S_EMPTY_6F49, target=None, cond=None, asm=None):
        self.kind = kind      # 'op', 'asm', 'b', 'bcond', 'bl', 'blr', 'br', 'ret', 'trap'
        self.code = code      # C statements (for 'op'), or condition expr for 'bcond'
        self.target = target  # branch target (int) or register expr
        self.cond = cond
        self.asm = asm        # AsmInsn for 'asm'


class AsmInsn:
    """An FP/SIMD instruction to pass through as inline asm."""
    __slots__ = (K.S_WORD_D537, K.S_VREGS_FD8A, K.S_GPRS_B3A0, K.S_FLAGS_13A4, K.S_CONSTS_031A)

    def __init__(self, word, vregs, gprs, flags=False, consts=None):
        self.word = word
        self.vregs = vregs    # set of V register numbers touched
        self.gprs = gprs      # list of (bit position, guest reg or 'sp')
        self.flags = flags
        self.consts = consts or []  # list of (bit position, constant) loaded into scratch


def is_fp_simd_dp(w):
    return bits(w, K.N_25_B7A5, K.N_3_4E07) == K.N_7_7902


def is_ldst(w):
    return bits(w, K.N_25_B7A5, 1) == 0 and bits(w, K.N_27_6706, 1) == 1


class Lifter:
    def __init__(self, base, simd=K.S_ASM_3C46):
        self.base = base  # absolute load address of the module (guest == host)
        if simd not in (K.S_ASM_3C46, K.S_C_0E09):
            raise ValueError(f'simd={simd!r}')
        self.sc = None
        if simd == K.S_C_0E09:
            from simd_c import SimdC
            self.sc = SimdC()

    def lift(self, w, pc):
        """pc is absolute. Returns an Insn."""
        op0 = bits(w, K.N_25_B7A5, K.N_4_4B22)
        if (op0 & K.N_14_8527) == K.N_8_2C62:
            return self.dp_imm(w, pc)
        if (op0 & K.N_14_8527) == K.N_10_4A44:
            return self.branch_sys(w, pc)
        if (op0 & K.N_5_EF2D) == K.N_4_4B22:
            return self.ldst(w, pc)
        if (op0 & K.N_7_7902) == K.N_5_EF2D:
            return self.dp_reg(w, pc)
        if (op0 & K.N_7_7902) == K.N_7_7902:
            return self.fp_simd(w, pc)
        # padding, trap words (e7ffdefe), header data: trap if ever executed
        return Insn(K.S_TRAP_1946, f'rt_udf(c, {hx(pc)});')

    # ------------------------------------------------------------ data processing (imm)

    def dp_imm(self, w, pc):
        op = bits(w, K.N_23_535F, K.N_3_4E07)
        sf = bits(w, K.N_31_EB1E, 1)
        rd, rn = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D)
        if op in (0, 1):  # PC-relative
            imm = sext((bits(w, K.N_5_EF2D, K.N_19_9400) << K.N_2_D473) | bits(w, K.N_29_3513, K.N_2_D473), K.N_21_6F4B)
            if bits(w, K.N_31_EB1E, 1):
                val = (pc & ~K.N_4095_1AFC) + (imm << K.N_12_6B51)
            else:
                val = pc + imm
            return Insn(K.S_OP_B27B, set_reg(1, rd, hx(val & ones(K.N_64_A68B))))
        if op == K.N_2_D473:  # add/sub imm
            opc, S, sh = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1), bits(w, K.N_22_785F, 1)
            imm = bits(w, K.N_10_4A44, K.N_12_6B51) << (K.N_12_6B51 if sh else 0)
            a = R(sf, rn, sp=True)
            if S:
                fn = (K.S_SUBS_8974 if opc else K.S_ADDS_F132) + (K.S_64_FD5F if sf else K.S_32_BBFB)
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{fn}(c, {a}, {hx(imm)})'))
            return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{a} {K.S_EMPTY_01F8 if opc else K.S_EMPTY_3604} {hx(imm)}', sp=True))
        if op == K.N_4_4B22:  # logical imm
            opc, N = bits(w, K.N_29_3513, K.N_2_D473), bits(w, K.N_22_785F, 1)
            if not sf and N:
                raise Unsupported(K.S_LOGICAL_IMM_N_1_32_BIT_582C)
            size = K.N_64_A68B if sf else K.N_32_E29C
            imm, _ = decode_bit_masks(N, bits(w, K.N_10_4A44, K.N_6_E7F6), bits(w, K.N_16_B17E, K.N_6_E7F6), size, True)
            a = R(sf, rn)
            if opc == 0:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{a} & {hx(imm)}', sp=True))
            if opc == 1:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{a} | {hx(imm)}', sp=True))
            if opc == K.N_2_D473:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{a} ^ {hx(imm)}', sp=True))
            return Insn(K.S_OP_B27B, set_reg(sf, rd, f'logic{size}(c, {a} & {hx(imm)})'))
        if op == K.N_5_EF2D:  # move wide
            opc, hw = bits(w, K.N_29_3513, K.N_2_D473), bits(w, K.N_21_6F4B, K.N_2_D473)
            if not sf and hw > 1:
                raise Unsupported(K.S_MOVW_HW_2A17)
            imm = bits(w, K.N_5_EF2D, K.N_16_B17E) << (hw * K.N_16_B17E)
            if opc == 0:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, hx(~imm & ones(K.N_64_A68B if sf else K.N_32_E29C))))
            if opc == K.N_2_D473:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, hx(imm)))
            if opc == K.N_3_4E07:
                mask = ~(K.N_65535_F2F8 << (hw * K.N_16_B17E)) & ones(K.N_64_A68B)
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'({X(rd)} & {hx(mask)}) | {hx(imm)}'))
            raise Unsupported(K.S_MOVW_OPC_0FE3)
        if op == K.N_6_E7F6:  # bitfield
            opc, N = bits(w, K.N_29_3513, K.N_2_D473), bits(w, K.N_22_785F, 1)
            immr, imms = bits(w, K.N_16_B17E, K.N_6_E7F6), bits(w, K.N_10_4A44, K.N_6_E7F6)
            size = K.N_64_A68B if sf else K.N_32_E29C
            if N != sf:
                raise Unsupported(K.S_BITFIELD_N_19F5)
            wmask, tmask = decode_bit_masks(N, imms, immr, size, False)
            src = R(sf, rn)
            rot = f'ROR{size}({src}, {immr})' if immr else src
            if opc == K.N_2_D473:  # UBFM
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{rot} & {hx(wmask & tmask)}'))
            if opc == 0:  # SBFM
                top = f'(uint{size}_t)(-(int{size}_t)(({src} >> {imms}) & 1))'
                return Insn(K.S_OP_B27B, set_reg(
                    sf, rd, f'({top} & {hx(~tmask & ones(size))}) | ({rot} & {hx(wmask & tmask)})'))
            if opc == 1:  # BFM
                dst = R(sf, rd)
                bot = f'(({dst} & {hx(~wmask & ones(size))}) | ({rot} & {hx(wmask)}))'
                return Insn(K.S_OP_B27B, set_reg(
                    sf, rd, f'({dst} & {hx(~tmask & ones(size))}) | ({bot} & {hx(tmask)})'))
            raise Unsupported(K.S_BITFIELD_OPC_1766)
        if op == K.N_7_7902:  # EXTR
            rm, lsb = bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_10_4A44, K.N_6_E7F6)
            size = K.N_64_A68B if sf else K.N_32_E29C
            if lsb == 0:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, R(sf, rm)))
            return Insn(K.S_OP_B27B, set_reg(
                sf, rd, f'({R(sf, rm)} >> {lsb}) | ((uint{size}_t){R(sf, rn)} << {size - lsb})'))
        raise Unsupported(f'dp_imm {w:08x}')

    # ------------------------------------------------------------ branches / system

    def branch_sys(self, w, pc):
        if bits(w, K.N_26_5F9C, K.N_5_EF2D) == K.N_5_EF2D:  # B / BL
            t = (pc + sext(bits(w, 0, K.N_26_5F9C), K.N_26_5F9C) * K.N_4_4B22) & ones(K.N_64_A68B)
            return Insn(K.S_BL_4BCD if bits(w, K.N_31_EB1E, 1) else K.S_B_FA34, target=t)
        if bits(w, K.N_25_B7A5, K.N_7_7902) == K.N_42_7347 and not bits(w, K.N_4_4B22, 1):  # B.cond
            t = pc + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22
            cond = bits(w, 0, K.N_4_4B22)
            if cond >= K.N_14_8527:
                return Insn(K.S_B_FA34, target=t)
            return Insn(K.S_BCOND_44B7, target=t, cond=f'COND_{COND_NAMES[cond].upper()}(c)')
        if bits(w, K.N_25_B7A5, K.N_6_E7F6) == K.N_26_5F9C:  # CBZ/CBNZ
            sf, nz, rt = bits(w, K.N_31_EB1E, 1), bits(w, K.N_24_C235, 1), bits(w, 0, K.N_5_EF2D)
            t = pc + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22
            return Insn(K.S_BCOND_44B7, target=t, cond=f'{R(sf, rt)} {K.S_EMPTY_A75E if nz else K.S_EMPTY_A0B1} 0')
        if bits(w, K.N_25_B7A5, K.N_6_E7F6) == K.N_27_6706:  # TBZ/TBNZ
            b = (bits(w, K.N_31_EB1E, 1) << K.N_5_EF2D) | bits(w, K.N_19_9400, K.N_5_EF2D)
            nz, rt = bits(w, K.N_24_C235, 1), bits(w, 0, K.N_5_EF2D)
            t = pc + sext(bits(w, K.N_5_EF2D, K.N_14_8527), K.N_14_8527) * K.N_4_4B22
            return Insn(K.S_BCOND_44B7, target=t, cond=f'(({X(rt)} >> {b}) & 1) {K.S_EMPTY_A75E if nz else K.S_EMPTY_A0B1} 0')
        if bits(w, K.N_25_B7A5, K.N_7_7902) == K.N_107_3346:  # branch register
            opc, rn = bits(w, K.N_21_6F4B, K.N_4_4B22), bits(w, K.N_5_EF2D, K.N_5_EF2D)
            if bits(w, K.N_16_B17E, K.N_5_EF2D) != K.N_31_EB1E or bits(w, K.N_10_4A44, K.N_6_E7F6) or bits(w, 0, K.N_5_EF2D):
                raise Unsupported(K.S_BRANCH_REG_VARIANT_F81F)
            if opc == 0:
                return Insn(K.S_BR_68B5, target=X(rn))
            if opc == 1:
                return Insn(K.S_BLR_FE23, target=X(rn))
            if opc == K.N_2_D473:
                return Insn(K.S_RET_2523)
            raise Unsupported(K.S_BRANCH_REG_OPC_0633)
        if bits(w, K.N_24_C235, K.N_8_2C62) == K.N_212_FA2B:  # exception generation
            opc, ll, imm = bits(w, K.N_21_6F4B, K.N_3_4E07), bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_16_B17E)
            if opc == 0 and ll == 1:
                return Insn(K.S_OP_B27B, f'rt_svc(c, {imm}, {hx(pc)});')
            if opc == 1 and ll == 0:
                return Insn(K.S_TRAP_1946, f'rt_brk(c, {imm}, {hx(pc)});')
            raise Unsupported(K.S_EXCEPTION_AB8B)
        if bits(w, K.N_22_785F, K.N_10_4A44) == K.N_852_929F:
            return self.system(w, pc)
        raise Unsupported(f'branch_sys {w:08x}')

    def system(self, w, pc):
        L = bits(w, K.N_21_6F4B, 1)
        op0, op1, crn, crm, op2, rt = (bits(w, K.N_19_9400, K.N_2_D473), bits(w, K.N_16_B17E, K.N_3_4E07), bits(w, K.N_12_6B51, K.N_4_4B22),
                                       bits(w, K.N_8_2C62, K.N_4_4B22), bits(w, K.N_5_EF2D, K.N_3_4E07), bits(w, 0, K.N_5_EF2D))
        if op0 == 0 and not L:
            if crn == K.N_2_D473 and op1 == K.N_3_4E07 and rt == K.N_31_EB1E:  # hints
                return Insn(K.S_OP_B27B, K.S_HINT_5CF7)
            if crn == K.N_3_4E07 and op1 == K.N_3_4E07 and rt == K.N_31_EB1E:  # barriers
                if op2 == K.N_2_D473:
                    return Insn(K.S_OP_B27B, K.S_C_EXCL_ADDR_0ULL_4867)
                return Insn(K.S_OP_B27B, K.S_ATOMIC_THREAD_FENCE_ATOMIC_S_55E6)
            raise Unsupported(f'pstate/system {w:08x}')
        if op0 == 1:  # SYS
            if not L and op1 == K.N_3_4E07 and crn == K.N_7_7902 and crm == K.N_4_4B22 and op2 == 1:  # DC ZVA
                return Insn(K.S_OP_B27B, f'memset((void*)(uintptr_t)({X(rt)} & ~63ull), 0, 64);')
            return Insn(K.S_OP_B27B, K.S_CACHE_MAINTENANCE_144B)
        key = (op0, op1, crn, crm, op2)
        if L:  # MRS
            src = {
                (K.N_3_4E07, K.N_3_4E07, K.N_13_3FDB, 0, K.N_3_4E07): K.S_C_TPIDRRO_D18D,
                (K.N_3_4E07, K.N_3_4E07, K.N_13_3FDB, 0, K.N_2_D473): K.S_C_TPIDR_E847,
                (K.N_3_4E07, K.N_3_4E07, K.N_14_8527, 0, 0): K.S_19200000ULL_F8A0,
                (K.N_3_4E07, K.N_3_4E07, K.N_14_8527, 0, 1): K.S_RT_TICKS_8115,
                (K.N_3_4E07, K.N_3_4E07, K.N_14_8527, 0, K.N_2_D473): K.S_RT_TICKS_8115,
                (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_2_D473, 0): K.S_C_NZCV_32C7,
                (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_4_4B22, 0): K.S_C_FPCR_C410 if self.sc else K.S_RT_GET_FPCR_F0BD,
                (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_4_4B22, 1): K.S_C_FPSR_119A if self.sc else K.S_RT_GET_FPSR_A7A5,
                (K.N_3_4E07, K.N_3_4E07, 0, 0, K.N_7_7902): K.S_4ULL_B5B7,
                (K.N_3_4E07, K.N_3_4E07, 0, 0, 1): K.S_0X8444C004ULL_0D50,
            }.get(key)
            if src is None:
                raise Unsupported(f'mrs {key}')
            return Insn(K.S_OP_B27B, set_reg(1, rt, src))
        dst = {
            (K.N_3_4E07, K.N_3_4E07, K.N_13_3FDB, 0, K.N_2_D473): K.S_C_TPIDR_V_3DFB,
            (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_2_D473, 0): K.S_C_NZCV_V_0XF0000000ULL_62E9,
            (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_4_4B22, 0): K.S_C_FPCR_V_0X7C00000ULL_8A61 if self.sc else K.S_RT_SET_FPCR_V_52F5,
            (K.N_3_4E07, K.N_3_4E07, K.N_4_4B22, K.N_4_4B22, 1): K.S_C_FPSR_V_0XF800009FULL_06E1 if self.sc else K.S_RT_SET_FPSR_V_066D,
        }.get(key)
        if dst is None:
            raise Unsupported(f'msr {key}')
        return Insn(K.S_OP_B27B, dst.format(v=X(rt)))

    # ------------------------------------------------------------ loads / stores

    def ldst(self, w, pc):
        V = bits(w, K.N_26_5F9C, 1)
        if V:
            if self.sc:
                return Insn(K.S_OP_B27B, self.sc.ldst(w, pc))
            return self.ldst_simd(w, pc)
        if bits(w, K.N_24_C235, K.N_6_E7F6) == K.N_8_2C62:
            return self.ldst_excl(w, pc)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_3_4E07 and bits(w, K.N_24_C235, K.N_2_D473) == 0:  # literal
            opc, rt = bits(w, K.N_30_624B, K.N_2_D473), bits(w, 0, K.N_5_EF2D)
            addr = pc + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22
            if opc == 0:
                return Insn(K.S_OP_B27B, set_reg(0, rt, f'LD32({hx(addr)})'))
            if opc == 1:
                return Insn(K.S_OP_B27B, set_reg(1, rt, f'LD64({hx(addr)})'))
            if opc == K.N_2_D473:
                return Insn(K.S_OP_B27B, set_reg(1, rt, f'(int64_t)(int32_t)LD32({hx(addr)})'))
            return Insn(K.S_OP_B27B, K.S_PRFM_54D2)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_5_EF2D:
            return self.ldst_pair(w)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_7_7902:
            return self.ldst_reg(w)
        raise Unsupported(f'ldst {w:08x}')

    def ldst_excl(self, w, pc):
        size, o2, L, o1, rs, o0, rn, rt = (bits(w, K.N_30_624B, K.N_2_D473), bits(w, K.N_23_535F, 1), bits(w, K.N_22_785F, 1), bits(w, K.N_21_6F4B, 1),
                                           bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_15_E629, 1), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D))
        nbits = K.N_8_2C62 << size
        addr = XS(rn)
        if o1:
            rt2 = bits(w, K.N_10_4A44, K.N_5_EF2D)
            if L:
                if nbits == K.N_64_A68B:
                    return Insn(K.S_OP_B27B, f'ldxp64(c, {addr}, &c->x[{rt}], &c->x[{rt2}]);')
                return Insn(K.S_OP_B27B, f'ldxp32(c, {addr}, &c->x[{rt}], &c->x[{rt2}]);')
            else:
                if nbits == K.N_64_A68B:
                    return Insn(K.S_OP_B27B, f'{{ uint32_t st = stxp64(c, {addr}, {X(rt)}, {X(rt2)}); {set_reg(0, rs, K.S_ST_F5C7)} }}')
                return Insn(K.S_OP_B27B, f'{{ uint32_t st = stxp32(c, {addr}, (uint32_t){X(rt)}, (uint32_t){X(rt2)}); {set_reg(0, rs, K.S_ST_F5C7)} }}')

        if o2 == 0:
            if L:
                return Insn(K.S_OP_B27B, set_reg(1, rt, f'ldx{nbits}(c, {addr})'))
            return Insn(K.S_OP_B27B, f'{{ uint32_t st = stx{nbits}(c, {addr}, {X(rt)}); {set_reg(0, rs, K.S_ST_F5C7)} }}')
        if L:
            return Insn(K.S_OP_B27B, set_reg(1, rt, f'lda{nbits}({addr})'))
        return Insn(K.S_OP_B27B, f'stl{nbits}({addr}, {X(rt)});')

    def ldst_pair(self, w):
        opc, mode, L = bits(w, K.N_30_624B, K.N_2_D473), bits(w, K.N_23_535F, K.N_2_D473), bits(w, K.N_22_785F, 1)
        imm7, rt2, rn, rt = sext(bits(w, K.N_15_E629, K.N_7_7902), K.N_7_7902), bits(w, K.N_10_4A44, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        if opc == K.N_3_4E07:
            raise Unsupported(K.S_PAIR_OPC_3_D6DA)
        scale = K.N_3_4E07 if opc == K.N_2_D473 else K.N_2_D473
        off = imm7 << scale
        base = XS(rn)
        post = mode == 1
        wb = mode in (1, K.N_3_4E07)
        ea = base if post else (f'{base} + {off}' if off >= 0 else f'{base} - {-off}')
        lines = [f'uint64_t ea = {ea};']
        if L:
            if opc == K.N_2_D473:
                lines.append(K.S_UINT64_T_A_LD64_EA_B_LD64_EA_2A27)
            elif opc == 1:  # LDPSW
                lines.append(K.S_UINT64_T_A_INT64_T_INT32_T_L_246E)
            else:
                lines.append(K.S_UINT64_T_A_LD32_EA_B_LD32_EA_7617)
        else:
            if opc == K.N_2_D473:
                lines.append(f'ST64(ea, {X(rt)}); ST64(ea + 8, {X(rt2)});')
            else:
                lines.append(f'ST32(ea, {W(rt)}); ST32(ea + 4, {W(rt2)});')
        if wb:
            dst = K.S_C_SP_4C74 if rn == K.N_31_EB1E else f'c->x[{rn}]'
            if not post:
                lines.append(f'{dst} = ea;')
            else:
                lines.append(f'{dst} = ea + {off};' if off >= 0 else f'{dst} = ea - {-off};')
        if L:
            lines.append(set_reg(1, rt, K.S_A_D749))
            lines.append(set_reg(1, rt2, K.S_B_FA34))
        return Insn(K.S_OP_B27B, K.S_EMPTY_DCF7 + K.S_EMPTY_730D.join(lines) + K.S_EMPTY_1498)

    def ldst_reg(self, w):
        size, opc, rn, rt = bits(w, K.N_30_624B, K.N_2_D473), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, 0, K.N_5_EF2D)
        base = XS(rn)
        wb = None
        if bits(w, K.N_24_C235, K.N_2_D473) == 1:
            ea = f'{base} + {hx(bits(w, K.N_10_4A44, K.N_12_6B51) << size)}'
        else:
            kind = bits(w, K.N_10_4A44, K.N_2_D473)
            if bits(w, K.N_21_6F4B, 1):
                if kind != K.N_2_D473:
                    raise Unsupported(K.S_LDST_REG_ATOMIC_PAC_C6F1)
                rm, option, S = bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_13_3FDB, K.N_3_4E07), bits(w, K.N_12_6B51, 1)
                if option not in (K.N_2_D473, K.N_3_4E07, K.N_6_E7F6, K.N_7_7902):
                    raise Unsupported(K.S_LDST_REG_OPTION_6B3F)
                ea = f'{base} + {extend_expr(rm, option, size if S else 0)}'
            else:
                imm9 = sext(bits(w, K.N_12_6B51, K.N_9_1958), K.N_9_1958)
                sign_imm = f'+ {imm9}' if imm9 >= 0 else f'- {-imm9}'
                if kind in (0, K.N_2_D473):  # unscaled, unprivileged
                    ea = f'{base} {sign_imm}'
                elif kind == 1:  # post
                    ea = base
                    wb = f'{base} {sign_imm}'
                else:  # pre
                    ea = f'{base} {sign_imm}'
                    wb = K.S_EA_337F
        nbits = K.N_8_2C62 << size
        if opc == 0:
            parts = [f'ST{nbits}(ea, {X(rt) if size == K.N_3_4E07 else W(rt)});']
            load = None
        elif opc == 1:
            parts = [f'uint64_t v = LD{nbits}(ea);']
            load = set_reg(1, rt, K.S_V_DEAF)
        elif size == K.N_3_4E07:
            return Insn(K.S_OP_B27B, K.S_PRFM_54D2)
        elif size == K.N_2_D473 and opc == K.N_3_4E07:
            raise Unsupported(K.S_LDST_SIZE2_OPC3_2CA2)
        else:
            parts = [f'uint64_t v = (uint64_t)(int64_t)(int{nbits}_t)LD{nbits}(ea);']
            load = set_reg(1 if opc == K.N_2_D473 else 0, rt, K.S_V_DEAF)
        parts.insert(0, f'uint64_t ea = {ea};')
        if wb:
            # writeback before the register load, so a load into rn wins
            parts.append(f'{K.S_C_SP_4C74 if rn == K.N_31_EB1E else f"c->x[{rn}]"} = {wb};')
        if load:
            parts.append(load)
        return Insn(K.S_OP_B27B, K.S_EMPTY_DCF7 + K.S_EMPTY_730D.join(parts) + K.S_EMPTY_1498)

    def ldst_simd(self, w, pc):
        """FP/SIMD loads and stores are passed through as asm; GPR address regs are remapped."""
        rt = bits(w, 0, K.N_5_EF2D)
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_3_4E07 and bits(w, K.N_24_C235, K.N_2_D473) == 0:  # literal -> LDR (imm, offset 0) via scratch
            opc = bits(w, K.N_30_624B, K.N_2_D473)
            size, o = {0: (K.N_2_D473, 1), 1: (K.N_3_4E07, 1), K.N_2_D473: (0, K.N_3_4E07)}[opc]
            addr = pc + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22
            new = (size << K.N_30_624B) | K.N_1023410176_9994 | (o << K.N_22_785F) | (K.N_31_EB1E << K.N_5_EF2D) | rt
            return Insn(K.S_ASM_3C46, asm=AsmInsn(new, {rt}, [], consts=[(K.N_5_EF2D, addr)]))
        if bits(w, K.N_31_EB1E, 1) == 0 and bits(w, K.N_24_C235, K.N_6_E7F6) in (K.N_12_6B51, K.N_13_3FDB):  # structures
            post = bits(w, K.N_23_535F, 1)
            rn, rm = bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D)
            if bits(w, K.N_24_C235, 1) == 0:
                n = {0: K.N_4_4B22, K.N_2_D473: K.N_4_4B22, K.N_4_4B22: K.N_3_4E07, K.N_6_E7F6: K.N_3_4E07, K.N_7_7902: 1, K.N_8_2C62: K.N_2_D473, K.N_10_4A44: K.N_2_D473}.get(bits(w, K.N_12_6B51, K.N_4_4B22))
                if n is None:
                    raise Unsupported(K.S_LDST_MULTIPLE_OPCODE_FF73)
            else:
                n = ((bits(w, K.N_13_3FDB, 1) << 1) | bits(w, K.N_21_6F4B, 1)) + 1
            vregs = {(rt + i) % K.N_32_E29C for i in range(n)}
            gprs = [(K.N_5_EF2D, K.S_SP_C63E if rn == K.N_31_EB1E else rn)]
            if post and rm != K.N_31_EB1E:
                gprs.append((K.N_16_B17E, rm))
            return Insn(K.S_ASM_3C46, asm=AsmInsn(w, vregs, gprs))
        rn = bits(w, K.N_5_EF2D, K.N_5_EF2D)
        gprs = [(K.N_5_EF2D, K.S_SP_C63E if rn == K.N_31_EB1E else rn)]
        vregs = {rt}
        if bits(w, K.N_27_6706, K.N_3_4E07) == K.N_5_EF2D:  # pair
            vregs.add(bits(w, K.N_10_4A44, K.N_5_EF2D))
        elif bits(w, K.N_27_6706, K.N_3_4E07) == K.N_7_7902 and bits(w, K.N_24_C235, K.N_2_D473) == 0 and bits(w, K.N_21_6F4B, 1) and bits(w, K.N_10_4A44, K.N_2_D473) == K.N_2_D473:
            rm = bits(w, K.N_16_B17E, K.N_5_EF2D)
            if rm != K.N_31_EB1E:
                gprs.append((K.N_16_B17E, rm))
        return Insn(K.S_ASM_3C46, asm=AsmInsn(w, vregs, gprs))

    # ------------------------------------------------------------ data processing (reg)

    def dp_reg(self, w, pc):
        sf = bits(w, K.N_31_EB1E, 1)
        rd, rn, rm = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D)
        size = K.N_64_A68B if sf else K.N_32_E29C
        op1 = bits(w, K.N_28_59E1, 1)
        if not op1:
            if bits(w, K.N_24_C235, K.N_4_4B22) == K.N_10_4A44:  # logical shifted
                opc, sh, N, amt = bits(w, K.N_29_3513, K.N_2_D473), bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_21_6F4B, 1), bits(w, K.N_10_4A44, K.N_6_E7F6)
                if not sf and amt >= K.N_32_E29C:
                    raise Unsupported(K.S_LOGICAL_SHIFT_AMOUNT_9643)
                b = shift_expr(sf, R(sf, rm), sh, amt)
                if N:
                    b = f'(uint{size}_t)~{b}'
                a = R(sf, rn)
                expr = {0: f'{a} & {b}', 1: f'{a} | {b}', K.N_2_D473: f'{a} ^ {b}', K.N_3_4E07: f'{a} & {b}'}[opc]
                if opc == K.N_3_4E07:
                    return Insn(K.S_OP_B27B, set_reg(sf, rd, f'logic{size}(c, (uint{size}_t)({expr}))'))
                return Insn(K.S_OP_B27B, set_reg(sf, rd, expr))
            if bits(w, K.N_24_C235, K.N_4_4B22) == K.N_11_4FC8:  # add/sub shifted or extended
                op, S = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1)
                if bits(w, K.N_21_6F4B, 1) == 0:
                    sh, amt = bits(w, K.N_22_785F, K.N_2_D473), bits(w, K.N_10_4A44, K.N_6_E7F6)
                    if sh == K.N_3_4E07:
                        raise Unsupported(K.S_ADD_SHIFT_ROR_CCF0)
                    a = R(sf, rn)
                    b = shift_expr(sf, R(sf, rm), sh, amt)
                    sp_ok = False
                else:
                    if bits(w, K.N_22_785F, K.N_2_D473):
                        raise Unsupported(K.S_ADD_EXT_OPT_0C89)
                    option, amt = bits(w, K.N_13_3FDB, K.N_3_4E07), bits(w, K.N_10_4A44, K.N_3_4E07)
                    a = R(sf, rn, sp=True)
                    b = extend_expr(rm, option, amt)
                    if not sf:
                        b = f'(uint32_t)({b})'
                    sp_ok = not S
                if S:
                    fn = (K.S_SUBS_8974 if op else K.S_ADDS_F132) + str(size)
                    return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{fn}(c, {a}, (uint{size}_t)({b}))'))
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{a} {K.S_EMPTY_01F8 if op else K.S_EMPTY_3604} {b}', sp=sp_ok))
            raise Unsupported(f'dp_reg {w:08x}')
        op2 = bits(w, K.N_21_6F4B, K.N_4_4B22)
        if bits(w, K.N_24_C235, K.N_4_4B22) == K.N_11_4FC8:  # 3-source
            op54, op31, o0, ra = bits(w, K.N_29_3513, K.N_2_D473), bits(w, K.N_21_6F4B, K.N_3_4E07), bits(w, K.N_15_E629, 1), bits(w, K.N_10_4A44, K.N_5_EF2D)
            if op54:
                raise Unsupported(K.S_DP3_OP54_AA94)
            sgn = K.S_EMPTY_01F8 if o0 else K.S_EMPTY_3604
            if op31 == 0:
                return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{R(sf, ra)} {sgn} {R(sf, rn)} * {R(sf, rm)}'))
            if op31 == 1:
                return Insn(K.S_OP_B27B, set_reg(1, rd, f'{X(ra)} {sgn} (uint64_t)((int64_t)(int32_t){W(rn)} * (int64_t)(int32_t){W(rm)})'))
            if op31 == K.N_5_EF2D:
                return Insn(K.S_OP_B27B, set_reg(1, rd, f'{X(ra)} {sgn} (uint64_t){W(rn)} * (uint64_t){W(rm)}'))
            if op31 == K.N_2_D473:
                return Insn(K.S_OP_B27B, set_reg(
                    1, rd, f'(uint64_t)(((__int128)(int64_t){X(rn)} * (__int128)(int64_t){X(rm)}) >> 64)'))
            if op31 == K.N_6_E7F6:
                return Insn(K.S_OP_B27B, set_reg(1, rd, f'(uint64_t)(((unsigned __int128){X(rn)} * {X(rm)}) >> 64)'))
            raise Unsupported(K.S_DP3_50BD)
        if op2 == 0:  # add/sub with carry
            op, S = bits(w, K.N_30_624B, 1), bits(w, K.N_29_3513, 1)
            fn = (K.S_SBC_6EDA if op else K.S_ADC_B279) + (K.S_S_ECE4 if S else K.S_EMPTY_6F49) + str(size)
            return Insn(K.S_OP_B27B, set_reg(sf, rd, f'{fn}(c, {R(sf, rn)}, {R(sf, rm)})'))
        if op2 == K.N_2_D473:  # conditional compare
            op, imm_form, cond, nzcv = bits(w, K.N_30_624B, 1), bits(w, K.N_11_4FC8, 1), bits(w, K.N_12_6B51, K.N_4_4B22), bits(w, 0, K.N_4_4B22)
            b = hx(rm) if imm_form else R(sf, rm)
            fn = (K.S_SUBS_8974 if op else K.S_ADDS_F132) + str(size)
            return Insn(K.S_OP_B27B, f'if ({cond_expr(cond)}) (void){fn}(c, {R(sf, rn)}, {b}); '
                              f'else c->nzcv = {hx(nzcv << K.N_28_59E1)};')
        if op2 == K.N_4_4B22:  # conditional select
            op, o2, cond = bits(w, K.N_30_624B, 1), bits(w, K.N_10_4A44, K.N_2_D473), bits(w, K.N_12_6B51, K.N_4_4B22)
            b = R(sf, rm)
            alt = {(0, 0): b, (0, 1): f'{b} + 1', (1, 0): f'~{b}', (1, 1): f'-{b}'}.get((op, o2))
            if alt is None:
                raise Unsupported(K.S_CSEL_VARIANT_9036)
            return Insn(K.S_OP_B27B, set_reg(sf, rd, f'({cond_expr(cond)}) ? {R(sf, rn)} : (uint{size}_t)({alt})'))
        if op2 == K.N_6_E7F6:
            opcode = bits(w, K.N_10_4A44, K.N_6_E7F6)
            if bits(w, K.N_30_624B, 1):  # 1-source
                if bits(w, K.N_16_B17E, K.N_5_EF2D):
                    raise Unsupported(K.S_DP1_OPCODE2_CC23)
                a = R(sf, rn)
                expr = {
                    0: f'rbit{size}({a})',
                    1: f'rev16_{size}({a})',
                    K.N_2_D473: f'__builtin_bswap32({a})' if not sf else f'rev32_64({a})',
                    K.N_3_4E07: f'__builtin_bswap64({a})' if sf else None,
                    K.N_4_4B22: f'clz{size}({a})',
                    K.N_5_EF2D: f'cls{size}({a})',
                }.get(opcode)
                if expr is None:
                    raise Unsupported(K.S_DP1_1A32)
                return Insn(K.S_OP_B27B, set_reg(sf, rd, expr))
            if K.N_16_B17E <= opcode <= K.N_23_535F:  # CRC32: host has the same instruction
                if self.sc:
                    return Insn(K.S_OP_B27B, self.sc.crc(w))
                gprs = [(p, r) for p, r in ((0, rd), (K.N_5_EF2D, rn), (K.N_16_B17E, rm)) if r != K.N_31_EB1E]
                return Insn(K.S_ASM_3C46, asm=AsmInsn(w, set(), gprs))
            a, b = R(sf, rn), R(sf, rm)
            expr = {
                K.N_2_D473: f'udiv{size}({a}, {b})',
                K.N_3_4E07: f'sdiv{size}({a}, {b})',
                K.N_8_2C62: f'{a} << ({b} & {size - 1})',
                K.N_9_1958: f'{a} >> ({b} & {size - 1})',
                K.N_10_4A44: f'(uint{size}_t)((int{size}_t){a} >> ({b} & {size - 1}))',
                K.N_11_4FC8: f'ROR{size}({a}, {b} & {size - 1})',
            }.get(opcode)
            if expr is None:
                raise Unsupported(K.S_DP2_1AA1)
            return Insn(K.S_OP_B27B, set_reg(sf, rd, expr))
        raise Unsupported(f'dp_reg {w:08x}')

    # ------------------------------------------------------------ FP / SIMD data processing

    def fp_simd(self, w, pc):
        if self.sc:
            return Insn(K.S_OP_B27B, self.sc.dp(w, pc))
        rd, rn, rm, ra = bits(w, 0, K.N_5_EF2D), bits(w, K.N_5_EF2D, K.N_5_EF2D), bits(w, K.N_16_B17E, K.N_5_EF2D), bits(w, K.N_10_4A44, K.N_5_EF2D)
        vregs = {rd, rn, rm, ra}
        gprs = []
        flags = False
        if (w & K.N_1595997184_9669) == K.N_505413632_3AC4:  # FP <-> integer
            opcode = bits(w, K.N_16_B17E, K.N_3_4E07)
            if opcode in (0, 1, K.N_4_4B22, K.N_5_EF2D, K.N_6_E7F6):
                gprs = [(0, rd)]
                vregs = {rn}
            else:
                gprs = [(K.N_5_EF2D, rn)]
                vregs = {rd}
        elif (w & K.N_1595932672_222D) == K.N_503316480_1CDD:  # FP <-> fixed point
            opcode = bits(w, K.N_16_B17E, K.N_3_4E07)
            if opcode in (0, 1):
                gprs = [(0, rd)]
                vregs = {rn}
            else:
                gprs = [(K.N_5_EF2D, rn)]
                vregs = {rd}
        elif (w & K.N_2682291200_E127) == K.N_234882048_19D3 and bits(w, K.N_29_3513, 1) == 0:  # AdvSIMD copy
            imm4 = bits(w, K.N_11_4FC8, K.N_4_4B22)
            if imm4 in (1, K.N_3_4E07):
                gprs = [(K.N_5_EF2D, rn)]
                vregs = {rd}
            elif imm4 in (K.N_5_EF2D, K.N_7_7902):
                gprs = [(0, rd)]
                vregs = {rn}
            else:
                vregs = {rd, rn}
        elif (w & K.N_3219164160_B2A1) == K.N_234881024_05C4:  # TBL/TBX
            n = bits(w, K.N_13_3FDB, K.N_2_D473) + 1
            vregs = {rd, rm} | {(rn + i) % K.N_32_E29C for i in range(n)}
        if (w & K.N_1595948032_1256) == K.N_505421824_BD7E or (w & K.N_1595935744_17A1) in (K.N_505414656_0021, K.N_505416704_B373):
            flags = True
        gprs = [(p, r) for p, r in gprs if r != K.N_31_EB1E]  # register 31 is zr in these encodings
        return Insn(K.S_ASM_3C46, asm=AsmInsn(w, vregs, gprs, flags))


def cond_expr(cond):
    return f'COND_{COND_NAMES[cond].upper()}(c)' if cond < K.N_14_8527 else K.S_1_9A76


def emit_asm_batch(batch):
    """Emit one inline-asm block for a run of FP/SIMD instructions."""
    vregs = set()
    gmap = {}
    flags = False
    consts = []
    words = []
    for a in batch:
        vregs |= a.vregs
        flags |= a.flags
        w = a.word
        for pos, g in a.gprs:
            if g not in gmap:
                if len(gmap) + len(consts) >= len(SCRATCH):
                    raise Unsupported(K.S_SCRATCH_EXHAUSTED_7476)
                gmap[g] = SCRATCH[len(gmap) + len(consts)]
            w = (w & ~(K.N_31_EB1E << pos)) | (gmap[g] << pos)
        for pos, val in a.consts:
            s = SCRATCH[len(gmap) + len(consts)]
            consts.append((s, val))
            w = (w & ~(K.N_31_EB1E << pos)) | (s << pos)
        words.append(w)
    lines = []
    ins = [K.S_R_C_8ED2]
    for g, s in gmap.items():
        off = OFF_SP if g == K.S_SP_C63E else OFF_X + K.N_8_2C62 * g
        lines.append(f'ldr x{s}, [%0, #{off}]')
    for i, (s, val) in enumerate(consts):
        ins.append(f'"r"({hx(val)})')
        lines.append(f'mov x{s}, %{len(ins) - 1}')
    for v in sorted(vregs):
        lines.append(f'ldr q{v}, [%0, #{OFF_V + K.N_16_B17E * v}]')
    if flags:
        lines.append(f'ldr x16, [%0, #{OFF_NZCV}]')
        lines.append(K.S_MSR_NZCV_X16_2AA0)
    lines += [f'.inst 0x{x:08x}' for x in words]
    if flags:
        lines.append(K.S_MRS_X16_NZCV_6CBB)
        lines.append(f'str x16, [%0, #{OFF_NZCV}]')
    for v in sorted(vregs):
        lines.append(f'str q{v}, [%0, #{OFF_V + K.N_16_B17E * v}]')
    for g, s in gmap.items():
        off = OFF_SP if g == K.S_SP_C63E else OFF_X + K.N_8_2C62 * g
        lines.append(f'str x{s}, [%0, #{off}]')
    clob = [f'"v{v}"' for v in sorted(vregs)]
    clob += [f'"x{s}"' for s in sorted(set(gmap.values()) | {s for s, _ in consts} | ({K.N_16_B17E} if flags else set()))]
    clob += [K.S_CC_F169, K.S_MEMORY_19C6]
    body = K.S_N_T_270D.join(lines)
    return f'__asm__ volatile("{body}" :: {K.S_EMPTY_3C01.join(ins)} : {K.S_EMPTY_3C01.join(clob)});'

"""Generate C for a whole NSO module (SF2 fork of LAS Recomp's tools/gen.py, MIT).

Usage: gen.py <name> <nso> <base> <outdir> [--hle hle_symbols.txt] [--symbols sym_main.txt] [--chunk N] [--simd asm|c]

--simd: FP/SIMD/CRC32 as AArch64 inline asm (default on arm64 hosts) or portable C (default elsewhere).
--symbols: tools/il2cpp_map.py output (`0xADDR NAME` lines) — seeds function boundaries and
           labels functions with their C# names. For SF2's `main` this is the only real
           symbol source: 83.7k methods from Il2CppDumper, vs ~700 organic dynsym exports.

Differences from upstream: the LA randomizer site table is gone; `main` versions come from
this fork's self-contained game_addresses.py; empty --symbols yields upstream behaviour.
"""
from _sf2_recompiler_constants import GEN as K
import argparse
import bisect
import io
import os
import platform
import re
import struct
import sys
import time
from collections import Counter

import unicorn as uc
from unicorn import arm64_const as A

from analyze import eh_frame_functions, eh_frame_handlers
from lifter import Insn, Lifter, Unsupported, bits, emit_asm_batch, hx, sext
from nso import Nso, R_AARCH64_RELATIVE

SCRATCH_MEM = K.SCRATCH_MEM


def sext26(w):
    v = w & K.N_67108863_97F6
    return v - K.N_67108864_2B3C if v & K.N_33554432_B589 else v


def fname(a):
    return f'f_{a:x}'


def sanitize(sym):
    return K.S_HLE_218C + re.sub(K.S_A_ZA_Z0_9_0714, K.S_EMPTY_BDC4, sym)


class Module:
    def __init__(self, name, path, base, simd=K.S_ASM_3C46, symfile=None):
        self.name = name
        self.nso = Nso(path)
        self.base = base
        n = self.nso
        self.lo = base + n.text[0]
        self.hi = base + n.text[0] + n.text[1]
        self.words = struct.unpack(f'<{n.text[1] // K.N_4_4B22}I', bytes(n.image[n.text[0]:n.text[0] + n.text[1]]))
        self.lifter = Lifter(base, simd)
        self.cache = {}
        self.stats = Counter()
        self.eh_sites = eh_frame_handlers(n, base)
        self.eh_calls = {}
        for lo, hi, pad, handlers in self.eh_sites:
            lo, hi = max(lo, self.lo), min(hi, self.hi)
            for a in range((lo + K.N_3_4E07) & ~K.N_3_4E07, hi, K.N_4_4B22):
                w = self.words[(a - self.lo) // K.N_4_4B22]
                if (w & K.N_4227858432_1BBE) == K.N_2483027968_26C3 or (w & K.N_4294966303_69F2) == K.N_3594452992_AA02:
                    self.eh_calls[a] = pad
        self.mu = None
        self.sym_starts, self.sym_names = set(), {}
        self.symfile = symfile
        if symfile:
            for line in open(symfile, encoding=K.S_UTF_8_5383):
                line = line.strip()
                if not line or line.startswith(K.S_EMPTY_4734):
                    continue
                addr_hex, name = line.split(None, 1)
                addr = base + int(addr_hex, 0)
                if self.in_text(addr):
                    self.sym_starts.add(addr)
                    if addr not in self.sym_names:
                        self.sym_names[addr] = name.strip()

    def in_text(self, a):
        return self.lo <= a < self.hi and a % K.N_4_4B22 == 0

    def word(self, a):
        return self.words[(a - self.lo) // K.N_4_4B22]

    def lift(self, a):
        ins = self.cache.get(a)
        if ins is None:
            try:
                ins = self.lifter.lift(self.word(a), a)
            except Unsupported as e:
                self.stats[K.S_UNSUPPORTED_A052] += 1
                ins = Insn(K.S_TRAP_1946, f'rt_udf(c, {hx(a)}); /* {e} */')
            self.cache[a] = ins
        return ins

    def boundaries(self):
        n, b = self.nso, self.base
        starts = set(self.sym_starts)
        for a in eh_frame_functions(n):
            starts.add(b + a)
        for name, info, shndx, value, size in n.syms:
            if shndx and (info & K.N_15_E629) == K.N_2_D473:
                starts.add(b + value)
        for off, typ, sym, add in n.relocs:
            if typ == R_AARCH64_RELATIVE:
                starts.add(b + add)
        # Hand-curated function entries the static scans can't find: pointers the guest computes at runtime
        # from il2cpp metadata (RGCTX / method-pointer values live in global-metadata.dat, which the VM loads
        # as data) have no representation in the NSO. Lines are image offsets, next to sym_main.txt; found by
        # dispatch-miss crashes ("no function at X" — that X is an image offset here) and review.
        if self.symfile:
            extra = os.path.join(os.path.dirname(self.symfile), K.S_EXTRA_STARTS_TXT_D0BB)
            if os.path.exists(extra):
                for line in open(extra, encoding=K.S_UTF_8_5383):
                    line = line.strip()
                    if not line or line.startswith(K.S_EMPTY_4734):
                        continue
                    starts.add(self.base + int(line, K.N_16_B17E))
        starts = sorted(a for a in starts if self.in_text(a))
        return starts

    # ------------------------------------------------------------ jump tables

    def emu(self):
        if self.mu is None:
            mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            n = self.nso
            mu.mem_map(self.base, len(n.image))
            mu.mem_write(self.base, bytes(n.image))
            mu.mem_map(SCRATCH_MEM, K.N_1048576_50B4)
            mu.reg_write(A.UC_ARM64_REG_CPACR_EL1, K.N_3145728_C4F0)
            self.mu = mu
        return self.mu

    def jump_table(self, br_pc, entry, end):
        """Resolve `br xN` targets from a bounds-checked jump table, or None."""
        r = self._jump_table_fallthrough(br_pc, entry, end)
        if r is None:
            r = self._jump_table_taken(br_pc, entry, end)
        return r

    def _index_regs(self, start, at, idx):
        """The index register plus registers holding the same value in start..at: sources it was
        copied/masked from, and copies of those (the table lookup sometimes recomputes the index
        from another copy after the bounds check)."""
        def copy_src(w):
            if (w & K.N_2139095040_0286) == K.N_301989888_BA93:  # AND (imm)
                return bits(w, K.N_5_EF2D, K.N_5_EF2D)
            if (w & K.N_2145452000_7F6A) == K.N_704644064_C2AF:  # MOV (ORR with zr)
                return bits(w, K.N_16_B17E, K.N_5_EF2D)
            if (w & K.N_2139095040_0286) in (K.N_1392508928_9665, K.N_318767104_DCA5):  # UBFM/SBFM (uxtb, uxth, sxtw...)
                return bits(w, K.N_5_EF2D, K.N_5_EF2D)
            return None
        regs = {idx}
        changed = True
        while changed:
            changed = False
            for a in range(start, at, K.N_4_4B22):
                w = self.word(a)
                rd, src = w & K.N_31_EB1E, copy_src(w)
                if src is None or src >= K.N_31_EB1E or rd >= K.N_31_EB1E:
                    continue
                for x, y in ((rd, src), (src, rd)):
                    if x in regs and y not in regs:
                        regs.add(y)
                        changed = True
        return sorted(regs)

    def _set_index(self, mu, regs, i):
        for r in regs:
            mu.reg_write(A.UC_ARM64_REG_X0 + r if r < K.N_29_3513 else A.UC_ARM64_REG_X29, i)

    def _emulate_table(self, br_pc, entry, end, start, idx, count, set_at):
        """Run start..br for each index value (set at address set_at), collect targets."""
        mu = self.emu()
        rn = bits(self.word(br_pc), K.N_5_EF2D, K.N_5_EF2D)

        def step_to(frm, to):
            pc = frm
            for _ in range(K.N_64_A68B):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        targets = []
        try:
            for i in range(count):
                for r in range(K.N_29_3513):
                    mu.reg_write(A.UC_ARM64_REG_X0 + r, SCRATCH_MEM + K.N_524288_9C3E)
                mu.reg_write(A.UC_ARM64_REG_SP, SCRATCH_MEM + K.N_524288_9C3E)
                a2 = entry
                while a2 < start:
                    w2 = self.word(a2)
                    if (w2 & K.N_520093696_B1B0) == K.N_268435456_510F or (w2 & K.N_4286578688_E248) == K.N_2432696320_4E29:
                        mu.emu_start(a2, 0, count=1)
                    a2 += K.N_4_4B22
                if set_at != start and not step_to(start, set_at):
                    return None
                self._set_index(mu, [idx], i)
                if not step_to(set_at, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < K.N_29_3513 else
                                (A.UC_ARM64_REG_X29 if rn == K.N_29_3513 else A.UC_ARM64_REG_X30))
                if not (entry <= t < end) or t % K.N_4_4B22:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return sorted(set(targets))

    def _jump_table_taken(self, br_pc, entry, end):
        """Form: cmp idx, #N ; b.ls/b.lo table ... table: <address math> ; br"""
        # start of the block holding the br
        s = br_pc
        while s - K.N_4_4B22 >= entry and br_pc - (s - K.N_4_4B22) <= K.N_4_4B22 * K.N_12_6B51 and self.lift(s - K.N_4_4B22).kind in (K.S_OP_B27B, K.S_ASM_3C46):
            s -= K.N_4_4B22
        for g in range(entry, end, K.N_4_4B22):
            w = self.word(g)
            if (w & K.N_4278190096_C03B) != K.N_1409286144_F8C4 or bits(w, 0, K.N_4_4B22) not in (K.N_9_1958, K.N_3_4E07):
                continue
            t = g + sext(bits(w, K.N_5_EF2D, K.N_19_9400), K.N_19_9400) * K.N_4_4B22
            if t != s:
                continue
            cw = self.word(g - K.N_4_4B22) if g - K.N_4_4B22 >= entry else 0
            if (cw & K.N_2139095071_06C6) != K.N_1895825439_A746 or bits(cw, K.N_22_785F, 1):
                continue
            limit = bits(cw, K.N_10_4A44, K.N_12_6B51)
            count = limit + 1 if bits(w, 0, K.N_4_4B22) == K.N_9_1958 else limit
            if count <= 0 or count > K.N_4096_8B92:
                continue
            return self._emulate_table(br_pc, entry, end, s, bits(cw, K.N_5_EF2D, K.N_5_EF2D), count, s)
        return None

    def _jump_table_fallthrough(self, br_pc, entry, end):
        rn = bits(self.word(br_pc), K.N_5_EF2D, K.N_5_EF2D)
        # find the guarding b.hi / b.hs and its cmp, scanning backwards
        a = br_pc - K.N_4_4B22
        guard = cmp = None
        while a >= entry and br_pc - a <= K.N_4_4B22 * K.N_16_B17E:
            w = self.word(a)
            if (w & K.N_4278190096_C03B) == K.N_1409286144_F8C4 and bits(w, 0, K.N_4_4B22) in (K.N_8_2C62, K.N_2_D473):
                guard = a
                break
            if self.lift(a).kind in (K.S_B_FA34, K.S_BL_4BCD, K.S_BLR_FE23, K.S_BR_68B5, K.S_RET_2523, K.S_TRAP_1946):
                return None
            a -= K.N_4_4B22
        if guard is None:
            return None
        a = guard - K.N_4_4B22
        while a >= entry and guard - a <= K.N_4_4B22 * K.N_8_2C62:
            w = self.word(a)
            # SUBS (imm) with Rd = zr, i.e. CMP #imm
            if (w & K.N_2139095071_06C6) == K.N_1895825439_A746:
                cmp = a
                break
            if self.lift(a).kind != K.S_OP_B27B:
                return None
            a -= K.N_4_4B22
        if cmp is None:
            return None
        w = self.word(cmp)
        if bits(w, K.N_22_785F, 1):
            return None
        limit = bits(w, K.N_10_4A44, K.N_12_6B51)
        count = limit + 1 if bits(self.word(guard), 0, K.N_4_4B22) == K.N_8_2C62 else limit
        if count <= 0 or count > K.N_4096_8B92:
            return None
        idx = bits(w, K.N_5_EF2D, K.N_5_EF2D)
        # window start: back from cmp to the first control-flow instruction
        start = cmp
        while start - K.N_4_4B22 >= entry and cmp - (start - K.N_4_4B22) <= K.N_4_4B22 * K.N_12_6B51 and self.lift(start - K.N_4_4B22).kind in (K.S_OP_B27B, K.S_ASM_3C46):
            start -= K.N_4_4B22
        idx_regs = self._index_regs(start, cmp, idx)
        mu = self.emu()
        targets = []

        def step_to(frm, to):
            # single-step: Unicorn's `until` is unreliable mid translation-block
            pc = frm
            for _ in range(K.N_64_A68B):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        try:
            for i in range(count):
                for r in range(K.N_29_3513):
                    mu.reg_write(A.UC_ARM64_REG_X0 + r, SCRATCH_MEM + K.N_524288_9C3E)
                mu.reg_write(A.UC_ARM64_REG_SP, SCRATCH_MEM + K.N_524288_9C3E)
                # replay address materialization (ADRP/ADR/ADD-imm) from the function entry, so table
                # bases computed before loops or branches are known
                a2 = entry
                while a2 < start:
                    w2 = self.word(a2)
                    if (w2 & K.N_520093696_B1B0) == K.N_268435456_510F or (w2 & K.N_4286578688_E248) == K.N_2432696320_4E29:
                        mu.emu_start(a2, 0, count=1)
                    a2 += K.N_4_4B22
                if not step_to(start, cmp):
                    return None
                self._set_index(mu, idx_regs, i)
                if not step_to(cmp, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < K.N_29_3513 else
                                (A.UC_ARM64_REG_X29 if rn == K.N_29_3513 else A.UC_ARM64_REG_X30))
                if not (entry <= t < end) or t % K.N_4_4B22:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return sorted(set(targets))

    # ------------------------------------------------------------ functions

    def computed_target(self, br_pc, entry):
        """True if the br register comes from address arithmetic (a table) rather than a load."""
        rn = bits(self.word(br_pc), K.N_5_EF2D, K.N_5_EF2D)
        a = br_pc - K.N_4_4B22
        while a >= entry and br_pc - a <= K.N_4_4B22 * K.N_16_B17E:
            w = self.word(a)
            if self.lift(a).kind not in (K.S_OP_B27B, K.S_ASM_3C46):
                return False
            if (w & K.N_31_EB1E) == rn:
                if (w & K.N_520093696_B1B0) == K.N_184549376_C117:  # ADD/SUB (shifted or extended register)
                    return True
                if (w & K.N_2667577344_556A) == K.N_268435456_510F:  # ADR
                    return True
                return False
            a -= K.N_4_4B22
        return False

    def scan(self, entry, end):
        """Collect labels and external entries referenced by a function."""
        labels, ext, tables = {entry}, set(), {}
        terminated = False
        for a in range(entry, end, K.N_4_4B22):
            ins = self.lift(a)
            if ins.kind in (K.S_B_FA34, K.S_BCOND_44B7):
                t = ins.target
                if entry <= t < end:
                    labels.add(t)
                elif self.in_text(t):
                    ext.add(t)
            elif ins.kind == K.S_BL_4BCD:
                if self.in_text(ins.target):
                    ext.add(ins.target)
            elif ins.kind == K.S_BR_68B5:
                jt = self.jump_table(a, entry, end)
                if jt:
                    tables[a] = jt
                    labels.update(jt)
                    self.stats[K.S_JUMPTABLES_00B8] += 1
                elif self.computed_target(a, entry):
                    # unresolved table jump: allow jumping to any instruction of the function
                    tables[a] = K.S_ALL_B9E2
                    self.stats[K.S_BR_ALL_LABELS_A46F] += 1
                else:
                    self.stats[K.S_BR_DISPATCH_51F8] += 1
            terminated = ins.kind in (K.S_B_FA34, K.S_BR_68B5, K.S_RET_2523, K.S_TRAP_1946)
        if not terminated and end < self.hi:
            ext.add(end)
        return labels, ext, tables

    def render_single(self, ins, a, jump, refs):
        """C for one lifted instruction (no batching); used for patch variants."""
        k = ins.kind
        if k == K.S_ASM_3C46:
            return [emit_asm_batch([ins.asm])]
        if k in (K.S_OP_B27B, K.S_TRAP_1946):
            return [ins.code]
        if k == K.S_B_FA34:
            return [jump(ins.target)]
        if k == K.S_BCOND_44B7:
            return [f'if ({ins.cond}) {{ {jump(ins.target)} }}']
        if k == K.S_BL_4BCD:
            t = ins.target
            if a in self.eh_calls:
                if self.in_text(t):
                    refs.add(t)
                    target_fn = fname(t)
                else:
                    target_fn = K.S_NULL_FF0E
                return [f'if (rt_guest_try_call(c, {hx(a)}, {hx(t)}, {target_fn})) goto L_exception_dispatch;']
            if self.in_text(t):
                refs.add(t)
                return [f'c->x[30] = {hx(a + K.N_4_4B22)}; {fname(t)}(c);']
            return [f'c->x[30] = {hx(a + K.N_4_4B22)}; c->pc = {hx(t)}; rt_dispatch(c);']
        if k == K.S_BLR_FE23:
            if a in self.eh_calls:
                return [f'if (rt_guest_try_call(c, {hx(a)}, {ins.target}, NULL)) goto L_exception_dispatch;']
            return [f'c->pc = {ins.target}; c->x[30] = {hx(a + K.N_4_4B22)}; rt_dispatch(c);']
        if k == K.S_BR_68B5:
            return [f'c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);']
        if k == K.S_RET_2523:
            return [K.S_RETURN_20B5]
        raise Unsupported(k)

    def emit_function(self, entry, end, labels, tables, hle):
        out = [f'void {fname(entry)}(Cpu *c) {{']
        if entry in self.sym_names:  # the C# method this is (tools/il2cpp_map.py)
            out.insert(0, f'// {self.sym_names[entry]}')
        if entry in hle:
            out.append(f'    {sanitize(hle[entry])}(c);')
            out.append(K.S_EMPTY_9AC4)
            return out
        # Whole-stub chunks: bodies that are only the nn/SDK 'udf' filler (0xe7ffdefe) possibly
        # followed by udf-pad words and/or a trailing ret. Census: 261 in sdk, a few all-stub in
        # main. Real HW faults on the first executed one — a "should never be called" placeholder.
        # We keep the chunk (control flow can still reach it) but the diagnostic runtime decides:
        # rt_stub logs the site + caller once and returns void-like so boot observation continues.
        if self.word(entry) == K.N_3892305662_89F3 and end - entry >= K.N_4_4B22:
            ok = True
            for a in range(entry + K.N_4_4B22, end, K.N_4_4B22):
                if self.word(a) not in (K.N_3892305662_89F3, 0x00000000, K.N_3596551104_2A0D):
                    ok = False
                    break
            if ok and end - entry <= K.N_384_37B7:
                out.append(K.S_WHOLE_STUB_CHUNK_UDF_PAD_RET_45DC)
                out.append(f'    rt_stub(c, {hx(entry)});')
                out.append(K.S_RETURN_F83B)
                out.append(K.S_EMPTY_9AC4)
                return out
        out.append(f'    FN_ENTER({hx(entry)});')
        batch = []
        refs = set()

        def flush():
            if batch:
                out.append(K.S_EMPTY_E6E5 + emit_asm_batch(batch))
                batch.clear()

        def jump(t):
            if entry <= t < end:
                return f'goto L_{t:x};'
            if not self.in_text(t):
                return f'c->pc = {hx(t)}; __attribute__((musttail)) return rt_dispatch(c);'
            refs.add(t)
            return f'__attribute__((musttail)) return {fname(t)}(c);'

        # Functions with jump tables also get the all-instruction fallback: a table whose emulation
        # missed some cases then still reaches the right instruction instead of aborting.
        all_labels = bool(tables)
        if all_labels:
            labels = set(range(entry, end, K.N_4_4B22))
        else:
            labels = set(labels)
        eh_calls = {a: self.eh_calls[a] for a in range(entry, end, K.N_4_4B22)
                    if a in self.eh_calls}
        eh_pads = sorted(set(eh_calls.values()))
        labels.update(pad for pad in eh_pads if entry <= pad < end)
        hooks_here = [a for a in range(entry, end, K.N_4_4B22) if a in HOOKS]
        if hooks_here:  # (a hook's jump targets need their labels)
            labels = set(labels)
            for a in hooks_here:
                for t in HOOKS[a][1]:
                    if not entry <= t < end:
                        raise ValueError(f'hook {a:#x}: target {t:#x} is outside its function')
                    labels.add(t)
        last = None
        for a in range(entry, end, K.N_4_4B22):
            ins = self.lift(a)
            if a in labels:
                flush()
                out.append(f'L_{a:x}:;')
            if a in HOOKS:  # hook site (tools/recompr/patches.py HOOKS): the runtime decides, the instruction stays as it is
                flush()
                word, targets, _ = HOOKS[a][:K.N_3_4E07]
                w = struct.unpack_from(K.S_I_833B, self.nso.image, a - self.base)[0]
                if w != word:
                    raise ValueError(f'hook {a:#x}: instruction {w:#010x}, expected {word:#010x}')
                body = K.S_EMPTY_730D.join(self.render_single(ins, a, jump, refs))
                jumps = K.S_EMPTY_730D.join(f'else if (h_ == {k + K.N_2_D473}) goto L_{t:x};' for k, t in enumerate(targets))
                callback = f'rt_hook(c, {hx(a)})'
                if len(HOOKS[a]) == K.N_4_4B22:
                    callback = f'((rt_hook_features & {HOOKS[a][K.N_3_4E07]}) ? {callback} : 0)'
                out.append(f'    {{ int h_ = {callback}; if (h_ == 1) return; if (h_ == 0) {{ {body} }} {jumps} }}'
                           f'  /* {a:#x}: hook (runtime/hle/hooks.c) */')
                last = K.S_OP_B27B
                continue
            if a in PATCHES:  # hand-written replacement for one instruction (tools/patches.py)
                flush()
                out.append(K.S_EMPTY_E6E5 + PATCHES[a](self, a))
                last = K.S_OP_B27B
                continue
            k = ins.kind
            if k == K.S_ASM_3C46:
                if len(batch) >= K.N_24_C235:
                    flush()
                batch.append(ins.asm)
                try:
                    emit_asm_batch(batch)
                except Unsupported:
                    batch.pop()
                    flush()
                    batch.append(ins.asm)
                last = k
                continue
            flush()
            if k == K.S_OP_B27B or k == K.S_TRAP_1946:
                out.append(K.S_EMPTY_E6E5 + ins.code)
            elif k == K.S_B_FA34:
                out.append(K.S_EMPTY_E6E5 + jump(ins.target))
            elif k == K.S_BCOND_44B7:
                out.append(f'    if ({ins.cond}) {{ {jump(ins.target)} }}')
            elif k == K.S_BL_4BCD:
                t = ins.target
                if a in eh_calls:
                    if self.in_text(t):
                        refs.add(t)
                        target_fn = fname(t)
                    else:
                        target_fn = K.S_NULL_FF0E
                    out.append(f'    if (rt_guest_try_call(c, {hx(a)}, {hx(t)}, {target_fn})) goto L_exception_dispatch;')
                elif self.in_text(t):
                    refs.add(t)
                    out.append(f'    c->x[30] = {hx(a + K.N_4_4B22)}; {fname(t)}(c);')
                else:
                    out.append(f'    c->x[30] = {hx(a + K.N_4_4B22)}; c->pc = {hx(t)}; rt_dispatch(c);')
            elif k == K.S_BLR_FE23:
                if a in eh_calls:
                    out.append(f'    if (rt_guest_try_call(c, {hx(a)}, {ins.target}, NULL)) goto L_exception_dispatch;')
                else:
                    out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + K.N_4_4B22)}; rt_dispatch(c);')
            elif k == K.S_BR_68B5:
                jt = tables.get(a)
                if jt == K.S_ALL_B9E2:
                    out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    continue
                if jt:
                    cases = K.S_EMPTY_730D.join(f'case {hx(t)}: goto L_{t:x};' for t in jt)
                    out.append(f'    switch ({ins.target}) {{ {cases} default: break; }}')
                    out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    continue
                out.append(f'    c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
            elif k == K.S_RET_2523:
                out.append(K.S_RETURN_F83B)
            last = k
        flush()
        if last not in (K.S_B_FA34, K.S_BR_68B5, K.S_RET_2523, K.S_TRAP_1946):
            out.append(K.S_EMPTY_E6E5 + jump(end) if end < self.hi else f'    rt_udf(c, {hx(end)});')
        if all_labels:
            out.append(K.S_L_DISPATCH_7BFD)
            cases = K.S_EMPTY_730D.join(f'case {hx(t)}: goto L_{t:x};' for t in range(entry, end, K.N_4_4B22))
            out.append(f'    switch (c->pc) {{ {cases} default: break; }}')
            out.append(K.S_ATTRIBUTE_MUSTTAIL_RETURN_RT_1F99)
        if eh_pads:
            out.append(K.S_L_EXCEPTION_DISPATCH_192A)
            cases = K.S_EMPTY_730D.join(f'case {hx(pad)}: goto L_{pad:x};' for pad in eh_pads
                             if entry <= pad < end)
            out.append(f'    switch (c->pc) {{ {cases} default: rt_udf(c, c->pc); }}')
        out.append(K.S_EMPTY_9AC4)
        self._refs = refs
        return out


# tools/recompr/patches.py, for the main module only (its sites are main addresses): loaded in main() once the game version
# is known from the NSO's build ID (tools/recompr/game_addresses.py; an unknown build ID stops the generator)
HOOKS, PATCHES = {}, {}


def write_if_changed(path, text):
    """Leaves unchanged chunks untouched so make only recompiles what a patch affected."""
    try:
        with open(path, encoding=K.S_UTF_8_5383) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, K.S_W_780F, encoding=K.S_UTF_8_5383, newline=K.S_N_AD65) as f:
        f.write(text)


def emit_eh_table(name, eh_sites):
    """Emit ordered exact-type/catch-all lookup; cleanup is excluded by the parser."""
    with io.StringIO() as f:
        f.write(K.S_INCLUDE_CPU_H_N_43BF)
        f.write(K.S_TYPEDEF_STRUCT_UINT64_T_LO_H_5CED)
        f.write(K.S_TYPEDEF_STRUCT_UINT64_T_TYPE_27D8)
        handlers = []
        sites = []
        for lo, hi, pad, clauses in eh_sites:
            if not clauses:
                continue
            first = len(handlers)
            handlers.extend(clauses)
            sites.append((lo, hi, pad, first, len(clauses)))
        f.write(f'static const EhSite {name}_eh_sites[] = {{\n')
        for lo, hi, pad, first, count in sites:
            f.write(f'    {{{hx(lo)}, {hx(hi)}, {hx(pad)}, {first}, {count}}},\n')
        if not sites:
            f.write(K.S_0_0_0_0_0_N_5EB7)
        f.write(K.S_N_27E8)
        f.write(f'static const EhHandler {name}_eh_handlers[] = {{\n')
        for typ, selector in handlers:
            f.write(f'    {{{hx(typ)}, {selector}}},\n')
        if not handlers:
            f.write(K.S_0_0_N_CE7A)
        f.write(K.S_N_27E8)
        f.write(f'bool {name}_eh_lookup(uint64_t site, uint64_t type, uint64_t *pad, uint64_t *selector) {{\n')
        f.write(f'    size_t lo = 0, hi = {len(sites)};\n')
        f.write(K.S_WHILE_LO_HI_SIZE_T_MID_LO_HI_5A9C)
        f.write(f'{name}_eh_sites[mid].lo <= site) lo = mid + 1; else hi = mid; }}\n')
        f.write(K.S_IF_LO_RETURN_FALSE_N_50FF)
        f.write(f'    const EhSite *s = &{name}_eh_sites[lo - 1];\n')
        f.write(K.S_IF_SITE_S_HI_RETURN_FALSE_N_4BAA)
        f.write(K.S_FOR_UINT32_T_I_0_I_S_COUNT_I_F146)
        f.write(f'        const EhHandler *h = &{name}_eh_handlers[s->first + i];\n')
        f.write(K.S_IF_H_TYPE_H_TYPE_TYPE_PAD_S__34F8)
        f.write(K.S_N_3D98)
        f.write(K.S_RETURN_FALSE_N_N_FB66)
        return f.getvalue()


def main():
    global HOOKS, PATCHES  # module-specific tables live here; branch code below only assigns
    ap = argparse.ArgumentParser()
    ap.add_argument(K.S_NAME_8423)
    ap.add_argument(K.S_NSO_EAB7)
    ap.add_argument(K.S_BASE_DBAA, type=lambda s: int(s, 0))
    ap.add_argument(K.S_OUTDIR_A3E9)
    ap.add_argument(K.S_HLE_DE93)
    ap.add_argument(K.S_SYMBOLS_1344)
    ap.add_argument(K.S_CHUNK_8442, type=int, default=K.N_1500_9F69)  # functions per file at most
    ap.add_argument(K.S_CHUNK_BYTES_1CFC, type=int, default=K.N_1500000_490D)  # and about this much C (even compile times)
    ap.add_argument(K.S_BIG_FN_369B, type=int, default=K.N_100000_3BB7)  # bytes of C from which a function gets its own file
    ap.add_argument(K.S_LIMIT_9EC1, type=int, default=0)
    ap.add_argument(K.S_SIMD_CB76, choices=(K.S_ASM_3C46, K.S_C_0E09),
                    default=K.S_ASM_3C46 if platform.machine().lower() in (K.S_ARM64_6AF1, K.S_AARCH64_E802) else K.S_C_0E09)
    args = ap.parse_args()

    t0 = time.time()
    m = Module(args.name, args.nso, args.base, args.simd, args.symbols)
    if args.name == K.S_MAIN_13FD:
        import game_addresses
        try:
            version = game_addresses.select_build_id(m.nso.build_id)
        except game_addresses.UnknownGameVersion as e:
            sys.exit(f'{args.name}: {e}')
        import patches
        HOOKS, PATCHES = patches.HOOKS, patches.PATCHES
        label, build_id = game_addresses.VERSIONS[version]
        print(f'{args.name}: game version {label} (build {build_id[:K.N_16_B17E]}), address table {version}', file=sys.stderr)
    elif args.name == K.S_SDK_0065:
        import patches
        HOOKS = patches.HOOKS_SDK  # sdk-side hook table (patches.py; addresses can't collide with main's)
    starts = m.boundaries()
    hle = {}
    if args.hle and os.path.exists(args.hle):
        want = {l.strip() for l in open(args.hle) if l.strip() and not l.startswith(K.S_EMPTY_4734)}
        for name, info, shndx, value, size in m.nso.syms:
            if shndx and name in want:
                hle[args.base + value] = name

    # Partition text into functions; any branch/call target outside its function becomes a new
    # boundary (splitting the containing function) until a fixpoint is reached.
    start_set = set(starts)
    for i, w in enumerate(m.words):
        if (w & K.N_4227858432_1BBE) == K.N_2483027968_26C3:
            t = m.lo + i * K.N_4_4B22 + sext26(w) * K.N_4_4B22
            if m.in_text(t):
                start_set.add(t)
    # Code addresses materialized in registers (callbacks, vtables built at runtime): ADR, or
    # ADRP followed shortly by ADD on the same register.
    adrp_regs = {}
    for i, w in enumerate(m.words):
        pc = m.lo + i * K.N_4_4B22
        if (w & K.N_2667577344_556A) == K.N_268435456_510F:  # ADR
            imm = ((w >> K.N_5_EF2D) & K.N_524287_6A2B) << K.N_2_D473 | ((w >> K.N_29_3513) & K.N_3_4E07)
            if imm & (1 << K.N_20_F5CA):
                imm -= 1 << K.N_21_6F4B
            if m.in_text(pc + imm):
                start_set.add(pc + imm)
        elif (w & K.N_2667577344_556A) == K.N_2415919104_EAD9:  # ADRP
            imm = ((w >> K.N_5_EF2D) & K.N_524287_6A2B) << K.N_2_D473 | ((w >> K.N_29_3513) & K.N_3_4E07)
            if imm & (1 << K.N_20_F5CA):
                imm -= 1 << K.N_21_6F4B
            adrp_regs[w & K.N_31_EB1E] = ((pc & ~K.N_4095_1AFC) + (imm << K.N_12_6B51), i)
        elif (w & K.N_4290772992_CE38) == K.N_2432696320_4E29:  # ADD (imm, 64-bit, no shift)
            rn, rd = (w >> K.N_5_EF2D) & K.N_31_EB1E, w & K.N_31_EB1E
            if rn in adrp_regs and i - adrp_regs[rn][1] <= K.N_8_2C62:
                t = adrp_regs[rn][0] + ((w >> K.N_10_4A44) & K.N_4095_1AFC)
                if m.in_text(t):
                    start_set.add(t)
    scanned = {}
    rounds = 0
    while True:
        rounds += 1
        order = sorted(start_set)
        new = set()
        for i, a in enumerate(order):
            end = order[i + 1] if i + 1 < len(order) else m.hi
            key = (a, end)
            if key not in scanned:
                scanned[key] = m.scan(a, end)
            new |= scanned[key][1] - start_set
        if not new:
            break
        start_set |= new
    entries = {}
    for i, a in enumerate(order):
        end = order[i + 1] if i + 1 < len(order) else m.hi
        labels, ext, tables = scanned[(a, end)]
        entries[a] = (end, labels, tables)
    print(f'{args.name}: {len(starts)} boundaries, {len(entries)} functions, {rounds} rounds, {dict(m.stats)}, '
          f'{time.time() - t0:.0f}s', file=sys.stderr)

    os.makedirs(args.outdir, exist_ok=True)
    order = sorted(entries)
    if args.limit:
        order = order[:args.limit]
    nchunks = 0
    ci = 0
    pending = None  # (address, lines, refs) of a big function that starts the next file
    while ci < len(order) or pending:
        body, refs, size, fns = [], set(), 0, []
        while (pending or ci < len(order)) and len(fns) < args.chunk and size < args.chunk_bytes:
            if pending:
                a, lines, r = pending
                pending = None
            else:
                a = order[ci]
                ci += 1
                end, labels, tables = entries[a]
                lines = m.emit_function(a, end, labels, tables, hle)
                r = getattr(m, K.S_REFS_DA61, set())
                m._refs = set()
            n = sum(len(l) + 1 for l in lines)
            if n > args.big_fn and fns:  # (a big function gets a file of its own: they compile in parallel)
                pending = (a, lines, r)
                break
            body += lines
            size += n
            fns.append(a)
            refs |= r
            if n > args.big_fn:
                break
        path = os.path.join(args.outdir, f'{args.name}_{nchunks:03d}.c')
        text = [K.S_INCLUDE_CPU_H_N_43BF + (K.S_INCLUDE_SIMD_H_N_5EEF if m.lifter.sc else K.S_EMPTY_6F49)]
        text += [f'void {fname(r)}(Cpu *c);\n' for r in sorted(refs)]
        text += [f'void {sanitize(hle[a])}(Cpu *c);\n' for a in sorted(set(hle) & set(fns))]
        text += [K.S_N_AD65.join(body), K.S_N_AD65]
        write_if_changed(path, K.S_EMPTY_6F49.join(text))
        nchunks += 1
        # (the start screen's setup reads these for its progress bar: runtime/gamebuild.c)
        print(f'{args.name}: progress {ci}/{len(order)} files {nchunks}', file=sys.stderr, flush=True)
    for fn in os.listdir(args.outdir):  # an earlier run's extra chunks
        mm = re.fullmatch(re.escape(args.name) + K.S_D_3_C_3E57, fn)
        if mm and int(mm.group(1)) >= nchunks:
            os.remove(os.path.join(args.outdir, fn))
    with io.StringIO() as f:
        f.write(K.S_INCLUDE_CPU_H_N_INCLUDE_STDB_E097)
        for a in order:
            f.write(f'void {fname(a)}(Cpu *c);\n')
        f.write(f'const uint64_t {args.name}_fn_addrs[] = {{\n')
        f.write(K.S_N_D524.join(hx(a) for a in order))
        f.write(K.S_N_27E8)
        f.write(f'const GuestFn {args.name}_fn_ptrs[] = {{\n')
        f.write(K.S_N_D524.join(fname(a) for a in order))
        f.write(K.S_N_27E8)
        f.write(f'const uint64_t {args.name}_fn_count = {len(order)};\n')
        # Preserve all 32 NSO build-ID bytes, including trailing padding. Runtime
        # checks both modules before executing any translated function.
        build_id = bytes.fromhex(m.nso.build_id).ljust(K.N_32_E29C, K.B_B_X00_38ED)
        f.write(f'const uint8_t {args.name}_nso_build_id[32] = {{' +
                K.S_EMPTY_EE1B.join(f'0x{b:02x}' for b in build_id) + K.S_N_27E8)
        write_if_changed(os.path.join(args.outdir, f'{args.name}_table.c'), f.getvalue())
    write_if_changed(os.path.join(args.outdir, f'{args.name}_eh.c'),
                     emit_eh_table(args.name, m.eh_sites))
    if args.name == K.S_MAIN_13FD:  # (what this code was generated from: the Makefile's game code library id, GAMELIB_BUILD_ID)
        write_if_changed(os.path.join(args.outdir, K.S_MAIN_BUILD_ID_TXT_065B), game_addresses.VERSIONS[version][1] + K.S_N_AD65)
    print(f'{args.name}: wrote {nchunks} chunks, {time.time() - t0:.0f}s total', file=sys.stderr)


if __name__ == K.S_MAIN_ACB5:
    main()

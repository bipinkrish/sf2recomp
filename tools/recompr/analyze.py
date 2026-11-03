"""Function discovery and instruction census for an NSO module."""
from _sf2_recompiler_constants import ANALYZE as K
import struct
import sys
from collections import Counter

import capstone
from nso import (Nso, R_AARCH64_ABS64, R_AARCH64_GLOB_DAT,
                 R_AARCH64_JUMP_SLOT, R_AARCH64_RELATIVE)


def read_encoded(img, off, enc, pc_base):
    fmt = enc & K.N_15_E629
    app = enc & K.N_112_B155
    if fmt == K.N_11_4FC8:      # sdata4
        v, size = struct.unpack_from(K.S_I_BA5E, img, off)[0], K.N_4_4B22
    elif fmt == K.N_3_4E07:    # udata4
        v, size = struct.unpack_from(K.S_I_833B, img, off)[0], K.N_4_4B22
    elif fmt in (K.N_12_6B51, K.N_4_4B22):
        v, size = struct.unpack_from(K.S_Q_D6E3 if fmt == K.N_12_6B51 else K.S_Q_C5AE, img, off)[0], K.N_8_2C62
    else:
        raise ValueError(f'unsupported encoding {enc:#x}')
    if app == K.N_16_B17E:
        v += off
    elif app == K.N_48_9801:
        v += pc_base
    return v, size


def eh_frame_functions(n):
    """Function start addresses from .eh_frame_hdr's binary search table."""
    return [loc for loc, _ in eh_frame_table(n)]


def eh_frame_table(n):
    """(function start, FDE address) entries from .eh_frame_hdr."""
    img, hdr = n.image, n.eh_start
    if not hdr or hdr >= len(img):
        return []
    ver, ptr_enc, cnt_enc, tbl_enc = img[hdr:hdr + K.N_4_4B22]
    off = hdr + K.N_4_4B22
    _, sz = read_encoded(img, off, ptr_enc, hdr)
    off += sz
    count, sz = read_encoded(img, off, cnt_enc, hdr)
    off += sz
    entries = []
    for _ in range(count):
        loc, sz = read_encoded(img, off, tbl_enc, hdr)
        off += sz
        fde, sz = read_encoded(img, off, tbl_enc, hdr)
        off += sz
        entries.append((loc, fde))
    return entries


def _uleb(img, off):
    value = shift = 0
    while off < len(img):
        b = img[off]
        off += 1
        value |= (b & K.N_127_922C) << shift
        if b < K.N_128_2747:
            return value, off
        shift += K.N_7_7902
        if shift > K.N_70_FF5A:
            break
    raise ValueError(K.S_BAD_ULEB128_C6C1)


def _sleb(img, off):
    value = shift = 0
    while off < len(img):
        b = img[off]
        off += 1
        value |= (b & K.N_127_922C) << shift
        shift += K.N_7_7902
        if b < K.N_128_2747:
            if b & K.N_64_A68B:
                value -= 1 << shift
            return value, off
        if shift > K.N_70_FF5A:
            break
    raise ValueError(K.S_BAD_SLEB128_82EC)


def _encoded_size(enc):
    fmt = enc & K.N_15_E629
    return {0x00: K.N_8_2C62, K.N_2_D473: K.N_2_D473, K.N_3_4E07: K.N_4_4B22, K.N_4_4B22: K.N_8_2C62,
            K.N_10_4A44: K.N_2_D473, K.N_11_4FC8: K.N_4_4B22, K.N_12_6B51: K.N_8_2C62}.get(fmt)


def _encoded(img, off, enc, data_base=0, func_base=0):
    """Read a DW_EH_PE value and return (value, next offset, indirect)."""
    if enc == K.N_255_9556:
        raise ValueError(K.S_OMITTED_DW_EH_PE_VALUE_93FB)
    fmt, app = enc & K.N_15_E629, enc & K.N_112_B155
    if app == K.N_80_4844:  # aligned native pointer in this 64-bit image
        off = (off + K.N_7_7902) & ~K.N_7_7902
    if off < 0:
        raise ValueError(K.S_NEGATIVE_DW_EH_PE_OFFSET_E1CB)
    if fmt == 0x01:
        value, end = _uleb(img, off)
    elif fmt == K.N_9_1958:
        value, end = _sleb(img, off)
    else:
        size = _encoded_size(enc)
        if size is None or off + size > len(img):
            raise ValueError(f'unsupported DW_EH_PE encoding {enc:#x}')
        signed = fmt in (K.N_10_4A44, K.N_11_4FC8, K.N_12_6B51)
        value = int.from_bytes(img[off:off + size], K.S_LITTLE_7BA5, signed=signed)
        end = off + size
    # A null encoded pointer stays null even with a relative application.
    if value == 0:
        return 0, end, bool(enc & K.N_128_2747)
    if app == K.N_16_B17E:       # pcrel
        value += off
    elif app == K.N_32_E29C:     # textrel (the NSO text segment starts at image offset zero)
        pass
    elif app == K.N_48_9801:     # datarel
        value += data_base
    elif app == K.N_64_A68B:     # funcrel
        value += func_base
    elif app not in (0, K.N_80_4844):
        raise ValueError(f'unsupported DW_EH_PE application {app:#x}')
    return value, end, bool(enc & K.N_128_2747)


def _relocated_pointer(n, off, base):
    """Resolve a pointer slot in the unrelocated NSO image to its guest address."""
    relocs = getattr(n, K.S_EH_RELOCS_3A99, None)
    if relocs is None:
        relocs = {r[0]: r for r in n.relocs}
        n._eh_relocs = relocs
    r = relocs.get(off)
    if r:
        _, typ, sym, add = r
        if typ == R_AARCH64_RELATIVE:
            return base + add
        if typ == R_AARCH64_ABS64 and sym < len(n.syms):
            name, _, shndx, value, _ = n.syms[sym]
            if shndx:
                return base + value + add
        if typ in (R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT) and sym < len(n.syms):
            name, _, shndx, value, _ = n.syms[sym]
            if shndx:
                return base + value
        # An unresolved symbol is not the null type entry used by catch (...).
        return None
    if off < 0 or off + K.N_8_2C62 > len(n.image):
        return None
    raw = int.from_bytes(n.image[off:off + K.N_8_2C62], K.S_LITTLE_7BA5)
    if raw == 0:
        return 0
    if raw >= base:
        return raw
    if 0 < raw < n.size:
        return base + raw
    return None


def _pointer_value(n, off, enc, value, indirect, base, data_base=0, func_base=0):
    if value == 0:
        return 0
    if indirect:
        return _relocated_pointer(n, value, base)
    if value >= base:
        return value
    if 0 <= value < n.size:
        return base + value
    return None


def _cie_info(n, cie_off, base):
    img = n.image
    length = int.from_bytes(img[cie_off:cie_off + K.N_4_4B22], K.S_LITTLE_7BA5)
    if length == K.N_4294967295_F807:
        length = int.from_bytes(img[cie_off + K.N_4_4B22:cie_off + K.N_12_6B51], K.S_LITTLE_7BA5)
        p, id_size = cie_off + K.N_12_6B51, K.N_8_2C62
    else:
        p, id_size = cie_off + K.N_4_4B22, K.N_4_4B22
    cie_end = p + length
    cie_id = int.from_bytes(img[p:p + id_size], K.S_LITTLE_7BA5)
    if cie_id != 0:
        raise ValueError(K.S_FDE_POINTS_TO_A_NON_CIE_RECO_1A4F)
    p += id_size
    version = img[p]
    p += 1
    aug_end = img.index(0, p, cie_end)
    augmentation = img[p:aug_end].decode(K.S_ASCII_0C02, K.S_REPLACE_71A6)
    p = aug_end + 1
    if version == K.N_4_4B22:
        p += K.N_2_D473  # address size and segment-selector size
    _, p = _uleb(img, p)  # code alignment
    _, p = _sleb(img, p)  # data alignment
    if version == 1:
        p += 1
    else:
        _, p = _uleb(img, p)  # return register
    fde_enc, lsda_enc = 0x00, K.N_255_9556
    if augmentation.startswith(K.S_Z_0DF4):
        aug_len, p = _uleb(img, p)
        aug_end = p + aug_len
        for ch in augmentation[1:]:
            if ch == K.S_P_2E20:
                enc = img[p]
                p += 1
                _, p, _ = _encoded(img, p, enc, data_base=n.eh_start, func_base=base)
            elif ch == K.S_L_F18E:
                lsda_enc = img[p]
                p += 1
            elif ch == K.S_R_501C:
                fde_enc = img[p]
                p += 1
            elif ch == K.S_S_657E:
                pass
            else:
                raise ValueError(f'unsupported CIE augmentation {ch!r}')
        p = aug_end
    return fde_enc, lsda_enc, augmentation.startswith(K.S_Z_0DF4), id_size


def _lsda_handlers(n, lsda, func_start, base):
    """Call-site ranges and ordered catch clauses from one LSDA."""
    img = n.image
    p = lsda
    lp_enc = img[p]
    p += 1
    if lp_enc == K.N_255_9556:
        lp_start = func_start
    else:
        lp_value, p, indirect = _encoded(img, p, lp_enc, data_base=n.eh_start,
                                         func_base=func_start)
        lp_start = _pointer_value(n, p, lp_enc, lp_value, indirect, base,
                                  n.eh_start, func_start)
        if lp_start is None:
            raise ValueError(K.S_UNRESOLVED_LSDA_LANDING_PAD__E0A1)
        lp_start -= base if lp_start >= base else 0

    ttype_enc = img[p]
    p += 1
    ttype_base = None
    if ttype_enc != K.N_255_9556:
        ttype_offset, p = _uleb(img, p)
        ttype_base = p + ttype_offset
    callsite_enc = img[p]
    p += 1
    callsite_len, p = _uleb(img, p)
    callsite_end = p + callsite_len
    if callsite_end > len(img):
        raise ValueError(K.S_LSDA_CALL_SITE_TABLE_EXCEEDS_AEB6)
    action_base = callsite_end
    result = []
    while p < callsite_end:
        start, p, _ = _encoded(img, p, callsite_enc, data_base=lsda,
                               func_base=func_start)
        length, p, _ = _encoded(img, p, callsite_enc, data_base=lsda,
                                func_base=func_start)
        pad, p, _ = _encoded(img, p, callsite_enc, data_base=lsda,
                             func_base=func_start)
        action, p = _uleb(img, p)
        if p > callsite_end:
            raise ValueError(K.S_TRUNCATED_LSDA_CALL_SITE_ENT_E1BF)
        if not pad or not action or not ttype_base:
            continue
        handlers, seen = [], set()
        action_off = action_base + action - 1
        for _ in range(K.N_128_2747):
            if action_off in seen or not action_base <= action_off < len(img):
                break
            seen.add(action_off)
            filt, next_off = _sleb(img, action_off)
            next_field = next_off
            displacement, _ = _sleb(img, next_off)
            # Filter zero is cleanup, not a handler. Positive filters index the
            # type table and are also the selector expected by the landing pad.
            if filt > 0:
                size = _encoded_size(ttype_enc)
                if size:
                    type_entry = ttype_base - filt * size
                    try:
                        encoded_type, _, indirect = _encoded(
                            img, type_entry, ttype_enc, data_base=n.eh_start,
                            func_base=func_start)
                        type_info = _pointer_value(n, type_entry, ttype_enc,
                                                   encoded_type, indirect, base,
                                                   n.eh_start, func_start)
                        if type_info is not None:
                            handlers.append((type_info, filt))
                    except (IndexError, ValueError):
                        pass
            if not displacement:
                break
            action_off = next_field + displacement
        if handlers:
            result.append((base + lp_start + start,
                           base + lp_start + start + length,
                           base + lp_start + pad, handlers))
    return result


def eh_frame_handlers(n, base):
    """EH call-site ranges with catch selectors and their absolute guest landing pads."""
    img = n.image
    result = []
    cie_cache = {}
    for func_start, fde_off in eh_frame_table(n):
        if fde_off < 0 or fde_off + K.N_8_2C62 > len(img):
            continue
        length = int.from_bytes(img[fde_off:fde_off + K.N_4_4B22], K.S_LITTLE_7BA5)
        if length == K.N_4294967295_F807:
            length = int.from_bytes(img[fde_off + K.N_4_4B22:fde_off + K.N_12_6B51], K.S_LITTLE_7BA5)
            p, id_size = fde_off + K.N_12_6B51, K.N_8_2C62
        else:
            p, id_size = fde_off + K.N_4_4B22, K.N_4_4B22
        if not length or fde_off + (K.N_12_6B51 if id_size == K.N_8_2C62 else K.N_4_4B22) + length > len(img):
            continue
        cie_field = fde_off + (K.N_12_6B51 if id_size == K.N_8_2C62 else K.N_4_4B22)
        cie_ptr = int.from_bytes(img[cie_field:cie_field + id_size], K.S_LITTLE_7BA5)
        cie_off = cie_field - cie_ptr
        try:
            info = cie_cache.get(cie_off)
            if info is None:
                info = _cie_info(n, cie_off, base)
                cie_cache[cie_off] = info
            fde_enc, lsda_enc, has_z_aug, _ = info
            p = cie_field + id_size
            start, p, _ = _encoded(img, p, fde_enc, data_base=n.eh_start,
                                   func_base=base)
            range_enc = fde_enc & K.N_15_E629
            extent, p, _ = _encoded(img, p, range_enc)
            if has_z_aug:
                aug_len, p = _uleb(img, p)
                aug_end = p + aug_len
                if lsda_enc != K.N_255_9556 and p < aug_end:
                    lsda_value, _, indirect = _encoded(img, p, lsda_enc,
                                                       data_base=n.eh_start,
                                                       func_base=start)
                    lsda = _pointer_value(n, p, lsda_enc, lsda_value,
                                          indirect, base, n.eh_start, start)
                    if lsda:
                        lsda -= base if lsda >= base else 0
                        result.extend(_lsda_handlers(n, lsda, start, base))
        except (IndexError, ValueError, struct.error):
            # Some NSOs have extension encodings outside the common zPLR forms. A bad or
            # unsupported FDE must not prevent generation of the rest of the module.
            continue
    result.sort(key=lambda site: (site[0], site[1]))
    return result


def discover(n):
    text_lo, text_hi = n.text[0], n.text[0] + n.text[1]
    in_text = lambda a: text_lo <= a < text_hi and a % K.N_4_4B22 == 0
    funcs = {}
    for a in eh_frame_functions(n):
        if in_text(a):
            funcs[a] = K.S_EH_5A38
    for name, info, shndx, value, size in n.syms:
        if shndx and in_text(value) and (info & K.N_15_E629) == K.N_2_D473:
            funcs.setdefault(value, K.S_SYM_A8A8)
    # Code pointers stored in data (vtables, callbacks)
    ptr_targets = set()
    for off, typ, sym, add in n.relocs:
        if typ == R_AARCH64_RELATIVE and in_text(add):
            ptr_targets.add(add)
    for a in ptr_targets:
        funcs.setdefault(a, K.S_PTR_1BEB)
    # BL targets
    code = bytes(n.image[text_lo:text_hi])
    words = struct.unpack(f'<{len(code) // K.N_4_4B22}I', code)
    for i, w in enumerate(words):
        if (w & K.N_4227858432_1BBE) == K.N_2483027968_26C3:
            imm = w & K.N_67108863_97F6
            if imm & K.N_33554432_B589:
                imm -= K.N_67108864_2B3C
            t = text_lo + i * K.N_4_4B22 + imm * K.N_4_4B22
            if in_text(t):
                funcs.setdefault(t, K.S_BL_4BCD)
    return funcs, words


def census(n, words):
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    c = Counter()
    bad = 0
    code = struct.pack(f'<{len(words)}I', *words)
    off = 0
    while off < len(code):
        got = False
        for ins in md.disasm_lite(code[off:], n.text[0] + off):
            c[ins[K.N_2_D473]] += 1
            off += K.N_4_4B22
            got = True
        if off < len(code):   # undecodable word: skip it
            bad += 1
            off += K.N_4_4B22
    return c, bad


if __name__ == K.S_MAIN_ACB5:
    n = Nso(sys.argv[1])
    funcs, words = discover(n)
    print(f'functions: {len(funcs)}  by source: {dict(Counter(funcs.values()))}')
    if K.S_CENSUS_7499 in sys.argv:
        c, bad = census(n, words)
        print(f'instructions: {sum(c.values())}  undecodable words: {bad}  distinct mnemonics: {len(c)}')
        print(K.S_EMPTY_730D.join(f'{m}:{k}' for m, k in c.most_common()))

"""Function discovery and instruction census for an NSO module."""
import struct
import sys
from collections import Counter

import capstone
from nso import (Nso, R_AARCH64_ABS64, R_AARCH64_GLOB_DAT,
                 R_AARCH64_JUMP_SLOT, R_AARCH64_RELATIVE)


def read_encoded(img, off, enc, pc_base):
    fmt = enc & 0x0F
    app = enc & 0x70
    if fmt == 0x0B:      # sdata4
        v, size = struct.unpack_from('<i', img, off)[0], 4
    elif fmt == 0x03:    # udata4
        v, size = struct.unpack_from('<I', img, off)[0], 4
    elif fmt in (0x0C, 0x04):
        v, size = struct.unpack_from('<q' if fmt == 0x0C else '<Q', img, off)[0], 8
    else:
        raise ValueError(f'unsupported encoding {enc:#x}')
    if app == 0x10:
        v += off
    elif app == 0x30:
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
    ver, ptr_enc, cnt_enc, tbl_enc = img[hdr:hdr + 4]
    off = hdr + 4
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
        value |= (b & 0x7f) << shift
        if b < 0x80:
            return value, off
        shift += 7
        if shift > 70:
            break
    raise ValueError('bad ULEB128')


def _sleb(img, off):
    value = shift = 0
    while off < len(img):
        b = img[off]
        off += 1
        value |= (b & 0x7f) << shift
        shift += 7
        if b < 0x80:
            if b & 0x40:
                value -= 1 << shift
            return value, off
        if shift > 70:
            break
    raise ValueError('bad SLEB128')


def _encoded_size(enc):
    fmt = enc & 0x0f
    return {0x00: 8, 0x02: 2, 0x03: 4, 0x04: 8,
            0x0a: 2, 0x0b: 4, 0x0c: 8}.get(fmt)


def _encoded(img, off, enc, data_base=0, func_base=0):
    """Read a DW_EH_PE value and return (value, next offset, indirect)."""
    if enc == 0xff:
        raise ValueError('omitted DW_EH_PE value')
    fmt, app = enc & 0x0f, enc & 0x70
    if app == 0x50:  # aligned native pointer in this 64-bit image
        off = (off + 7) & ~7
    if off < 0:
        raise ValueError('negative DW_EH_PE offset')
    if fmt == 0x01:
        value, end = _uleb(img, off)
    elif fmt == 0x09:
        value, end = _sleb(img, off)
    else:
        size = _encoded_size(enc)
        if size is None or off + size > len(img):
            raise ValueError(f'unsupported DW_EH_PE encoding {enc:#x}')
        signed = fmt in (0x0a, 0x0b, 0x0c)
        value = int.from_bytes(img[off:off + size], 'little', signed=signed)
        end = off + size
    # A null encoded pointer stays null even with a relative application.
    if value == 0:
        return 0, end, bool(enc & 0x80)
    if app == 0x10:       # pcrel
        value += off
    elif app == 0x20:     # textrel (the NSO text segment starts at image offset zero)
        pass
    elif app == 0x30:     # datarel
        value += data_base
    elif app == 0x40:     # funcrel
        value += func_base
    elif app not in (0, 0x50):
        raise ValueError(f'unsupported DW_EH_PE application {app:#x}')
    return value, end, bool(enc & 0x80)


def _relocated_pointer(n, off, base):
    """Resolve a pointer slot in the unrelocated NSO image to its guest address."""
    relocs = getattr(n, '_eh_relocs', None)
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
    if off < 0 or off + 8 > len(n.image):
        return None
    raw = int.from_bytes(n.image[off:off + 8], 'little')
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
    length = int.from_bytes(img[cie_off:cie_off + 4], 'little')
    if length == 0xffffffff:
        length = int.from_bytes(img[cie_off + 4:cie_off + 12], 'little')
        p, id_size = cie_off + 12, 8
    else:
        p, id_size = cie_off + 4, 4
    cie_end = p + length
    cie_id = int.from_bytes(img[p:p + id_size], 'little')
    if cie_id != 0:
        raise ValueError('FDE points to a non-CIE record')
    p += id_size
    version = img[p]
    p += 1
    aug_end = img.index(0, p, cie_end)
    augmentation = img[p:aug_end].decode('ascii', 'replace')
    p = aug_end + 1
    if version == 4:
        p += 2  # address size and segment-selector size
    _, p = _uleb(img, p)  # code alignment
    _, p = _sleb(img, p)  # data alignment
    if version == 1:
        p += 1
    else:
        _, p = _uleb(img, p)  # return register
    fde_enc, lsda_enc = 0x00, 0xff
    if augmentation.startswith('z'):
        aug_len, p = _uleb(img, p)
        aug_end = p + aug_len
        for ch in augmentation[1:]:
            if ch == 'P':
                enc = img[p]
                p += 1
                _, p, _ = _encoded(img, p, enc, data_base=n.eh_start, func_base=base)
            elif ch == 'L':
                lsda_enc = img[p]
                p += 1
            elif ch == 'R':
                fde_enc = img[p]
                p += 1
            elif ch == 'S':
                pass
            else:
                raise ValueError(f'unsupported CIE augmentation {ch!r}')
        p = aug_end
    return fde_enc, lsda_enc, augmentation.startswith('z'), id_size


def _lsda_handlers(n, lsda, func_start, base):
    """Call-site ranges and ordered catch clauses from one LSDA."""
    img = n.image
    p = lsda
    lp_enc = img[p]
    p += 1
    if lp_enc == 0xff:
        lp_start = func_start
    else:
        lp_value, p, indirect = _encoded(img, p, lp_enc, data_base=n.eh_start,
                                         func_base=func_start)
        lp_start = _pointer_value(n, p, lp_enc, lp_value, indirect, base,
                                  n.eh_start, func_start)
        if lp_start is None:
            raise ValueError('unresolved LSDA landing-pad base')
        lp_start -= base if lp_start >= base else 0

    ttype_enc = img[p]
    p += 1
    ttype_base = None
    if ttype_enc != 0xff:
        ttype_offset, p = _uleb(img, p)
        ttype_base = p + ttype_offset
    callsite_enc = img[p]
    p += 1
    callsite_len, p = _uleb(img, p)
    callsite_end = p + callsite_len
    if callsite_end > len(img):
        raise ValueError('LSDA call-site table exceeds image')
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
            raise ValueError('truncated LSDA call-site entry')
        if not pad or not action or not ttype_base:
            continue
        handlers, seen = [], set()
        action_off = action_base + action - 1
        for _ in range(128):
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
        if fde_off < 0 or fde_off + 8 > len(img):
            continue
        length = int.from_bytes(img[fde_off:fde_off + 4], 'little')
        if length == 0xffffffff:
            length = int.from_bytes(img[fde_off + 4:fde_off + 12], 'little')
            p, id_size = fde_off + 12, 8
        else:
            p, id_size = fde_off + 4, 4
        if not length or fde_off + (12 if id_size == 8 else 4) + length > len(img):
            continue
        cie_field = fde_off + (12 if id_size == 8 else 4)
        cie_ptr = int.from_bytes(img[cie_field:cie_field + id_size], 'little')
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
            range_enc = fde_enc & 0x0f
            extent, p, _ = _encoded(img, p, range_enc)
            if has_z_aug:
                aug_len, p = _uleb(img, p)
                aug_end = p + aug_len
                if lsda_enc != 0xff and p < aug_end:
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
    in_text = lambda a: text_lo <= a < text_hi and a % 4 == 0
    funcs = {}
    for a in eh_frame_functions(n):
        if in_text(a):
            funcs[a] = 'eh'
    for name, info, shndx, value, size in n.syms:
        if shndx and in_text(value) and (info & 0xF) == 2:
            funcs.setdefault(value, 'sym')
    # Code pointers stored in data (vtables, callbacks)
    ptr_targets = set()
    for off, typ, sym, add in n.relocs:
        if typ == R_AARCH64_RELATIVE and in_text(add):
            ptr_targets.add(add)
    for a in ptr_targets:
        funcs.setdefault(a, 'ptr')
    # BL targets
    code = bytes(n.image[text_lo:text_hi])
    words = struct.unpack(f'<{len(code) // 4}I', code)
    for i, w in enumerate(words):
        if (w & 0xFC000000) == 0x94000000:
            imm = w & 0x03FFFFFF
            if imm & 0x02000000:
                imm -= 0x04000000
            t = text_lo + i * 4 + imm * 4
            if in_text(t):
                funcs.setdefault(t, 'bl')
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
            c[ins[2]] += 1
            off += 4
            got = True
        if off < len(code):   # undecodable word: skip it
            bad += 1
            off += 4
    return c, bad


if __name__ == '__main__':
    n = Nso(sys.argv[1])
    funcs, words = discover(n)
    print(f'functions: {len(funcs)}  by source: {dict(Counter(funcs.values()))}')
    if '--census' in sys.argv:
        c, bad = census(n, words)
        print(f'instructions: {sum(c.values())}  undecodable words: {bad}  distinct mnemonics: {len(c)}')
        print(' '.join(f'{m}:{k}' for m, k in c.most_common()))

"""NSO loader: decompress segments, build the memory image, parse MOD0/dynamic info."""
from _sf2_recompiler_constants import NSO as K
import struct
import sys
import lz4.block

DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ = 0, 1, K.N_2_D473, K.N_4_4B22, K.N_5_EF2D, K.N_6_E7F6, K.N_7_7902, K.N_8_2C62
DT_STRSZ, DT_JMPREL, DT_RELACOUNT = K.N_10_4A44, K.N_23_535F, K.N_1879048185_F11C
R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT, R_AARCH64_RELATIVE = K.N_257_4C97, K.N_1025_4637, K.N_1026_582C, K.N_1027_60F2


class Nso:
    def __init__(self, path):
        raw = open(path, K.S_RB_81FE).read()
        assert raw[:K.N_4_4B22] == K.B_B_NSO0_4F90, path
        flags = struct.unpack_from(K.S_I_833B, raw, K.N_12_6B51)[0]
        self.build_id = raw[K.N_64_A68B:K.N_96_7B1A].rstrip(K.B_B_X00_38ED).hex()
        segs = []
        for i in range(K.N_3_4E07):
            file_off, mem_off, dec_size = struct.unpack_from(K.S_III_0506, raw, K.N_16_B17E + i * K.N_16_B17E)
            comp_size = struct.unpack_from(K.S_I_833B, raw, K.N_96_7B1A + i * K.N_4_4B22)[0]
            data = raw[file_off:file_off + comp_size]
            if flags & (1 << i):
                data = lz4.block.decompress(data, uncompressed_size=dec_size)
            segs.append((mem_off, data))
        self.bss_size = struct.unpack_from(K.S_I_833B, raw, K.N_60_39FA)[0]
        self.text = (segs[0][0], len(segs[0][1]))
        self.rodata = (segs[1][0], len(segs[1][1]))
        self.data = (segs[K.N_2_D473][0], len(segs[K.N_2_D473][1]))
        end = segs[K.N_2_D473][0] + len(segs[K.N_2_D473][1]) + self.bss_size
        self.size = (end + K.N_4095_1AFC) & ~K.N_4095_1AFC
        img = bytearray(self.size)
        for off, d in segs:
            img[off:off + len(d)] = d
        self.image = img
        self._parse_mod0()

    def u32(self, off): return struct.unpack_from(K.S_I_833B, self.image, off)[0]
    def u64(self, off): return struct.unpack_from(K.S_Q_C5AE, self.image, off)[0]

    def _parse_mod0(self):
        mod0 = self.u32(K.N_4_4B22)
        assert self.image[mod0:mod0 + K.N_4_4B22] == K.B_B_MOD0_0A33
        rel = lambda i: mod0 + struct.unpack_from(K.S_I_BA5E, self.image, mod0 + K.N_4_4B22 + i * K.N_4_4B22)[0]
        self.dynamic, self.bss_start, self.bss_end = rel(0), rel(1), rel(K.N_2_D473)
        self.eh_start, self.eh_end, self.module_obj = rel(K.N_3_4E07), rel(K.N_4_4B22), rel(K.N_5_EF2D)
        dyn = {}
        needed = []
        off = self.dynamic
        while True:
            tag, val = self.u64(off), self.u64(off + K.N_8_2C62)
            off += K.N_16_B17E
            if tag == DT_NULL:
                break
            if tag == DT_NEEDED:
                needed.append(val)
            dyn.setdefault(tag, val)
        self.dyn = dyn
        strtab = dyn.get(DT_STRTAB, 0)
        self.strtab = strtab
        self.needed = [self.cstr(strtab + n) for n in needed]
        # dynsym count from DT_HASH nchain
        self.syms = []
        if DT_HASH in dyn and DT_SYMTAB in dyn:
            nchain = self.u32(dyn[DT_HASH] + K.N_4_4B22)
            st = dyn[DT_SYMTAB]
            for i in range(nchain):
                o = st + i * K.N_24_C235
                name, info, other, shndx = struct.unpack_from(K.S_IBBH_9D02, self.image, o)
                value, size = struct.unpack_from(K.S_QQ_9918, self.image, o + K.N_8_2C62)
                self.syms.append((self.cstr(strtab + name), info, shndx, value, size))
        self.relocs = []
        for base_tag, size_tag in ((DT_RELA, DT_RELASZ), (DT_JMPREL, DT_PLTRELSZ)):
            if base_tag in dyn:
                b, n = dyn[base_tag], dyn[size_tag] // K.N_24_C235
                for i in range(n):
                    r_off, r_info, r_add = struct.unpack_from(K.S_QQQ_34EA, self.image, b + i * K.N_24_C235)
                    self.relocs.append((r_off, r_info & K.N_4294967295_F807, r_info >> K.N_32_E29C, r_add))

    def cstr(self, off):
        end = self.image.index(K.B_B_X00_38ED, off)
        return self.image[off:end].decode(K.S_UTF_8_5383, K.S_REPLACE_71A6)

    def imports(self):
        return [s for s in self.syms if s[0] and s[K.N_2_D473] == 0]

    def exports(self):
        return [s for s in self.syms if s[0] and s[K.N_2_D473] != 0]


if __name__ == K.S_MAIN_ACB5:
    from collections import Counter
    for p in sys.argv[1:]:
        n = Nso(p)
        print(f'== {p}  build {n.build_id[:K.N_16_B17E]}  image 0x{n.size:x}')
        print(f'  text 0x{n.text[0]:x}+0x{n.text[1]:x}  ro 0x{n.rodata[0]:x}+0x{n.rodata[1]:x}  '
              f'data 0x{n.data[0]:x}+0x{n.data[1]:x}  bss 0x{n.bss_size:x}')
        print(f'  needed {n.needed}  syms {len(n.syms)}  imports {len(n.imports())}  exports {len(n.exports())}')
        print(f'  relocs {dict(Counter(r[1] for r in n.relocs))}')

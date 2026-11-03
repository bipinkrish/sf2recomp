#include "constants/core.h"
// NSO loading, relocation, and symbol resolution.
#include <stdlib.h>
#include <string.h>

#include "rt.h"

Module g_modules[SF2C_LOADER_N_4_4B22];
int g_nmodules;

enum { DT_NULL = 0, DT_PLTRELSZ = SF2C_LOADER_N_2_D473, DT_HASH = SF2C_LOADER_N_4_4B22, DT_STRTAB = SF2C_LOADER_N_5_EF2D, DT_SYMTAB = SF2C_LOADER_N_6_E7F6, DT_RELA = SF2C_LOADER_N_7_7902, DT_RELASZ = SF2C_LOADER_N_8_2C62,
       DT_JMPREL = SF2C_LOADER_N_23_535F, DT_INIT_ARRAY = SF2C_LOADER_N_25_B7A5, DT_FINI_ARRAY = SF2C_LOADER_N_26_5F9C, DT_INIT_ARRAYSZ = SF2C_LOADER_N_27_6706, DT_FINI_ARRAYSZ = SF2C_LOADER_N_28_59E1 };
enum { R_ABS64 = SF2C_LOADER_N_257_4C97, R_GLOB_DAT = SF2C_LOADER_N_1025_4637, R_JUMP_SLOT = SF2C_LOADER_N_1026_582C, R_RELATIVE = SF2C_LOADER_N_1027_60F2 };

typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } Elf64Sym;
typedef struct { uint64_t off, info; int64_t addend; } Elf64Rela;

size_t rt_lz4_decompress(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen) {  // (LZ4 block; also shader_prep.c)
    const uint8_t *s = src, *se = src + srclen;
    uint8_t *d = dst, *de = dst + dstlen;
    while (s < se) {
        uint8_t tok = *s++;
        size_t lit = tok >> SF2C_LOADER_N_4_4B22;
        if (lit == SF2C_LOADER_N_15_E629) { uint8_t b; do { b = *s++; lit += b; } while (b == SF2C_LOADER_N_255_9556); }
        if (d + lit > de || s + lit > se) break;
        memcpy(d, s, lit);
        d += lit; s += lit;
        if (s >= se) break;
        size_t off = s[0] | (s[1] << SF2C_LOADER_N_8_2C62);
        s += SF2C_LOADER_N_2_D473;
        size_t ml = tok & SF2C_LOADER_N_15_E629;
        if (ml == SF2C_LOADER_N_15_E629) { uint8_t b; do { b = *s++; ml += b; } while (b == SF2C_LOADER_N_255_9556); }
        ml += SF2C_LOADER_N_4_4B22;
        uint8_t *m = d - off;
        if (m < dst || d + ml > de) break;
        while (ml--) *d++ = *m++;
    }
    return d - dst;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, SF2C_LOADER_S_RB_46CA);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*len);
    fread(p, 1, *len, f);
    fclose(f);
    return p;
}

static void load_nso(Module *m, const char *path, uint64_t base) {
    size_t len;
    uint8_t *raw = read_file(path, &len);
    if (!raw || memcmp(raw, SF2C_LOADER_S_NSO0_F632, SF2C_LOADER_N_4_4B22)) FATAL(SF2C_LOADER_S_CANNOT_LOAD_S_N_E740, path);
    uint32_t flags = *(uint32_t *)(raw + SF2C_LOADER_N_0XC_D882);
    uint32_t seg[SF2C_LOADER_N_3_4E07][SF2C_LOADER_N_3_4E07];  // file_off, mem_off, size
    for (int i = 0; i < SF2C_LOADER_N_3_4E07; i++) memcpy(seg[i], raw + SF2C_LOADER_N_0X10_BF9C + i * SF2C_LOADER_N_0X10_BF9C, SF2C_LOADER_N_12_6B51);
    uint32_t bss = *(uint32_t *)(raw + SF2C_LOADER_N_0X3C_E42C);
    uint64_t size = (seg[SF2C_LOADER_N_2_D473][1] + seg[SF2C_LOADER_N_2_D473][SF2C_LOADER_N_2_D473] + bss + SF2C_LOADER_N_0X3FFF_F000) & ~SF2C_LOADER_N_0X3FFFULL_DDD1;
    plat_commit(base, size);  // inside the guest window: zero-filled, read/write
    for (int i = 0; i < SF2C_LOADER_N_3_4E07; i++) {
        uint32_t csize = *(uint32_t *)(raw + SF2C_LOADER_N_0X60_3D6E + i * SF2C_LOADER_N_4_4B22);
        uint8_t *dst = (uint8_t *)base + seg[i][1];
        if (flags & (1 << i)) {
            size_t got = rt_lz4_decompress(raw + seg[i][0], csize, dst, seg[i][SF2C_LOADER_N_2_D473]);
            if (got != seg[i][SF2C_LOADER_N_2_D473]) FATAL(SF2C_LOADER_S_LZ4_S_SEG_D_ZU_U_N_AF48, path, i, got, seg[i][SF2C_LOADER_N_2_D473]);
        } else {
            memcpy(dst, raw + seg[i][0], seg[i][SF2C_LOADER_N_2_D473]);
        }
    }
    free(raw);
    m->base = base;
    m->size = size;
    m->text_off = seg[0][1]; m->text_size = seg[0][SF2C_LOADER_N_2_D473];
    m->ro_off = seg[1][1];   m->ro_size = seg[1][SF2C_LOADER_N_2_D473];
    m->data_off = seg[SF2C_LOADER_N_2_D473][1]; m->data_size = seg[SF2C_LOADER_N_2_D473][SF2C_LOADER_N_2_D473] + bss;

    uint32_t mod0 = *(uint32_t *)(base + SF2C_LOADER_N_4_4B22);
    uint64_t mod = base + mod0;
    m->dynamic = mod + *(int32_t *)(mod + SF2C_LOADER_N_4_4B22);
    for (uint64_t *d = (uint64_t *)m->dynamic; d[0] != DT_NULL; d += SF2C_LOADER_N_2_D473) {
        switch (d[0]) {
        case DT_SYMTAB: m->symtab = base + d[1]; break;
        case DT_STRTAB: m->strtab = base + d[1]; break;
        case DT_HASH: m->nsyms = *(uint32_t *)(base + d[1] + SF2C_LOADER_N_4_4B22); break;
        case SF2C_LOADER_N_12_6B51: m->init = base + d[1]; break;  // DT_INIT
        case SF2C_LOADER_N_13_3FDB: m->fini = base + d[1]; break;  // DT_FINI
        case DT_INIT_ARRAY: m->init_array = base + d[1]; break;
        case DT_INIT_ARRAYSZ: m->init_array_sz = d[1]; break;
        case DT_FINI_ARRAY: m->fini_array = base + d[1]; break;
        case DT_FINI_ARRAYSZ: m->fini_array_sz = d[1]; break;
        }
    }
    LOG(SF2C_LOADER_S_LOADED_5S_AT_LLX_SIZE_LLX_SY_DC29, m->name, base, size, m->nsyms);
}

static Elf64Sym *sym(Module *m, uint64_t i) { return (Elf64Sym *)m->symtab + i; }
static const char *symname(Module *m, Elf64Sym *s) { return (const char *)(m->strtab + s->name); }

uint64_t loader_find_symbol(const char *name) {
    for (int mi = 0; mi < g_nmodules; mi++) {
        Module *m = &g_modules[mi];
        for (uint64_t i = 1; i < m->nsyms; i++) {
            Elf64Sym *s = sym(m, i);
            if (s->shndx && !strcmp(symname(m, s), name)) return m->base + s->value;
        }
    }
    return 0;
}

const char *loader_symbol_at(uint64_t addr, uint64_t *offset) {
    const char *best = NULL;
    uint64_t best_addr = 0;
    for (int mi = 0; mi < g_nmodules; mi++) {
        Module *m = &g_modules[mi];
        if (addr < m->base || addr >= m->base + m->size) continue;
        for (uint64_t i = 1; i < m->nsyms; i++) {
            Elf64Sym *s = sym(m, i);
            uint64_t a = m->base + s->value;
            if (s->shndx && (s->info & SF2C_LOADER_N_0XF_7419) == SF2C_LOADER_N_2_D473 && a <= addr && a > best_addr) {
                best = symname(m, s);
                best_addr = a;
            }
        }
    }
    if (offset) *offset = addr - best_addr;
    return best;
}

static uint64_t resolve(Module *m, uint32_t symidx) {
    Elf64Sym *s = sym(m, symidx);
    const char *name = symname(m, s);
    if (s->shndx) return m->base + s->value;
    uint64_t a = loader_find_symbol(name);
    if (a) {
        LOG(SF2C_LOADER_S_LOADER_SYMBOL_S_FOUND_IN_GUE_AAD6, name, (unsigned long long)a);
    }
    HostFn impl = hle_lookup_impl(name);
    if (impl) {
        LOG(SF2C_LOADER_S_LOADER_RESOLVE_HLE_S_N_68FD, name);
        return hle_register(name, impl);  // host implementation wins over the guest SDK's
    }
    if (a) return a;
    if ((s->info >> SF2C_LOADER_N_4_4B22) == SF2C_LOADER_N_2_D473) return 0;  // weak undefined
    LOG(SF2C_LOADER_S_LOADER_RESOLVE_UNRESOLVED_HL_0527, name);
    return hle_register(name, NULL);
}

static void relocate(Module *m) {
    uint64_t rela = 0, relasz = 0, jmprel = 0, pltsz = 0;
    for (uint64_t *d = (uint64_t *)m->dynamic; d[0] != DT_NULL; d += SF2C_LOADER_N_2_D473) {
        if (d[0] == DT_RELA) rela = m->base + d[1];
        if (d[0] == DT_RELASZ) relasz = d[1];
        if (d[0] == DT_JMPREL) jmprel = m->base + d[1];
        if (d[0] == DT_PLTRELSZ) pltsz = d[1];
    }
    int unresolved = 0;
    for (int pass = 0; pass < SF2C_LOADER_N_2_D473; pass++) {
        Elf64Rela *r = (Elf64Rela *)(pass ? jmprel : rela);
        uint64_t n = (pass ? pltsz : relasz) / sizeof(Elf64Rela);
        for (uint64_t i = 0; i < n; i++) {
            uint64_t *where = (uint64_t *)(m->base + r[i].off);
            uint32_t type = (uint32_t)r[i].info, si = r[i].info >> SF2C_LOADER_N_32_E29C;
            switch (type) {
            case R_RELATIVE: *where = m->base + r[i].addend; break;
            case R_ABS64:
            case R_GLOB_DAT:
            case R_JUMP_SLOT: {
                uint64_t s = resolve(m, si);
                *where = s ? s + r[i].addend : 0;
                if (s >= HLE_BASE && s < HLE_BASE + HLE_SIZE) unresolved++;
                break;
            }
            default: FATAL(SF2C_LOADER_S_S_RELOC_TYPE_U_N_E2DA, m->name, type);
            }
        }
    }
    LOG(SF2C_LOADER_S_RELOCATED_5S_D_IMPORTS_BOUND_928E, m->name, unresolved);
}

void loader_load(const char *exefs) {
    char path[SF2C_LOADER_N_1024_E39E];
    static const struct { const char *name; uint64_t base; } mods[] = {{SF2C_LOADER_S_MAIN_E4A9, MAIN_BASE}, {SF2C_LOADER_S_SDK_DE0B, SDK_BASE}};
    for (unsigned i = 0; i < SF2C_LOADER_N_2_D473; i++) {
        Module *m = &g_modules[g_nmodules++];
        m->name = mods[i].name;
        snprintf(path, sizeof path, SF2C_LOADER_S_S_S_22D8, exefs, m->name);
        load_nso(m, path, mods[i].base);
    }
    for (int i = 0; i < g_nmodules; i++) relocate(&g_modules[i]);
}

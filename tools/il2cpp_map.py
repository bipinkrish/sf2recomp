#!/usr/bin/env python3
"""Convert Il2CppDumper output (script.json) into a symbol map for the recompilation fork.

Il2CppDumper resolves method addresses by walking the il2cpp metadata (CodeRegistration /
MetadataRegistration found via NSO relocations).  Because `main` exports only ~700
organic dynsym symbols (nnMain, malloc/free, SDK init), the overwhelming majority of
internals are only visible in script.json.  This tool turns that into:

  game/il2cpp/sym_main.txt   "ADDR NAME" lines, sorted by address (the fork's
                             --symbol table / label source and lift order input)
  game/il2cpp/anchors.txt    CodeRegistration / MetadataRegistration / signature anchors

Reproduce the dump first (see README):
  dotnet tools/il2cpp/Il2CppDumper.dll game/extracted/update/exefs/main.uncompressed \
      game/extracted/update/romfs/Data/Managed/Metadata/global-metadata.dat game/il2cpp

Usage:
  python3 tools/il2cpp_map.py [--indir game/il2cpp] [--nso game/extracted/update/exefs/main.uncompressed]
"""
from _sf2_tool_constants import IL2CPP_MAP as K
import argparse
import json
import os
import struct


def load_nso_layout(path):
    """Parse the NSO0 header of the .uncompressed image to report segment layout."""
    raw = open(path, K.S_RB_81FE).read(K.N_128_2747)
    assert raw[:K.N_4_4B22] == K.B_B_NSO0_4F90, path
    segs = []
    for i in range(K.N_3_4E07):
        file_off, mem_off, dec_size = struct.unpack_from(K.S_III_0506, raw, K.N_16_B17E + i * K.N_16_B17E)
        segs.append((mem_off, dec_size, file_off))
    return {
        K.S_TEXT_2691:  {K.S_VADDR_FDBF: segs[0][0], K.S_SIZE_B488: segs[0][1]},
        K.S_RO_29AC:    {K.S_VADDR_FDBF: segs[1][0], K.S_SIZE_B488: segs[1][1]},
        K.S_DATA_7553:  {K.S_VADDR_FDBF: segs[K.N_2_D473][0], K.S_SIZE_B488: segs[K.N_2_D473][1]},
        K.S_BSS_SIZE_73EA: struct.unpack_from(K.S_I_833B, raw, K.N_60_39FA)[0],
    }


def write_sorted_syms(path, syms):
    seen = {}
    for addr, name in syms:
        # Il2CppDumper can emit several names for one address (generic sharing).
        # Keep the first and remember extras in comments.
        if addr not in seen:
            seen[addr] = name
        else:
            seen[addr] += f' ; alias:{name}'
    with open(path, K.S_W_780F, encoding=K.S_UTF_8_5383, newline=K.S_N_AD65) as f:
        for addr in sorted(seen):
            f.write(f'0x{addr:08X} {seen[addr]}\n')
    return len(seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(K.S_INDIR_B906, default=K.S_GAME_IL2CPP_940A)
    ap.add_argument(K.S_NSO_DD57, default=K.S_GAME_EXTRACTED_UPDATE_EXEFS__B205)
    ap.add_argument(K.S_CODEREG_99C7, type=lambda x: int(x, 0), default=K.N_3924202_93F9)
    ap.add_argument(K.S_METAREG_19FA, type=lambda x: int(x, 0), default=K.N_3924256_EF0C)
    args = ap.parse_args()

    script = os.path.join(args.indir, K.S_SCRIPT_JSON_6126)
    with open(script, encoding=K.S_UTF_8_SIG_EACA) as stream:
        raw = json.load(stream)
    methods = raw[K.S_SCRIPTMETHOD_986B]

    layout = load_nso_layout(args.nso) if os.path.exists(args.nso) else None

    sym_path = os.path.join(args.indir, K.S_SYM_MAIN_TXT_5D45)
    syms = [(m[K.S_ADDRESS_D3D1], m[K.S_NAME_1BF1]) for m in methods]
    n = write_sorted_syms(sym_path, syms)

    anchors_path = os.path.join(args.indir, K.S_ANCHORS_TXT_6205)

    # Anchors are only printed by the dumper console, so they are passed in
    # explicitly; the defaults below are the values Il2CppDumper 6.7.46 found
    # for this build (update main, metadata v24).
    with open(anchors_path, K.S_W_780F, encoding=K.S_UTF_8_5383, newline=K.S_N_AD65) as f:
        if layout:
            f.write(K.S_NSO0_LAYOUT_OF_MAIN_MEM_OFF__5F2E)
            for name in (K.S_TEXT_2691, K.S_RO_29AC, K.S_DATA_7553):
                s = layout[name]
                f.write(f'# {name:4s} vaddr=0x{s[K.S_VADDR_FDBF]:X} size=0x{s[K.S_SIZE_B488]:X}\n')
            f.write(f'# bss        size=0x{layout[K.S_BSS_SIZE_73EA]:X}\n')
        f.write(K.S_CODEREGISTRATION_0X_X_N_DBDC % args.codereg)
        f.write(K.S_METADATAREGISTRATION_0X_X_N_D209 % args.metareg)

    print(f'symbols written : {sym_path} ({n} unique addresses from {len(methods)} methods)')
    print(f'anchors written : {anchors_path}')
    if layout:
        print(f'image: text 0x{layout[K.S_TEXT_2691][K.S_VADDR_FDBF]:X}+0x{layout[K.S_TEXT_2691][K.S_SIZE_B488]:X}, '
              f'ro 0x{layout[K.S_RO_29AC][K.S_VADDR_FDBF]:X}+0x{layout[K.S_RO_29AC][K.S_SIZE_B488]:X}, '
              f'data 0x{layout[K.S_DATA_7553][K.S_VADDR_FDBF]:X}+0x{layout[K.S_DATA_7553][K.S_SIZE_B488]:X}, '
              f'bss 0x{layout[K.S_BSS_SIZE_73EA]:X}')


if __name__ == K.S_MAIN_ACB5:
    main()

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
import argparse
import json
import os
import struct


def load_nso_layout(path):
    """Parse the NSO0 header of the .uncompressed image to report segment layout."""
    raw = open(path, 'rb').read(0x80)
    assert raw[:4] == b'NSO0', path
    segs = []
    for i in range(3):
        file_off, mem_off, dec_size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
        segs.append((mem_off, dec_size, file_off))
    return {
        'text':  {'vaddr': segs[0][0], 'size': segs[0][1]},
        'ro':    {'vaddr': segs[1][0], 'size': segs[1][1]},
        'data':  {'vaddr': segs[2][0], 'size': segs[2][1]},
        'bss_size': struct.unpack_from('<I', raw, 0x3C)[0],
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
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        for addr in sorted(seen):
            f.write(f'0x{addr:08X} {seen[addr]}\n')
    return len(seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--indir', default='game/il2cpp')
    ap.add_argument('--nso', default='game/extracted/update/exefs/main.uncompressed')
    ap.add_argument('--codereg', type=lambda x: int(x, 0), default=0x3BE0EA)
    ap.add_argument('--metareg', type=lambda x: int(x, 0), default=0x3BE120)
    args = ap.parse_args()

    script = os.path.join(args.indir, 'script.json')
    with open(script, encoding='utf-8-sig') as stream:
        raw = json.load(stream)
    methods = raw['ScriptMethod']

    layout = load_nso_layout(args.nso) if os.path.exists(args.nso) else None

    sym_path = os.path.join(args.indir, 'sym_main.txt')
    syms = [(m['Address'], m['Name']) for m in methods]
    n = write_sorted_syms(sym_path, syms)

    anchors_path = os.path.join(args.indir, 'anchors.txt')

    # Anchors are only printed by the dumper console, so they are passed in
    # explicitly; the defaults below are the values Il2CppDumper 6.7.46 found
    # for this build (update main, metadata v24).
    with open(anchors_path, 'w', encoding='utf-8', newline='\n') as f:
        if layout:
            f.write('# NSO0 layout of main (mem_off / size)\n')
            for name in ('text', 'ro', 'data'):
                s = layout[name]
                f.write(f'# {name:4s} vaddr=0x{s["vaddr"]:X} size=0x{s["size"]:X}\n')
            f.write(f'# bss        size=0x{layout["bss_size"]:X}\n')
        f.write('CodeRegistration 0x%X\n' % args.codereg)
        f.write('MetadataRegistration 0x%X\n' % args.metareg)

    print(f'symbols written : {sym_path} ({n} unique addresses from {len(methods)} methods)')
    print(f'anchors written : {anchors_path}')
    if layout:
        print(f'image: text 0x{layout["text"]["vaddr"]:X}+0x{layout["text"]["size"]:X}, '
              f'ro 0x{layout["ro"]["vaddr"]:X}+0x{layout["ro"]["size"]:X}, '
              f'data 0x{layout["data"]["vaddr"]:X}+0x{layout["data"]["size"]:X}, '
              f'bss 0x{layout["bss_size"]:X}')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""NCA header decryption + structural parsing, mirroring hactool's nca.c structs.

Every convention here was verified against this title's NCAs (base `b5028f4e…`,
patch `9e558c36…`) and cross-checked with hactool/nstool/LibHac output:

* Header AES-128-XTS: 0x200-byte units, tweak = big-endian unit index,
  GF(2^128) advance in the little-endian integer domain (0x87 reduction),
  raw key order k1 = header_key[0:16], k2 = header_key[16:32].
* Header byte map: magic "NCA3" @0x200, distribution/content_type/crypto_type
  @0x201..3, kaek_ind @0x204, nca_size @0x208, title_id @0x210, sdk_version
  @0x21C, crypto_type2 @0x220, fixed_key_generation @0x221, rights_id @0x230,
  section entries @0x240 (u32 media_start, u32 media_end, 8 pad), section
  header hashes @0x280, encrypted key area @0x300, fs headers @0x400.
* fs header: @0x2 partition_type (0 = RomFs, 1 = PartitionFs), @0x3 hash_type
  (3 = HierarchicalIntegrity, 2 = HierarchicalSha256), @0x4 crypt_type
  (3 = AES-CTR plain, 4 = AES-CTR-EX/BKTR, 1 = none), @0x6 compression_type;
  section counter @0x140; fs-specific superblock union @0x8.
* pfs0 (HierSha256) superblock: master_hash @fs+0x8, block_size @0x28,
  hash_table_offset/size @0x30/0x38, pfs0_offset @0x40, pfs0_size @0x48.
  The PFS0 partition header sits at section_offset + pfs0_offset.
* romfs (HI/BKTR) superblock: "IVFC" @fs+0x8, id @0xC (0x20000 for RomFs),
  master_hash_size @0x10 (=0x20), num_levels @0x14 (=7; level[6] is zeroed —
  five hash levels at lv0..lv4 then the data level at lv5), level headers at
  fs+0x18 + i*0x18 ({u64 logical_offset, u64 hash_data_size, u32 block_size,
  u32 reserved}; block size stored as exponent, 0xe = 0x4000), master_hash at
  fs+0xC8. For BKTR romfs the data level's size *is* the whole rewritten
  romfs partition (base 0x5621B0A8 @ logical 0x2C4000; patch 0x56D85738
  @ logical 0x2C8000). BKTR sections add two table headers: relocation at
  fs+0x100 and subsection at fs+0x120, each {u64 offset, u64 size, "BKTR",
  u32 version, u32 num_entries, u32 pad}; the table blocks themselves are
  0x8000 bytes at those section-relative offsets (patch: reloc @0x5AB4600
  with 102 entries, subsection @0x5ABC600 with 1 entry).
* Section AES-CTR: key = plaintext titlekey, counter = rev(section_ctr)
  || BE64(absolute_position >> 4) — hactool seeds IV[j] = section_ctr[7-j].
"""
import struct

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

FS_HDR = 0x400
FS_HDR_SIZE = 0x200


def _xts_units(key32, data, tweak0=0, unit=0x200):
    """Switch XTS: BE sector tweak, LE GF multiply (verified empirically)."""
    k1, k2 = key32[:16], key32[16:]
    out = bytearray()
    for u in range(0, len(data), unit):
        T = bytearray(
            Cipher(algorithms.AES(k2), modes.ECB()).encryptor().update(
                (tweak0 + u // unit).to_bytes(16, "big")))
        ecb = Cipher(algorithms.AES(k1), modes.ECB()).decryptor()
        for j in range(0, len(data[u:u + unit]), 16):
            t = bytes(T)
            x = bytes(a ^ b for a, b in zip(data[u + j:u + j + 16], t))
            out += bytes(a ^ b for a, b in zip(ecb.update(x), t))
            i = int.from_bytes(T, "little")
            i = ((i << 1) & ((1 << 128) - 1)) ^ (0x87 if (i >> 127) else 0)
            T[:] = i.to_bytes(16, "little")
    return bytes(out)


def decrypt_nca_header(path, header_key):
    """Decrypt the 0xC00 NCA3 header; verifies the magic, else raises."""
    with open(path, "rb") as f:
        blob = f.read(0xC00)
    for keys in (header_key, header_key[16:] + header_key[:16]):
        head = _xts_units(keys, blob[:0x400])
        if head[0x200:0x204] == b"NCA3":
            return head + _xts_units(keys, blob[0x400:0xC00], tweak0=2)
    raise ValueError("failed to decrypt NCA header of %s (magic not NCA3)" % path)


def ctr_iv(section_ctr, abs_pos):
    """Counter for section AES-CTR at absolute NCA position `abs_pos`."""
    return bytes(section_ctr[::-1]) + (abs_pos >> 4).to_bytes(8, "big")


def ctr_read(path, key, section_ctr, abs_start, length):
    """Decrypt `length` bytes read at absolute NCA offset `abs_start`."""
    with open(path, "rb") as f:
        f.seek(abs_start)
        blob = f.read(length)
    c = Cipher(algorithms.AES(key), modes.CTR(ctr_iv(section_ctr, abs_start))).decryptor()
    return c.update(blob) + c.finalize()


def fs_header(hdr, index):
    off = FS_HDR + index * FS_HDR_SIZE
    fs = hdr[off:off + FS_HDR_SIZE]
    return {
        "index": index,
        "partition_type": fs[2],   # 0 RomFs, 1 PartitionFs
        "hash_type": fs[3],        # 3 = HI, 2 = HierSha256
        "crypt_type": fs[4],       # 3 = CTR, 4 = BKTR, 1 = none
        "compression_type": fs[6],
        "section_ctr": fs[0x140:0x148],
        "offset": struct.unpack_from("<II", hdr, 0x240 + index * 0x10)[0] << 9,
        "size": (lambda m, e: (e - m) << 9)(*struct.unpack_from("<II", hdr, 0x240 + index * 0x10)),
        "bytes": fs,
    }


def parse_sections(hdr):
    "All non-empty sections, as dicts (offset in bytes)."
    secs = []
    for i in range(4):
        m, e = struct.unpack_from("<II", hdr, 0x240 + i * 0x10)
        if m != 0 or e != 0:
            secs.append(fs_header(hdr, i))
    return secs


def parse_ivfc_levels(fs):
    """Decoded ivfc header of an HI romfs fs header (see module docstring)."""
    if fs[8:12] != b"IVFC":
        return None
    mid, = struct.unpack_from("<I", fs, 0xC)
    mhs, nlv = struct.unpack_from("<II", fs, 0x10)
    levels = []
    for i in range(nlv):
        lo, hds, bs, rsv = struct.unpack_from("<QQII", fs, 0x18 + i * 0x18)
        levels.append({"logical_offset": lo, "hash_data_size": hds,
                       "block_size_exp": bs})
    return {"id": mid, "master_hash_size": mhs, "num_levels": nlv,
            "levels": levels, "master_hash": fs[0xC8:0xE8].hex()}


def parse_pfs0_superblock(fs):
    "Data offset/size of the PFS0 partition inside a HierSha256 section."
    return {
        "master_hash": fs[0x8:0x28].hex(),
        "block_size": struct.unpack_from("<I", fs, 0x28)[0],
        "hash_table_offset": struct.unpack_from("<Q", fs, 0x30)[0],
        "hash_table_size": struct.unpack_from("<Q", fs, 0x38)[0],
        "pfs0_offset": struct.unpack_from("<Q", fs, 0x40)[0],
        "pfs0_size": struct.unpack_from("<Q", fs, 0x48)[0],
    }


def parse_bktr_headers(fs):
    """The two BKTR table headers ({offset, size, num_entries}) of fs header."""
    out = []
    for base in (0x100, 0x120):
        if fs[base + 0x10:base + 0x14] != b"BKTR":
            continue
        out.append({
            "offset": struct.unpack_from("<Q", fs, base)[0],
            "size": struct.unpack_from("<Q", fs, base + 8)[0],
            "num_entries": struct.unpack_from("<I", fs, base + 0x18)[0],
        })
    return out


def rights_id(hdr):
    return hdr[0x230:0x240].hex().upper()


if __name__ == "__main__":
    import sys
    nca_path, keys_path = sys.argv[1], sys.argv[2]
    hk = None
    for line in open(keys_path):
        n, _, v = line.partition("=")
        if n.strip() == "header_key":
            hk = bytes.fromhex("".join(v.split()))
    hdr = decrypt_nca_header(nca_path, hk)
    print("magic:", hdr[0x200:0x204].decode(), "size:",
          hex(struct.unpack_from("<Q", hdr, 0x208)[0]),
          "titleid:", hex(struct.unpack_from("<Q", hdr, 0x210)[0]),
          "keygen:", hdr[0x220], "rid:", rights_id(hdr))
    for s in parse_sections(hdr):
        print("section %d: off=%#012x size=%#x partition=%d hash=%d crypt=%d "
              "ctr=%s" % (s["index"], s["offset"], s["size"], s["partition_type"],
                          s["hash_type"], s["crypt_type"], s["section_ctr"].hex()))
        if s["hash_type"] == 3:
            iv = parse_ivfc_levels(s["bytes"])
            print("  ivfc id=%#x levels=%s master_hash=%s" %
                  (iv["id"], [(hex(l["logical_offset"]), hex(l["hash_data_size"]))
                              for l in iv["levels"]], iv["master_hash"]))
            for bk in parse_bktr_headers(s["bytes"]):
                print("  bktr table: off=%#x size=%#x num_entries=%d" %
                      (bk["offset"], bk["size"], bk["num_entries"]))
        elif s["hash_type"] == 2:
            p = parse_pfs0_superblock(s["bytes"])
            print("  pfs0: data at +%#x size=%#x (hash table +%#x size %#x)" %
                  (p["pfs0_offset"], p["pfs0_size"], p["hash_table_offset"],
                   p["hash_table_size"]))

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
from _sf2_tool_constants import NCA_HEADER as K
import struct

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

FS_HDR = K.FS_HDR
FS_HDR_SIZE = K.FS_HDR_SIZE


def _xts_units(key32, data, tweak0=0, unit=K.N_512_94F8):
    """Switch XTS: BE sector tweak, LE GF multiply (verified empirically)."""
    k1, k2 = key32[:K.N_16_B17E], key32[K.N_16_B17E:]
    out = bytearray()
    for u in range(0, len(data), unit):
        T = bytearray(
            Cipher(algorithms.AES(k2), modes.ECB()).encryptor().update(
                (tweak0 + u // unit).to_bytes(K.N_16_B17E, K.S_BIG_0D48)))
        ecb = Cipher(algorithms.AES(k1), modes.ECB()).decryptor()
        for j in range(0, len(data[u:u + unit]), K.N_16_B17E):
            t = bytes(T)
            x = bytes(a ^ b for a, b in zip(data[u + j:u + j + K.N_16_B17E], t))
            out += bytes(a ^ b for a, b in zip(ecb.update(x), t))
            i = int.from_bytes(T, K.S_LITTLE_7BA5)
            i = ((i << 1) & ((1 << K.N_128_2747) - 1)) ^ (K.N_135_1367 if (i >> K.N_127_922C) else 0)
            T[:] = i.to_bytes(K.N_16_B17E, K.S_LITTLE_7BA5)
    return bytes(out)


def decrypt_nca_header(path, header_key):
    """Decrypt the 0xC00 NCA3 header; verifies the magic, else raises."""
    with open(path, K.S_RB_81FE) as f:
        blob = f.read(K.N_3072_8214)
    for keys in (header_key, header_key[K.N_16_B17E:] + header_key[:K.N_16_B17E]):
        head = _xts_units(keys, blob[:K.N_1024_E39E])
        if head[K.N_512_94F8:K.N_516_4771] == K.B_B_NCA3_40F0:
            return head + _xts_units(keys, blob[K.N_1024_E39E:K.N_3072_8214], tweak0=K.N_2_D473)
    raise ValueError(K.S_FAILED_TO_DECRYPT_NCA_HEADER_2088 % path)


def ctr_iv(section_ctr, abs_pos):
    """Counter for section AES-CTR at absolute NCA position `abs_pos`."""
    return bytes(section_ctr[::-1]) + (abs_pos >> K.N_4_4B22).to_bytes(K.N_8_2C62, K.S_BIG_0D48)


def ctr_read(path, key, section_ctr, abs_start, length):
    """Decrypt `length` bytes read at absolute NCA offset `abs_start`."""
    with open(path, K.S_RB_81FE) as f:
        f.seek(abs_start)
        blob = f.read(length)
    c = Cipher(algorithms.AES(key), modes.CTR(ctr_iv(section_ctr, abs_start))).decryptor()
    return c.update(blob) + c.finalize()


def fs_header(hdr, index):
    off = FS_HDR + index * FS_HDR_SIZE
    fs = hdr[off:off + FS_HDR_SIZE]
    return {
        K.S_INDEX_2314: index,
        K.S_PARTITION_TYPE_375F: fs[K.N_2_D473],   # 0 RomFs, 1 PartitionFs
        K.S_HASH_TYPE_0CF9: fs[K.N_3_4E07],        # 3 = HI, 2 = HierSha256
        K.S_CRYPT_TYPE_2A1E: fs[K.N_4_4B22],       # 3 = CTR, 4 = BKTR, 1 = none
        K.S_COMPRESSION_TYPE_B69B: fs[K.N_6_E7F6],
        K.S_SECTION_CTR_0F19: fs[K.N_320_8882:K.N_328_2452],
        K.S_OFFSET_9569: struct.unpack_from(K.S_II_AEAE, hdr, K.N_576_F345 + index * K.N_16_B17E)[0] << K.N_9_1958,
        K.S_SIZE_B488: (lambda m, e: (e - m) << K.N_9_1958)(*struct.unpack_from(K.S_II_AEAE, hdr, K.N_576_F345 + index * K.N_16_B17E)),
        K.S_BYTES_A2C9: fs,
    }


def parse_sections(hdr):
    "All non-empty sections, as dicts (offset in bytes)."
    secs = []
    for i in range(K.N_4_4B22):
        m, e = struct.unpack_from(K.S_II_AEAE, hdr, K.N_576_F345 + i * K.N_16_B17E)
        if m != 0 or e != 0:
            secs.append(fs_header(hdr, i))
    return secs


def parse_ivfc_levels(fs):
    """Decoded ivfc header of an HI romfs fs header (see module docstring)."""
    if fs[K.N_8_2C62:K.N_12_6B51] != K.B_B_IVFC_1A1B:
        return None
    mid, = struct.unpack_from(K.S_I_833B, fs, K.N_12_6B51)
    mhs, nlv = struct.unpack_from(K.S_II_AEAE, fs, K.N_16_B17E)
    levels = []
    for i in range(nlv):
        lo, hds, bs, rsv = struct.unpack_from(K.S_QQII_8879, fs, K.N_24_C235 + i * K.N_24_C235)
        levels.append({K.S_LOGICAL_OFFSET_89E8: lo, K.S_HASH_DATA_SIZE_E054: hds,
                       K.S_BLOCK_SIZE_EXP_B176: bs})
    return {K.S_ID_657E: mid, K.S_MASTER_HASH_SIZE_22EB: mhs, K.S_NUM_LEVELS_68DD: nlv,
            K.S_LEVELS_8ECE: levels, K.S_MASTER_HASH_64CE: fs[K.N_200_27BA:K.N_232_835D].hex()}


def parse_pfs0_superblock(fs):
    "Data offset/size of the PFS0 partition inside a HierSha256 section."
    return {
        K.S_MASTER_HASH_64CE: fs[K.N_8_2C62:K.N_40_D59E].hex(),
        K.S_BLOCK_SIZE_B3F7: struct.unpack_from(K.S_I_833B, fs, K.N_40_D59E)[0],
        K.S_HASH_TABLE_OFFSET_04C1: struct.unpack_from(K.S_Q_C5AE, fs, K.N_48_9801)[0],
        K.S_HASH_TABLE_SIZE_AE3C: struct.unpack_from(K.S_Q_C5AE, fs, K.N_56_7688)[0],
        K.S_PFS0_OFFSET_A5C6: struct.unpack_from(K.S_Q_C5AE, fs, K.N_64_A68B)[0],
        K.S_PFS0_SIZE_86E4: struct.unpack_from(K.S_Q_C5AE, fs, K.N_72_8722)[0],
    }


def parse_bktr_headers(fs):
    """The two BKTR table headers ({offset, size, num_entries}) of fs header."""
    out = []
    for base in (K.N_256_51E8, K.N_288_23C6):
        if fs[base + K.N_16_B17E:base + K.N_20_F5CA] != K.B_B_BKTR_C83C:
            continue
        out.append({
            K.S_OFFSET_9569: struct.unpack_from(K.S_Q_C5AE, fs, base)[0],
            K.S_SIZE_B488: struct.unpack_from(K.S_Q_C5AE, fs, base + K.N_8_2C62)[0],
            K.S_NUM_ENTRIES_6CFD: struct.unpack_from(K.S_I_833B, fs, base + K.N_24_C235)[0],
        })
    return out


def rights_id(hdr):
    return hdr[K.N_560_6BCA:K.N_576_F345].hex().upper()


if __name__ == K.S_MAIN_ACB5:
    import sys
    nca_path, keys_path = sys.argv[1], sys.argv[K.N_2_D473]
    hk = None
    for line in open(keys_path):
        n, _, v = line.partition(K.S_EMPTY_C874)
        if n.strip() == K.S_HEADER_KEY_A4EF:
            hk = bytes.fromhex(K.S_EMPTY_6F49.join(v.split()))
    hdr = decrypt_nca_header(nca_path, hk)
    print(K.S_MAGIC_360E, hdr[K.N_512_94F8:K.N_516_4771].decode(), K.S_SIZE_7FE1,
          hex(struct.unpack_from(K.S_Q_C5AE, hdr, K.N_520_0B35)[0]),
          K.S_TITLEID_0D40, hex(struct.unpack_from(K.S_Q_C5AE, hdr, K.N_528_BD3A)[0]),
          K.S_KEYGEN_15EB, hdr[K.N_544_D359], K.S_RID_D14B, rights_id(hdr))
    for s in parse_sections(hdr):
        print(K.S_SECTION_D_OFF_012X_SIZE_X_PA_46C2 % (s[K.S_INDEX_2314], s[K.S_OFFSET_9569], s[K.S_SIZE_B488], s[K.S_PARTITION_TYPE_375F],
                          s[K.S_HASH_TYPE_0CF9], s[K.S_CRYPT_TYPE_2A1E], s[K.S_SECTION_CTR_0F19].hex()))
        if s[K.S_HASH_TYPE_0CF9] == K.N_3_4E07:
            iv = parse_ivfc_levels(s[K.S_BYTES_A2C9])
            print(K.S_IVFC_ID_X_LEVELS_S_MASTER_HA_623F %
                  (iv[K.S_ID_657E], [(hex(l[K.S_LOGICAL_OFFSET_89E8]), hex(l[K.S_HASH_DATA_SIZE_E054]))
                              for l in iv[K.S_LEVELS_8ECE]], iv[K.S_MASTER_HASH_64CE]))
            for bk in parse_bktr_headers(s[K.S_BYTES_A2C9]):
                print(K.S_BKTR_TABLE_OFF_X_SIZE_X_NUM__9B5D %
                      (bk[K.S_OFFSET_9569], bk[K.S_SIZE_B488], bk[K.S_NUM_ENTRIES_6CFD]))
        elif s[K.S_HASH_TYPE_0CF9] == K.N_2_D473:
            p = parse_pfs0_superblock(s[K.S_BYTES_A2C9])
            print(K.S_PFS0_DATA_AT_X_SIZE_X_HASH_T_EA45 %
                  (p[K.S_PFS0_OFFSET_A5C6], p[K.S_PFS0_SIZE_86E4], p[K.S_HASH_TABLE_OFFSET_04C1],
                   p[K.S_HASH_TABLE_SIZE_AE3C]))

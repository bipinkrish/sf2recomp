#!/usr/bin/env python3
"""Derive title.keys from the embedded .tik tickets of the NSP.

Convention (critical): title.keys entries are the ENCRYPTED titlekeys — exactly
the 16 bytes at offset 0x180 of the ticket body. Every consumer (hactool
--titlekey, nstool --titlekey, LibHac ExternalKeyReader) decrypts them itself
with the appropriate titlekek. Writing the decrypted (plaintext) form instead
silently double-decrypts and yields garbage sections.

A ticket is paired with its NCA by rights id: the NCA header carries its rights
id at 0x230 (see nca_header.py), identical to the ticket file name — note an
update NCA carries the update rights id but the *base* title id at 0x210, so
rights ids (not title ids) are the reliable join key. The
titlekek revision is picked by a self-contained oracle: with each candidate
plaintext titlekey, the PFS0 partition header inside the paired NCA's exefs
(behind the HierSha256 hash table) must decrypt to the "PFS0" magic under the
switch CTR convention — a wrong key cannot produce that.
"""
from _sf2_tool_constants import EXTRACT_TITLEKEYS as K
import argparse
import glob
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nca_header import decrypt_nca_header, parse_sections



def load_keys(path):
    keys = {}
    with open(path) as f:
        for line in f:
            if K.S_EMPTY_C874 in line:
                k, v = line.split(K.S_EMPTY_C874, 1)
                value = v.split(K.S_EMPTY_4734, 1)[0].split(K.S_EMPTY_CF57, 1)[0].strip()
                if value:
                    keys[k.strip().lower()] = value.split()[0]
    return keys


def ecb_decrypt(key, block):
    c = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return c.update(block) + c.finalize()


def find_dec_titlekey(enc_tk, keys, nca_path, hdr):
    """Try every titlekek revision; exactly one must pass the PFS0 oracle.

    Section AES-CTR counter = rev(section_ctr) || BE64(absolute_position >> 4)
    (hactool seeds IV[j] = section_ctr[7-j]). The exefs is a HierarchicalSha256
    section, so the PFS0 partition header sits behind the hash table, at
    section_offset + pfs0_offset (0x8000, stored in the fs-header superblock).
    """
    sec = next(s for s in parse_sections(hdr) if s[K.S_PARTITION_TYPE_375F] == 1 and s[K.S_HASH_TYPE_0CF9] == K.N_2_D473)  # exefs
    fs = hdr[K.N_1024_E39E + sec[K.S_INDEX_2314] * K.N_512_94F8: K.N_1024_E39E + (sec[K.S_INDEX_2314] + 1) * K.N_512_94F8]
    pfs0_off = struct.unpack_from(K.S_Q_C5AE, fs, K.N_64_A68B)[0]
    pos = sec[K.S_OFFSET_9569] + pfs0_off
    with open(nca_path, K.S_RB_81FE) as f:
        f.seek(pos)
        blob = f.read(K.N_16_B17E)
    iv = bytes(sec[K.S_SECTION_CTR_0F19][::-1]) + (pos >> K.N_4_4B22).to_bytes(K.N_8_2C62, K.S_BIG_0D48)
    hits = []
    for rev in range(K.N_32_E29C):
        name = K.S_TITLEKEK_02X_C422 % rev
        if name not in keys:
            continue
        plain = ecb_decrypt(bytes.fromhex(keys[name]), enc_tk)
        c = Cipher(algorithms.AES(plain), modes.CTR(iv)).decryptor()
        if (c.update(blob) + c.finalize())[:K.N_4_4B22] == K.B_B_PFS0_D3B7:
            hits.append((rev, plain))
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_KEYS_B749, required=True, help=K.S_PATH_TO_YOUR_LOCAL_PROD_KEYS_A2FA)
    parser.add_argument(K.S_NSP_DIR_685F, required=True, help=K.S_DIRECTORY_CONTAINING_EXTRACT_3DDF)
    args = parser.parse_args()
    nsp_dir = os.path.abspath(args.nsp_dir)
    keys = load_keys(args.keys)
    hk = bytes.fromhex(keys[K.S_HEADER_KEY_A4EF])

    # rights id (hex, exactly as in the .tik file names) -> (nca, header)
    by_rid = {}
    for nca in sorted(glob.glob(os.path.join(nsp_dir, K.S_NCA_6C1D)),
                      key=os.path.getsize, reverse=True):
        hdr = decrypt_nca_header(nca, hk)
        by_rid.setdefault(hdr[K.N_560_6BCA:K.N_576_F345].hex().upper(), (nca, hdr))

    results = []
    for tik in sorted(glob.glob(os.path.join(nsp_dir, K.S_TIK_C76D))):
        rid = os.path.basename(tik)[:K.N_32_E29C].upper()
        with open(tik, K.S_RB_81FE) as ticket:
            data = ticket.read()
        enc_tk = data[K.N_384_37B7:K.N_400_26D2]
        entry = by_rid.get(rid)
        if not entry:
            print(K.S_WARN_S_NO_NCA_CARRIES_THIS_R_CE72 % rid)
            continue
        nca, hdr = entry
        hits = find_dec_titlekey(enc_tk, keys, nca, hdr)
        if len(hits) != 1:
            print(K.S_FAIL_S_EXPECTED_EXACTLY_ONE__72FA %
                  (rid, len(hits)))
            sys.exit(1)
        rev, _ = hits[0]
        print(K.S_OK_S_TITLEKEK_02X_VERIFIED_K_7759 % (rid, rev))
        results.append((rid, enc_tk.hex().upper()))

    out_path = os.path.join(nsp_dir, K.S_TITLE_KEYS_5210)
    fd = os.open(out_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, K.N_384_37B7)
    with os.fdopen(fd, K.S_W_780F) as f:
        for rid, enc in results:
            f.write(K.S_S_S_N_5E3B % (rid, enc))
    os.chmod(out_path, K.N_384_37B7)
    print(K.S_WROTE_2BB8, os.path.abspath(out_path), K.S_ENCRYPTED_TITLEKEYS_635D)


if __name__ == K.S_MAIN_ACB5:
    main()

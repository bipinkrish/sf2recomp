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
            if "=" in line:
                k, v = line.split("=", 1)
                value = v.split("#", 1)[0].split(";", 1)[0].strip()
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
    sec = next(s for s in parse_sections(hdr) if s["partition_type"] == 1 and s["hash_type"] == 2)  # exefs
    fs = hdr[0x400 + sec["index"] * 0x200: 0x400 + (sec["index"] + 1) * 0x200]
    pfs0_off = struct.unpack_from("<Q", fs, 0x40)[0]
    pos = sec["offset"] + pfs0_off
    with open(nca_path, "rb") as f:
        f.seek(pos)
        blob = f.read(16)
    iv = bytes(sec["section_ctr"][::-1]) + (pos >> 4).to_bytes(8, "big")
    hits = []
    for rev in range(0x20):
        name = "titlekek_%02x" % rev
        if name not in keys:
            continue
        plain = ecb_decrypt(bytes.fromhex(keys[name]), enc_tk)
        c = Cipher(algorithms.AES(plain), modes.CTR(iv)).decryptor()
        if (c.update(blob) + c.finalize())[:4] == b"PFS0":
            hits.append((rev, plain))
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keys", required=True, help="path to your local prod.keys")
    parser.add_argument("--nsp-dir", required=True, help="directory containing extracted NCAs and tickets")
    args = parser.parse_args()
    nsp_dir = os.path.abspath(args.nsp_dir)
    keys = load_keys(args.keys)
    hk = bytes.fromhex(keys["header_key"])

    # rights id (hex, exactly as in the .tik file names) -> (nca, header)
    by_rid = {}
    for nca in sorted(glob.glob(os.path.join(nsp_dir, "*.nca")),
                      key=os.path.getsize, reverse=True):
        hdr = decrypt_nca_header(nca, hk)
        by_rid.setdefault(hdr[0x230:0x240].hex().upper(), (nca, hdr))

    results = []
    for tik in sorted(glob.glob(os.path.join(nsp_dir, "*.tik"))):
        rid = os.path.basename(tik)[:32].upper()
        with open(tik, "rb") as ticket:
            data = ticket.read()
        enc_tk = data[0x180:0x190]
        entry = by_rid.get(rid)
        if not entry:
            print("WARN %s: no NCA carries this rights id in its header" % rid)
            continue
        nca, hdr = entry
        hits = find_dec_titlekey(enc_tk, keys, nca, hdr)
        if len(hits) != 1:
            print("FAIL %s: expected exactly one titlekek pass; got %d" %
                  (rid, len(hits)))
            sys.exit(1)
        rev, _ = hits[0]
        print("OK   %s: titlekek_%02x verified (key withheld)" % (rid, rev))
        results.append((rid, enc_tk.hex().upper()))

    out_path = os.path.join(nsp_dir, "title.keys")
    fd = os.open(out_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        for rid, enc in results:
            f.write("%s = %s\n" % (rid, enc))
    os.chmod(out_path, 0o600)
    print("wrote", os.path.abspath(out_path), "(encrypted titlekeys)")


if __name__ == "__main__":
    main()

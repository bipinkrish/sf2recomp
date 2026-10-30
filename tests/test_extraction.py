"""Input preparation rejects unsupported packages without damaging local outputs."""
import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import extract
import extract_titlekeys


def bundle(path, *, update=131072, xml=True, name_override=None):
    files = {}
    for kind, title in extract.TITLE_IDS.items():
        content = ("a" if kind == "Application" else "b") * 32
        files[content + ".nca"] = b"synthetic program fixture"
        files[f"{title:016x}" + "0" * 16 + ".tik"] = b"synthetic ticket fixture"
        if xml:
            files[kind + ".cnmt.xml"] = (f"<ContentMeta><Type>{kind}</Type><Id>0x{title:016x}</Id>"
                f"<Version>{0 if kind == 'Application' else update}</Version>"
                f"<Content><Type>Program</Type><Id>{content}</Id></Content></ContentMeta>").encode()
    if name_override:
        files[name_override] = b"unsafe fixture"
    strings, table, payload = b"", b"", b""
    for name, data in files.items():
        table += struct.pack("<QQII", len(payload), len(data), len(strings), 0)
        strings += name.encode() + b"\0"
        payload += data
    path.write_bytes(struct.pack("<4sIII", b"PFS0", len(files), len(strings), 0) + table + strings + payload)


class ExtractionTests(unittest.TestCase):
    def test_supported_bundle_and_clear_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.nsp"
            bundle(path)
            chosen = extract.select_programs(path, extract.pfs0_entries(path))
            self.assertEqual(set(chosen), {"Application", "Patch"})
            bundle(path, xml=False)
            with self.assertRaisesRegex(ValueError, "combined SF2"):
                extract.select_programs(path, extract.pfs0_entries(path))
            bundle(path, update=65536)
            with self.assertRaisesRegex(ValueError, "requires v131072"):
                extract.select_programs(path, extract.pfs0_entries(path))
            bundle(path, name_override="../outside.nca")
            with self.assertRaisesRegex(ValueError, "unsafe"):
                extract.pfs0_entries(path)
            bundle(path)
            path.write_bytes(path.read_bytes()[:-8])
            with self.assertRaisesRegex(ValueError, "out-of-range"):
                extract.pfs0_entries(path)

    def test_invalid_input_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "keys").write_text("synthetic fixture")
            (root / "bad.nsp").write_bytes(b"invalid")
            previous = root / "game/extracted/update/previous"
            previous.parent.mkdir(parents=True)
            previous.write_text("previous working extraction")
            with contextlib.redirect_stderr(io.StringIO()):
                result = extract.main([str(root / "bad.nsp"), str(root / "keys"), "--game-dir", str(root / "game")])
            self.assertEqual(result, 1)
            self.assertEqual(previous.read_text(), "previous working extraction")
            self.assertEqual(list((root / "game").glob(".extract-*")), [])

    def test_publish_rolls_back_partial_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, stage = root / "game", root / "stage"
            for name in ("nsp", "extracted"):
                (game / name).mkdir(parents=True)
                (game / name / "old").write_text(name)
                (stage / name).mkdir(parents=True)
                (stage / name / "new").write_text(name)
            for directory, marker in ((game, b"old official metadata"), (stage, b"new official metadata")):
                (directory / "extracted/update").mkdir()
                (directory / "extracted/update/control.nacp").write_bytes(marker)
            original = Path.rename
            def fail_second(path, target):
                if path == stage / "extracted":
                    raise OSError("simulated publication failure")
                return original(path, target)
            with patch.object(Path, "rename", fail_second):
                with self.assertRaises(OSError):
                    extract.publish(stage, game)
            for name in ("nsp", "extracted"):
                self.assertEqual((game / name / "old").read_text(), name)
                self.assertFalse((game / name / "new").exists())
            self.assertEqual((game / "extracted/update/control.nacp").read_bytes(), b"old official metadata")

    def test_titlekey_oracle_uses_the_real_section_parser(self):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.nca"
            header = bytearray(0xc00)
            struct.pack_into("<II", header, 0x240, 6, 8)
            header[0x402], header[0x403] = 1, 2  # PartitionFs, HierSha256
            struct.pack_into("<Q", header, 0x440, 0x200)
            counter = bytes(range(8))
            header[0x540:0x548] = counter
            position = 0xc00 + 0x200
            plain, kek = bytes(range(16)), bytes(range(16, 32))
            encrypt = Cipher(algorithms.AES(kek), modes.ECB()).encryptor()
            encrypted_key = encrypt.update(plain) + encrypt.finalize()
            iv = counter[::-1] + (position >> 4).to_bytes(8, "big")
            encrypt = Cipher(algorithms.AES(plain), modes.CTR(iv)).encryptor()
            encrypted_header = encrypt.update(b"PFS0" + bytes(12)) + encrypt.finalize()
            path.write_bytes(bytes(position) + encrypted_header)
            self.assertEqual(extract_titlekeys.find_dec_titlekey(
                encrypted_key, {"titlekek_04": kek.hex()}, path, header), [(4, plain)])

    def test_titlekey_cli_uses_supplied_keys_without_logging_values(self):
        with tempfile.TemporaryDirectory(prefix="sf2 keys ") as temporary:
            root = Path(temporary)
            rid = "0100AB000ABFE000" + "0" * 16
            secret = bytes.fromhex("99" * 16)
            keys = root / "my keys"
            keys.write_text("header_key = " + "00" * 32 + " # fixture\n")
            (root / "program.nca").write_bytes(b"fixture")
            ticket = bytearray(0x190)
            ticket[0x180:0x190] = bytes.fromhex("88" * 16)
            (root / (rid + ".tik")).write_bytes(ticket)
            header = bytearray(0xc00)
            header[0x230:0x240] = bytes.fromhex(rid)
            output = io.StringIO()
            with patch.object(sys, "argv", ["extract_titlekeys.py", "--keys", str(keys), "--nsp-dir", str(root)]), \
                 patch.object(extract_titlekeys, "decrypt_nca_header", return_value=header), \
                 patch.object(extract_titlekeys, "find_dec_titlekey", return_value=[(4, secret)]), \
                 contextlib.redirect_stdout(output):
                extract_titlekeys.main()
            self.assertNotIn(secret.hex().upper(), output.getvalue())
            self.assertNotIn("88" * 16, output.getvalue())
            self.assertEqual((root / "title.keys").stat().st_mode & 0o777, 0o600)
            self.assertIn("88" * 16, (root / "title.keys").read_text())


if __name__ == "__main__":
    unittest.main()

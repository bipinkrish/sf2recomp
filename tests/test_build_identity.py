"""Translated functions must only execute against their exact original NSOs."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_ID, SDK_ID = bytes(range(32)), bytes(range(32, 64))


def nso_header(build_id):
    header = bytearray(0x60)
    header[:4] = b"NSO0"
    header[0x40:0x60] = build_id
    return header


class BuildIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="sf2-identity-harness-")
        cls.binary = pathlib.Path(cls.workspace.name) / "test"
        source = cls.binary.with_suffix(".c")
        arrays = "\n".join("const uint8_t %s_nso_build_id[32] = {%s};" %
                           (name, ",".join(map(str, build_id)))
                           for name, build_id in (("main", MAIN_ID), ("sdk", SDK_ID)))
        source.write_text(r'''
#include <stdio.h>
#include "gamelib.h"
const uint64_t main_fn_addrs[] = {0x7000001000}, sdk_fn_addrs[] = {0x7004001000};
const uint64_t main_fn_count = 1, sdk_fn_count = 1;
const GuestFn main_fn_ptrs[] = {0}, sdk_fn_ptrs[] = {0};
''' + arrays + r'''
int main(int argc, char **argv) {
    char why[512] = {0};
    if (argc != 2) return 3;
    if (!gamelib_load(argv[1], why, sizeof why)) {
        fprintf(stderr, "%s\n", why);
        return 2;
    }
    const GameLibTables *tables = gamelib_tables();
    if (*tables->main_count != 1 || *tables->sdk_count != 1) return 4;
    return 0;
}
''')
        subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"),
                        str(ROOT / "runtime/gamelib.c"), str(source), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def check_load(self, main, sdk):
        with tempfile.TemporaryDirectory(prefix="sf2-identity-") as folder:
            root = pathlib.Path(folder)
            exefs = root / "exefs"
            exefs.mkdir()
            if main is not None:
                (exefs / "main").write_bytes(main)
            if sdk is not None:
                (exefs / "sdk").write_bytes(sdk)
            return subprocess.run([str(self.binary), str(root)], capture_output=True, text=True)

    def test_matching_source_build_ids_accept(self):
        result = self.check_load(nso_header(MAIN_ID), nso_header(SDK_ID))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_main_and_sdk_mismatches_reject_including_id_tail(self):
        # Differences beyond the short display ID must also reject wrong code.
        wrong_main = bytearray(MAIN_ID)
        wrong_main[-1] ^= 0x80
        wrong_sdk = bytearray(SDK_ID)
        wrong_sdk[-1] ^= 0x80
        for name, main, sdk in (("main", nso_header(wrong_main), nso_header(SDK_ID)),
                                ("sdk", nso_header(MAIN_ID), nso_header(wrong_sdk))):
            with self.subTest(module=name):
                result = self.check_load(main, sdk)
                self.assertEqual(result.returncode, 2)
                self.assertIn("exefs/" + name, result.stderr)
                self.assertIn("regenerate and rebuild", result.stderr)

    def test_invalid_or_missing_nso_headers_reject(self):
        for name in ("main", "sdk"):
            for bad in (None, b"NSO0", b"not an NSO" + bytes(100)):
                with self.subTest(module=name, bad=bad):
                    main, sdk = nso_header(MAIN_ID), nso_header(SDK_ID)
                    if name == "main":
                        main = bad
                    else:
                        sdk = bad
                    result = self.check_load(main, sdk)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("exefs/" + name, result.stderr)
                    self.assertIn("missing or is not a valid NSO", result.stderr)


if __name__ == "__main__":
    unittest.main()

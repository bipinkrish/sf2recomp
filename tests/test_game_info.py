"""Native identity comes from original control metadata, never the CNMT ordinal."""
import os
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativeGameInfoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="sf2-game-info-")
        cls.binary = pathlib.Path(cls.workspace.name) / "test"
        harness = cls.binary.with_suffix(".c")
        harness.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "game_info.h"
bool plat_exe_path(char *out, size_t size) {
    return snprintf(out, size, "%s", getenv("SF2_TEST_EXE")) < (int)size;
}
int main(int argc, char **argv) {
    game_info_load(argv[1]);
    printf("version=%s\ntitle=%s\nicon=%s\nnumber=%u\naoc=%llx\n",
        game_display_version(), game_window_title(), game_icon_path(),
        game_application_version(), (unsigned long long)game_add_on_content_base_id());
}
''')
        subprocess.run(["clang", "-O2", "-Wall", "-I", str(ROOT / "runtime"),
                        str(ROOT / "runtime/game_info.c"), str(harness), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def identity(self, game, executable_dir):
        env = dict(os.environ, SF2_TEST_EXE=str(executable_dir / "sf2"))
        result = subprocess.run([str(self.binary), str(game)], env=env, check=True,
                                capture_output=True, text=True)
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def nacp(self, directory, version=b"1.1.0"):
        data = bytearray(0x4000)
        data[0x3060:0x3070] = version.ljust(16, b"\0")
        struct.pack_into("<Q", data, 0x3070, 0x0100AB000ABFF000)
        (directory / "control.nacp").write_bytes(data)

    def test_official_version_title_icon_and_dlc_id(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            self.nacp(game)
            (game / "icon.png").write_bytes(b"local game icon")
            info = self.identity(game, game)
            self.assertEqual(info["version"], "1.1.0")
            self.assertEqual(info["title"], "Shadow Fight 2 (sf2recomp) - 1.1.0")
            self.assertEqual(info["icon"], str(game / "icon.png"))
            self.assertEqual(int(info["number"]), (1 << 22) | (1 << 12))
            self.assertEqual(int(info["aoc"], 16), 0x0100AB000ABFF000)

    def test_packaged_metadata_beside_executable_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = pathlib.Path(tmp)
            game = output / "extracted"
            game.mkdir()
            (game / "control.nacp").write_bytes(b"truncated")
            self.nacp(output, b"1.0.0")
            (output / "icon.png").write_bytes(b"local icon")
            info = self.identity(game, output)
            self.assertEqual(info["version"], "1.0.0")
            self.assertEqual(info["icon"], str(output / "icon.png"))

    def test_missing_or_malformed_metadata_never_claims_guessed_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            for payload in (None, b"truncated", b"1.1.0\nwrong"):
                if payload == b"truncated":
                    (game / "control.nacp").write_bytes(payload)
                elif payload:
                    self.nacp(game, payload)
                info = self.identity(game, game)
                self.assertEqual(info["version"], "")
                self.assertEqual(info["title"], "Shadow Fight 2 (sf2recomp)")
                self.assertEqual(info["number"], "0")

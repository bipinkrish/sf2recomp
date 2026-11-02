"""Conversion output must contain runtime inputs and preserve editable settings."""
import importlib.util
import hashlib
import pathlib
import json
import os
import shutil
import subprocess
from unittest.mock import patch
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sf2_convert", ROOT / "tools/convert.py")
convert = importlib.util.module_from_spec(spec)
spec.loader.exec_module(convert)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sf2-package-")
        self.base = pathlib.Path(self.temp.name)
        self.game = self.base / "game files"
        (self.game / "exefs").mkdir(parents=True)
        (self.game / "romfs/Data/Managed/Metadata").mkdir(parents=True)
        for name, build_id in (("main", "e15fc665ec42684aa6edd1ae03624d96"), ("sdk", "1234")):
            header = bytearray(0x60)
            header[:4] = b"NSO0"
            header[0x40:0x60] = bytes.fromhex(build_id).ljust(32, b"\0")
            (self.game / "exefs" / name).write_bytes(header)
        (self.game / "exefs/main.npdm").write_text("local metadata")
        (self.game / "exefs/main.uncompressed").write_text("analysis only")
        (self.game / "romfs/Data/Managed/Metadata/global-metadata.dat").write_bytes(b"test")
        (self.game / "prod.keys").write_text("private test input")
        nacp = bytearray(0x4000)
        nacp[:len(b"Shadow Fight 2")] = b"Shadow Fight 2"
        nacp[0x3060:0x3065] = b"1.1.0"
        (self.game / "control.nacp").write_bytes(nacp)
        from PIL import Image
        Image.new("RGB", (8, 8), (70, 80, 90)).save(self.game / "icon.png")
        self.exe = self.base / "sf2"
        self.exe.write_text('#!/bin/sh\n[ "$1" = --check-game ]\n')
        self.exe.chmod(0o755)
        self.output = self.base / "native game"
        self.ffmpeg = self.base / "ffmpeg"
        for name in ("bin/ffmpeg", "share/ffmpeg/BUILD.txt",
                     "share/ffmpeg/" + convert.FFMPEG_ARCHIVE_NAME,
                     "share/licenses/FFmpeg/COPYING.LGPLv2.1", "lib/libavcodec.so.1"):
            path = self.ffmpeg / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        import setup_fonts
        self.fonts = self.base / "fonts"
        self.fonts.mkdir()
        for name in (*setup_fonts.FILES, "SOURCE.txt"):
            (self.fonts / name).write_bytes(b"font fixture")
        self.font_directory_patch = patch.object(convert, "FONT_DIRECTORY", self.fonts)
        self.font_directory_patch.start()
        self.addCleanup(self.font_directory_patch.stop)
        self.font_pin_patch = patch.object(setup_fonts, "FILES", {
            name: (url, hashlib.sha256(b"font fixture").hexdigest())
            for name, (url, _) in setup_fonts.FILES.items()
        })
        self.font_pin_patch.start()
        self.addCleanup(self.font_pin_patch.stop)

    def tearDown(self):
        self.temp.cleanup()

    def test_runtime_only_and_settings_preserved_on_reconversion(self):
        convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
        self.assertTrue((self.output / "sf2").is_file())
        completed = pathlib.Path("profiles/completed/save_0/userdata/users.xml")
        self.assertEqual((self.output / completed).read_bytes(), (ROOT / completed).read_bytes())
        self.assertEqual((self.output / "lib/ffmpeg/bin/ffmpeg").read_bytes(), b"fixture")
        self.assertEqual((self.output / "lib/ffmpeg/lib/libavcodec.so.1").read_bytes(), b"fixture")
        self.assertEqual((self.output / "lib/fonts/NotoSans-Regular.ttf").read_bytes(), b"font fixture")
        self.assertEqual((self.output / "lib/fonts/NotoSansCJKsc-Regular.otf").read_bytes(), b"font fixture")
        self.assertEqual((self.output / "licenses/Noto/NotoSans-LICENSE.txt").read_bytes(), b"font fixture")
        self.assertFalse((self.output / "sf2recomp").exists())
        self.assertEqual(json.loads((self.output / "build.json").read_text())["executable"], "sf2")
        config = self.output / "sf2recomp.ini"
        self.assertIn("[controls.keyboard]", config.read_text())
        self.assertTrue((self.output / "extracted/romfs/Data/Managed/Metadata/global-metadata.dat").is_file())
        self.assertFalse((self.output / "extracted/exefs/main.uncompressed").exists())
        self.assertFalse(any(self.output.rglob("*.keys")))
        for name in ("LASRecomp.txt", "SMAA.txt", "SDL3.txt", "Ryujinx.txt"):
            self.assertEqual((self.output / "licenses" / name).read_bytes(), (ROOT / "licenses" / name).read_bytes())
        self.assertEqual((self.output / "control.nacp").read_bytes(), (self.game / "control.nacp").read_bytes())
        self.assertEqual((self.output / "icon.png").read_bytes(), (self.game / "icon.png").read_bytes())
        self.assertEqual(json.loads((self.output / "game-info.json").read_text())["display_version"], "1.1.0")
        desktop = self.output / (convert.APP_ID + ".desktop")
        self.assertIn("Name=Shadow Fight 2 (sf2recomp)\n", desktop.read_text())
        self.assertIn("Version=1.0\n", desktop.read_text())
        self.assertIn("X-AppVersion=1.1.0\n", desktop.read_text())
        self.assertIn(f'Exec="{self.output}/sf2"', desktop.read_text())
        self.assertIn("StartupWMClass=" + convert.APP_ID, desktop.read_text())
        if shutil.which("desktop-file-validate"):
            subprocess.run(["desktop-file-validate", str(desktop)], check=True)
        config.write_text("[controls.keyboard]\na = Space\n")
        save = self.output / "save/user.xml"
        save.parent.mkdir()
        save.write_text("saved progress")
        shutil.copy2(self.exe, self.output / "sf2recomp")  # Existing conversion before rename.
        (self.output / "licenses/LAS-RUNTIME-LICENSE.txt").write_text("old duplicate")
        (self.output / "licenses/SMAA-LICENSE.txt").write_text("old duplicate")
        convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
        self.assertEqual(config.read_text(), "[controls.keyboard]\na = Space\n")
        self.assertEqual(save.read_text(), "saved progress")
        self.assertFalse((self.output / "sf2recomp").exists())
        self.assertTrue((self.output / "sf2").is_file())
        self.assertFalse((self.output / "licenses/LAS-RUNTIME-LICENSE.txt").exists())
        self.assertFalse((self.output / "licenses/SMAA-LICENSE.txt").exists())

    def test_user_desktop_registration_is_explicit(self):
        with patch.dict(os.environ, {"XDG_DATA_HOME": str(self.base / "user applications")}):
            convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
            self.assertFalse((self.base / "user applications").exists())
            installed = convert.install_desktop(self.output)
            self.assertEqual(installed, self.base / "user applications/applications" / (convert.APP_ID + ".desktop"))
            self.assertEqual(installed.read_bytes(), (self.output / (convert.APP_ID + ".desktop")).read_bytes())

    def test_missing_decoder_preserves_existing_output(self):
        self.output.mkdir()
        old_executable = self.output / "sf2"
        old_executable.write_bytes(b"existing game")
        (self.ffmpeg / "bin/ffmpeg").unlink()
        with self.assertRaisesRegex(ValueError, "FFmpeg build is incomplete"):
            convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
        self.assertEqual(old_executable.read_bytes(), b"existing game")
        self.assertEqual(list(self.output.iterdir()), [old_executable])

    def test_missing_official_metadata_rejected_before_output_creation(self):
        (self.game / "control.nacp").unlink()
        with self.assertRaisesRegex(ValueError, "Official control.nacp"):
            convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
        self.assertFalse(self.output.exists())

    def test_missing_font_preserves_existing_output(self):
        self.output.mkdir()
        old_executable = self.output / "sf2"
        old_executable.write_bytes(b"existing game")
        (self.fonts / "NotoSans-Regular.ttf").unlink()
        with self.assertRaisesRegex(ValueError, "Bundled fonts missing or changed"):
            convert.package_game(self.game, self.output, self.exe, self.base / "sdl", self.ffmpeg)
        self.assertEqual(old_executable.read_bytes(), b"existing game")
        self.assertEqual(list(self.output.iterdir()), [old_executable])

    def test_desktop_special_characters_are_quoted_without_shell_execution(self):
        output = self.base / 'game % "quoted" $value \u005c folder'
        output.mkdir()
        desktop = convert.write_desktop(output, {"display_version": "1.1.0"})
        text = desktop.read_text()
        self.assertIn("%%", text)
        self.assertIn("\\\\$value", text)
        if shutil.which("desktop-file-validate"):
            subprocess.run(["desktop-file-validate", str(desktop)], check=True)

    def test_reject_source_overlap_without_mutation(self):
        before = (self.game / "exefs/main").read_bytes()
        with self.assertRaises(ValueError):
            convert.package_game(self.game, self.game / "nested", self.exe, self.base, self.ffmpeg)
        self.assertEqual((self.game / "exefs/main").read_bytes(), before)
        self.assertFalse((self.game / "nested").exists())

    def test_reject_unknown_build_before_output_creation(self):
        (self.game / "exefs/main").write_bytes(b"NSO0" + bytes(0x5c))
        with self.assertRaises(ValueError):
            convert.package_game(self.game, self.output, self.exe, self.base, self.ffmpeg)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()

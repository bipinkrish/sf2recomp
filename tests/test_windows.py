"""Windows helpers and package invariants that can be checked on Linux."""
import ctypes
import importlib.util
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import test_convert

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("windows_dlls", ROOT / "tools/windows_dlls.py")
dlls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dlls)


class WindowsHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="sf2-win-helpers-")
        folder = pathlib.Path(cls.work.name)
        source = folder / "helpers.c"
        source.write_text('''
#include "platform/windows_cmdline.h"
#include "platform/path_util.h"
int quote(char *out, size_t cap, const char *arg) { return win_append_arg(out, cap, arg); }
size_t root(const char *p) { return plat_path_root(p, 1); }
''')
        subprocess.run(["clang", "-shared", "-fPIC", "-O2", "-I", str(ROOT / "runtime"),
                        str(source), "-o", str(folder / "helpers.so")], check=True)
        cls.lib = ctypes.CDLL(str(folder / "helpers.so"))
        cls.lib.quote.argtypes = (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p)
        cls.lib.root.argtypes = (ctypes.c_char_p,)
        cls.lib.root.restype = ctypes.c_size_t

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_command_quoting_and_overflow(self):
        args = ["", "simple", "two words", 'say "hello"', "C:\\space dir\\", "é"]
        out = ctypes.create_string_buffer(512)
        for arg in args:
            self.assertEqual(self.lib.quote(out, len(out), arg.encode()), 1)
        self.assertEqual(out.value.decode(), subprocess.list2cmdline(args))
        small = ctypes.create_string_buffer(8)
        self.assertEqual(self.lib.quote(small, len(small), b"this is too long"), 0)
        self.assertLess(len(small.value), len(small))

    def test_drive_unc_and_root_relative_paths(self):
        for path, root in {"C:/game/save": 3, "C:/": 3, "//server/share/game": 15,
                           "//server/share": 14, "/rooted": 1, "relative": 0,
                           "C:relative": 0, "": 0, "//server": 0}.items():
            with self.subTest(path=path):
                self.assertEqual(self.lib.root(path.encode()), root)

    def test_dll_closure_is_recursive_and_case_insensitive(self):
        with tempfile.TemporaryDirectory() as work:
            folder = pathlib.Path(work)
            for name in ("SDL3.dll", "libwinpthread-1.dll"):
                (folder / name).write_bytes(b"fixture")
            graph = {"sf2.exe": ["SDL3.DLL", "KERNEL32.dll", "vulkan-1.dll"],
                     "SDL3.dll": ["libwinpthread-1.dll"], "libwinpthread-1.dll": ["ucrtbase.dll"]}
            with patch.object(dlls, "imports", side_effect=lambda p, _: graph[p.name]):
                found = dlls.dependency_closure([folder / "sf2.exe"], [folder])
            self.assertEqual(set(found), {"sdl3.dll", "libwinpthread-1.dll"})

    def test_missing_or_msys_dll_is_rejected(self):
        for name in ("missing.dll", "msys-2.0.dll", "cygwin1.dll"):
            with self.subTest(name=name), patch.object(dlls, "imports", return_value=[name]):
                with self.assertRaises(ValueError):
                    dlls.dependency_closure([ROOT / "sf2.exe"], [])


class WindowsPackageTests(unittest.TestCase):
    def setUp(self):
        test_convert.PackageTests.setUp(self)
        self.exe = self.base / "sf2.exe"
        self.exe.write_bytes(b"PE fixture")
        (self.ffmpeg / "bin/ffmpeg.exe").write_bytes(b"decoder fixture")
        (self.ffmpeg / "bin/avcodec-1.dll").write_bytes(b"codec fixture")
        self.sdl = self.base / "sdl"
        (self.sdl / "bin").mkdir(parents=True)
        (self.sdl / "bin/SDL3.dll").write_bytes(b"SDL fixture")

    def tearDown(self):
        self.temp.cleanup()

    def test_native_package_placement_and_save_preservation(self):
        convert = test_convert.convert
        self.output.mkdir()
        (self.output / "sf2recomp.ini").write_text("custom ini")
        (self.output / "save").mkdir()
        (self.output / "save/progress").write_text("keep progress")
        with patch.object(convert, "run"), patch.object(convert, "dependency_closure", side_effect=[
            {"sdl3.dll": self.sdl / "bin/SDL3.dll"}, {"avcodec-1.dll": self.ffmpeg / "bin/avcodec-1.dll"}
        ]):
            convert.package_game(self.game, self.output, self.exe, self.sdl, self.ffmpeg, "windows")
        self.assertEqual((self.output / "sf2.exe").read_bytes(), b"PE fixture")
        self.assertEqual((self.output / "SDL3.dll").read_bytes(), b"SDL fixture")
        self.assertEqual((self.output / "lib/ffmpeg/bin/avcodec-1.dll").read_bytes(), b"codec fixture")
        self.assertTrue((self.output / "lib/ffmpeg/bin/ffmpeg.exe").is_file())
        self.assertFalse(list(self.output.glob("*.desktop")))
        self.assertEqual(json.loads((self.output / "build.json").read_text())["target"], "windows")
        self.assertEqual((self.output / "sf2recomp.ini").read_text(), "custom ini")
        self.assertEqual((self.output / "save/progress").read_text(), "keep progress")

    def test_missing_dll_leaves_existing_output_untouched(self):
        convert = test_convert.convert
        self.output.mkdir()
        (self.output / "sf2.exe").write_bytes(b"existing game")
        with patch.object(convert, "run"), patch.object(convert, "dependency_closure", side_effect=ValueError("Missing DLL")):
            with self.assertRaisesRegex(ValueError, "Missing DLL"):
                convert.package_game(self.game, self.output, self.exe, self.sdl, self.ffmpeg, "windows")
        self.assertEqual([p.name for p in self.output.iterdir()], ["sf2.exe"])
        self.assertEqual((self.output / "sf2.exe").read_bytes(), b"existing game")

    def test_release_conversion_keeps_decoder_dlls_next_to_ffmpeg(self):
        convert = test_convert.convert
        release_root = self.base / "portable tool"
        release_root.mkdir()
        for name in ("sf2recomp.ini", "LICENSE", "THIRD_PARTY_NOTICES"):
            shutil.copy2(ROOT / name, release_root / name)
        shutil.copytree(ROOT / "profiles", release_root / "profiles")
        native = release_root / "native/lib"
        native.mkdir(parents=True)
        (native / "SDL3.dll").write_bytes(b"release SDL")
        (self.ffmpeg / "bin/avutil-1.dll").write_bytes(b"decoder dependency")
        with patch.object(convert, "ROOT", release_root), patch.object(convert, "run"), \
                patch.object(convert, "release_manifest", return_value={"version": "test"}), \
                patch.object(convert, "dependency_closure", side_effect=AssertionError("No objdump needed on the user's PC")):
            convert.package_game(self.game, self.output, self.exe, self.sdl, self.ffmpeg, "windows")
        decoder = self.output / "lib/ffmpeg/bin"
        self.assertEqual((decoder / "avcodec-1.dll").read_bytes(), b"codec fixture")
        self.assertEqual((decoder / "avutil-1.dll").read_bytes(), b"decoder dependency")
        self.assertEqual((self.output / "SDL3.dll").read_bytes(), b"release SDL")
        report = json.loads((self.output / "windows-dlls.json").read_text())
        self.assertEqual(report["decoder"], ["avcodec-1.dll", "avutil-1.dll"])


if __name__ == "__main__":
    unittest.main()

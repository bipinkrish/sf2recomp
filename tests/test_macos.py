"""macOS host selection, Mach-O relocation and conversion regression coverage."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_convert
import host_tools
import macos_dylibs
import setup_ffmpeg
from _sf2_tool_constants import PACKAGE_RELEASE

ROOT = Path(__file__).resolve().parents[1]


class MacOSHostTests(unittest.TestCase):
    def test_native_tools_are_isolated_and_recognize_macho(self):
        with patch.object(host_tools.sys, "platform", "darwin"), patch.object(host_tools.os, "name", "posix"):
            self.assertEqual(host_tools.host_target(), "macos")
            host_tools.require_host("macos")
            with self.assertRaises(ValueError):
                host_tools.require_host("linux")
        self.assertEqual(host_tools.tool_path("hactool", "macos"), ROOT / "tools/bin/macos/hactool")
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / "tool"
            for magic in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
                executable.write_bytes(magic + b"fixture")
                self.assertTrue(host_tools.native_executable(executable, "macos"))
                self.assertFalse(host_tools.native_executable(executable, "linux"))
            for magic in (b"\x7fELF", b"MZ..", b""):
                executable.write_bytes(magic)
                self.assertFalse(host_tools.native_executable(executable, "macos"))

    def test_arm_build_removes_linux_and_x86_flags(self):
        make = "gmake" if host_tools.host_target() == "macos" else "make"
        result = subprocess.run([make, "-n", "TARGET=macos", "HOST_ARCH=arm64", "macos-smoke"],
                                cwd=ROOT, capture_output=True, text=True, check=True)
        self.assertIn("runtime/platform/macos.c", result.stdout)
        self.assertIn("build/macos/macos-platform-test", result.stdout)
        for flag in ("-mcx16", "-latomic", "-ldl", "-fuse-ld=lld"):
            self.assertNotIn(flag, result.stdout)

    def test_ffmpeg_requires_darwin_libraries(self):
        with tempfile.TemporaryDirectory() as folder:
            prefix = Path(folder)
            for name in ("bin/ffmpeg", "share/ffmpeg/BUILD.txt", "share/ffmpeg/" + setup_ffmpeg.ARCHIVE_NAME,
                         "share/licenses/FFmpeg/COPYING.LGPLv2.1", "share/licenses/FFmpeg/LICENSE.md"):
                path = prefix / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("FFmpeg " + setup_ffmpeg.VERSION)
            (prefix / "lib").mkdir()
            (prefix / "lib/libavcodec.so.1").write_bytes(b"ELF")
            self.assertFalse(setup_ffmpeg.installed(prefix, "macos"))
            (prefix / "lib/libavcodec.62.dylib").write_bytes(b"Mach-O")
            self.assertTrue(setup_ffmpeg.installed(prefix, "macos"))


class MacOSRelocationTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec('ziglang'), 'requires the release Zig compiler')
    def test_bundled_corefoundation_stub_links_for_apple_silicon(self):
        # Cross-linking on Linux exercises the same TBD parser as the Mac release.
        with tempfile.TemporaryDirectory(prefix='sf2 mac link ') as folder:
            root = Path(folder)
            stub = root / 'CoreFoundation.tbd'
            stub.write_text(PACKAGE_RELEASE.MACOS_COREFOUNDATION_TBD)
            source = root / 'probe.c'
            source.write_text('extern void CFRelease(void*); int main(void) { CFRelease(0); return 0; }\n')
            output = root / 'probe'
            env = {**os.environ, 'ZIG_GLOBAL_CACHE_DIR': str(root / 'zig-global'),
                   'ZIG_LOCAL_CACHE_DIR': str(root / 'zig-local')}
            subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-target', 'aarch64-macos.15.0',
                            str(source), str(stub), '-o', str(output)], env=env, check=True,
                           capture_output=True, text=True, timeout=90)
            self.assertEqual(output.read_bytes()[:4], b'\xcf\xfa\xed\xfe')

    def test_rewrites_only_bundled_libraries_and_signs_after_edits(self):
        with tempfile.TemporaryDirectory(prefix="sf2 mac libs ") as folder:
            root = Path(folder)
            exe, codec = root / "sf2", root / "lib/ffmpeg/lib/libavcodec.62.dylib"
            sdl = root / "lib/libSDL3.0.dylib"
            for path in (exe, codec, sdl):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            alias = sdl.with_name("libSDL3.dylib")
            alias.symlink_to(sdl.name)
            graph = {
                exe: ["/build/deps/lib/libSDL3.0.dylib", "/opt/homebrew/lib/libvulkan.1.dylib",
                      "/usr/lib/libSystem.B.dylib"],
                sdl: ["/build/deps/lib/libSDL3.0.dylib", "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa"],
                codec: ["@rpath/libavcodec.62.dylib"],
            }
            calls = []
            with patch.object(macos_dylibs, "linked_libraries", side_effect=lambda p: graph[p]), \
                 patch.object(macos_dylibs.subprocess, "run", side_effect=lambda command, **kw: calls.append(command)):
                macos_dylibs.relocate([exe], [sdl, alias, codec])
            self.assertIn(["install_name_tool", "-change", "/build/deps/lib/libSDL3.0.dylib",
                           "@loader_path/lib/libSDL3.0.dylib", str(exe)], calls)
            self.assertEqual(sum(command[0] == "codesign" for command in calls), 3)
            first_sign = next(i for i, command in enumerate(calls) if command[0] == "codesign")
            self.assertTrue(all(command[0] == "codesign" for command in calls[first_sign:]))
            self.assertFalse(any("libvulkan" in argument for command in calls for argument in command))


class MacOSConversionTests(unittest.TestCase):
    def setUp(self):
        test_convert.PackageTests.setUp(self)
        self.addCleanup(self.temp.cleanup)
        (self.ffmpeg / "lib/libavcodec.62.dylib").write_bytes(b"codec fixture")
        self.sdl = self.base / "sdl"
        (self.sdl / "lib").mkdir(parents=True)
        (self.sdl / "lib/libSDL3.0.dylib").write_bytes(b"SDL fixture")
        (self.sdl / "lib/libSDL3.dylib").symlink_to("libSDL3.0.dylib")

    def test_native_folder_has_dylibs_preserves_state_and_skips_linux_launcher(self):
        self.output.mkdir()
        (self.output / "sf2recomp.ini").write_text("custom")
        (self.output / "save").mkdir()
        (self.output / "save/progress").write_text("keep")
        convert = test_convert.convert
        with patch.object(convert, "relocate_macos") as relocate:
            convert.package_game(self.game, self.output, self.exe, self.sdl, self.ffmpeg, "macos")
        self.assertEqual(json.loads((self.output / "build.json").read_text())["target"], "macos")
        self.assertEqual((self.output / "lib/libSDL3.dylib").readlink(), Path("libSDL3.0.dylib"))
        self.assertTrue((self.output / "lib/ffmpeg/lib/libavcodec.62.dylib").is_file())
        self.assertFalse(list(self.output.rglob("*.so*")))
        self.assertFalse(list(self.output.glob("*.desktop")))
        self.assertEqual((self.output / "sf2recomp.ini").read_text(), "custom")
        self.assertEqual((self.output / "save/progress").read_text(), "keep")
        relocate.assert_called_once()

    def test_conversion_selects_gmake_native_output_and_same_python_for_setup(self):
        convert = test_convert.convert
        with patch.object(convert, "require_host"), patch.object(convert, "run") as run, \
             patch.object(convert, "package_game") as package, patch.dict(os.environ, {}, clear=True):
            result = convert.main(["--target", "macos", "--game", str(self.game),
                                   "--skip-generation", "--output", str(self.output)])
        self.assertEqual(result, 0)
        commands = [call.args for call in run.call_args_list]
        self.assertTrue(any(command[0] == "gmake" for command in commands))
        self.assertEqual(run.call_args_list[0].kwargs["env"]["TARGET"], "macos")
        self.assertEqual(package.call_args.args[2], ROOT / "build/macos/sf2")


if __name__ == "__main__":
    unittest.main()

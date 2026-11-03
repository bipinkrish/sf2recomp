"""Cross-host input preparation, analysis caching and failure preservation."""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import tarfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_il2cpp as analysis
import build_extraction_tools as builder
import build_il2cpp
import host_tools
import extract
import test_convert
from test_extraction import bundle


class HostToolsTests(unittest.TestCase):
    def test_windows_build_uses_separate_output_and_installs_pe_tools(self):
        with tempfile.TemporaryDirectory(prefix="sf2 native tools ") as directory:
            root = Path(directory)
            commands = []
            def run(*args, **kwargs):
                commands.append(args)
                if args[:2] == ("cmake", "--build"):
                    output = root / "build/tools/extraction/windows/bin"
                    output.mkdir(parents=True)
                    for name in ("hactool.exe", "nstool.exe"):
                        (output / name).write_bytes(b"MZ fixture")
            with patch.object(builder, "ROOT", root), patch.object(host_tools, "ROOT", root), \
                 patch.object(builder, "require_host"), patch.object(builder.shutil, "which", side_effect=lambda s: s), \
                 patch.object(builder, "recipe", return_value={"target": "windows", "lock": "fixture", "cmake": "fixture"}), \
                 patch.object(builder.setup_extraction, "prepare"), patch.object(builder, "run", side_effect=run):
                builder.build("windows", jobs=2, offline=True)
                self.assertEqual(builder.tool_path("hactool", "windows"), root / "tools/bin/windows/hactool.exe")
                self.assertTrue(host_tools.native_executable(root / "tools/bin/windows/nstool.exe", "windows"))
                self.assertTrue(builder.installed("windows"))
            configure = commands[0]
            self.assertIn("Ninja", configure)
            self.assertIn(root / "build/tools/extraction/windows", configure)
            self.assertFalse((root / "tools/bin/hactool").exists())

    def test_windows_does_not_accept_an_elf_tool(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "hactool.exe"
            binary.write_bytes(b"\x7fELF fixture")
            self.assertFalse(host_tools.native_executable(binary, "windows"))

    def test_sdk_check_accepts_sdk_and_rejects_runtime_only_install(self):
        with patch.object(host_tools.shutil, "which", return_value="C:/Program Files/dotnet/dotnet.exe"), \
             patch.object(host_tools, "run", return_value=subprocess.CompletedProcess([], 0, "8.0.100 [SDK]\n")) as run:
            self.assertEqual(host_tools.dotnet_sdk(), "C:/Program Files/dotnet/dotnet.exe")
            self.assertEqual(run.call_args.args, ("C:/Program Files/dotnet/dotnet.exe", "--list-sdks"))
        with patch.object(host_tools.shutil, "which", return_value="dotnet"), \
             patch.object(host_tools, "run", return_value=subprocess.CompletedProcess([], 0, "")):
            with self.assertRaisesRegex(ValueError, "runtime-only"):
                host_tools.dotnet_sdk()

    def test_nsp_rejects_windows_reserved_names_and_case_aliases(self):
        with tempfile.TemporaryDirectory() as folder:
            nsp = Path(folder) / "fixture.nsp"
            for name in ("CON.nca", "LPT1.txt", "bad?.nca", "bad.", "bad ", "Application.CNMT.XML"):
                with self.subTest(name=name):
                    bundle(nsp, name_override=name)
                    with self.assertRaisesRegex(ValueError, "unsafe or duplicate"):
                        extract.pfs0_entries(nsp)


class AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sf2 Unicode-\u65e5\u672c analysis ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "checkout"
        self.game = Path(self.temp.name) / "game input"
        (self.game / "exefs").mkdir(parents=True)
        (self.game / "romfs/Data/Managed/Metadata").mkdir(parents=True)
        header = bytearray(0x80)
        header[:4] = b"NSO0"
        header[0x40:0x60] = analysis.BUILD_ID
        (self.game / "exefs/main").write_bytes(header)
        self.metadata = self.game / "romfs/Data/Managed/Metadata/global-metadata.dat"
        self.metadata.write_bytes(b"synthetic metadata")
        (self.root / "tools/il2cpp").mkdir(parents=True)
        (self.root / "tools/il2cpp/config.json").write_text('{"RequireAnyKey":false,"GenerateStruct":true}')
        shutil.copy2(ROOT / "tools/il2cpp_map.py", self.root / "tools/il2cpp_map.py")
        shutil.copy2(ROOT / "tools/_sf2_tool_constants.py", self.root / "tools/_sf2_tool_constants.py")
        self.output = Path(self.temp.name) / "symbols"
        self.calls = []
        self.empty_output = False
        self.hactool = self.root / "tools/bin/windows/hactool.exe"
        for patcher in (
            patch.object(analysis, "ROOT", self.root),
            patch.object(analysis, "dotnet_sdk", return_value="dotnet.exe"),
            patch.object(analysis, "ensure_tools", return_value=(self.hactool, self.root / "nstool.exe")),
            patch.object(analysis.build_il2cpp, "ensure", return_value=self.root / "tools/il2cpp/Il2CppDumper.dll"),
            patch.object(analysis, "run", side_effect=self.run_command),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_command(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        if args[0] == self.hactool:
            output = Path(next(a.split("=", 1)[1] for a in args if isinstance(a, str) and a.startswith("--uncompressed=")))
            shutil.copy2(self.game / "exefs/main", output)
        elif args[0] == "dotnet.exe":
            self.assertEqual(kwargs["stdin"], subprocess.DEVNULL)
            self.assertIn("DOTNET_ROLL_FORWARD", kwargs["env"])
            if not self.empty_output:
                (args[-1] / "script.json").write_text(json.dumps({"ScriptMethod": [
                    {"Address": 0x1234, "Name": "Player.\u65e5\u672c\u8a9e"}]}), encoding="utf-8")
        else:
            subprocess.run([str(a) for a in args], check=True, capture_output=True)

    def test_windows_analysis_creates_utf8_symbols_and_reuses_verified_cache(self):
        result = analysis.prepare_symbols(self.game, self.output)
        self.assertEqual(result.read_text(encoding="utf-8"), "0x00001234 Player.\u65e5\u672c\u8a9e\n")
        self.assertFalse((self.game / "exefs/main.uncompressed").exists())
        calls = len(self.calls)
        self.assertEqual(analysis.prepare_symbols(self.game, self.output), result)
        self.assertEqual(len(self.calls), calls)
        self.metadata.write_bytes(b"changed metadata")
        analysis.prepare_symbols(self.game, self.output)
        self.assertGreater(len(self.calls), calls)

    def test_zero_exit_without_fresh_dumper_output_preserves_previous_symbols(self):
        analysis.prepare_symbols(self.game, self.output)
        old_map = (self.output / "sym_main.txt").read_bytes()
        old_stamp = (self.output / "source.json").read_bytes()
        self.empty_output = True
        with self.assertRaisesRegex(ValueError, "no script.json"):
            analysis.prepare_symbols(self.game, self.output, force=True)
        self.assertEqual((self.output / "sym_main.txt").read_bytes(), old_map)
        self.assertEqual((self.output / "source.json").read_bytes(), old_stamp)
        self.assertFalse(list(self.output.parent.glob(".sf2-analysis-*")))

    def test_tampered_symbol_cache_is_regenerated(self):
        analysis.prepare_symbols(self.game, self.output)
        (self.output / "sym_main.txt").write_text("stale")
        analysis.prepare_symbols(self.game, self.output)
        self.assertIn("Player.", (self.output / "sym_main.txt").read_text(encoding="utf-8"))


class WindowsConversionTests(unittest.TestCase):
    def setUp(self):
        test_convert.PackageTests.setUp(self)
        self.addCleanup(self.temp.cleanup)

    def test_windows_nsp_conversion_prepares_symbols_without_linux_or_shell_extractors(self):
        convert = test_convert.convert
        root = self.base / "checkout"
        game = root / "game/extracted/update"
        shutil.copytree(self.game, game)
        with patch.object(convert, "ROOT", root), patch.object(convert, "require_host"), \
             patch.object(convert, "run") as run, \
             patch.object(convert, "prepare_symbols", return_value=root / "game/il2cpp/sym_main.txt") as symbols, \
             patch.object(convert, "package_game") as package, \
             patch.object(convert.importlib.util, "find_spec", return_value=object()), \
             patch.dict(os.environ, {"SDL_DIR": str(self.base / "sdl")}, clear=False):
            self.assertEqual(convert.main(["--target", "windows", "--nsp", str(self.base / "local.nsp"),
                                           "--keys", str(self.base / "prod.keys"), "--output", str(self.output)]), 0)
        symbols.assert_called_once_with(game)
        commands = [call.args for call in run.call_args_list]
        self.assertEqual(commands[0][:2], (sys.executable, root / "tools/extract.py"))
        self.assertIn("--validate-only", commands[0])
        self.assertEqual(commands[1][:2], (sys.executable, root / "tools/extract.py"))
        self.assertFalse(any(any(str(a).endswith(".sh") for a in command) for command in commands[:2]))
        self.assertEqual(package.call_args.args[2], root / "build/windows/sf2.exe")
        self.assertEqual(package.call_args.args[-1], "windows")

    def test_windows_extraction_runs_bktr_and_publishes_complete_metadata(self):
        nsp = self.base / "combined fixture.nsp"
        bundle(nsp)
        entries = extract.pfs0_entries(nsp)
        raw = nsp.read_bytes()
        files = {name: raw[e.offset:e.offset + e.size] for name, e in entries.items()}
        files["Patch.cnmt.xml"] = files["Patch.cnmt.xml"].replace(b"</ContentMeta>",
            b"<Content><Type>Control</Type><Id>" + b"c" * 32 + b"</Id></Content></ContentMeta>")
        files["c" * 32 + ".nca"] = b"synthetic control"
        strings = table = payload = b""
        for name, data in files.items():
            table += struct.pack("<QQII", len(payload), len(data), len(strings), 0)
            strings += name.encode() + b"\0"
            payload += data
        nsp.write_bytes(struct.pack("<4sIII", b"PFS0", len(files), len(strings), 0) + table + strings + payload)
        keys = self.base / "local keys"
        keys.write_text("not a real key")
        root = self.base / "checkout"
        game = self.base / "new extraction"
        hactool, nstool = root / "tools/bin/windows/hactool.exe", root / "tools/bin/windows/nstool.exe"
        calls = []
        def run(*args, **kwargs):
            calls.append((args, kwargs))
            if args[0] == hactool:
                if "pfs0" in args:
                    out = Path(next(a.split("=", 1)[1] for a in args if isinstance(a, str) and a.startswith("--pfs0dir=")))
                    for name, data in files.items():
                        (out / name).write_bytes(data)
                else:
                    out = Path(next(a.split("=", 1)[1] for a in args if isinstance(a, str) and a.startswith("--uncompressed=")))
                    shutil.copy2(args[-1], out)
            elif args[0] == nstool:
                if "-x" in args:
                    out = args[args.index("-x") + 1] / "Data/Managed/Metadata/global-metadata.dat"
                    out.parent.mkdir(parents=True)
                    out.write_bytes(b"synthetic metadata")
                else:
                    flag = next(a for a in args if str(a).startswith("--part"))
                    out = args[args.index(flag) + 1]
                    out.mkdir(parents=True, exist_ok=True)
                    if out.name == "exefs":
                        header = bytearray(0x80)
                        header[:4], header[0x40:0x60] = b"NSO0", analysis.BUILD_ID
                        for name in ("main", "sdk", "rtld", "subsdk0", "main.npdm"):
                            (out / name).write_bytes(header)
                    elif out.name == "control":
                        from PIL import Image
                        nacp = bytearray(0x4000)
                        nacp[:14], nacp[0x3060:0x3065] = b"Shadow Fight 2", b"1.1.0"
                        (out / "control.nacp").write_bytes(nacp)
                        Image.new("RGB", (8, 8)).save(out / "icon_AmericanEnglish.dat", format="JPEG")
            elif args[0] == "dotnet.exe":
                self.assertIn("DOTNET_ROLL_FORWARD", kwargs["env"])
                if "build" in args:
                    out = args[args.index("-o") + 1]
                    out.mkdir(parents=True)
                    (out / "Sf2Bktr.dll").write_bytes(b"managed merger fixture")
                else:
                    Path(kwargs["env"]["SF2BKTR_DUMP_OUT"]).write_bytes(b"merged fixture")
            elif args[0] == sys.executable:
                (args[-1] / "title.keys").write_text("synthetic encrypted titlekey")
        with patch.object(extract, "ROOT", root), patch.object(extract, "ensure_tools", return_value=(hactool, nstool)), \
             patch.object(extract, "dotnet_sdk", return_value="dotnet.exe"), \
             patch.object(extract, "host_target", return_value="windows"), patch.object(extract, "run", side_effect=run), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(extract.main([str(nsp), str(keys), "--game-dir", str(game)]), 0)
        self.assertEqual(json.loads((game / "extracted/update/game-info.json").read_text())["display_version"], "1.1.0")
        self.assertTrue((game / "extracted/update/icon.png").is_file())
        merger_calls = [args for args, _ in calls if args[0] == "dotnet.exe"]
        self.assertIn(root / "build/tools/bktr/windows", merger_calls[0])
        self.assertEqual(merger_calls[1][1], root / "build/tools/bktr/windows/Sf2Bktr.dll")
        self.assertFalse(list(game.glob(".extract-*")))


class DumperBuildTests(unittest.TestCase):
    def test_managed_publish_is_host_independent_and_tracks_assemblies(self):
        with tempfile.TemporaryDirectory(prefix="sf2 dumper build ") as folder:
            root = Path(folder)
            cache = root / "build/tools/il2cpp"
            cache.mkdir(parents=True)
            archive = cache / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                member = tarfile.TarInfo(f"Il2CppDumper-{build_il2cpp.COMMIT}/Il2CppDumper/Il2CppDumper.csproj")
                member.size = len(b"<Project/>")
                tar.addfile(member, io.BytesIO(b"<Project/>"))
            sha = host_tools.digest(archive)
            target = root / "tools/il2cpp"
            target.mkdir(parents=True)
            (target / "config.json").write_text("keep repository config")
            def publish(*args, **kwargs):
                self.assertIn("-p:UseAppHost=false", args)
                self.assertIn("-p:TargetFrameworks=net6.0", args)
                output = args[args.index("-o") + 1]
                output.mkdir()
                for name in ("Il2CppDumper.dll", "Mono.Cecil.dll", "Il2CppDumper.deps.json", "Il2CppDumper.runtimeconfig.json"):
                    (output / name).write_bytes(b"managed fixture")
            with patch.object(build_il2cpp, "ROOT", root), patch.object(build_il2cpp, "SHA256", sha), \
                 patch.object(build_il2cpp, "dotnet_sdk", return_value="C:/Program Files/dotnet/dotnet.exe"), \
                 patch.object(build_il2cpp, "run", side_effect=publish):
                build_il2cpp.build()
                self.assertTrue(build_il2cpp.installed())
                self.assertEqual((target / "config.json").read_text(), "keep repository config")
                (target / "Mono.Cecil.dll").write_bytes(b"changed assembly")
                self.assertFalse(build_il2cpp.installed())


if __name__ == "__main__":
    unittest.main()

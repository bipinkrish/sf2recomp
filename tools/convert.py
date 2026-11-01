#!/usr/bin/env python3
"""Build a native Linux, macOS or Windows SF2 folder from local game files."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from game_metadata import APP_ID, parse_nacp
from setup_ffmpeg import ARCHIVE_NAME as FFMPEG_ARCHIVE_NAME, VERSION as FFMPEG_VERSION
from setup_fonts import FONT_DIRECTORY, prepare as prepare_fonts
from windows_dlls import dependency_closure
from analyze_il2cpp import prepare_symbols
from host_tools import host_target, require_host
from macos_dylibs import relocate as relocate_macos


def run(*command, env=None):
    subprocess.run([part.as_posix() if isinstance(part, Path) else str(part) for part in command],
                   cwd=ROOT, check=True, **({"env": env} if env is not None else {}))


def source_ids(game):
    ids = {}
    for name in ("main", "sdk"):
        with (game / "exefs" / name).open("rb") as stream:
            header = stream.read(0x60)
        if len(header) != 0x60 or header[:4] != b"NSO0":
            raise ValueError(f"exefs/{name} is not a valid NSO")
        ids[name] = header[0x40:0x60].hex()
    # Hooks and IL2CPP metadata layout have only been validated for this update.
    if ids["main"] != "e15fc665ec42684aa6edd1ae03624d96".ljust(64, "0"):
        raise ValueError("Supported input: Shadow Fight 2 update v131072.")
    if not (game / "romfs/Data/Managed/Metadata/global-metadata.dat").is_file():
        raise ValueError("The extracted updated RomFS/IL2CPP metadata is missing.")
    return ids




def desktop_text(text):
    """Escape the desktop-entry string format independently of command quoting."""
    return str(text).replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def desktop_exec(path):
    # Exec has a second quoting layer; percent signs are field-code escapes.
    if "=" in str(path):
        raise ValueError("desktop executable paths cannot contain '='; choose another output path")
    quoted = "".join("\\" + c if c in '\\"`$' else c for c in str(path))
    return desktop_text('"' + quoted.replace("%", "%%") + '"')


def write_desktop(output, info):
    desktop = output / (APP_ID + ".desktop")
    desktop.write_text(
        "[Desktop Entry]\nVersion=1.0\nType=Application\n"
        "Name=Shadow Fight 2 (sf2recomp)\n"
        f"Exec={desktop_exec(output / 'sf2')}\n"
        f"Path={desktop_text(output)}\nIcon={desktop_text(output / 'icon.png')}\n"
        "Terminal=false\nCategories=Game;\n"
        f"StartupWMClass={APP_ID}\nX-AppVersion={desktop_text(info['display_version'])}\n")
    desktop.chmod(0o755)
    return desktop


def install_desktop(output):
    """Explicit opt-in: register this local output in the user's application menu."""
    data = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    if not data.is_absolute():
        raise ValueError("XDG_DATA_HOME must be an absolute path")
    applications = data / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    target = applications / (APP_ID + ".desktop")
    shutil.copy2(output.resolve() / (APP_ID + ".desktop"), target)
    print(f"Registered user application: {target}")
    return target


def package_game(game, output, executable, sdl_dir, ffmpeg_dir, target="linux"):
    """Copy only runtime inputs; keys, tickets, compiler outputs and tools stay out."""
    if target not in {"linux", "windows", "macos"}:
        raise ValueError(f"Unsupported package target: {target}")
    windows = target == "windows"
    macos = target == "macos"
    exe_name = "sf2.exe" if windows else "sf2"
    decoder_name = "ffmpeg.exe" if windows else "ffmpeg"
    source_ids(game)
    output = output.resolve()
    if output == ROOT or ROOT.is_relative_to(output) or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError("Output must not overlap the source repository or extracted game directory.")
    if not (game / "control.nacp").is_file() or not (game / "icon.png").is_file():
        raise ValueError("Official control.nacp/icon.png are missing; rerun tools/extract.sh with your NSP and keys.")
    info = parse_nacp((game / "control.nacp").read_bytes())
    if target == "linux":
        desktop_exec(output / "sf2")  # Validate launcher compatibility before copying files.
    # This checks the actual linked build IDs without opening a graphical window.
    run(executable, "--check-game", game)
    ffmpeg_executable = ffmpeg_dir / "bin" / decoder_name
    ffmpeg_build = ffmpeg_dir / "share/ffmpeg/BUILD.txt"
    ffmpeg_source = ffmpeg_dir / "share/ffmpeg" / FFMPEG_ARCHIVE_NAME
    ffmpeg_license = ffmpeg_dir / "share/licenses/FFmpeg"
    ffmpeg_libraries = (sorted(ffmpeg_dir.glob("bin/*.dll")) if windows else
                        sorted({*ffmpeg_dir.glob("lib/libav*.dylib"), *ffmpeg_dir.glob("lib/libsw*.dylib")}) if macos else
                        sorted({*ffmpeg_dir.glob("lib/libav*.so*"), *ffmpeg_dir.glob("lib/libsw*.so*")}))
    if (not ffmpeg_executable.is_file() or not ffmpeg_build.is_file() or not ffmpeg_source.is_file()
            or not ffmpeg_license.is_dir() or not ffmpeg_libraries):
        raise ValueError("The bundled FFmpeg build is incomplete; run tools/setup_ffmpeg.py --build.")
    prepare_fonts(FONT_DIRECTORY, check=True)
    # Resolve the complete PE import graph before touching an existing output.
    # Each executable has its own DLL search directory; decoder DLLs stay beside
    # ffmpeg.exe, and application DLLs stay beside sf2.exe.
    game_dlls, decoder_dlls = {}, {}
    if windows:
        search = [executable.parent, sdl_dir / "bin"]
        game_dlls = dependency_closure([executable], search)
        decoder_dlls = dependency_closure([ffmpeg_executable], [ffmpeg_dir / "bin", *search])
    output.mkdir(parents=True, exist_ok=True)
    exefs = output / "extracted/exefs"
    exefs.mkdir(parents=True, exist_ok=True)
    for name in ("main", "sdk", "main.npdm"):
        shutil.copy2(game / "exefs" / name, exefs / name)
    shutil.copytree(game / "romfs", output / "extracted/romfs", dirs_exist_ok=True)
    # Install the executable after its data. Existing user settings and saves stay.
    shutil.copy2(executable, output / exe_name)
    # Earlier conversions used the compiler project's name for the game binary.
    # Remove that generated file when refreshing an existing converted folder.
    legacy = output / "sf2recomp"
    if legacy.is_file() and not legacy.is_symlink():
        legacy.unlink()
    for name in ("control.nacp", "icon.png"):
        shutil.copy2(game / name, output / name)
    (output / "game-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if target == "linux":
        write_desktop(output, info)
    config = output / "sf2recomp.ini"
    if not config.exists():
        shutil.copy2(ROOT / "sf2recomp.ini", config)
    notices = output / "licenses"
    notices.mkdir(exist_ok=True)
    for name, path in {
        "PROJECT-LICENSE.txt": ROOT / "LICENSE",
        "THIRD_PARTY_NOTICES.txt": ROOT / "THIRD_PARTY_NOTICES",
    }.items():
        shutil.copy2(path, notices / name)
    for path in (ROOT / "licenses").glob("*.txt"):
        shutil.copy2(path, notices / path.name)
    # Retire duplicate filenames written before license texts were centralized.
    for old_name in ("LAS-RUNTIME-LICENSE.txt", "SMAA-LICENSE.txt"):
        (notices / old_name).unlink(missing_ok=True)
    # Local shared SDL is needed alongside the executable; OS/driver libraries
    # remain supplied by the user's Linux installation. Static SDL needs no copy.
    libraries = [] if windows else list((sdl_dir / "lib").glob("libSDL3*.dylib" if macos else "libSDL3.so*"))
    if libraries:
        lib = output / "lib"
        lib.mkdir(exist_ok=True)
        for path in libraries:
            destination = lib / path.name
            if destination.is_symlink():
                destination.unlink()
            if path.is_symlink():
                # Keep aliases relative inside the output, avoiding duplicate data
                # or symlinks back to the build machine's dependency prefix.
                if destination.exists():
                    destination.unlink()
                destination.symlink_to(path.resolve().name)
            else:
                shutil.copy2(path, destination)
    # Converted outputs carry the small LGPL-only decoder and its matching shared libraries. The game never asks the
    # player to install FFmpeg or find it on PATH.
    ffmpeg_bundle = output / "lib/ffmpeg"
    if ffmpeg_bundle.exists():
        shutil.rmtree(ffmpeg_bundle)
    ffmpeg_bin = ffmpeg_bundle / "bin"
    ffmpeg_lib = ffmpeg_bundle / "lib"
    ffmpeg_bin.mkdir(parents=True)
    ffmpeg_lib.mkdir(parents=True)
    shutil.copy2(ffmpeg_executable, ffmpeg_bin / decoder_name)
    if windows:
        for path in game_dlls.values():
            shutil.copy2(path, output / path.name)
        for path in decoder_dlls.values():
            shutil.copy2(path, ffmpeg_bin / path.name)
    for path in ([] if windows else ffmpeg_libraries):
        destination = ffmpeg_lib / path.name
        if path.is_symlink():
            destination.symlink_to(path.resolve().name)
        else:
            shutil.copy2(path, destination)
    ffmpeg_notices = notices / "FFmpeg"
    if ffmpeg_notices.exists():
        shutil.rmtree(ffmpeg_notices)
    shutil.copytree(ffmpeg_license, ffmpeg_notices)
    shutil.copy2(ffmpeg_source, ffmpeg_notices / f"ffmpeg-{FFMPEG_VERSION}-source.tar.xz")
    shutil.copy2(ffmpeg_build, ffmpeg_notices / "BUILD.txt")
    if macos:
        relocate_macos([output / exe_name, ffmpeg_bin / decoder_name],
                       [*(output / "lib" / p.name for p in libraries),
                        *(ffmpeg_lib / p.name for p in ffmpeg_libraries)])
    shutil.copytree(FONT_DIRECTORY, output / "lib/fonts", dirs_exist_ok=True)
    font_notices = notices / "Noto"
    font_notices.mkdir(exist_ok=True)
    for name in ("NotoSans-LICENSE.txt", "NotoSansCJK-LICENSE.txt", "SOURCE.txt"):
        shutil.copy2(FONT_DIRECTORY / name, font_notices / name)
    (output / "build.json").write_text(json.dumps({
        "target": target, "executable": exe_name, "source_build_ids": source_ids(game),
        "windows_validation": "experimental; hardware playtesting required" if windows else None,
        "macos_validation": "experimental; native build and hardware playtesting required" if macos else None,
        "official_title": info["title"], "official_display_version": info["display_version"],
        "configuration": "sf2recomp.ini (read at runtime)",
        "cutscene_decoder": f"FFmpeg {FFMPEG_VERSION} (bundled, LGPL-only build)",
    }, indent=2) + "\n")
    if windows:
        # Keep dependency notices from the exact installed build prefix.
        native_notices = sdl_dir / "share/licenses"
        if native_notices.is_dir():
            shutil.copytree(native_notices, notices / "windows-dependencies", dirs_exist_ok=True)
        (output / "windows-dlls.json").write_text(json.dumps({
            "application": sorted(game_dlls), "decoder": sorted(decoder_dlls),
            "system_vulkan_loader": "provided by the installed GPU driver",
        }, indent=2) + "\n")
        (output / "RUN.txt").write_text(
            "Run sf2.exe. Keep extracted/, lib/, and the packaged DLLs beside it.\n"
            "Edit sf2recomp.ini, then restart to apply settings. Saves/config are preserved on rebuild.\n"
            "Requirements: Windows 10 1903+ x64 or Windows 11; x64 CPU with CMPXCHG16B; Vulkan 1.3 GPU driver.\n"
            "Integrated and dedicated GPUs use the same Vulkan renderer; Windows GPU compatibility needs testing.\n"
            "MSYS2, Python and build tools are only needed for conversion, not to run this folder.\n"
            "Cutscene decoder, matching source/licenses and fallback fonts are bundled.\n"
            "This Windows port is experimental and has not yet been validated on Windows hardware.\n"
        )
    elif macos:
        (output / "RUN.txt").write_text(
            "Run ./sf2 from Terminal. Keep extracted/, lib/ and sf2recomp.ini beside it.\n"
            "This is a native folder build for the conversion host's architecture, not a universal app.\n"
            "Requires macOS 13+ (or the minimum required by your installed dependencies), "
            "Vulkan 1.3+ loader, MoltenVK and shaderc from the build's Homebrew/SDK installation.\n"
            "SDL and the LGPL-only FFmpeg decoder/source/licenses are bundled when built locally.\n"
            "Saves/config are preserved on rebuild. No Python/.NET SDK is needed to play.\n"
            "Experimental port: native compilation and GPU/gameplay compatibility remain unverified.\n"
        )
    else:
        (output / "RUN.txt").write_text(
            "Run ./sf2 from this folder or any working directory.\n"
            f"Official game: {info['title']} {info['display_version']}.\n"
            f"Desktop launcher: {APP_ID}.desktop (local paths; regenerate if moved).\n"
            "Edit sf2recomp.ini for language/audio/video/keyboard/gamepad mappings; restart to apply.\n"
            "Keep extracted/ beside the executable. This output contains your local game-derived files.\n"
            "Cutscene playback uses the FFmpeg decoder and source bundled in lib/ffmpeg and licenses/FFmpeg; no FFmpeg install is needed.\n"
            "Keep lib/fonts/ alongside the executable for shared-font fallback (Noto, SIL OFL 1.1).\n"
            "System requirements: Linux x86-64, Vulkan loader/driver, shaderc and C/C++ runtime libraries.\n"
            "Software fallback: SDL_VIDEODRIVER=x11 LARECOMP_VK_NO_GMEM=1 "
            "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./sf2\n"
            "Combat and hardware-driver compatibility remain incomplete.\n")
    with (output / "RUN.txt").open("a") as instructions:
        instructions.write(
            "LAN Sparring: start the same build on both computers on the same local network.\n"
            "Choose Sparring > LAN Multiplayer > Host on one and Join on the other; select the discovered host.\n"
            "Each player uses their normal local player-1 controls. Restart keeps the connection; leaving Sparring closes it.\n"
            "No Python or pip packages are required to play LAN. Native LAN is enabled by default; "
            "[gameplay] allow_sparring_lan_multiplayer=false restores direct Normal Sparring.\n")
    print(f"Native game folder: {output}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--nsp", type=Path, help="supported combined base + update v131072 NSP")
    source.add_argument("--game", type=Path, help="already extracted updated game directory")
    ap.add_argument("--keys", type=Path, help="your local prod.keys, required with --nsp")
    ap.add_argument("--target", choices=("linux", "windows", "macos"), default=host_target())
    ap.add_argument("--output", type=Path)
    ap.add_argument("--symbols", type=Path, help="reuse an existing sym_main.txt instead of running local analysis")
    ap.add_argument("--skip-generation", action="store_true", help="compile existing game/gen files; build identity is still checked")
    ap.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    ap.add_argument("--package-only", action="store_true", help="package an existing matching local build")
    ap.add_argument("--install-desktop", action="store_true",
                    help="explicitly register the output launcher in your user application menu")
    args = ap.parse_args(argv)
    try:
        windows = args.target == "windows"
        require_host(args.target)
        if args.target != "linux" and args.install_desktop:
            raise ValueError("--install-desktop is a Linux option.")
        if args.package_only and args.skip_generation:
            raise ValueError("--skip-generation is unnecessary with --package-only")
        if args.output is None:
            args.output = ROOT / f"out/shadow-fight-2-{args.target}"
        if args.jobs < 1:
            raise ValueError("--jobs must be positive")
        if args.nsp:
            if not args.keys:
                raise ValueError("--nsp requires --keys pointing to your locally obtained prod.keys")
            if args.package_only:
                raise ValueError("--package-only requires --game")
            run(sys.executable, ROOT / "tools/extract.py", args.nsp.resolve(), args.keys.resolve(), "--validate-only")
            run(sys.executable, ROOT / "tools/extract.py", args.nsp.resolve(), args.keys.resolve())
            game = ROOT / "game/extracted/update"
        else:
            game = args.game.resolve(strict=True)
        source_ids(game)
        prefix = os.environ.get("SDL_DIR")
        if windows and not prefix:
            prefix = subprocess.check_output(["cygpath", "-m", os.environ.get("MINGW_PREFIX", "/ucrt64")], text=True).strip()
        sdl_dir = Path(prefix or ROOT / ("deps/macos" if args.target == "macos" else "deps")).resolve()
        ffmpeg_dir = Path(os.environ.get("FFMPEG_DIR", ROOT / (
            "deps/ffmpeg" if args.target == "linux" else f"deps/ffmpeg-{args.target}"))).resolve()
        if not args.package_only and not args.skip_generation:
            missing = [name for name in ("capstone", "unicorn", "lz4", "cryptography", "PIL")
                       if importlib.util.find_spec(name) is None]
            if missing:
                raise ValueError("Install build dependencies: python3 -m pip install -r tools/requirements.txt")
        if not args.package_only:
            environment = os.environ.copy()
            environment.update(TARGET=args.target, SDL_DIR=sdl_dir.as_posix(), PYTHON=Path(sys.executable).as_posix())
            run("bash", ROOT / "tools/setup_runtime.sh", "--ensure", env=environment)
        run(sys.executable, ROOT / "tools/setup_ffmpeg.py", "--ensure", "--target", args.target)
        if not args.package_only and not args.skip_generation:
            if args.symbols:
                symbols = args.symbols.resolve(strict=True)
            else:
                symbols = prepare_symbols(game)
            run(sys.executable, ROOT / "tools/recompr/gen.py", "main", game / "exefs/main",
                "0x7000000000", ROOT / "game/gen/main", "--symbols", symbols)
            run(sys.executable, ROOT / "tools/recompr/gen.py", "sdk", game / "exefs/sdk",
                "0x7004000000", ROOT / "game/gen/sdk")
        if not args.package_only:
            make = "gmake" if args.target == "macos" else "make"
            run(make, f"-j{args.jobs}", f"TARGET={args.target}", f"SDL_DIR={sdl_dir.as_posix()}",
                f"PYTHON={Path(sys.executable).as_posix()}")
        executable = ROOT / ("build/windows/sf2.exe" if windows else
                             "build/macos/sf2" if args.target == "macos" else "build/sf2")
        package_game(game, args.output, executable, sdl_dir, ffmpeg_dir, args.target)
        if args.install_desktop:
            install_desktop(args.output)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

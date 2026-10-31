#!/usr/bin/env python3
"""Build a native Linux SF2 folder locally from the user's supported game files."""
import argparse
import hashlib
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


def run(*command):
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True)


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


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def prepare_symbols(game):
    symbols = ROOT / "game/il2cpp"
    symbols.mkdir(parents=True, exist_ok=True)
    stamp = symbols / "source.json"
    inputs = {
        "main": digest(game / "exefs/main"),
        "metadata": digest(game / "romfs/Data/Managed/Metadata/global-metadata.dat"),
    }
    if stamp.is_file() and (symbols / "sym_main.txt").is_file():
        try:
            previous = json.loads(stamp.read_text())
            if previous == {**inputs, "symbols": digest(symbols / "sym_main.txt")}:
                return symbols / "sym_main.txt"
        except (ValueError, OSError):
            pass
    uncompressed = game / "exefs/main.uncompressed"
    # Recreate this intermediate from the validated input, never reuse a stale one.
    if not (ROOT / "tools/bin/hactool").is_file():
        run("sh", ROOT / "tools/build_extraction_tools.sh")
    run(ROOT / "tools/bin/hactool", "-t", "nso0", "--disablekeywarns",
        f"--uncompressed={uncompressed}", game / "exefs/main")
    if not (ROOT / "tools/il2cpp/Il2CppDumper.dll").is_file():
        run(sys.executable, ROOT / "tools/build_il2cpp.py")
    run("sh", ROOT / "tools/il2cpp_run.sh", game, symbols)
    inputs["symbols"] = digest(symbols / "sym_main.txt")
    stamp.write_text(json.dumps(inputs, indent=2) + "\n")
    return symbols / "sym_main.txt"



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


def package_game(game, output, executable, sdl_dir, ffmpeg_dir):
    """Copy only runtime inputs; keys, tickets, compiler outputs and tools stay out."""
    source_ids(game)
    output = output.resolve()
    if output == ROOT or ROOT.is_relative_to(output) or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError("Output must not overlap the source repository or extracted game directory.")
    if not (game / "control.nacp").is_file() or not (game / "icon.png").is_file():
        raise ValueError("Official control.nacp/icon.png are missing; rerun tools/extract.sh with your NSP and keys.")
    info = parse_nacp((game / "control.nacp").read_bytes())
    desktop_exec(output / "sf2")  # Validate launcher compatibility before copying files.
    # This checks the actual linked build IDs without opening a graphical window.
    run(executable, "--check-game", game)
    ffmpeg_executable = ffmpeg_dir / "bin/ffmpeg"
    ffmpeg_build = ffmpeg_dir / "share/ffmpeg/BUILD.txt"
    ffmpeg_source = ffmpeg_dir / "share/ffmpeg" / FFMPEG_ARCHIVE_NAME
    ffmpeg_license = ffmpeg_dir / "share/licenses/FFmpeg"
    ffmpeg_libraries = sorted({*ffmpeg_dir.glob("lib/libav*.so*"), *ffmpeg_dir.glob("lib/libsw*.so*")})
    if (not ffmpeg_executable.is_file() or not ffmpeg_build.is_file() or not ffmpeg_source.is_file()
            or not ffmpeg_license.is_dir() or not ffmpeg_libraries):
        raise ValueError("The bundled FFmpeg build is incomplete; run tools/setup_ffmpeg.py --build.")
    output.mkdir(parents=True, exist_ok=True)
    exefs = output / "extracted/exefs"
    exefs.mkdir(parents=True, exist_ok=True)
    for name in ("main", "sdk", "main.npdm"):
        shutil.copy2(game / "exefs" / name, exefs / name)
    shutil.copytree(game / "romfs", output / "extracted/romfs", dirs_exist_ok=True)
    # Install the executable after its data. Existing user settings and saves stay.
    shutil.copy2(executable, output / "sf2")
    # Earlier conversions used the compiler project's name for the game binary.
    # Remove that generated file when refreshing an existing converted folder.
    legacy = output / "sf2recomp"
    if legacy.is_file() and not legacy.is_symlink():
        legacy.unlink()
    for name in ("control.nacp", "icon.png"):
        shutil.copy2(game / name, output / name)
    (output / "game-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n")
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
    libraries = list((sdl_dir / "lib").glob("libSDL3.so*"))
    if libraries:
        lib = output / "lib"
        lib.mkdir(exist_ok=True)
        for path in libraries:
            target = lib / path.name
            if target.is_symlink():
                target.unlink()
            if path.is_symlink():
                # Keep aliases relative inside the output, avoiding duplicate data
                # or symlinks back to the build machine's dependency prefix.
                if target.exists():
                    target.unlink()
                target.symlink_to(path.resolve().name)
            else:
                shutil.copy2(path, target)
    # Converted outputs carry the small LGPL-only decoder and its matching shared libraries. The game never asks the
    # player to install FFmpeg or find it on PATH.
    ffmpeg_bundle = output / "lib/ffmpeg"
    if ffmpeg_bundle.exists():
        shutil.rmtree(ffmpeg_bundle)
    ffmpeg_bin = ffmpeg_bundle / "bin"
    ffmpeg_lib = ffmpeg_bundle / "lib"
    ffmpeg_bin.mkdir(parents=True)
    ffmpeg_lib.mkdir(parents=True)
    shutil.copy2(ffmpeg_executable, ffmpeg_bin / "ffmpeg")
    for path in ffmpeg_libraries:
        target = ffmpeg_lib / path.name
        if path.is_symlink():
            target.symlink_to(path.resolve().name)
        else:
            shutil.copy2(path, target)
    ffmpeg_notices = notices / "FFmpeg"
    if ffmpeg_notices.exists():
        shutil.rmtree(ffmpeg_notices)
    shutil.copytree(ffmpeg_license, ffmpeg_notices)
    shutil.copy2(ffmpeg_source, ffmpeg_notices / f"ffmpeg-{FFMPEG_VERSION}-source.tar.xz")
    shutil.copy2(ffmpeg_build, ffmpeg_notices / "BUILD.txt")
    (output / "build.json").write_text(json.dumps({
        "target": "linux", "executable": "sf2", "source_build_ids": source_ids(game),
        "official_title": info["title"], "official_display_version": info["display_version"],
        "configuration": "sf2recomp.ini (read at runtime)",
        "cutscene_decoder": f"FFmpeg {FFMPEG_VERSION} (bundled, LGPL-only build)",
    }, indent=2) + "\n")
    (output / "RUN.txt").write_text(
        "Run ./sf2 from this folder or any working directory.\n"
        f"Official game: {info['title']} {info['display_version']}.\n"
        f"Desktop launcher: {APP_ID}.desktop (local paths; regenerate if moved).\n"
        "Edit sf2recomp.ini for language/audio/video/keyboard/gamepad mappings; restart to apply.\n"
        "Keep extracted/ beside the executable. This output contains your local game-derived files.\n"
        "Cutscene playback uses the FFmpeg decoder and source bundled in lib/ffmpeg and licenses/FFmpeg; no FFmpeg install is needed.\n"
        "System requirements: Linux x86-64, Vulkan loader/driver, shaderc and C/C++ runtime libraries.\n"
        "Software fallback: SDL_VIDEODRIVER=x11 LARECOMP_VK_NO_GMEM=1 "
        "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./sf2\n"
        "Combat and hardware-driver compatibility remain incomplete.\n")
    print(f"Native game folder: {output}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--nsp", type=Path, help="supported combined base + update v131072 NSP")
    source.add_argument("--game", type=Path, help="already extracted updated game directory")
    ap.add_argument("--keys", type=Path, help="your local prod.keys, required with --nsp")
    ap.add_argument("--output", type=Path, default=ROOT / "out/shadow-fight-2-linux")
    ap.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    ap.add_argument("--package-only", action="store_true", help="package an existing matching local build")
    ap.add_argument("--install-desktop", action="store_true",
                    help="explicitly register the output launcher in your user application menu")
    args = ap.parse_args(argv)
    try:
        if args.jobs < 1:
            raise ValueError("--jobs must be positive")
        if args.nsp:
            if not args.keys:
                raise ValueError("--nsp requires --keys pointing to your locally obtained prod.keys")
            if args.package_only:
                raise ValueError("--package-only requires --game")
            run("sh", ROOT / "tools/extract.sh", args.nsp.resolve(), args.keys.resolve(), "--validate-only")
            if not all((ROOT / "tools/bin" / name).is_file() for name in ("hactool", "nstool")):
                run("sh", ROOT / "tools/build_extraction_tools.sh")
            run("sh", ROOT / "tools/extract.sh", args.nsp.resolve(), args.keys.resolve())
            game = ROOT / "game/extracted/update"
        else:
            game = args.game.resolve(strict=True)
        source_ids(game)
        sdl_dir = Path(os.environ.get("SDL_DIR", ROOT / "deps")).resolve()
        ffmpeg_dir = Path(os.environ.get("FFMPEG_DIR", ROOT / "deps/ffmpeg")).resolve()
        if not args.package_only:
            missing = [name for name in ("capstone", "unicorn", "lz4", "cryptography", "PIL")
                       if importlib.util.find_spec(name) is None]
            if missing:
                raise ValueError("Install build dependencies: python3 -m pip install -r tools/requirements.txt")
            run(ROOT / "tools/setup_runtime.sh", "--ensure")
        run(sys.executable, ROOT / "tools/setup_ffmpeg.py", "--ensure")
        if not args.package_only:
            symbols = prepare_symbols(game)
            run(sys.executable, ROOT / "tools/recompr/gen.py", "main", game / "exefs/main",
                "0x7000000000", ROOT / "game/gen/main", "--symbols", symbols)
            run(sys.executable, ROOT / "tools/recompr/gen.py", "sdk", game / "exefs/sdk",
                "0x7004000000", ROOT / "game/gen/sdk")
            run("make", f"-j{args.jobs}", f"SDL_DIR={sdl_dir}")
        package_game(game, args.output, ROOT / "build/sf2", sdl_dir, ffmpeg_dir)
        if args.install_desktop:
            install_desktop(args.output)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

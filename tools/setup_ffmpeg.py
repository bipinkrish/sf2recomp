#!/usr/bin/env python3
"""Build the small, LGPL-only FFmpeg decoder for the native host target."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from host_tools import host_target, require_host

ROOT = Path(__file__).resolve().parent.parent
VERSION = "9.0.1"
ARCHIVE_NAME = f"ffmpeg-{VERSION}.tar.xz"
ARCHIVE_URL = f"https://ffmpeg.org/releases/{ARCHIVE_NAME}"
ARCHIVE_SHA256 = "cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635"
CONFIGURE = [
    "--disable-gpl", "--disable-nonfree", "--disable-version3",
    "--disable-autodetect", "--disable-network", "--disable-doc", "--disable-debug",
    "--disable-static", "--enable-shared", "--enable-ffmpeg", "--disable-ffplay", "--disable-ffprobe",
    "--disable-everything",
    "--enable-decoder=h264,aac", "--enable-parser=h264,aac", "--enable-demuxer=mov",
    "--enable-protocol=file,pipe", "--enable-encoder=rawvideo,pcm_s16le",
    "--enable-muxer=rawvideo,pcm_s16le", "--enable-filter=scale,fps,format,aresample",
    "--enable-swscale", "--enable-swresample", "--enable-pthreads",
]


def paths(target="linux"):
    default = ROOT / ("deps/ffmpeg" if target == "linux" else f"deps/ffmpeg-{target}")
    prefix = Path(os.environ.get("FFMPEG_DIR", default)).resolve()
    work = Path(os.environ.get("SF2_DEPS_WORK", ROOT / "build/dependencies")).resolve()
    return prefix, work


def installed(prefix, target="linux"):
    marker = prefix / "share/ffmpeg/BUILD.txt"
    return (
        (prefix / ("bin/ffmpeg.exe" if target == "windows" else "bin/ffmpeg")).is_file()
        and any(p.is_file() for p in prefix.glob("bin/avcodec-*.dll" if target == "windows" else ("lib/libavcodec.*.dylib" if target == "macos" else "lib/libavcodec.so.*")))
        and marker.is_file()
        and f"FFmpeg {VERSION}" in marker.read_text()
        and (prefix / "share/ffmpeg" / ARCHIVE_NAME).is_file()
        and (prefix / "share/licenses/FFmpeg/COPYING.LGPLv2.1").is_file()
        and (prefix / "share/licenses/FFmpeg/LICENSE.md").is_file()
    )


def source_archive(work):
    archive = Path(os.environ.get("FFMPEG_SOURCE_ARCHIVE", work / "archives" / ARCHIVE_NAME)).resolve()
    if not archive.is_file():
        if os.environ.get("FFMPEG_SOURCE_ARCHIVE"):
            raise FileNotFoundError(f"FFMPEG_SOURCE_ARCHIVE not found: {archive}")
        if not shutil.which("curl"):
            raise RuntimeError("Missing curl; provide FFMPEG_SOURCE_ARCHIVE for an offline build.")
        archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = archive.with_suffix(archive.suffix + ".part")
        subprocess.run([
            "curl", "--fail", "--location", "--proto", "=https", "--tlsv1.2",
            "--connect-timeout", "20", "--max-time", "180", "--retry", "2", "--continue-at", "-",
            ARCHIVE_URL, "-o", str(temporary),
        ], check=True)
        temporary.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != ARCHIVE_SHA256:
        raise ValueError(f"FFmpeg source archive checksum mismatch: {archive}")
    return archive


def build(prefix, work, force=False, target="linux"):
    marker = prefix / "share/ffmpeg/BUILD.txt"
    if not force and installed(prefix, target):
        print(f"Bundled FFmpeg {VERSION} is ready: {prefix}")
        return

    for command in ("clang", "make", "tar"):
        if not shutil.which(command):
            raise RuntimeError(f"Missing build tool: {command}")
    archive = source_archive(work)
    source = work / f"ffmpeg-{VERSION}-{target}-source"
    if source.exists():
        shutil.rmtree(source)
    source.mkdir(parents=True)
    subprocess.run([
        "tar", *(["--force-local"] if target == "windows" else []), "-xJf", archive.as_posix(), "--strip-components=1", "-C", source.as_posix(),
    ], check=True)

    config = ["--cc=clang", f"--prefix={prefix.as_posix()}", f"--bindir={(prefix / 'bin').as_posix()}",
              f"--libdir={(prefix / 'lib').as_posix()}", *CONFIGURE]
    if target == "macos":
        minimum = os.environ.get("MACOS_MIN", "13.0")
        config += ["--target-os=darwin", "--install-name-dir=@rpath",
                   f"--extra-cflags=-mmacosx-version-min={minimum}",
                   f"--extra-ldflags=-mmacosx-version-min={minimum} -Wl,-headerpad_max_install_names"]
    if target == "windows":
        config.remove("--enable-pthreads")
        config += ["--target-os=mingw32", "--arch=x86_64", "--enable-w32threads"]
    if not shutil.which("nasm") and not shutil.which("yasm"):
        config.append("--disable-x86asm")
        print("nasm/yasm not found; building FFmpeg without x86 assembly optimizations.")
    subprocess.run(["bash", (source / "configure").as_posix(), *config], cwd=source, check=True)
    jobs = int(os.environ.get("JOBS", str(min(os.cpu_count() or 1, 8))))
    if jobs < 1:
        raise ValueError("JOBS must be a positive integer")
    subprocess.run(["make", f"-j{jobs}"], cwd=source, check=True)
    subprocess.run(["make", "install"], cwd=source, check=True)

    share = prefix / "share/ffmpeg"
    licenses = prefix / "share/licenses/FFmpeg"
    share.mkdir(parents=True, exist_ok=True)
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, share / ARCHIVE_NAME)
    shutil.copy2(source / "COPYING.LGPLv2.1", licenses / "COPYING.LGPLv2.1")
    shutil.copy2(source / "LICENSE.md", licenses / "LICENSE.md")
    (share / "BUILD.txt").write_text(
        f"FFmpeg {VERSION}\nSource: {ARCHIVE_URL}\nSHA256: {ARCHIVE_SHA256}\n"
        "License: LGPL 2.1 or later; built without GPL or nonfree components.\n"
        "Configure options:\n  " + " ".join(
            option for option in config
            if not option.startswith(("--prefix=", "--bindir=", "--libdir="))
        ) + "\n"
    )
    if not installed(prefix, target):
        raise RuntimeError(f"FFmpeg install is incomplete: {prefix}")
    print(f"Bundled FFmpeg {VERSION} built at {prefix}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("linux", "windows", "macos"), default=host_target())
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check for the built decoder without downloading")
    mode.add_argument("--build", action="store_true", help="build FFmpeg from the pinned upstream source")
    mode.add_argument("--ensure", action="store_true", help="build the decoder only when it is missing")
    parser.add_argument("--help-env", action="store_true", help="show supported environment overrides")
    args = parser.parse_args(argv)
    if args.help_env:
        print("FFMPEG_DIR changes the local install prefix.\n"
              "FFMPEG_SOURCE_ARCHIVE supplies the pinned source archive for offline setup.\n"
              "SF2_DEPS_WORK changes the source/build cache. JOBS controls parallel compilation.")
        return 0
    prefix, work = paths(args.target)
    try:
        require_host(args.target)
        if args.target == "windows" and (os.name != "nt" or os.environ.get("MSYSTEM") != "UCRT64"):
            raise ValueError("Build Windows FFmpeg in the MSYS2 UCRT64 shell on Windows x64.")
        if args.check:
            if not installed(prefix, args.target):
                raise RuntimeError(f"Bundled FFmpeg is missing; run {Path(__file__).name} --build")
            print(f"Bundled FFmpeg found: {prefix}")
        elif args.ensure:
            build(prefix, work, target=args.target)
        else:
            build(prefix, work, force=args.build, target=args.target)
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"FFmpeg setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

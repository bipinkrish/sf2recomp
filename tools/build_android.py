#!/usr/bin/env python3
"""Prepare pinned Android dependencies and build the locally generated game APK."""
import argparse
import hashlib
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/dependencies"
SOURCES = {
    "shaderc": ("google/shaderc", "v2025.4", "8a89fb6612ace8954470aae004623374a8fc8b7a34a4277bee5527173b064faf"),
    "glslang": ("KhronosGroup/glslang", "d213562e35573012b6348b2d584457c3704ac09b", "9537e5896d10bc493775675153687e078d83aead3a5a3b90e9ac28af2bc966fc"),
    "spirv-tools": ("KhronosGroup/SPIRV-Tools", "19042c8921f35f7bec56b9e5c96c5f5691588ca8", "7ab19030df38a50cc93be26ebd3913c3e7ef3a118e1ded65a829e63b0c7ccf12"),
    "spirv-headers": ("KhronosGroup/SPIRV-Headers", "01e0577914a75a2569c846778c2f93aa8e6feddd", "494bd30dc13ba798af70edd989f8df82c90757c8ce9433598480f5e00e04c454"),
    "vma": ("GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator", "v3.2.1", "5e7749504cb802427ffb7bec38a0b6a15db46ae253f00560acb3e624d9fe695c"),
}


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def source(archive, destination, url, digest):
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(".part")
        run("curl", "--fail", "--location", "--proto", "=https", "--tlsv1.2",
            "--connect-timeout", "20", "--max-time", "180", "--retry", "2", url, "-o", temporary)
        temporary.replace(archive)
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != digest:
        raise ValueError(f"Checksum mismatch: {archive}")
    if not destination.is_dir():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            with tarfile.open(archive) as tar:
                tar.extractall(temporary, filter="data")
            roots = list(Path(temporary).iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f"Unexpected archive layout: {archive}")
            roots[0].rename(destination)


def prepare():
    version = "3.4.16"
    source(WORK / f"SDL3-{version}.tar.gz", WORK / f"SDL3-{version}",
           f"https://github.com/libsdl-org/SDL/releases/download/release-{version}/SDL3-{version}.tar.gz",
           "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68")
    for name, (repo, revision, digest) in SOURCES.items():
        source(WORK / f"android/{name}.tar.gz", WORK / f"android/{name}/source",
               f"https://codeload.github.com/{repo}/tar.gz/{revision}", digest)
    run(sys.executable, ROOT / "tools/setup_smaa.py")
    assets = ROOT / "build/android-assets"
    run(sys.executable, ROOT / "tools/setup_fonts.py", "--output", assets / "lib/fonts")
    assets.mkdir(parents=True, exist_ok=True)
    config = (ROOT / "sf2recomp.ini").read_text()
    config = config.replace("show_intro = true", "show_intro = false")
    config = config.replace("target_fps = 120", "target_fps = 60")
    config = config.replace("internal_resolution = auto", "internal_resolution = 0.5x")
    config = config.replace("player_setup = keyboard_vs_controller", "player_setup = solo")
    (assets / "sf2recomp.ini").write_text(config)
    licenses = assets / "licenses"
    licenses.mkdir(exist_ok=True)
    for name in ("LICENSE", "THIRD_PARTY_NOTICES"):
        shutil.copyfile(ROOT / name, licenses / name)
    for path in (ROOT / "licenses").glob("*"):
        if path.is_file(): shutil.copyfile(path, licenses / path.name)
    for name in SOURCES:
        src = WORK / f"android/{name}/source"
        target = licenses / name
        target.mkdir(exist_ok=True)
        for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
            for path in src.glob(pattern):
                if path.is_file(): shutil.copyfile(path, target / path.name)
    shutil.copyfile(WORK / f"SDL3-{version}/LICENSE.txt", licenses / "SDL3.txt")
    (licenses / "android-sources.txt").write_text("\n".join(
        f"{name}: https://github.com/{repo}/tree/{revision}\nArchive SHA256: {digest}"
        for name, (repo, revision, digest) in SOURCES.items()) + "\n")


def game_identity(required=True):
    identities = []
    for module in ("main", "sdk"):
        table = ROOT / f"game/gen/{module}/{module}_table.c"
        text = table.read_text() if table.is_file() else ""
        match = re.search(rf"{module}_nso_build_id\[32\]\s*=\s*\{{([^}}]+)\}}", text)
        values = re.findall(r"0x([0-9a-fA-F]{2})\b", match[1]) if match else []
        if len(values) != 32:
            if required: raise ValueError("Generate supported game code first: make gen SIMD=c")
            return
        identities.append(f"{module}={''.join(values).lower()}")
    (ROOT / "build/android-assets/game-builds.properties").write_text("\n".join(identities) + "\n")


def gradle_command():
    if os.environ.get("GRADLE"): return os.environ["GRADLE"]
    return str(ROOT / "android" / ("gradlew.bat" if os.name == "nt" else "gradlew"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", choices=("arm64-v8a", "x86_64"), default="arm64-v8a")
    parser.add_argument("--prepare", action="store_true", help="only prepare source dependencies/assets")
    parser.add_argument("--install", action="store_true", help="also install on the ADB-selected device")
    args = parser.parse_args()
    prepare()
    game_identity(required=not args.prepare)
    if args.prepare: return
    sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Android/Sdk"))
    for relative in ("platforms/android-35/android.jar", "ndk/28.2.13676358/build/cmake/android.toolchain.cmake"):
        if not (sdk / relative).exists(): raise ValueError(f"Missing Android SDK component: {sdk / relative}")
    environment = os.environ.copy()
    environment["ANDROID_HOME"] = str(sdk)
    # Repack the APK from scratch while retaining native object caches. AGP's
    # incremental ZIP writer can leave an entire old libmain.so as unused bytes
    # after a relink, doubling these large locally translated packages.
    apk = ROOT / "android/app/build/outputs/apk/debug/app-debug.apk"
    apk.unlink(missing_ok=True)
    run(gradle_command(), "--no-daemon", f"-Psf2Abi={args.abi}",
        "installDebug" if args.install else "assembleDebug", cwd=ROOT / "android", env=environment)
    output = ROOT / f"build/android/sf2-recomp-{args.abi}-debug.apk"
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(apk, output)
    print(f"APK: {output}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))

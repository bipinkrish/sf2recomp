#!/usr/bin/env python3
"""Prepare pinned Android dependencies and build the locally generated game APK."""
from _sf2_tool_constants import BUILD_ANDROID as K
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from downloads import download

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / K.S_BUILD_DEPENDENCIES_7621
SOURCES = {
    K.S_SHADERC_7D52: (K.S_GOOGLE_SHADERC_9727, K.S_V2025_4_52FB, K.S_8A89FB6612ACE8954470AAE00462_1D30),
    K.S_GLSLANG_A5C9: (K.S_KHRONOSGROUP_GLSLANG_0B19, K.S_D213562E35573012B6348B2D5844_1E42, K.S_9537E5896D10BC49377567515368_E247),
    K.S_SPIRV_TOOLS_8E6A: (K.S_KHRONOSGROUP_SPIRV_TOOLS_AAC5, K.S_19042C8921F35F7BEC56B9E5C96C_21F4, K.S_7AB19030DF38A50CC93BE26EBD39_C6B4),
    K.S_SPIRV_HEADERS_046D: (K.S_KHRONOSGROUP_SPIRV_HEADERS_44AD, K.S_01E0577914A75A2569C846778C2F_41F3, K.S_494BD30DC13BA798AF70EDD989F8_4AD9),
    K.S_VMA_BF11: (K.S_GPUOPEN_LIBRARIESANDSDKS_VUL_E5A1, K.S_V3_2_1_4541, K.S_5E7749504CB802427FFB7BEC38A0_CE1B),
}


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def source(archive, destination, url, digest):
    archive.parent.mkdir(parents=True, exist_ok=True)
    download(url, archive, digest)
    with archive.open(K.S_RB_81FE) as stream:
        actual = hashlib.file_digest(stream, K.S_SHA256_9465).hexdigest()
    if actual != digest:
        raise ValueError(f"Checksum mismatch: {archive}")
    if not destination.is_dir():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            with tarfile.open(archive) as tar:
                tar.extractall(temporary, filter=K.S_DATA_7553)
            roots = list(Path(temporary).iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f"Unexpected archive layout: {archive}")
            roots[0].rename(destination)


def prepare():
    version = K.S_3_4_16_41C9
    source(WORK / f"SDL3-{version}.tar.gz", WORK / f"SDL3-{version}",
           f"https://github.com/libsdl-org/SDL/releases/download/release-{version}/SDL3-{version}.tar.gz",
           K.S_7322236CD12090C3EB40B9728BE4_91ED)
    for name, (repo, revision, digest) in SOURCES.items():
        source(WORK / f"android/{name}.tar.gz", WORK / f"android/{name}/source",
               f"https://codeload.github.com/{repo}/tar.gz/{revision}", digest)
    run(sys.executable, ROOT / K.S_TOOLS_SETUP_SMAA_PY_7978)
    assets = ROOT / K.S_BUILD_ANDROID_ASSETS_AB98
    run(sys.executable, ROOT / K.S_TOOLS_SETUP_FONTS_PY_0859, K.S_OUTPUT_3033, assets / K.S_LIB_FONTS_BD4E)
    assets.mkdir(parents=True, exist_ok=True)
    config = (ROOT / K.S_SF2RECOMP_INI_8714).read_text()
    config = config.replace(K.S_TARGET_FPS_120_1367, K.S_TARGET_FPS_60_389D)
    config = config.replace(K.S_INTERNAL_RESOLUTION_AUTO_2FA3, K.S_INTERNAL_RESOLUTION_0_5X_D8D0)
    config = config.replace(K.S_ASPECT_RATIO_ORIGINAL_1AA1, K.S_ASPECT_RATIO_NATIVE_0560)
    config = config.replace(K.S_PLAYER_SETUP_KEYBOARD_VS_CON_132F, K.S_PLAYER_SETUP_SOLO_8400)
    (assets / K.S_SF2RECOMP_INI_8714).write_text(config)
    shutil.copytree(ROOT / K.S_PROFILES_COMPLETED_A426, assets / K.S_PROFILES_COMPLETED_A426, dirs_exist_ok=True)
    licenses = assets / K.S_LICENSES_A26A
    licenses.mkdir(exist_ok=True)
    for name in (K.S_LICENSE_6485, K.S_THIRD_PARTY_NOTICES_F9CA):
        shutil.copyfile(ROOT / name, licenses / name)
    for path in (ROOT / K.S_LICENSES_A26A).glob(K.S_EMPTY_15A7):
        if path.is_file(): shutil.copyfile(path, licenses / path.name)
    for name in SOURCES:
        src = WORK / f"android/{name}/source"
        target = licenses / name
        target.mkdir(exist_ok=True)
        for pattern in (K.S_LICENSE_C5DC, K.S_COPYING_A9EF, K.S_NOTICE_66F3):
            for path in src.glob(pattern):
                if path.is_file(): shutil.copyfile(path, target / path.name)
    shutil.copyfile(WORK / f"SDL3-{version}/LICENSE.txt", licenses / K.S_SDL3_TXT_3E49)
    (licenses / K.S_ANDROID_SOURCES_TXT_73F3).write_text(K.S_N_AD65.join(
        f"{name}: https://github.com/{repo}/tree/{revision}\nArchive SHA256: {digest}"
        for name, (repo, revision, digest) in SOURCES.items()) + K.S_N_AD65)


def game_identity(required=True):
    identities = []
    for module in (K.S_MAIN_13FD, K.S_SDK_0065):
        table = ROOT / f"game/gen/{module}/{module}_table.c"
        text = table.read_text() if table.is_file() else K.S_EMPTY_6F49
        match = re.search(rf"{module}_nso_build_id\[32\]\s*=\s*\{{([^}}]+)\}}", text)
        values = re.findall(K.S_0X_0_9A_FA_F_2_B_8A15, match[1]) if match else []
        if len(values) != K.N_32_E29C:
            if required: raise ValueError(K.S_GENERATE_SUPPORTED_GAME_CODE_AAF0)
            return
        identities.append(f"{module}={K.S_EMPTY_6F49.join(values).lower()}")
    (ROOT / K.S_BUILD_ANDROID_ASSETS_GAME_BU_A979).write_text(K.S_N_AD65.join(identities) + K.S_N_AD65)
    return dict(line.split(K.S_EMPTY_C874, 1) for line in identities)


def package_game(game, identities):
    """Package the user's local data, ready to play without an Android importer."""
    # The runtime checks the generated code's exact build IDs again at launch.
    # Check before touching the last good asset package as well.
    for module, expected in identities.items():
        path = game / K.S_EXEFS_00A0 / module
        with path.open(K.S_RB_81FE) as stream:
            header = stream.read(K.N_256_51E8)
        if len(header) != K.N_256_51E8 or header[:K.N_4_4B22] != K.B_B_NSO0_4F90 or header[K.N_64_A68B:K.N_96_7B1A].hex() != expected:
            raise ValueError(f"{path} does not match the generated game code")
    if not (game / K.S_ROMFS_DATA_GLOBALGAMEMANAGER_C4F7).is_file():
        raise ValueError(f"Missing extracted game assets: {game / K.S_ROMFS_0A7E}")
    paths = [game / K.S_EXEFS_00A0 / name for name in (K.S_MAIN_13FD, K.S_SDK_0065)]
    paths += sorted(p for p in (game / K.S_ROMFS_0A7E).rglob(K.S_EMPTY_15A7) if p.is_file())
    paths += [game / name for name in (K.S_CONTROL_NACP_C2FD, K.S_GAME_INFO_JSON_44B1, K.S_ICON_PNG_9A4E) if (game / name).is_file()]
    signature = [[str(p.resolve()), p.stat().st_size, p.stat().st_mtime_ns] for p in paths]
    assets = ROOT / K.S_BUILD_ANDROID_ASSETS_AB98
    archive, cache = assets / K.S_GAME_ZIP_3019, ROOT / K.S_BUILD_ANDROID_GAME_PACKAGE_J_6790
    previous = json.loads(cache.read_text()) if cache.is_file() else {}
    if not archive.is_file() or previous.get(K.S_FILES_BE37) != signature:
        temporary = archive.with_suffix(K.S_ZIP_TMP_DDAC)
        try:
            with zipfile.ZipFile(temporary, K.S_W_780F, compression=zipfile.ZIP_DEFLATED, compresslevel=1) as output:
                for path in paths:
                    output.write(path, path.relative_to(game).as_posix())
            with temporary.open(K.S_RB_81FE) as stream:
                identity = hashlib.file_digest(stream, K.S_SHA256_9465).hexdigest()
            temporary.replace(archive)
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps({K.S_FILES_BE37: signature, K.S_IDENTITY_4838: identity}))
        finally:
            temporary.unlink(missing_ok=True)
    else:
        identity = previous[K.S_IDENTITY_4838]
    (assets / K.S_GAME_PACKAGE_ID_TXT_CB4E).write_text(identity + K.S_N_AD65)
    icon = ROOT / K.S_BUILD_ANDROID_RES_DRAWABLE_N_0F9C
    if (game / K.S_ICON_PNG_9A4E).is_file():
        icon.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(game / K.S_ICON_PNG_9A4E, icon)
    else:
        icon.unlink(missing_ok=True)
    from game_metadata import parse_nacp
    metadata = {K.S_TITLE_BED6: K.S_SHADOW_FIGHT_2_3618, K.S_PUBLISHER_DF8D: K.S_NEKKI_LIMITED_06D7, K.S_DISPLAY_VERSION_3D6E: K.S_1_1_0_0766}
    if (game / K.S_CONTROL_NACP_C2FD).is_file():
        metadata.update(parse_nacp((game / K.S_CONTROL_NACP_C2FD).read_bytes()))
    elif (game / K.S_GAME_INFO_JSON_44B1).is_file():
        info = json.loads((game / K.S_GAME_INFO_JSON_44B1).read_text())
        metadata.update({key: info[key] for key in metadata if info.get(key)})
    metadata.update(source_platform=K.S_NINTENDO_SWITCH_1816, source_build_id=identities[K.S_MAIN_13FD])
    (ROOT / K.S_BUILD_ANDROID_GAME_METADATA__22D3).write_text(json.dumps(metadata, ensure_ascii=False) + K.S_N_AD65)
    print(f"Bundled local game: {archive.stat().st_size / K.N_1024_E39E**K.N_2_D473:.0f} MiB", flush=True)


def publish_apk(apk, output):
    """Preserve the old inode while another process installs that APK."""
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(K.S_APK_TMP_D74A)
    try:
        shutil.copyfile(apk, temporary)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def gradle_command():
    if os.environ.get(K.S_GRADLE_ECF7): return os.environ[K.S_GRADLE_ECF7]
    return str(ROOT / K.S_ANDROID_63EA / (K.S_GRADLEW_BAT_19B4 if os.name == K.S_NT_88F0 else K.S_GRADLEW_C26A))


def gradle_arguments():
    if os.environ.get(K.S_GRADLE_ECF7):
        return [gradle_command()]
    java = Path(os.environ[K.S_JAVA_HOME_10D6]) / K.S_BIN_0955 / (K.S_JAVA_EXE_7EFD if os.name == K.S_NT_88F0 else K.S_JAVA_E680) if os.environ.get(K.S_JAVA_HOME_10D6) else K.S_JAVA_E680
    return [java, K.S_CLASSPATH_5F0D, ROOT / K.S_ANDROID_GRADLE_WRAPPER_GRADL_05C8,
            K.S_ORG_GRADLE_WRAPPER_GRADLEWRA_078B]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_ABI_BED3, choices=(K.S_ARM64_V8A_ACD6, K.S_X86_64_CB4A), default=K.S_ARM64_V8A_ACD6)
    parser.add_argument(K.S_PREPARE_D57C, action=K.S_STORE_TRUE_2C29, help=K.S_ONLY_PREPARE_SOURCE_DEPENDEN_AEA5)
    parser.add_argument(K.S_INSTALL_775D, action=K.S_STORE_TRUE_2C29, help=K.S_ALSO_INSTALL_ON_THE_ADB_SELE_296C)
    parser.add_argument(K.S_GAME_1AB6, type=Path, default=ROOT / K.S_GAME_EXTRACTED_UPDATE_E969,
                        help=K.S_EXTRACTED_GAME_FILES_TO_BUND_8C11)
    args = parser.parse_args()
    prepare()
    identities = game_identity(required=not args.prepare)
    if args.prepare: return
    package_game(args.game.resolve(), identities)
    sdk = Path(os.environ.get(K.S_ANDROID_HOME_00C5, Path.home() / K.S_ANDROID_SDK_C772))
    from setup_android import TOOLCHAIN, NDK
    for relative in (K.S_PLATFORMS_ANDROID_AA33 + TOOLCHAIN[K.S_COMPILE_SDK_D63B] + K.S_ANDROID_JAR_7F93,
                     f"ndk/{NDK}/build/cmake/android.toolchain.cmake"):
        if not (sdk / relative).exists(): raise ValueError(f"Missing Android SDK component: {sdk / relative}")
    environment = os.environ.copy()
    environment[K.S_ANDROID_HOME_00C5] = str(sdk)
    # Repack the APK from scratch while retaining native object caches. AGP's
    # incremental ZIP writer can leave an entire old libmain.so as unused bytes
    # after a relink, doubling these large locally translated packages.
    apk = ROOT / K.S_ANDROID_APP_BUILD_OUTPUTS_AP_AC34
    apk.unlink(missing_ok=True)
    run(*gradle_arguments(), K.S_NO_DAEMON_7C54, f"-Psf2Abi={args.abi}", f"-Psf2Python={Path(sys.executable).as_posix()}",
        K.S_INSTALLDEBUG_81D1 if args.install else K.S_ASSEMBLEDEBUG_3EB6, cwd=ROOT / K.S_ANDROID_63EA, env=environment)
    output = ROOT / f"build/android/sf2-recomp-{args.abi}-debug.apk"
    # A wireless install may still hold the previous APK open. Publish through
    # rename so a rebuild cannot truncate the file ADB is currently streaming.
    publish_apk(apk, output)
    print(f"APK: {output}")


if __name__ == K.S_MAIN_ACB5:
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))

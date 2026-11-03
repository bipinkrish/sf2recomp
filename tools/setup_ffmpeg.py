#!/usr/bin/env python3
"""Build the small, LGPL-only FFmpeg decoder for the native host target."""
from _sf2_tool_constants import SETUP_FFMPEG as K
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from host_tools import build_bash, host_target, require_host
from downloads import unpack

ROOT = Path(__file__).resolve().parent.parent
VERSION = K.VERSION
ARCHIVE_NAME = f"ffmpeg-{VERSION}.tar.xz"
ARCHIVE_URL = f"https://ffmpeg.org/releases/{ARCHIVE_NAME}"
ARCHIVE_SHA256 = K.ARCHIVE_SHA256
CONFIGURE = [
    K.S_DISABLE_GPL_A01A, K.S_DISABLE_NONFREE_8A96, K.S_DISABLE_VERSION3_C339,
    K.S_DISABLE_AUTODETECT_BA3D, K.S_DISABLE_NETWORK_255B, K.S_DISABLE_DOC_B53C, K.S_DISABLE_DEBUG_10EE,
    K.S_DISABLE_STATIC_D849, K.S_ENABLE_SHARED_A649, K.S_ENABLE_FFMPEG_37D4, K.S_DISABLE_FFPLAY_A180, K.S_DISABLE_FFPROBE_105D,
    K.S_DISABLE_EVERYTHING_757F,
    K.S_ENABLE_DECODER_H264_AAC_F002, K.S_ENABLE_PARSER_H264_AAC_7F7E, K.S_ENABLE_DEMUXER_MOV_6788,
    K.S_ENABLE_PROTOCOL_FILE_PIPE_8E3C, K.S_ENABLE_ENCODER_RAWVIDEO_PCM__AF78,
    K.S_ENABLE_MUXER_RAWVIDEO_PCM_S1_F09B, K.S_ENABLE_FILTER_SCALE_FPS_FORM_F63C,
    K.S_ENABLE_SWSCALE_6FA9, K.S_ENABLE_SWRESAMPLE_1B95, K.S_ENABLE_PTHREADS_C557,
]


def paths(target=K.S_LINUX_09D1):
    default = ROOT / (K.S_DEPS_FFMPEG_660D if target == K.S_LINUX_09D1 else f"deps/ffmpeg-{target}")
    prefix = Path(os.environ.get(K.S_FFMPEG_DIR_7F8C, default)).resolve()
    work = Path(os.environ.get(K.S_SF2_DEPS_WORK_99AB, ROOT / K.S_BUILD_DEPENDENCIES_7621)).resolve()
    return prefix, work


def installed(prefix, target=K.S_LINUX_09D1):
    marker = prefix / K.S_SHARE_FFMPEG_BUILD_TXT_C10B
    return (
        (prefix / (K.S_BIN_FFMPEG_EXE_3E82 if target == K.S_WINDOWS_F2ED else K.S_BIN_FFMPEG_9C11)).is_file()
        and any(p.is_file() for p in prefix.glob(K.S_BIN_AVCODEC_DLL_909F if target == K.S_WINDOWS_F2ED else (K.S_LIB_LIBAVCODEC_DYLIB_B456 if target == K.S_MACOS_91E2 else K.S_LIB_LIBAVCODEC_SO_5E67)))
        and marker.is_file()
        and f"FFmpeg {VERSION}" in marker.read_text()
        and (prefix / K.S_SHARE_FFMPEG_F670 / ARCHIVE_NAME).is_file()
        and (prefix / K.S_SHARE_LICENSES_FFMPEG_COPYIN_C623).is_file()
        and (prefix / K.S_SHARE_LICENSES_FFMPEG_LICENS_0C50).is_file()
    )


def source_archive(work):
    archive = Path(os.environ.get(K.S_FFMPEG_SOURCE_ARCHIVE_576E, work / K.S_ARCHIVES_86EA / ARCHIVE_NAME)).resolve()
    if not archive.is_file():
        if os.environ.get(K.S_FFMPEG_SOURCE_ARCHIVE_576E):
            raise FileNotFoundError(f"FFMPEG_SOURCE_ARCHIVE not found: {archive}")
        if not shutil.which(K.S_CURL_F8AE):
            raise RuntimeError(K.S_MISSING_CURL_PROVIDE_FFMPEG__DA15)
        archive.parent.mkdir(parents=True, exist_ok=True)
        temporary = archive.with_suffix(archive.suffix + K.S_PART_BC6F)
        subprocess.run([
            K.S_CURL_F8AE, K.S_FAIL_91BC, K.S_LOCATION_CAAB, K.S_PROTO_0ADB, K.S_HTTPS_0B80, K.S_TLSV1_2_EC63,
            K.S_CONNECT_TIMEOUT_A6CF, K.S_20_1C3F, K.S_MAX_TIME_7021, K.S_180_38B2, K.S_RETRY_04A1, K.S_2_5283, K.S_CONTINUE_AT_9758, K.S_EMPTY_01F8,
            ARCHIVE_URL, K.S_O_010C, str(temporary),
        ], check=True)
        temporary.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != ARCHIVE_SHA256:
        raise ValueError(f"FFmpeg source archive checksum mismatch: {archive}")
    return archive


def build(prefix, work, force=False, target=K.S_LINUX_09D1):
    marker = prefix / K.S_SHARE_FFMPEG_BUILD_TXT_C10B
    if not force and installed(prefix, target):
        print(f"Bundled FFmpeg {VERSION} is ready: {prefix}")
        return

    shell = build_bash()
    for command in (K.S_CLANG_5CE7, K.S_MAKE_D1F9):
        if not shutil.which(command):
            raise RuntimeError(f"Missing build tool: {command}")
    archive = source_archive(work)
    source = work / f"ffmpeg-{VERSION}-{target}-source"
    if source.exists():
        shutil.rmtree(source)
    unpack(archive, source, root=f"ffmpeg-{VERSION}")

    config = [K.S_CC_CLANG_98B4, f"--prefix={prefix.as_posix()}", f"--bindir={(prefix / K.S_BIN_0955).as_posix()}",
              f"--libdir={(prefix / K.S_LIB_427F).as_posix()}", *CONFIGURE]
    if target == K.S_MACOS_91E2:
        minimum = os.environ.get(K.S_MACOS_MIN_B2C9, K.S_13_0_8773)
        config += [K.S_TARGET_OS_DARWIN_84BB, K.S_INSTALL_NAME_DIR_RPATH_31B5,
                   f"--extra-cflags=-mmacosx-version-min={minimum}",
                   f"--extra-ldflags=-mmacosx-version-min={minimum} -Wl,-headerpad_max_install_names"]
    if target == K.S_WINDOWS_F2ED:
        config.remove(K.S_ENABLE_PTHREADS_C557)
        config += [K.S_TARGET_OS_MINGW32_8AD9, K.S_ARCH_X86_64_CBA9, K.S_ENABLE_W32THREADS_E3E1]
    if not shutil.which(K.S_NASM_114F) and not shutil.which(K.S_YASM_D323):
        config.append(K.S_DISABLE_X86ASM_69DD)
        print(K.S_NASM_YASM_NOT_FOUND_BUILDING_E91C)
    subprocess.run([shell, (source / K.S_CONFIGURE_3A7A).as_posix(), *config], cwd=source, check=True)
    jobs = int(os.environ.get(K.S_JOBS_ABED, str(min(os.cpu_count() or 1, K.N_8_2C62))))
    if jobs < 1:
        raise ValueError(K.S_JOBS_MUST_BE_A_POSITIVE_INTE_A57D)
    subprocess.run([K.S_MAKE_D1F9, f"-j{jobs}"], cwd=source, check=True)
    subprocess.run([K.S_MAKE_D1F9, K.S_INSTALL_7EB3], cwd=source, check=True)

    share = prefix / K.S_SHARE_FFMPEG_F670
    licenses = prefix / K.S_SHARE_LICENSES_FFMPEG_760A
    share.mkdir(parents=True, exist_ok=True)
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, share / ARCHIVE_NAME)
    shutil.copy2(source / K.S_COPYING_LGPLV2_1_D4ED, licenses / K.S_COPYING_LGPLV2_1_D4ED)
    shutil.copy2(source / K.S_LICENSE_MD_02B5, licenses / K.S_LICENSE_MD_02B5)
    (share / K.S_BUILD_TXT_B4AC).write_text(
        f"FFmpeg {VERSION}\nSource: {ARCHIVE_URL}\nSHA256: {ARCHIVE_SHA256}\n"
        "License: LGPL 2.1 or later; built without GPL or nonfree components.\n"
        "Configure options:\n  " + K.S_EMPTY_730D.join(
            option for option in config
            if not option.startswith((K.S_PREFIX_845D, K.S_BINDIR_18CC, K.S_LIBDIR_9AAB))
        ) + K.S_N_AD65
    )
    if not installed(prefix, target):
        raise RuntimeError(f"FFmpeg install is incomplete: {prefix}")
    print(f"Bundled FFmpeg {VERSION} built at {prefix}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_TARGET_2BBE, choices=(K.S_LINUX_09D1, K.S_WINDOWS_F2ED, K.S_MACOS_91E2), default=host_target())
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(K.S_CHECK_464A, action=K.S_STORE_TRUE_2C29, help=K.S_CHECK_FOR_THE_BUILT_DECODER__7D5D)
    mode.add_argument(K.S_BUILD_65C4, action=K.S_STORE_TRUE_2C29, help=K.S_BUILD_FFMPEG_FROM_THE_PINNED_D874)
    mode.add_argument(K.S_ENSURE_C475, action=K.S_STORE_TRUE_2C29, help=K.S_BUILD_THE_DECODER_ONLY_WHEN__2964)
    parser.add_argument(K.S_HELP_ENV_EF78, action=K.S_STORE_TRUE_2C29, help=K.S_SHOW_SUPPORTED_ENVIRONMENT_O_1665)
    args = parser.parse_args(argv)
    if args.help_env:
        print(K.S_FFMPEG_DIR_CHANGES_THE_LOCAL_FC5F)
        return 0
    prefix, work = paths(args.target)
    try:
        require_host(args.target)
        if args.target == K.S_WINDOWS_F2ED and (os.name != K.S_NT_88F0 or os.environ.get(K.S_MSYSTEM_844D) != K.S_UCRT64_E4F4):
            raise ValueError(K.S_BUILD_WINDOWS_FFMPEG_IN_THE__7A7A)
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


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

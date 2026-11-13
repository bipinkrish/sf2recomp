#!/usr/bin/env python3
"""Build a native Linux, macOS or Windows SF2 folder from local game files."""
from _sf2_tool_constants import CONVERT as K
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / K.S_TOOLS_F83E))
from conversion_log import LOG_ACTIVE, run_logged_cli
from conversion_progress import ConversionProgress
from desktop_integration import desktop_exec, linux_desktop, register_linux_desktop, write_macos_app
from game_metadata import APP_ID, parse_nacp
from setup_ffmpeg import ARCHIVE_NAME as FFMPEG_ARCHIVE_NAME, VERSION as FFMPEG_VERSION
from setup_fonts import FONT_DIRECTORY, prepare as prepare_fonts
from windows_dlls import dependency_closure
from analyze_il2cpp import prepare_symbols
from host_tools import build_bash, host_target, require_host
from macos_dylibs import relocate as relocate_macos
from release_support import manifest as release_manifest, compile_sources


def run(*command, env=None):
    subprocess.run([part.as_posix() if isinstance(part, Path) else str(part) for part in command],
                   cwd=ROOT, check=True, **({K.S_ENV_4706: env} if env is not None else {}))


def source_ids(game):
    ids = {}
    for name in (K.S_MAIN_13FD, K.S_SDK_0065):
        with (game / K.S_EXEFS_00A0 / name).open(K.S_RB_81FE) as stream:
            header = stream.read(K.N_96_7B1A)
        if len(header) != K.N_96_7B1A or header[:K.N_4_4B22] != K.B_B_NSO0_4F90:
            raise ValueError(f"exefs/{name} is not a valid NSO")
        ids[name] = header[K.N_64_A68B:K.N_96_7B1A].hex()
    # Hooks and IL2CPP metadata layout have only been validated for this update.
    if ids[K.S_MAIN_13FD] != K.S_E15FC665EC42684AA6EDD1AE0362_06CD.ljust(K.N_64_A68B, K.S_0_59A9):
        raise ValueError(K.S_SUPPORTED_INPUT_SHADOW_FIGHT_FB57)
    if not (game / K.S_ROMFS_DATA_MANAGED_METADATA__F282).is_file():
        raise ValueError(K.S_THE_EXTRACTED_UPDATED_ROMFS__BB88)
    return ids


def write_desktop(output, info):
    desktop = output / (APP_ID + K.S_DESKTOP_5B29)
    desktop.write_text(linux_desktop(output / K.S_SF2_4CDA, output / K.S_ICON_PNG_9A4E,
                                    title=K.S_SHADOW_FIGHT_2_SF2RECOMP_891C, identifier=APP_ID,
                                    version=info[K.S_DISPLAY_VERSION_3D6E], category=K.S_GAME_3173), encoding=K.S_UTF_8_D983)
    desktop.chmod(K.N_493_560A)
    return desktop


def install_desktop(output):
    """Explicit opt-in: register this local output in the user's application menu."""
    desktop = output.resolve() / (APP_ID + K.S_DESKTOP_5B29)
    return register_linux_desktop(APP_ID, desktop.read_text(encoding=K.S_UTF_8_D983))


def package_game(game, output, executable, sdl_dir, ffmpeg_dir, target=K.S_LINUX_09D1):
    """Copy only runtime inputs; keys, tickets, compiler outputs and tools stay out."""
    if target not in {K.S_LINUX_09D1, K.S_WINDOWS_F2ED, K.S_MACOS_91E2}:
        raise ValueError(f"Unsupported package target: {target}")
    windows = target == K.S_WINDOWS_F2ED
    macos = target == K.S_MACOS_91E2
    exe_name = K.S_SF2_EXE_6245 if windows else K.S_SF2_4CDA
    decoder_name = K.S_FFMPEG_EXE_762F if windows else K.S_FFMPEG_5ADA
    source_ids(game)
    output = output.resolve()
    if output == ROOT or ROOT.is_relative_to(output) or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError(K.S_OUTPUT_MUST_NOT_OVERLAP_THE__352D)
    if not (game / K.S_CONTROL_NACP_C2FD).is_file() or not (game / K.S_ICON_PNG_9A4E).is_file():
        raise ValueError(K.S_OFFICIAL_CONTROL_NACP_ICON_P_F9D1)
    info = parse_nacp((game / K.S_CONTROL_NACP_C2FD).read_bytes())
    if target == K.S_LINUX_09D1:
        desktop_exec(output / K.S_SF2_4CDA)  # Validate launcher compatibility before copying files.
    # This checks the actual linked build IDs without opening a graphical window.
    run(executable, K.S_CHECK_GAME_D4C1, game)
    ffmpeg_executable = ffmpeg_dir / K.S_BIN_0955 / decoder_name
    ffmpeg_build = ffmpeg_dir / K.S_SHARE_FFMPEG_BUILD_TXT_C10B
    ffmpeg_source = ffmpeg_dir / K.S_SHARE_FFMPEG_F670 / FFMPEG_ARCHIVE_NAME
    ffmpeg_license = ffmpeg_dir / K.S_SHARE_LICENSES_FFMPEG_760A
    ffmpeg_libraries = (sorted(ffmpeg_dir.glob(K.S_BIN_DLL_AD63)) if windows else
                        sorted({*ffmpeg_dir.glob(K.S_LIB_LIBAV_DYLIB_9D98), *ffmpeg_dir.glob(K.S_LIB_LIBSW_DYLIB_1411)}) if macos else
                        sorted({*ffmpeg_dir.glob(K.S_LIB_LIBAV_SO_E292), *ffmpeg_dir.glob(K.S_LIB_LIBSW_SO_F6B9)}))
    if (not ffmpeg_executable.is_file() or not ffmpeg_build.is_file() or not ffmpeg_source.is_file()
            or not ffmpeg_license.is_dir() or not ffmpeg_libraries):
        raise ValueError(K.S_THE_BUNDLED_FFMPEG_BUILD_IS__274D)
    prepare_fonts(FONT_DIRECTORY, check=True)
    # Resolve the complete PE import graph before touching an existing output.
    # Each executable has its own DLL search directory; decoder DLLs stay beside
    # ffmpeg.exe, and application DLLs stay beside sf2.exe.
    game_dlls, decoder_dlls = {}, {}
    release = release_manifest()
    if windows and not release:
        search = [executable.parent, sdl_dir / K.S_BIN_0955]
        game_dlls = dependency_closure([executable], search)
        decoder_dlls = dependency_closure([ffmpeg_executable], [ffmpeg_dir / K.S_BIN_0955, *search])
    output.mkdir(parents=True, exist_ok=True)
    exefs = output / K.S_EXTRACTED_EXEFS_FE8A
    exefs.mkdir(parents=True, exist_ok=True)
    for name in (K.S_MAIN_13FD, K.S_SDK_0065, K.S_MAIN_NPDM_478C):
        shutil.copy2(game / K.S_EXEFS_00A0 / name, exefs / name)
    shutil.copytree(game / K.S_ROMFS_0A7E, output / K.S_EXTRACTED_ROMFS_A728, dirs_exist_ok=True)
    # Install the executable after its data. Existing user settings and saves stay.
    if macos:
        staged = output / K.S_SF2_PENDING_FB91
        shutil.copy2(executable, staged)
        staged.replace(output / exe_name)
    else:
        shutil.copy2(executable, output / exe_name)
    # Earlier conversions used the compiler project's name for the game binary.
    # Remove that generated file when refreshing an existing converted folder.
    legacy = output / K.S_SF2RECOMP_14FD
    if legacy.is_file() and not legacy.is_symlink():
        legacy.unlink()
    for name in (K.S_CONTROL_NACP_C2FD, K.S_ICON_PNG_9A4E):
        shutil.copy2(game / name, output / name)
    (output / K.S_GAME_INFO_JSON_44B1).write_text(json.dumps(info, ensure_ascii=False, indent=K.N_2_D473) + K.S_N_AD65, encoding=K.S_UTF_8_5383)
    if target == K.S_LINUX_09D1:
        write_desktop(output, info)
    config = output / K.S_SF2RECOMP_INI_8714
    if not config.exists():
        shutil.copy2(ROOT / K.S_SF2RECOMP_INI_8714, config)
    shutil.copytree(ROOT / K.S_PROFILES_COMPLETED_A426, output / K.S_PROFILES_COMPLETED_A426, dirs_exist_ok=True)
    notices = output / K.S_LICENSES_A26A
    notices.mkdir(exist_ok=True)
    for name, path in {
        K.S_PROJECT_LICENSE_TXT_6AD3: ROOT / K.S_LICENSE_6485,
        K.S_THIRD_PARTY_NOTICES_TXT_8B60: ROOT / K.S_THIRD_PARTY_NOTICES_F9CA,
    }.items():
        shutil.copy2(path, notices / name)
    for path in (ROOT / K.S_LICENSES_A26A).glob(K.S_TXT_2E63):
        shutil.copy2(path, notices / path.name)
    # Retire duplicate filenames written before license texts were centralized.
    for old_name in (K.S_LAS_RUNTIME_LICENSE_TXT_DE07, K.S_SMAA_LICENSE_TXT_879F):
        (notices / old_name).unlink(missing_ok=True)
    # Local shared SDL is needed alongside the executable; OS/driver libraries
    # remain supplied by the user's Linux installation. Static SDL needs no copy.
    libraries = [] if windows else list((sdl_dir / K.S_LIB_427F).glob(K.S_LIBSDL3_DYLIB_666A if macos else K.S_LIBSDL3_SO_9A8B))
    if libraries:
        lib = output / K.S_LIB_427F
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
    ffmpeg_bundle = output / K.S_LIB_FFMPEG_12A4
    if ffmpeg_bundle.exists():
        shutil.rmtree(ffmpeg_bundle)
    ffmpeg_bin = ffmpeg_bundle / K.S_BIN_0955
    ffmpeg_lib = ffmpeg_bundle / K.S_LIB_427F
    ffmpeg_bin.mkdir(parents=True)
    ffmpeg_lib.mkdir(parents=True)
    shutil.copy2(ffmpeg_executable, ffmpeg_bin / decoder_name)
    if windows:
        # Releases already seal the decoder's dependency closure beside ffmpeg.exe.
        # Keep those DLLs in the converted folder too, where Windows searches.
        if release:
            decoder_dlls = {path.name.lower(): path for path in ffmpeg_libraries}
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
    ffmpeg_notices = notices / K.S_FFMPEG_B4B8
    if ffmpeg_notices.exists():
        shutil.rmtree(ffmpeg_notices)
    shutil.copytree(ffmpeg_license, ffmpeg_notices)
    shutil.copy2(ffmpeg_source, ffmpeg_notices / f"ffmpeg-{FFMPEG_VERSION}-source.tar.xz")
    shutil.copy2(ffmpeg_build, ffmpeg_notices / K.S_BUILD_TXT_B4AC)
    if macos and not release:
        relocate_macos([output / exe_name, ffmpeg_bin / decoder_name],
                       [*(output / K.S_LIB_427F / p.name for p in libraries),
                        *(ffmpeg_lib / p.name for p in ffmpeg_libraries)])
    shutil.copytree(FONT_DIRECTORY, output / K.S_LIB_FONTS_BD4E, dirs_exist_ok=True)
    if release:
        destination = output if windows else output / K.S_LIB_427F
        destination.mkdir(exist_ok=True)
        for path in (ROOT / K.S_NATIVE_LIB_601C).iterdir():
            if path.is_file() and (K.S_SO_FD8B in path.name or path.suffix in (K.S_DLL_E982, K.S_DYLIB_CB5B)):
                shutil.copy2(path, destination / path.name)
                if windows:
                    shutil.copy2(path, ffmpeg_bin / path.name)
        if macos:
            # The loader resolves this ICD relative to itself, after conversion.
            shutil.copy2(ROOT / K.S_NATIVE_MOLTENVK_ICD_JSON_4F2D, output / K.S_MOLTENVK_ICD_JSON_8F3A)
        if (ROOT / K.S_NATIVE_LICENSES_9EA2).is_dir():
            shutil.copytree(ROOT / K.S_NATIVE_LICENSES_9EA2, notices / K.S_RUNTIME_DEPENDENCIES_0145, dirs_exist_ok=True)
    font_notices = notices / K.S_NOTO_FABF
    font_notices.mkdir(exist_ok=True)
    for name in (K.S_NOTOSANS_LICENSE_TXT_5F00, K.S_NOTOSANSCJK_LICENSE_TXT_6796, K.S_SOURCE_TXT_5804):
        shutil.copy2(FONT_DIRECTORY / name, font_notices / name)
    from version import read as tool_version
    (output / K.S_BUILD_JSON_4596).write_text(json.dumps({
        K.S_SF2RECOMP_14FD: tool_version(),
        K.S_TARGET_0B9D: target, K.S_EXECUTABLE_1A15: exe_name, K.S_SOURCE_BUILD_IDS_8451: source_ids(game),
        K.S_WINDOWS_VALIDATION_FBEC: K.S_EXPERIMENTAL_HARDWARE_PLAYTE_5310 if windows else None,
        K.S_MACOS_VALIDATION_BAB2: K.S_EXPERIMENTAL_NATIVE_BUILD_AN_3F04 if macos else None,
        K.S_OFFICIAL_TITLE_E5A2: info[K.S_TITLE_BED6], K.S_OFFICIAL_DISPLAY_VERSION_6A1E: info[K.S_DISPLAY_VERSION_3D6E],
        K.S_CONFIGURATION_52DE: K.S_SF2RECOMP_INI_READ_AT_RUNTIM_B87B,
        K.S_CUTSCENE_DECODER_9E30: f"FFmpeg {FFMPEG_VERSION} (bundled, LGPL-only build)",
    }, indent=K.N_2_D473) + K.S_N_AD65)
    if windows:
        # Keep dependency notices from the exact installed build prefix.
        native_notices = sdl_dir / K.S_SHARE_LICENSES_384B
        if native_notices.is_dir():
            shutil.copytree(native_notices, notices / K.S_WINDOWS_DEPENDENCIES_7270, dirs_exist_ok=True)
        (output / K.S_WINDOWS_DLLS_JSON_ABAE).write_text(json.dumps({
            K.S_APPLICATION_35A1: sorted(game_dlls), K.S_DECODER_4738: sorted(decoder_dlls),
            K.S_SYSTEM_VULKAN_LOADER_B2D3: K.S_PROVIDED_BY_THE_INSTALLED_GP_525D,
        }, indent=K.N_2_D473) + K.S_N_AD65)
        (output / K.S_RUN_TXT_0818).write_text(
            K.S_RUN_SF2_EXE_KEEP_EXTRACTED_L_EC47
        )
    elif macos:
        (output / K.S_RUN_TXT_0818).write_text(
            K.S_OPEN_SHADOW_FIGHT_2_APP_IN_F_DB7A
        )
    else:
        (output / K.S_RUN_TXT_0818).write_text(
            "Run ./sf2 from this folder or any working directory.\n"
            f"Official game: {info[K.S_TITLE_BED6]} {info[K.S_DISPLAY_VERSION_3D6E]}.\n"
            f"Desktop launcher: {APP_ID}.desktop (local paths; regenerate if moved).\n"
            "Edit sf2recomp.ini for language/audio/video/keyboard/gamepad mappings; restart to apply.\n"
            "Keep extracted/ beside the executable. This output contains your local game-derived files.\n"
            "Cutscene playback uses the FFmpeg decoder and source bundled in lib/ffmpeg and licenses/FFmpeg; no FFmpeg install is needed.\n"
            "Keep lib/fonts/ alongside the executable for shared-font fallback (Noto, SIL OFL 1.1).\n"
            "System requirements: Linux x86-64, Vulkan loader/driver, shaderc and C/C++ runtime libraries.\n"
            "Software fallback: SDL_VIDEODRIVER=x11 SF2RECOMP_VK_NO_GMEM=1 "
            "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./sf2\n"
            "Combat and hardware-driver compatibility remain incomplete.\n")
    if release:
        requirements = {K.S_LINUX_09D1: K.S_LINUX_X86_64_GLIBC_2_39_AVX2_4470,
                        K.S_WINDOWS_F2ED: K.S_WINDOWS_10_11_X64_CMPXCHG16B_C9BD,
                        K.S_MACOS_91E2: K.S_MACOS_15_ON_THIS_BUILD_S_CPU_54A4}
        (output / K.S_RUN_TXT_0818).write_text(
            (K.S_OPEN_SHADOW_FIGHT_2_APP_IN_F_98A0 if macos else f"Run {exe_name}.\n")
            + "Keep extracted/, lib/, licenses/ and the packaged libraries together.\n"
            f"SF2Recomp {release[K.S_VERSION_25EB]}; original game {info[K.S_DISPLAY_VERSION_3D6E]}.\n"
            f"Requirements: {requirements[target]}\n"
            "No Python, .NET SDK, compiler, Homebrew or Android SDK installation is needed to play this folder.\n"
            "Edit sf2recomp.ini and restart to apply settings. Saves are in save/ by default.\n")
    with (output / K.S_RUN_TXT_0818).open(K.S_A_D749, encoding=K.S_UTF_8_D983) as instructions:
        instructions.write(K.S_GAME_LOGS_ARE_IN_LOGS_SHADOW_04FE)
        instructions.write(K.S_LAN_SPARRING_START_THE_SAME__C79B)
    if macos:
        from version import read
        write_macos_app(output, output / exe_name, ROOT / K.S_ASSETS_SF2RECOMP_PNG_B232,
                        title=K.S_SHADOW_FIGHT_2_5A40, identifier=APP_ID, version=read()[K.S_VERSION_43CC], hardlink=True)
    print(f"Native game folder: {output}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument(K.S_NSP_C95D, type=Path, help=K.S_SUPPORTED_COMBINED_BASE_UPDA_9680)
    source.add_argument(K.S_GAME_1AB6, type=Path, help=K.S_ALREADY_EXTRACTED_UPDATED_GA_F336)
    ap.add_argument(K.S_KEYS_B749, type=Path, help=K.S_YOUR_LOCAL_PROD_KEYS_REQUIRE_ECFE)
    ap.add_argument(K.S_TARGET_2BBE, choices=(K.S_LINUX_09D1, K.S_WINDOWS_F2ED, K.S_MACOS_91E2), default=host_target())
    ap.add_argument(K.S_OUTPUT_3033, type=Path)
    ap.add_argument(K.S_SYMBOLS_1344, type=Path, help=K.S_REUSE_AN_EXISTING_SYM_MAIN_T_4B1A)
    ap.add_argument(K.S_SKIP_GENERATION_DC48, action=K.S_STORE_TRUE_2C29, help=K.S_COMPILE_EXISTING_GAME_GEN_FI_5F51)
    ap.add_argument(K.S_JOBS_0E7C, type=int, default=min(os.cpu_count() or 1, K.N_8_2C62))
    ap.add_argument(K.S_PACKAGE_ONLY_049B, action=K.S_STORE_TRUE_2C29, help=K.S_PACKAGE_AN_EXISTING_MATCHING_D111)
    ap.add_argument(K.S_INSTALL_DESKTOP_D4E9, action=K.S_STORE_TRUE_2C29,
                    help=K.S_EXPLICITLY_REGISTER_THE_OUTP_DE51)
    args = ap.parse_args(argv)
    try:
        if argv is None and not os.environ.get(LOG_ACTIVE):
            return run_logged_cli(__file__, sys.argv[1:])
        windows = args.target == K.S_WINDOWS_F2ED
        require_host(args.target)
        if args.target != K.S_LINUX_09D1 and args.install_desktop:
            raise ValueError(K.S_INSTALL_DESKTOP_IS_A_LINUX_O_947A)
        if args.package_only and args.skip_generation:
            raise ValueError(K.S_SKIP_GENERATION_IS_UNNECESSA_CC4B)
        if args.output is None:
            args.output = ROOT / f"out/shadow-fight-2-{args.target}"
        if args.jobs < 1:
            raise ValueError(K.S_JOBS_MUST_BE_POSITIVE_A3BA)
        release = release_manifest()
        generate = not (args.package_only or args.skip_generation)
        steps = [K.S_VALIDATE_INPUT_2904, K.S_EXTRACT_GAME_AA35] if args.nsp else []
        steps += [K.S_PREPARE_CONVERSION_936A]
        if generate:
            steps += [K.S_PREPARE_SYMBOLS_683B, K.S_TRANSLATE_GAME_CODE_80DD, K.S_TRANSLATE_SDK_CODE_912B]
        if not args.package_only:
            steps += [K.S_COMPILE_GAME_5C31]
            if release:
                steps.append(K.S_LINK_GAME_A897)
        progress = ConversionProgress([*steps, K.S_PACKAGE_GAME_69F7])
        if args.nsp:
            if not args.keys:
                raise ValueError(K.S_NSP_REQUIRES_KEYS_POINTING_T_B937)
            if args.package_only:
                raise ValueError(K.S_PACKAGE_ONLY_REQUIRES_GAME_7CAF)
            progress.advance()
            run(sys.executable, ROOT / K.S_TOOLS_EXTRACT_PY_B0D4, args.nsp.resolve(), args.keys.resolve(), K.S_VALIDATE_ONLY_9863)
            progress.advance()
            run(sys.executable, ROOT / K.S_TOOLS_EXTRACT_PY_B0D4, args.nsp.resolve(), args.keys.resolve())
            game = ROOT / K.S_GAME_EXTRACTED_UPDATE_E969
        else:
            game = args.game.resolve(strict=True)
        progress.advance()
        source_ids(game)
        prefix = None if release else os.environ.get(K.S_SDL_DIR_2C71)
        if windows and not prefix and not release:
            prefix = subprocess.check_output([K.S_CYGPATH_26B1, K.S_M_705B, os.environ.get(K.S_MINGW_PREFIX_49B8, K.S_UCRT64_3FD8)], text=True).strip()
        sdl_dir = Path(prefix or ROOT / (K.S_DEPS_MACOS_FBC7 if args.target == K.S_MACOS_91E2 else K.S_DEPS_3548)).resolve()
        ffmpeg_dir = Path(os.environ.get(K.S_FFMPEG_DIR_7F8C, ROOT / (
            K.S_DEPS_FFMPEG_660D if args.target == K.S_LINUX_09D1 else f"deps/ffmpeg-{args.target}"))).resolve()
        if release:
            ffmpeg_dir = ROOT / (K.S_DEPS_FFMPEG_660D if args.target == K.S_LINUX_09D1 else f"deps/ffmpeg-{args.target}")
        if generate:
            missing = [name for name in (K.S_CAPSTONE_C514, K.S_UNICORN_D491, K.S_LZ4_7811, K.S_CRYPTOGRAPHY_DE2A, K.S_PIL_17DB)
                       if importlib.util.find_spec(name) is None]
            if missing:
                raise ValueError(K.S_INSTALL_BUILD_DEPENDENCIES_P_DFAC)
        if not args.package_only and not release:
            environment = os.environ.copy()
            environment.update(TARGET=args.target, SDL_DIR=sdl_dir.as_posix(), PYTHON=Path(sys.executable).as_posix())
            run(build_bash(), ROOT / K.S_TOOLS_SETUP_RUNTIME_SH_9A82, K.S_ENSURE_C475, env=environment)
        if not release:
            run(sys.executable, ROOT / K.S_TOOLS_SETUP_FFMPEG_PY_88E9, K.S_ENSURE_C475, K.S_TARGET_2BBE, args.target)
        if generate:
            progress.advance()
            if args.symbols:
                symbols = args.symbols.resolve(strict=True)
            else:
                symbols = prepare_symbols(game)
            progress.advance()
            run(sys.executable, ROOT / K.S_TOOLS_RECOMPR_GEN_PY_1453, K.S_MAIN_13FD, game / K.S_EXEFS_MAIN_8CC5,
                K.S_0X7000000000_2DF4, ROOT / K.S_GAME_GEN_MAIN_DE2C, K.S_SYMBOLS_1344, symbols)
            progress.advance()
            run(sys.executable, ROOT / K.S_TOOLS_RECOMPR_GEN_PY_1453, K.S_SDK_0065, game / K.S_EXEFS_SDK_9162,
                K.S_0X7004000000_DDC3, ROOT / K.S_GAME_GEN_SDK_7AA1)
        if not args.package_only:
            progress.advance()
        if not args.package_only and not release:
            make = K.S_GMAKE_E81C if args.target == K.S_MACOS_91E2 else K.S_MAKE_D1F9
            run(make, f"-j{args.jobs}", f"TARGET={args.target}", f"SDL_DIR={sdl_dir.as_posix()}",
                f"PYTHON={Path(sys.executable).as_posix()}")
        executable = ROOT / (K.S_BUILD_WINDOWS_SF2_EXE_9B1B if windows else
                             K.S_BUILD_MACOS_SF2_D728 if args.target == K.S_MACOS_91E2 else K.S_BUILD_SF2_ECEA)
        if release and not args.package_only:
            sources = list((ROOT / K.S_GAME_GEN_45C5).glob(K.S_C_E03C))
            compile_sources(sources, executable, args.jobs, before_link=progress.advance)
        progress.advance()
        package_game(game, args.output, executable, sdl_dir, ffmpeg_dir, args.target)
        if args.install_desktop:
            install_desktop(args.output)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

#!/usr/bin/env python3
"""SF2Recomp portable desktop converter and optional Android APK builder."""
from _sf2_tool_constants import RELEASE as K
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from release_support import ROOT, manifest, environment, expand, compile_sources


@contextmanager
def conversion_lock():
    path = ROOT / K.S_BUILD_CONVERSION_LOCK_70E6
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open(K.S_A_B_2805) as stream:
        stream.seek(0)
        if os.name == K.S_NT_88F0:
            import msvcrt
            if path.stat().st_size == 0:
                stream.write(K.B_B_0_74DA)
                stream.flush()
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                raise ValueError(K.S_ANOTHER_CONVERSION_IS_USING__958D) from None
        else:
            import fcntl
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise ValueError(K.S_ANOTHER_CONVERSION_IS_USING__958D) from None
        yield


def check():
    """Exercise the packaged compiler, complete runtime link, and native loader."""
    import capstone, unicorn, lz4.block, certifi
    import io
    import ssl
    from PIL import Image
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    # Tcl initialization checks that the GUI's native module and script data
    # survived trimming and relocation, without requiring a display in CI.
    import tkinter
    tkinter.Tcl().eval(K.S_INFO_PATCHLEVEL_1337)
    ssl.create_default_context(cafile=certifi.where())
    payload = K.B_B_SF2RECOMP_DEPENDENCY_CHECK_9A52
    if lz4.block.decompress(lz4.block.compress(payload)) != payload:
        raise ValueError(K.S_BUNDLED_LZ4_FAILED_ITS_ROUND_0DC5)
    block = Cipher(algorithms.AES(bytes(K.N_16_B17E)), modes.ECB()).encryptor().update(bytes(K.N_16_B17E))
    if block.hex() != K.S_66E94BD4EF8A2C3B884CFA59CA34_EA59:
        raise ValueError(K.S_BUNDLED_AES_FAILED_ITS_KNOWN_CA9C)
    image_data = io.BytesIO()
    Image.new(K.S_RGB_4A24, (1, 1)).save(image_data, format=K.S_JPEG_E58B)
    image_data.seek(0)
    with Image.open(image_data) as image:
        image.load()
    data = manifest()
    if not data:
        raise ValueError(K.S_RUN_THIS_CHECK_FROM_AN_ASSEM_A6E5)
    check_decoder(data[K.S_TARGET_0B9D])
    for command in data[K.S_HELPERS_7DB9].values():
        if not Path(expand(command)[0]).is_file():
            raise ValueError(K.S_A_BUNDLED_INPUT_HELPER_IS_MI_B2A4)
    with tempfile.TemporaryDirectory(prefix=K.S_RELEASE_CHECK_8823, dir=ROOT / K.S_BUILD_0BA9) as temporary:
        stage = Path(temporary)
        source = stage / K.S_RELEASE_PROBE_C_69AB
        source.write_text(K.S_INCLUDE_CPU_H_N_INCLUDE_SIMD_8CD6, encoding=K.S_UTF_8_5383)
        with source.open(K.S_A_D749) as extra:
            extra.write(K.S_BOOL_MAIN_EH_LOOKUP_UINT64_T_0B66)
        from analyze_il2cpp import BUILD_ID
        source.write_text(source.read_text().replace(K.S_MAIN_NSO_BUILD_ID_32_0_3E16,
                          K.S_MAIN_NSO_BUILD_ID_32_5C28 + K.S_EMPTY_EE1B.join(str(b) for b in BUILD_ID) + K.S_EMPTY_9AC4))
        macos = sys.platform == K.S_DARWIN_5439
        executable = K.S_SF2_8A0A if macos else K.S_PROBE_EXE_E8BB if os.name == K.S_NT_88F0 else K.S_PROBE_A3E2
        binary = compile_sources([source], stage / executable)
        if macos:
            shutil.copytree(ROOT / K.S_NATIVE_LIB_601C, stage / K.S_LIB_427F)
        elif os.name == K.S_NT_88F0:
            # Match converted Windows folders: runtime DLLs sit beside the executable.
            for library in (ROOT / K.S_NATIVE_LIB_601C).glob(K.WINDOWS_RUNTIME_DLL_GLOB):
                shutil.copy2(library, stage / library.name)
        game = stage / (K.S_EXTRACTED_3466 if macos else K.S_SYNTHETIC_GAME_A790)
        (game / K.S_EXEFS_00A0).mkdir(parents=True)
        for name in (K.S_MAIN_13FD, K.S_SDK_0065):
            header = bytearray(K.B_B_NSO0_4F90 + bytes(K.N_96_7B1A - K.N_4_4B22))
            if name == K.S_MAIN_13FD: header[K.N_64_A68B:K.N_96_7B1A] = BUILD_ID
            (game / K.S_EXEFS_00A0 / name).write_bytes(header)
        subprocess.run([str(binary), K.S_CHECK_GAME_D4C1, str(game)], check=True, env=environment(), timeout=K.N_30_624B)
        native_version = subprocess.check_output([str(binary), K.S_VERSION_B4E3], env=environment(), text=True).strip()
        if native_version != K.S_SF2RECOMP_7368 + data[K.S_VERSION_25EB]:
            raise ValueError(K.S_THE_PREBUILT_RUNTIME_VERSION_E19F)
        if macos:
            from desktop_integration import write_macos_app
            from game_metadata import APP_ID
            app = write_macos_app(stage, binary, ROOT / K.S_ASSETS_SF2RECOMP_PNG_B232, title=K.S_SHADOW_FIGHT_2_5A40,
                                  identifier=APP_ID, version=data[K.S_VERSION_25EB], hardlink=True)
            bundled = app / K.S_CONTENTS_MACOS_SF2_5E7B
            env = dict(environment(), SF2_GAME_LOG=K.S_1_3915)
            subprocess.run([str(bundled), K.S_CHECK_GAME_D4C1], check=True, env=env, timeout=K.N_30_624B)
            if not (stage / K.S_LOGS_SHADOW_FIGHT_2_LOG_B270).is_file():
                raise ValueError(K.S_THE_MACOS_GAME_BUNDLE_DID_NO_C75E)
        # Force the Python native modules to load and execute, not just import.
        decoded = list(capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM).disasm_lite(bytes.fromhex(K.S_1F2003D5_D2B8), 0))
        if len(decoded) != 1 or decoded[0][K.N_2_D473] != K.S_NOP_E336:
            raise ValueError(K.S_BUNDLED_CAPSTONE_FAILED_TO_D_6155)
        machine = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
        machine.mem_map(K.N_65536_0F25, K.N_4096_8B92)
        machine.mem_write(K.N_65536_0F25, bytes.fromhex(K.S_1F2003D5_D2B8))
        machine.emu_start(K.N_65536_0F25, K.N_65540_1A9E)
        for name in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5, K.S_BKTR_7B22, K.S_IL2CPP_38B2):
            command = expand(data[K.S_HELPERS_7DB9][name])
            # Some helpers use exit 1 for usage, but loader failures must fail.
            result = subprocess.run([*command, K.S_HELP_24B2], cwd=stage, env=environment(),
                                    capture_output=True, timeout=K.N_30_624B)
            if result.returncode not in (0, 1):
                raise ValueError(f"Bundled {name} failed to start: {result.stderr.decode(errors=K.S_REPLACE_71A6)}")
    print(K.S_RELEASE_CHECK_PASSED_PYTHON__C041)


def check_decoder(target):
    """Start the shipped decoder without build-host DLL directories on PATH."""
    decoder = ROOT / K.S_DEPS_E72A / (K.S_FFMPEG_B521 if target == K.S_LINUX_F32A else f"ffmpeg-{target}") / K.S_BIN_49FE
    decoder /= K.S_FFMPEG_EXE_C804 if target == K.S_WINDOWS_E294 else K.S_FFMPEG_B521
    env = environment()
    env[K.S_PATH_8306] = K.S_EMPTY_12AE
    env.pop(K.S_LD_PRELOAD_A3F7, None)
    env.pop(K.S_DYLD_LIBRARY_PATH_A363, None)
    env.pop(K.S_DYLD_INSERT_LIBRARIES_7927, None)
    # Match the movie process's private decoder library path on Linux.
    if target == K.S_LINUX_F32A:
        env[K.S_LD_LIBRARY_PATH_6EAC] = str(decoder.parent.parent / K.S_LIB_5A09)
    if target == K.S_WINDOWS_E294:
        env[K.S_PATH_8306] = str(Path(os.environ.get(K.S_SYSTEMROOT_D7DA, K.S_C_WINDOWS_D008)) / K.S_SYSTEM32_0C2E)
    subprocess.run([str(decoder), K.S_VERSION_88D4], check=True, capture_output=True,
                   env=env, timeout=K.N_30_624B)


def android(args):
    from conversion_progress import ConversionProgress
    from setup_android import ensure
    steps = [K.S_PREPARE_ANDROID_TOOLS_E64F]
    if args.nsp:
        steps.append(K.S_EXTRACT_GAME_AA35)
    progress = ConversionProgress([*steps, K.S_PREPARE_SYMBOLS_683B, K.S_TRANSLATE_GAME_CODE_80DD,
                                   K.S_TRANSLATE_SDK_CODE_912B, K.S_BUILD_ANDROID_APK_7619, K.S_PACKAGE_APK_C775])
    progress.advance()
    env = ensure(accept_licenses=args.accept_android_licenses)
    if args.nsp:
        progress.advance()
        subprocess.run([sys.executable, str(ROOT / K.S_TOOLS_EXTRACT_PY_B0D4), str(args.nsp.resolve()),
                        str(args.keys.resolve())], check=True, env=env)
        game = ROOT / K.S_GAME_EXTRACTED_UPDATE_E969
    else:
        game = args.game.resolve()
    from convert import source_ids
    progress.advance()
    source_ids(game)
    from analyze_il2cpp import prepare_symbols
    symbols = prepare_symbols(game)
    for name, base in ((K.S_MAIN_13FD, K.S_0X7000000000_2DF4), (K.S_SDK_0065, K.S_0X7004000000_DDC3)):
        progress.advance()
        command = [sys.executable, str(ROOT / K.S_TOOLS_RECOMPR_GEN_PY_1453), name,
                   str(game / K.S_EXEFS_00A0 / name), base, str(ROOT / K.S_GAME_GEN_45C5 / name), K.S_SIMD_C_D046]
        if name == K.S_MAIN_13FD:
            command += [K.S_SYMBOLS_1344, str(symbols)]
        subprocess.run(command, check=True, env=env)
    progress.advance()
    subprocess.run([sys.executable, str(ROOT / K.S_TOOLS_BUILD_ANDROID_PY_ACAD), K.S_GAME_1AB6, str(game),
                    K.S_ABI_BED3, args.abi], check=True, env=env)
    progress.advance()
    args.output.mkdir(parents=True, exist_ok=True)
    apk = args.output / f"sf2-android-{args.abi}.apk"
    from build_android import publish_apk
    publish_apk(ROOT / f"build/android/sf2-recomp-{args.abi}-debug.apk", apk)
    print(K.S_INSTALL_THIS_APK_ON_YOUR_AND_69CF, apk)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    from version import read
    parser.add_argument(K.S_VERSION_B4E3, action=K.S_VERSION_25EB, version=K.S_SF2RECOMP_7368 + read()[K.S_VERSION_25EB])
    source = parser.add_mutually_exclusive_group()
    source.add_argument(K.S_NSP_C95D, type=Path)
    source.add_argument(K.S_GAME_1AB6, type=Path, help=K.S_ALREADY_EXTRACTED_UPDATED_GA_4AE8)
    parser.add_argument(K.S_KEYS_B749, type=Path)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, default=ROOT / K.S_OUT_SHADOW_FIGHT_2_F137)
    parser.add_argument(K.S_ANDROID_7590, action=K.S_STORE_TRUE_2C29, help=K.S_BUILD_AN_APK_USING_THE_OPTIO_88B6)
    parser.add_argument(K.S_ABI_BED3, choices=(K.S_ARM64_V8A_ACD6, K.S_X86_64_CB4A), default=K.S_ARM64_V8A_ACD6)
    parser.add_argument(K.S_ACCEPT_ANDROID_LICENSES_236F, action=K.S_STORE_TRUE_2C29)
    parser.add_argument(K.S_JOBS_0E7C, type=int, default=min(os.cpu_count() or 1, K.N_8_2C62))
    parser.add_argument(K.S_CHECK_464A, action=K.S_STORE_TRUE_2C29, help=K.S_TEST_THE_RELEASE_WITHOUT_GAM_F65A)
    parser.add_argument(K.S_PLAY_7BC5, action=K.S_STORE_TRUE_2C29, help=K.S_START_THE_CONVERTED_DESKTOP__6F60)
    parser.add_argument(K.S_INSTALL_DESKTOP_337D, action=K.S_STORE_TRUE_430C, help=K.S_ADD_THE_CONVERTER_TO_THE_LIN_648B)
    parser.add_argument(K.S_GUI_39FA, action=K.S_STORE_TRUE_430C, help=K.S_OPEN_THE_CONVERSION_WINDOW_1417)
    parser.add_argument(K.S_CLI_30A2, action=K.S_STORE_TRUE_430C, help=K.S_USE_INTERACTIVE_TERMINAL_PRO_EA78)
    args = parser.parse_args(argv)
    data = manifest()
    if not data:
        raise ValueError(K.S_USE_TOOLS_CONVERT_PY_IN_A_SO_3077)
    from host_tools import host_target
    if data[K.S_TARGET_0B9D] != host_target():
        raise ValueError(K.S_DOWNLOAD_THE_SF2RECOMP_RELEA_B302)
    if args.install_desktop:
        if data[K.S_TARGET_0B9D] != K.S_LINUX_F32A:
            parser.error(K.S_INSTALL_DESKTOP_IS_AVAILABLE_1EE9)
        from desktop_integration import CONVERTER_ID, LINUX_CONVERTER_ICON, linux_desktop, register_linux_desktop
        entry = linux_desktop(ROOT / K.S_SF2RECOMP_50A6, ROOT / LINUX_CONVERTER_ICON, title=K.S_SF2RECOMP_2D44,
                              identifier=CONVERTER_ID, version=data[K.S_VERSION_43CC], category=K.S_UTILITY_2BDB,
                              window_class=K.S_SF2RECOMP_0F38)
        register_linux_desktop(CONVERTER_ID, entry)
        return 0
    if args.gui or not (sys.argv[1:] if argv is None else argv):
        from converter_ui import main as gui_main
        return gui_main()
    if args.jobs < 1:
        parser.error(K.S_JOBS_MUST_BE_POSITIVE_A3BA)
    if args.check:
        with conversion_lock():
            check()
        return 0
    from conversion_log import LOG_ACTIVE, run_logged_cli
    if argv is None and not os.environ.get(LOG_ACTIVE):
        return run_logged_cli(Path(__file__).with_name(K.S_RELEASE_ENTRY_PY_4DB7), sys.argv[1:], isolated=True)
    if not args.nsp and not args.game:
        if not sys.stdin.isatty():
            parser.error(K.S_PROVIDE_NSP_AND_KEYS_OR_GAME_3901)
        print(K.S_SF2RECOMP_CONVERT_YOUR_OWN_S_266E)
        args.nsp = Path(input(K.S_NSP_FILE_D5CD).strip().strip(K.S_EMPTY_3251))
        args.keys = Path(input(K.S_PROD_KEYS_FILE_C001).strip().strip(K.S_EMPTY_3251))
        choice = input(K.S_BUILD_FOR_THIS_DESKTOP_OR_AN_15E4).strip().lower()
        args.android = choice == K.S_ANDROID_63EA
        destination = input(f"Output folder [{args.output}]: ").strip().strip(K.S_EMPTY_3251)
        if destination:
            args.output = Path(destination)
        from setup_android import ready as android_ready
        if args.android and not args.accept_android_licenses and not android_ready():
            print(K.S_ANDROID_DOWNLOADS_SEVERAL_GB_7963)
            args.accept_android_licenses = input(K.S_ACCEPT_THE_ANDROID_SDK_LICEN_2C63).strip().lower() == K.S_YES_31EB
    if args.nsp and not args.keys:
        parser.error(K.S_NSP_REQUIRES_KEYS_930A)
    if args.android and args.play:
        parser.error(K.S_PLAY_IS_FOR_THE_DESKTOP_GAME_6825)
    args.output = args.output.resolve()
    # These paths belong to the release, never the caller's development tools.
    os.environ.update(environment())
    with conversion_lock():
        if args.android:
            android(args)
        else:
            import convert
            command = [K.S_TARGET_2BBE, data[K.S_TARGET_0B9D], K.S_OUTPUT_3033, str(args.output), K.S_JOBS_0E7C, str(args.jobs)]
            command += [K.S_NSP_C95D, str(args.nsp), K.S_KEYS_B749, str(args.keys)] if args.nsp else [K.S_GAME_1AB6, str(args.game)]
            if convert.main(command):
                return 1
    if args.play:
        windows = os.name == K.S_NT_88F0
        executable = args.output / (K.S_SF2_EXE_6245 if windows else K.S_SF2_4CDA)
        subprocess.run([str(executable)], cwd=args.output, check=True)
    return 0


if __name__ == K.S_MAIN_ACB5:
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(K.S_SF2RECOMP_8F7E, error, file=sys.stderr)
        sys.exit(1)

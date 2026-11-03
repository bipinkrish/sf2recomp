#!/usr/bin/env python3
"""Build a portable SF2Recomp distribution using only project-owned inputs.

Run on each native build host. A relocatable python-build-standalone prefix,
populated with tools/{requirements,release-requirements}.txt, is required.
"""
from _sf2_tool_constants import PACKAGE_RELEASE as K
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

from host_tools import ROOT, dotnet_sdk, host_target, run


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return destination


def tracked_sources(destination):
    # Deliberate allowlist, never recursively copy the working directory. In
    # particular game/, build/, keys, tickets and local APKs cannot enter a release.
    names = subprocess.check_output([K.S_GIT_1023, K.S_LS_FILES_99C1, K.S_Z_F171], cwd=ROOT).decode().split(K.S_X00_17A1)
    # New release sources may still be uncommitted while locally validating.
    names += [str(p.relative_to(ROOT)) for p in (ROOT / K.S_TOOLS_F83E).glob(K.S_PY_0398)]
    names += [str(p.relative_to(ROOT)) for p in (ROOT / K.S_RUNTIME_A233).rglob(K.S_EMPTY_15A7) if p.is_file()]
    names += [str(p.relative_to(ROOT)) for p in (ROOT / K.S_TOOLS_RECOMPR_6C0F).glob(K.S_PY_0398)]
    names += [str(p.relative_to(ROOT)) for p in (ROOT / K.S_LICENSES_A26A).glob(K.S_TXT_2E63)]
    names += [str(p.relative_to(ROOT)) for p in (ROOT / K.S_ANDROID_APP_SRC_B0FE).rglob(K.S_EMPTY_15A7) if p.is_file()]
    names += [K.S_ANDROID_TOOLCHAIN_PROPERTIES_C485]
    names += [K.S_TOOLS_RELEASE_REQUIREMENTS_T_C924]
    for name in sorted(set(names)):
        if (name.startswith((K.S_RUNTIME_DCB2, K.S_ANDROID_6555, K.S_LICENSES_A259, K.S_PROFILES_COMPLETED_7E4B, K.S_TOOLS_RECOMPR_D6DD))
                or re.fullmatch(K.S_TOOLS_PY_JSON_TXT_5978, name)
                or name in (K.S_LICENSE_6485, K.S_THIRD_PARTY_NOTICES_F9CA, K.S_SF2RECOMP_INI_8714, K.S_TOOLS_IL2CPP_CONFIG_JSON_BC09)):
            path = ROOT / name
            if path.is_file():
                copy(path, destination / name)
    shutil.copytree(ROOT / K.S_THIRD_PARTY_SMAA_8DBE, destination / K.S_THIRD_PARTY_SMAA_8DBE)


def managed_helpers(destination, rid):
    import build_il2cpp
    build_il2cpp.ensure()  # Downloads and verifies the exact upstream source.
    sdk = dotnet_sdk()
    with tempfile.TemporaryDirectory(dir=ROOT / K.S_BUILD_0BA9) as temporary:
        source = Path(temporary)
        with tarfile.open(ROOT / K.S_BUILD_TOOLS_IL2CPP_SOURCE_TA_A6A4) as archive:
            archive.extractall(source, filter=K.S_DATA_7553)
        project = source / f"Il2CppDumper-{build_il2cpp.COMMIT}/Il2CppDumper/Il2CppDumper.csproj"
        for name, path in ((K.S_IL2CPP_38B2, project), (K.S_BKTR_7B22, ROOT / K.S_TOOLS_BKTR_SF2BKTR_CSPROJ_431A)):
            run(sdk, K.S_PUBLISH_8AD6, path, K.S_C_3F7E, K.S_RELEASE_D1CF, K.S_R_99C5, rid, K.S_SELF_CONTAINED_DB33, K.S_TRUE_DFF8,
                K.S_P_USEAPPHOST_TRUE_F922, K.S_P_TARGETFRAMEWORKS_NET8_0_D829, K.S_P_TARGETFRAMEWORK_NET8_0_A0C0, K.S_F_7900, K.S_NET8_0_6DBB,
                K.S_O_010C, destination / K.S_NATIVE_A716 / name, K.S_NOLOGO_5926)
    copy(ROOT / K.S_TOOLS_IL2CPP_CONFIG_JSON_BC09, destination / K.S_NATIVE_IL2CPP_CONFIG_JSON_C22B)


def find_library(name, directories, suffix):
    for directory in directories:
        path = directory / (K.S_LIB_427F + name + suffix)
        if path.exists():
            return path
    raise ValueError(f"Cannot locate {name} in {directories}")


def linux_libraries(destination, sdl, helpers):
    # Build on Ubuntu 24.04 in CI, so the non-glibc libraries have a fixed baseline.
    directories = [sdl / K.S_LIB_427F, Path(K.S_USR_LIB_X86_64_LINUX_GNU_359A), Path(K.S_LIB_X86_64_LINUX_GNU_6CDA)]
    roots = [find_library(name, directories, K.S_SO_FD8B) for name in (K.S_SDL3_20ED, K.S_SHADERC_7D52, K.S_VULKAN_D1D8)]
    roots += [Path(subprocess.check_output([K.S_CLANG_E943, K.S_PRINT_FILE_NAME_LIBSTDC_SO_733C], text=True).strip()),
              Path(subprocess.check_output([K.S_CLANG_5CE7, K.S_PRINT_FILE_NAME_LIBATOMIC_SO_166A], text=True).strip())]
    roots += [p for p in (Path(K.S_USR_LIB_X86_64_LINUX_GNU_LIB_A08A), Path(K.S_USR_LIB_X86_64_LINUX_GNU_LIB_8C6E)) if p.exists()]
    pending = [*roots, *helpers]
    license_sources = list(roots)
    copied, visited = {}, set()
    system = re.compile(K.S_LD_LINUX_LIB_C_M_DL_PTHREAD__5BFD)
    patchelf = shutil.which(K.S_PATCHELF_FA8A)
    if not patchelf:
        raise ValueError(K.S_RELEASE_ASSEMBLY_NEEDS_PATCH_307F)
    def install(path):
        name = subprocess.check_output([patchelf, K.S_PRINT_SONAME_B76A, str(path)], text=True).strip() or path.name
        if name not in copied:
            copied[name] = copy(path.resolve(), destination / K.S_NATIVE_LIB_601C / name)
        return K.S_ROOT_NATIVE_LIB_F292 + name
    link = [install(path) for path in roots[:K.N_5_EF2D]]
    gcc_runtime = Path(subprocess.check_output([K.S_CLANG_5CE7, K.S_PRINT_LIBGCC_FILE_NAME_DC22], text=True).strip())
    copy(gcc_runtime, destination / K.S_NATIVE_LIB_LIBGCC_A_6EBE)
    link += [K.S_ROOT_NATIVE_LIB_LIBGCC_A_5B88]
    gcc_shared = find_library(K.S_GCC_S_E377, directories, K.S_SO_1_EE4D)
    link.append(install(gcc_shared))
    pending.append(gcc_shared)
    while pending:
        binary = pending.pop()
        if binary.resolve() in visited:
            continue
        visited.add(binary.resolve())
        result = subprocess.check_output([K.S_LDD_2B62, str(binary)], text=True)
        if K.S_NOT_FOUND_B9FC in result:
            raise ValueError(f"Missing release dependency for {binary}:\n{result}")
        for name, path in re.findall(K.S_S_S_S_DD8C, result, re.MULTILINE):
            if not system.match(name):
                install(Path(path))
                pending.append(Path(path))
                license_sources.append(Path(path))
    for path in roots[K.N_5_EF2D:]:
        install(path)
    for path in copied.values():
        run(patchelf, K.S_SET_RPATH_525B, K.S_ORIGIN_737F, path)
    for path in helpers:
        if path.read_bytes()[:K.N_4_4B22] == K.B_B_X7FELF_C9CC:
            relative = os.path.relpath(destination / K.S_NATIVE_LIB_601C, path.parent)
            run(patchelf, K.S_SET_RPATH_525B, K.S_ORIGIN_7DCB + relative, path)
    notices = destination / K.S_NATIVE_LICENSES_9EA2
    notices.mkdir(parents=True, exist_ok=True)
    for source in [*license_sources, gcc_runtime, gcc_shared]:
        result = subprocess.run([K.S_DPKG_QUERY_54DC, K.S_S_2D13, str(source.resolve())], capture_output=True, text=True)
        for line in result.stdout.splitlines():
            package = line.rsplit(K.S_EMPTY_7EEF, 1)[0].split(K.S_EMPTY_4266)[0]
            copyright_file = Path(K.S_USR_SHARE_DOC_8FAB) / package / K.S_COPYRIGHT_8894
            if copyright_file.is_file():
                copy(copyright_file, notices / (package + K.S_COPYRIGHT_TXT_2038))
    return link


def macos_libraries(destination, sdl, helpers):
    from macos_dylibs import linked_libraries, relocate
    brew = Path(subprocess.check_output([K.S_BREW_1BEA, K.S_PREFIX_061F], text=True).strip())
    directories = [sdl / K.S_LIB_427F, brew / K.S_LIB_427F]
    roots = [find_library(name, directories, K.S_DYLIB_CB5B) for name in (K.S_SDL3_20ED, K.S_SHADERC_SHARED_F65B, K.S_VULKAN_D1D8)]
    molten = Path(subprocess.check_output([K.S_BREW_1BEA, K.S_PREFIX_061F, K.S_MOLTEN_VK_0692], text=True).strip()) / K.S_LIB_LIBMOLTENVK_DYLIB_61C2
    pending = [*roots, molten, *helpers]
    copied, visited = {}, set()
    while pending:
        binary = pending.pop()
        actual = binary.resolve()
        if actual in visited:
            continue
        visited.add(actual)
        if binary not in helpers:
            copied[binary.name] = copy(actual, destination / K.S_NATIVE_LIB_601C / binary.name)
            prefix = actual.parent.parent
            for pattern in (K.S_LICENSE_C5DC, K.S_NOTICE_66F3, K.S_COPYING_A9EF):
                for notice in prefix.glob(pattern):
                    if notice.is_file():
                        copy(notice, destination / K.S_NATIVE_LICENSES_9EA2 / prefix.parent.name / notice.name)
        for name in linked_libraries(binary):
            if name.startswith((K.S_USR_LIB_1101, K.S_SYSTEM_LIBRARY_F934)):
                continue
            if name.startswith(K.S_LOADER_PATH_1531):
                path = binary.parent / name.removeprefix(K.S_LOADER_PATH_1531)
            elif name.startswith(K.S_RPATH_6EEF):
                path = next((d / Path(name).name for d in [binary.parent, *directories] if (d / Path(name).name).exists()), None)
                if path is None:
                    raise ValueError(f"Cannot resolve {name} from {binary}")
            else:
                path = Path(name)
            if path.resolve() != actual:
                pending.append(path)
    relocate(helpers, list(copied.values()))
    (destination / K.S_NATIVE_LICENSES_9EA2).mkdir(parents=True, exist_ok=True)
    (destination / K.S_NATIVE_LICENSES_HOMEBREW_SOU_5CCA).write_text(
        subprocess.check_output([K.S_BREW_1BEA, K.S_INFO_EF33, K.S_JSON_V2_DCAF, K.S_INSTALLED_430F], text=True), encoding=K.S_UTF_8_5383)
    (destination / K.S_NATIVE_MOLTENVK_ICD_JSON_4F2D).write_text(json.dumps({K.S_FILE_FORMAT_VERSION_F7AD: K.S_1_0_0_6533,
        K.S_ICD_5FD1: {K.S_LIBRARY_PATH_5872: K.S_LIB_LIBMOLTENVK_DYLIB_61C2, K.S_API_VERSION_05A5: K.S_1_3_0_D6F4, K.S_IS_PORTABILITY_DRIVER_1913: True}}))
    return [K.S_ROOT_NATIVE_LIB_F292 + p.name for p in roots]


def windows_toolchain(destination, prefix, helpers):
    from windows_dlls import dependency_closure
    toolchain = destination / K.S_NATIVE_TOOLCHAIN_61F4
    for name in (K.S_CLANG_EXE_9FB8, K.S_LD_LLD_EXE_4C6B):
        copy(prefix / K.S_BIN_0955 / name, toolchain / K.S_BIN_0955 / name)
    shutil.copytree(prefix / K.S_LIB_CLANG_EF86, toolchain / K.S_LIB_CLANG_EF86)
    shutil.copytree(prefix / K.S_INCLUDE_8C9D, toolchain / K.S_INCLUDE_8C9D)
    # UCRT64 CRT/import libraries make the compiler independent of MSYS2.
    for path in (prefix / K.S_LIB_427F).glob(K.S_A_0DDF):
        copy(path, toolchain / K.S_LIB_427F / path.name)
    seeds = [*helpers, *list((toolchain / K.S_BIN_0955).glob(K.S_EXE_4D40)),
             prefix / K.S_BIN_SDL3_DLL_5FEC, prefix / K.S_BIN_LIBSHADERC_SHARED_DLL_0CDB]
    libraries = dependency_closure(seeds, [prefix / K.S_BIN_0955, *(p.parent for p in helpers)])
    for path in [prefix / K.S_BIN_SDL3_DLL_5FEC, prefix / K.S_BIN_LIBSHADERC_SHARED_DLL_0CDB, *libraries.values()]:
        copy(path, destination / K.S_NATIVE_LIB_601C / path.name)
    # LLVM's DLLs must also be adjacent to clang/LLD for loader startup.
    for path in (destination / K.S_NATIVE_LIB_601C).glob(K.S_DLL_166A):
        copy(path, toolchain / K.S_BIN_0955 / path.name)
    resources = sorted((toolchain / K.S_LIB_CLANG_EF86).iterdir())[-1].name
    compiler = [K.S_ROOT_NATIVE_TOOLCHAIN_BIN_CL_F3AB, K.S_SYSROOT_ROOT_NATIVE_TOOLCHAI_6686,
                K.S_TARGET_X86_64_W64_WINDOWS_GN_8EC4, K.S_RESOURCE_DIR_ROOT_NATIVE_TOO_443D + resources,
                K.S_B_ROOT_NATIVE_TOOLCHAIN_BIN_0898, K.S_L_ROOT_NATIVE_TOOLCHAIN_LIB_AEFE]
    return compiler


def write_launchers(destination, windows):
    if windows:
        (destination / K.S_SF2RECOMP_CMD_6CF6).write_text(K.S_ECHO_OFF_R_NSETLOCAL_R_NSET__6F91, encoding=K.S_UTF_8_5383)
    else:
        text = K.S_BIN_SH_NCASE_0_IN_FOLDER_0_F_F136
        for name in (K.S_SF2RECOMP_14FD, K.S_SF2RECOMP_COMMAND_D317):
            path = destination / name
            path.write_text(text, encoding=K.S_UTF_8_5383)
            path.chmod(K.N_493_560A)


def audit(destination):
    forbidden = {K.S_NSP_BD26, K.S_NSZ_FBB4, K.S_NCA_8098, K.S_NSO_4A4A, K.S_XCI_A600, K.S_TIK_71B5, K.S_KEYS_CE28, K.S_APK_24FB}
    for path in destination.rglob(K.S_EMPTY_15A7):
        relative = path.relative_to(destination)
        if relative.parts[0] in (K.S_GAME_CF8E, K.S_BUILD_0BA9, K.S_OUT_FB65, K.S_EXTENSIONS_CAF7, K.S_CACHE_5BBF) or path.suffix.lower() in forbidden:
            raise ValueError(f"Private/generated content in release: {relative}")
        if path.is_symlink() and not path.resolve().is_relative_to(destination.resolve()):
            raise ValueError(f"Release symlink escapes its folder: {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_PYTHON_PREFIX_81DB, type=Path, required=True)
    parser.add_argument(K.S_VERSION_B4E3, required=True)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, default=ROOT / K.S_BUILD_RELEASES_375F)
    parser.add_argument(K.S_SKIP_PREPARE_DA0C, action=K.S_STORE_TRUE_2C29, help=K.S_REUSE_ALREADY_BUILT_NATIVE_R_2E73)
    parser.add_argument(K.S_JOBS_0E7C, type=int, default=min(os.cpu_count() or 1, K.N_8_2C62))
    args = parser.parse_args()
    from version import read
    version = read()
    if args.version != version[K.S_VERSION_25EB]:
        parser.error(K.S_VERSION_MUST_MATCH_RUNTIME_V_19B5)
    if not re.fullmatch(K.S_A_ZA_Z0_9_CD79, args.version):
        parser.error(K.S_VERSION_MUST_CONTAIN_ONLY_LE_C5E1)
    target = host_target()
    arch = K.S_ARM64_6AF1 if platform.machine().lower() in (K.S_AARCH64_E802, K.S_ARM64_6AF1) else K.S_X86_64_CB4A
    rid = {K.S_LINUX_09D1: K.S_LINUX_X64_127A, K.S_WINDOWS_F2ED: K.S_WIN_X64_0E11, K.S_MACOS_91E2: K.S_OSX_BCFA + (K.S_ARM64_6AF1 if arch == K.S_ARM64_6AF1 else K.S_X64_2974)}[target]
    if target != K.S_MACOS_91E2 and arch != K.S_X86_64_CB4A:
        raise ValueError(K.S_DESKTOP_RELEASES_SUPPORT_LIN_A858)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = f"sf2recomp-{args.version}-{target}-{arch}"
    destination = output / name
    if destination.exists():
        raise ValueError(f"Release staging already exists: {destination}; use a fresh --output folder")
    destination.mkdir()
    prefix = (Path(subprocess.check_output([K.S_CYGPATH_26B1, K.S_M_705B, os.environ.get(K.S_MINGW_PREFIX_49B8, K.S_UCRT64_3FD8)], text=True).strip())
              if target == K.S_WINDOWS_F2ED else None)
    sdl = Path(os.environ.get(K.S_SDL_DIR_2C71, prefix if prefix else ROOT / (K.S_DEPS_MACOS_FBC7 if target == K.S_MACOS_91E2 else K.S_DEPS_3548))).resolve()
    ffmpeg = Path(os.environ.get(K.S_FFMPEG_DIR_7F8C, ROOT / (K.S_DEPS_FFMPEG_660D if target == K.S_LINUX_09D1 else f"deps/ffmpeg-{target}"))).resolve()
    make = K.S_GMAKE_E81C if target == K.S_MACOS_91E2 else K.S_MAKE_D1F9
    runtime_build = ROOT / K.S_BUILD_RELEASE_RUNTIME_F59D
    if not args.skip_prepare:
        from build_extraction_tools import ensure_tools
        ensure_tools(target)
        run(sys.executable, ROOT / K.S_TOOLS_SETUP_FFMPEG_PY_88E9, K.S_ENSURE_C475, K.S_TARGET_2BBE, target)
        run(make, f"TARGET={target}", K.S_B_BUILD_RELEASE_RUNTIME_108D, f"SDL_DIR={sdl.as_posix()}",
            f"PYTHON={Path(sys.executable).as_posix()}", f"-j{args.jobs}", K.S_RUNTIME_OBJS_B904, cwd=ROOT)
    tracked_sources(destination)
    shutil.copytree(args.python_prefix.resolve(), destination / K.S_PYTHON_0529, symlinks=True,
                    ignore=shutil.ignore_patterns(K.S_PYCACHE_6B2B, K.S_PYC_6D8E, K.S_EXTERNALLY_MANAGED_4ED4, K.S_PATCHELF_5CE9))
    # python-build-standalone can ship absolute interpreter aliases. Make them relative.
    for path in (destination / K.S_PYTHON_0529).rglob(K.S_EMPTY_15A7):
        if path.is_symlink() and Path(os.readlink(path)).is_absolute():
            original = path.resolve()
            if original.is_relative_to(args.python_prefix.resolve()):
                path.unlink()
                path.symlink_to(os.path.relpath(destination / K.S_PYTHON_0529 / original.relative_to(args.python_prefix.resolve()), path.parent))
    objects = sorted((runtime_build / K.S_RT_3DE6).rglob(K.S_O_DB6A))
    if not objects:
        raise ValueError(K.S_NO_PREBUILT_RUNTIME_OBJECTS__5B15)
    archive = destination / K.S_NATIVE_RUNTIME_A_6CA1
    archive.parent.mkdir(parents=True, exist_ok=True)
    ar = shutil.which(K.S_LLVM_AR_16A1) or shutil.which(K.S_LLVM_AR_18_84A9) or shutil.which(K.S_AR_6C5B)
    run(ar, K.S_RCS_94B4, archive, *objects)
    from build_extraction_tools import tool_path
    suffix = K.S_EXE_A67B if target == K.S_WINDOWS_F2ED else K.S_EMPTY_6F49
    helpers = {}
    for tool in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5):
        path = copy(tool_path(tool, target), destination / K.S_NATIVE_HELPERS_4ED3 / (tool + suffix))
        helpers[tool] = [K.S_ROOT_A64C + path.relative_to(destination).as_posix()]
    managed_helpers(destination, rid)
    for tool, executable in ((K.S_BKTR_7B22, K.S_SF2BKTR_F7F7), (K.S_IL2CPP_38B2, K.S_IL2CPPDUMPER_2793)):
        helpers[tool] = [f"{{root}}/native/{tool}/{executable}{suffix}"]
    helper_binaries = [destination / command[0].removeprefix(K.S_ROOT_A64C) for command in helpers.values()]
    from setup_fonts import FONT_DIRECTORY
    shutil.copytree(FONT_DIRECTORY, destination / FONT_DIRECTORY.relative_to(ROOT))
    ffmpeg_destination = destination / K.S_DEPS_3548 / ffmpeg.name
    shutil.copytree(ffmpeg, ffmpeg_destination, symlinks=True)
    shutil.copytree(sdl / K.S_SHARE_LICENSES_384B, destination / K.S_NATIVE_LICENSES_9EA2, dirs_exist_ok=True) if (sdl / K.S_SHARE_LICENSES_384B).is_dir() else None
    if target == K.S_WINDOWS_F2ED:
        compiler = windows_toolchain(destination, prefix, helper_binaries)
        common = [K.S_MCX16_6162, K.S_D_FILE_OFFSET_BITS_64_65D6, K.S_D_WIN32_WINNT_0X0A00_53DA]
        probe = [K.S_FUSE_LD_LLD_F8AB, K.S_PTHREAD_AF25, K.S_LM_656A]
        libraries = [K.S_LSDL3_C720, K.S_LVULKAN_1_00BF, K.S_LSHADERC_SHARED_B0DE, K.S_LSTDC_1622, K.S_LWINPTHREAD_D6C4,
                     K.S_LWS2_32_F964, K.S_LWINMM_8846, K.S_LSHELL32_F44A, K.S_LOLE32_E1E5, K.S_LADVAPI32_76E9, K.S_LUSER32_B276]
        link = [*probe, K.S_WL_WHOLE_ARCHIVE_0FF3, K.S_ROOT_NATIVE_RUNTIME_A_DC0A, K.S_WL_NO_WHOLE_ARCHIVE_ED9D,
                K.S_WL_STACK_16777216_AC84, *libraries]
    else:
        compiler = [K.S_PYTHON_44E8, K.S_M_705B, K.S_ZIGLANG_DE01, K.S_CC_C2C9]
        triple = (K.S_AARCH64_E802 if arch == K.S_ARM64_6AF1 else K.S_X86_64_CB4A) + (K.S_MACOS_15_0_F78C if target == K.S_MACOS_91E2 else K.S_LINUX_GNU_2_39_ACD3)
        compiler += [K.S_TARGET_B534, triple]
        common = [K.S_FPIC_23DC] + ([K.S_MCX16_6162] if arch == K.S_X86_64_CB4A else [])
        probe = [K.S_LM_656A, K.S_PTHREAD_AF25]
        if target == K.S_LINUX_09D1:
            libraries = linux_libraries(destination, sdl, helper_binaries)
            link = [K.S_WL_WHOLE_ARCHIVE_0FF3, K.S_ROOT_NATIVE_RUNTIME_A_DC0A, K.S_WL_NO_WHOLE_ARCHIVE_ED9D, *libraries,
                    K.S_RDYNAMIC_6578, K.S_WL_RPATH_ORIGIN_LIB_7E44, K.S_LM_656A, K.S_LDL_264D, K.S_PTHREAD_AF25]
        else:
            libraries = macos_libraries(destination, sdl, helper_binaries)
            # This project-authored interface stub describes only the four public
            # CoreFoundation symbols used by platform/macos.c. No Apple SDK ships.
            stub = destination / K.S_NATIVE_COREFOUNDATION_TBD_DFC2
            stub.write_text(K.S_TAPI_TBD_V3_NARCHS_ARM64_X86_F69E)
            link = [K.S_WL_FORCE_LOAD_ROOT_NATIVE_RU_2805, *libraries, K.S_ROOT_NATIVE_COREFOUNDATION_T_EB83,
                    K.S_LC_7BB6, K.S_WL_RPATH_EXECUTABLE_PATH_LIB_2A55, K.S_LM_656A, K.S_PTHREAD_AF25]
            from macos_dylibs import relocate
            ffmpeg_libs = list((ffmpeg_destination / K.S_LIB_427F).glob(K.S_DYLIB_75EC))
            relocate([ffmpeg_destination / K.S_BIN_FFMPEG_9C11], ffmpeg_libs)
    config = {K.S_SCHEMA_9B05: 1, **version, K.S_TARGET_0B9D: target, K.S_ARCH_94E7: arch,
              K.S_BUILD_ID_34C8: hashlib.sha256(archive.read_bytes()).hexdigest(),
              K.S_COMPILER_01AF: compiler, K.S_CFLAGS_4DFE: common, K.S_LDFLAGS_861D: link, K.S_PROBE_LDFLAGS_BA61: probe, K.S_HELPERS_7DB9: helpers}
    (destination / K.S_RELEASE_JSON_5FBE).write_text(json.dumps(config, indent=K.N_2_D473) + K.S_N_AD65, encoding=K.S_UTF_8_5383)
    write_launchers(destination, target == K.S_WINDOWS_F2ED)
    copy(ROOT / K.S_DOCS_RELEASE_QUICKSTART_TXT_C3FA, destination / K.S_START_HERE_TXT_AEEE)
    audit(destination)
    inventory = {str(p.relative_to(destination)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(destination.rglob(K.S_EMPTY_15A7)) if p.is_file()}
    (destination / K.S_RELEASE_FILES_JSON_D994).write_text(json.dumps(inventory, indent=K.N_2_D473) + K.S_N_AD65, encoding=K.S_UTF_8_5383)
    print(K.S_ASSEMBLED_B56B, destination)
    print(K.S_RUN_THE_RELOCATED_RELEASE_S__6CF5)


if __name__ == K.S_MAIN_ACB5:
    main()

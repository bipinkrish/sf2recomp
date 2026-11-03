#!/usr/bin/env python3
"""Build pinned native hactool/NSTool executables on Linux, macOS or Windows UCRT64."""
from _sf2_tool_constants import BUILD_EXTRACTION_TOOLS as K
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from host_tools import ROOT, digest, host_target, native_executable, require_host, run, tool_directory, tool_path
import setup_extraction


def recipe(target):
    return {K.S_TARGET_0B9D: target, K.S_LOCK_1CF9: digest(ROOT / K.S_TOOLS_EXTRACTION_DEPENDENCIE_C95A),
            K.S_CONSTANTS_A73B: digest(Path(__file__).with_name(K.S_SF2_TOOL_CONSTANTS_PY_B537)),
            K.S_CMAKE_38DF: digest(ROOT / K.S_TOOLS_EXTRACTION_CMAKELISTS__817D),
            K.S_DRIVER_0846: digest(ROOT / K.S_TOOLS_BUILD_EXTRACTION_TOOLS_F65F),
            K.S_MANIFEST_DB06: digest(ROOT / K.S_RUNTIME_PLATFORM_WINDOWS_MAN_62E2) if target == K.S_WINDOWS_F2ED else None,
            K.S_RESOURCE_FB17: digest(ROOT / K.S_TOOLS_EXTRACTION_WINDOWS_RC__1DFC) if target == K.S_WINDOWS_F2ED else None}


def installed(target):
    try:
        record = json.loads((tool_directory(target) / K.S_SF2_TOOLS_JSON_A00F).read_text(encoding=K.S_UTF_8_5383))
        names = {tool_path(name, target).name for name in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5)}
        return (record[K.S_RECIPE_29B4] == recipe(target) and names == record[K.S_FILES_BE37].keys()
                and all(native_executable(tool_directory(target) / name, target)
                        and digest(tool_directory(target) / name) == value for name, value in record[K.S_FILES_BE37].items()))
    except (OSError, ValueError, KeyError):
        return False


def build(target=None, jobs=None, offline=False):
    target = target or host_target()
    require_host(target)
    jobs = jobs if jobs is not None else int(os.environ.get(K.S_JOBS_ABED, min(os.cpu_count() or 1, K.N_8_2C62)))
    if jobs < 1:
        raise ValueError(K.S_JOBS_JOBS_MUST_BE_POSITIVE_9291)
    for command in (K.S_CMAKE_38DF, K.S_NINJA_F8C2, K.S_CLANG_5CE7, K.S_CLANG_E943):
        if not shutil.which(command):
            raise ValueError(f"Missing input-tool build dependency: {command}; see docs/windows.md or tools/README.md")
    sources = ROOT / K.S_TOOLS_THIRD_PARTY_B64E
    archives = Path(os.environ.get(K.S_SF2_SOURCE_ARCHIVES_E3CD, ROOT / K.S_BUILD_DEPENDENCIES_ARCHIVES_BD96)).resolve()
    setup_extraction.prepare(sources, archives, offline=offline)
    build_dir = ROOT / K.S_BUILD_TOOLS_EXTRACTION_47E0 / target
    run(K.S_CMAKE_38DF, K.S_S_2D13, ROOT / K.S_TOOLS_EXTRACTION_E02E, K.S_B_E004, build_dir, K.S_G_85F0, K.S_NINJA_0E7F,
        K.S_DCMAKE_BUILD_TYPE_RELEASE_08E3, K.S_DCMAKE_C_COMPILER_CLANG_15A5, K.S_DCMAKE_CXX_COMPILER_CLANG_048D)
    run(K.S_CMAKE_38DF, K.S_BUILD_65C4, build_dir, K.S_PARALLEL_CC42, jobs, K.S_TARGET_2BBE, K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5)
    outputs = [build_dir / K.S_BIN_0955 / tool_path(name, target).name for name in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5)]
    if not all(native_executable(path, target) for path in outputs):
        raise ValueError(K.S_INPUT_TOOL_BUILD_DID_NOT_PRO_398F)
    destination = tool_directory(target)
    destination.mkdir(parents=True, exist_ok=True)
    for source in outputs:
        staged = destination / (source.name + K.S_TMP_31A6)
        shutil.copy2(source, staged)
        staged.replace(destination / source.name)
    marker = destination / K.S_SF2_TOOLS_JSON_A00F
    pending = marker.with_suffix(K.S_TMP_31A6)
    pending.write_text(json.dumps({K.S_RECIPE_29B4: recipe(target), K.S_FILES_BE37: {p.name: digest(p) for p in outputs}}, indent=K.N_2_D473)
                       + K.S_N_AD65, encoding=K.S_UTF_8_5383)
    pending.replace(marker)
    print(f"Native {target} extraction tools ready: {destination}")


def ensure_tools(target=None):
    target = target or host_target()
    from release_support import manifest, helper
    if manifest():
        return tuple(Path(helper(name)[0]) for name in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5))
    if not installed(target):
        build(target)
    return tool_path(K.S_HACTOOL_A8CB, target), tool_path(K.S_NSTOOL_84D5, target)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_TARGET_2BBE, choices=(K.S_LINUX_09D1, K.S_WINDOWS_F2ED, K.S_MACOS_91E2), default=host_target())
    parser.add_argument(K.S_JOBS_0E7C, type=int)
    parser.add_argument(K.S_OFFLINE_42E7, action=K.S_STORE_TRUE_2C29, help=K.S_USE_THE_EXISTING_CHECKSUM_VE_C8F2)
    args = parser.parse_args(argv)
    try:
        build(args.target, args.jobs, args.offline)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Extraction-tool build failed: {error}", file=sys.stderr)
        return 1


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

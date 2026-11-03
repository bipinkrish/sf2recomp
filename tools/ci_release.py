#!/usr/bin/env python3
"""Prepare, relocate, verify and archive the same portable package CI publishes."""
from _sf2_tool_constants import CI_RELEASE as K
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def prepare_python():
    subprocess.run([K.S_UV_E1AC, K.S_PYTHON_0529, K.S_INSTALL_7EB3, K.S_3_12_12_FD18], check=True)
    python = subprocess.check_output([K.S_UV_E1AC, K.S_PYTHON_0529, K.S_FIND_1234, K.S_MANAGED_PYTHON_AF4A, K.S_3_12_12_FD18], text=True).strip()
    subprocess.run([K.S_UV_E1AC, K.S_PIP_39E4, K.S_INSTALL_7EB3, K.S_PYTHON_6240, python, K.S_SYSTEM_7D71, K.S_BREAK_SYSTEM_PACKAGES_46CF,
                    K.S_R_99C5, str(ROOT / K.S_TOOLS_REQUIREMENTS_TXT_1096), K.S_R_99C5, str(ROOT / K.S_TOOLS_RELEASE_REQUIREMENTS_T_C924)], check=True)
    with Path(os.environ[K.S_GITHUB_ENV_4B73]).open(K.S_A_D749, encoding=K.S_UTF_8_5383) as env:
        env.write(K.S_SF2_RELEASE_PYTHON_39E8 + python + K.S_N_AD65)


def verify_and_archive(folder):
    from package_release import audit
    audit(folder)
    data = json.loads((folder / K.S_RELEASE_JSON_5FBE).read_text())
    # Renaming catches baked-in prefixes; spaces exercise argument handling.
    moved = folder.parent / (K.S_PORTABLE_CHECK_92C3 + folder.name)
    folder.rename(moved)
    python = moved / K.S_PYTHON_0529 / (K.S_PYTHON_EXE_82C7 if os.name == K.S_NT_88F0 else K.S_BIN_PYTHON3_0540)
    env = os.environ.copy()
    for key in (K.S_PYTHONPATH_D57E, K.S_PYTHONHOME_E7F0, K.S_DOTNET_ROOT_BF5B, K.S_SDKROOT_3AA0, K.S_CPATH_DFA1, K.S_LIBRARY_PATH_5BFF, K.S_LD_LIBRARY_PATH_A0EE, K.S_DYLD_LIBRARY_PATH_984F, K.S_MSYSTEM_844D, K.S_MINGW_PREFIX_49B8):
        env.pop(key, None)
    env[K.S_PATH_B629] = str(moved / K.S_NATIVE_LIB_601C)
    try:
        subprocess.run([str(python), K.S_I_C6C0, str(moved / K.S_TOOLS_RELEASE_ENTRY_PY_A933), K.S_CHECK_464A], cwd=moved.parent, env=env, check=True)
    finally:
        # --check creates private writable caches. None may enter the archive.
        for name in (K.S_BUILD_0BA9, K.S_CACHE_5BBF, K.S_GAME_CF8E, K.S_OUT_FB65):
            shutil.rmtree(moved / name, ignore_errors=True)
        for cache in moved.rglob(K.S_PYCACHE_6B2B):
            shutil.rmtree(cache)
        moved.rename(folder)
    audit(folder)
    if os.name == K.S_NT_88F0:
        archive = folder.with_suffix(K.S_ZIP_14DF)
        with zipfile.ZipFile(archive, K.S_W_780F, zipfile.ZIP_DEFLATED, compresslevel=K.N_6_E7F6) as output:
            for path in sorted(folder.rglob(K.S_EMPTY_15A7)):
                if path.is_file():
                    output.write(path, path.relative_to(folder.parent))
    else:
        archive = folder.with_name(folder.name + K.S_TAR_GZ_6F43)
        with tarfile.open(archive, K.S_W_GZ_B81C) as output:
            output.add(folder, arcname=folder.name)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + K.S_SHA256_389B).write_text(f"{checksum}  {archive.name}\n", encoding=K.S_UTF_8_5383)
    print(K.S_VERIFIED_RELEASE_CAF5, archive)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_COMMAND_CFC6, choices=(K.S_PREPARE_PYTHON_C2A2, K.S_BUILD_0BA9, K.S_VERIFY_E290))
    parser.add_argument(K.S_FOLDER_312E, type=Path)
    args = parser.parse_args()
    if args.command == K.S_PREPARE_PYTHON_C2A2:
        prepare_python()
    elif args.command == K.S_VERIFY_E290:
        verify_and_archive(args.folder.resolve())
    else:
        from version import read
        subprocess.run([sys.executable, str(ROOT / K.S_TOOLS_PACKAGE_RELEASE_PY_A93F), K.S_PYTHON_PREFIX_81DB, sys.prefix,
                        K.S_VERSION_B4E3, read()[K.S_VERSION_25EB]], cwd=ROOT, check=True)
        folders = [p for p in (ROOT / K.S_BUILD_RELEASES_375F).iterdir() if p.is_dir()]
        if len(folders) != 1:
            raise ValueError(K.S_EXPECTED_EXACTLY_ONE_STAGED__85B7)
        verify_and_archive(folders[0])


if __name__ == K.S_MAIN_ACB5:
    main()

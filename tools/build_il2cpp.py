#!/usr/bin/env python3
"""Build the pinned official Il2CppDumper source into ignored local tools."""
from _sf2_tool_constants import BUILD_IL2CPP as K
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import tempfile
import sys
from host_tools import digest, dotnet_sdk, run

ROOT = Path(__file__).resolve().parent.parent
COMMIT = K.COMMIT  # official v6.7.46
SHA256 = K.SHA256
URL = f"https://codeload.github.com/Perfare/Il2CppDumper/tar.gz/{COMMIT}"


def installed():
    target = ROOT / K.S_TOOLS_IL2CPP_2F50
    try:
        record = json.loads((target / K.S_SF2_BUILD_JSON_ADB8).read_text(encoding=K.S_UTF_8_5383))
        required = {K.S_IL2CPPDUMPER_DLL_B554, K.S_MONO_CECIL_DLL_401F, K.S_IL2CPPDUMPER_DEPS_JSON_CDBB, K.S_IL2CPPDUMPER_RUNTIMECONFIG_J_E2E4}
        return (record[K.S_COMMIT_73BC] == COMMIT and record[K.S_SOURCE_SHA256_91CF] == SHA256
                and required <= record[K.S_FILES_BE37].keys()
                and all(digest(target / name) == value for name, value in record[K.S_FILES_BE37].items()))
    except (OSError, ValueError, KeyError):
        return False


def build():
    dotnet = dotnet_sdk()
    cache = ROOT / K.S_BUILD_TOOLS_IL2CPP_3F1C
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / K.S_SOURCE_TAR_GZ_5E36
    if not archive.exists():
        print(K.S_DOWNLOADING_OFFICIAL_IL2CPPD_E869, flush=True)
        with urllib.request.urlopen(URL, timeout=K.N_60_39FA) as source:
            data = source.read()
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise ValueError(K.S_IL2CPPDUMPER_SOURCE_CHECKSUM_6AEA)
        archive.write_bytes(data)
    if digest(archive) != SHA256:
        raise ValueError(f"Unexpected source archive; remove {archive} and retry.")
    # Build from a fresh verified tree so stale or locally added source files do
    # not enter the next publish. No shell or Windows apphost is needed.
    with tempfile.TemporaryDirectory(prefix=K.S_BUILD_FFD5, dir=cache) as temporary:
        stage = Path(temporary)
        with tarfile.open(archive) as source:
            source.extractall(stage, filter=K.S_DATA_7553)
        project = stage / f"Il2CppDumper-{COMMIT}/Il2CppDumper/Il2CppDumper.csproj"
        output = stage / K.S_PUBLISH_8AD6
        run(dotnet, K.S_PUBLISH_8AD6, project, K.S_C_3F7E, K.S_RELEASE_D1CF, K.S_F_7900, K.S_NET6_0_4DD4, K.S_P_TARGETFRAMEWORKS_NET6_0_9F11,
            K.S_P_USEAPPHOST_FALSE_8237, K.S_SELF_CONTAINED_DB33, K.S_FALSE_D719, K.S_NOLOGO_5926, K.S_O_010C, output)
        required = {K.S_IL2CPPDUMPER_DLL_B554, K.S_MONO_CECIL_DLL_401F, K.S_IL2CPPDUMPER_DEPS_JSON_CDBB, K.S_IL2CPPDUMPER_RUNTIMECONFIG_J_E2E4}
        files = {path.name: digest(path) for pattern in (K.S_DLL_166A, K.S_DEPS_JSON_894A, K.S_RUNTIMECONFIG_JSON_7081)
                 for path in output.glob(pattern)}
        if not required <= files.keys():
            raise ValueError(K.S_IL2CPPDUMPER_PUBLISH_DID_NOT_155F)
        target = ROOT / K.S_TOOLS_IL2CPP_2F50
        target.mkdir(parents=True, exist_ok=True)
        for name in files:
            pending = target / (name + K.S_TMP_31A6)
            shutil.copy2(output / name, pending)
            pending.replace(target / name)
        marker = target / K.S_SF2_BUILD_JSON_ADB8
        pending = marker.with_suffix(K.S_TMP_31A6)
        pending.write_text(json.dumps({K.S_COMMIT_73BC: COMMIT, K.S_SOURCE_SHA256_91CF: SHA256, K.S_FILES_BE37: files}, indent=K.N_2_D473) + K.S_N_AD65,
                           encoding=K.S_UTF_8_5383)
        pending.replace(marker)
    print(K.S_BUILT_LOCAL_TOOLS_IL2CPP_IL2_3DF7)


def ensure():
    if not installed():
        build()
    return ROOT / K.S_TOOLS_IL2CPP_IL2CPPDUMPER_DL_92DB


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_ENSURE_C475, action=K.S_STORE_TRUE_2C29, help=K.S_REUSE_AN_INTACT_BUILD_OF_THE_156E)
    args = parser.parse_args(argv)
    try:
        ensure() if args.ensure else build()
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"IL2CPP tool build failed: {error}", file=sys.stderr)
        return 1


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

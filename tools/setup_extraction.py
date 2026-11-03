#!/usr/bin/env python3
"""Fetch pinned, checksum-verified extraction-tool sources into an ignored folder."""
from _sf2_tool_constants import SETUP_EXTRACTION as K
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / K.S_TOOLS_EXTRACTION_DEPENDENCIE_C95A


def digest(path):
    with path.open(K.S_RB_81FE) as stream:
        return hashlib.file_digest(stream, K.S_SHA256_9465).hexdigest()


def installed(output, pins):
    try:
        record = json.loads((output / K.S_SF2_SOURCES_JSON_E557).read_text())
        return record[K.S_DEPENDENCIES_0F3F] == pins and all(
            (output / name).is_file() and digest(output / name) == expected
            for name, expected in record[K.S_FILES_BE37].items()
        )
    except (OSError, ValueError, KeyError):
        return False


def prepare(output, archives, offline=False, check=False):
    pins = json.loads(LOCK.read_text())
    if installed(output, pins):
        return
    if check:
        raise ValueError(K.S_EXTRACTION_SOURCES_MISSING_O_43B8)
    archives.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Verify every archive and stage all sources before replacing any old source.
    with tempfile.TemporaryDirectory(prefix=K.S_SF2_EXTRACTION_243D, dir=output.parent) as temporary:
        stage = Path(temporary)
        for item in pins:
            archive = archives / f"{item[K.S_NAME_8423]}-{item[K.S_REVISION_6501]}.tar.gz"
            if not archive.is_file():
                if offline:
                    raise ValueError(f"Offline source archive missing: {archive}")
                partial = archive.with_suffix(K.S_PART_BC6F)
                url = f"https://codeload.github.com/{item[K.S_REPOSITORY_0C0B]}/tar.gz/{item[K.S_REVISION_6501]}"
                try:
                    subprocess.run([
                        K.S_CURL_F8AE, K.S_FAIL_91BC, K.S_SILENT_90C6, K.S_SHOW_ERROR_194D, K.S_LOCATION_CAAB,
                        K.S_PROTO_0ADB, K.S_HTTPS_0B80, K.S_TLSV1_2_EC63, K.S_CONNECT_TIMEOUT_A6CF, K.S_20_1C3F,
                        K.S_MAX_TIME_7021, K.S_180_38B2, K.S_RETRY_04A1, K.S_2_5283, url, K.S_O_010C, str(partial),
                    ], check=True)
                    if digest(partial) != item[K.S_SHA256_9465]:
                        raise ValueError(f"Source archive checksum mismatch: {item[K.S_NAME_8423]}")
                    partial.replace(archive)
                finally:
                    partial.unlink(missing_ok=True)
            if digest(archive) != item[K.S_SHA256_9465]:
                raise ValueError(f"Source archive checksum mismatch: {archive}")
            unpack = stage / (K.S_UNPACK_A2EB + item[K.S_NAME_8423])
            unpack.mkdir()
            with tarfile.open(archive) as source:
                source.extractall(unpack, filter=K.S_DATA_7553)
            roots = list(unpack.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f"Unexpected archive layout: {item[K.S_NAME_8423]}")
            destination = stage / K.S_READY_BFF4 / item[K.S_DESTINATION_7AA5]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(roots[0], destination, dirs_exist_ok=True, symlinks=True)
        ready = stage / K.S_READY_BFF4
        files = {str(p.relative_to(ready)): digest(p) for p in ready.rglob(K.S_EMPTY_15A7) if p.is_file()}
        output.mkdir(parents=True, exist_ok=True)
        # Roll back publication if either directory cannot be installed.
        moved = []
        published = []
        try:
            for name in (K.S_HACTOOL_A8CB, K.S_NSTOOL_84D5):
                old = output / name
                if old.exists():
                    old.rename(stage / (K.S_PREVIOUS_982E + name))
                    moved.append(name)
                (ready / name).rename(old)
                published.append(name)
            record = stage / K.S_SOURCES_JSON_B6C7
            record.write_text(json.dumps({K.S_DEPENDENCIES_0F3F: pins, K.S_FILES_BE37: files}, indent=K.N_2_D473) + K.S_N_AD65)
            record.replace(output / K.S_SF2_SOURCES_JSON_E557)
        except BaseException:
            for name in published:
                shutil.rmtree(output / name)
            for name in moved:
                (stage / (K.S_PREVIOUS_982E + name)).rename(output / name)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_CHECK_464A, action=K.S_STORE_TRUE_2C29, help=K.S_VERIFY_INSTALLED_SOURCES_NO__1B1E)
    parser.add_argument(K.S_OFFLINE_42E7, action=K.S_STORE_TRUE_2C29, help=K.S_USE_VERIFIED_CACHED_ARCHIVES_D05A)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, default=ROOT / K.S_TOOLS_THIRD_PARTY_B64E)
    parser.add_argument(K.S_ARCHIVES_039E, type=Path, default=Path(os.environ.get(
        K.S_SF2_SOURCE_ARCHIVES_E3CD, ROOT / K.S_BUILD_DEPENDENCIES_ARCHIVES_BD96)))
    args = parser.parse_args()
    try:
        prepare(args.output, args.archives, args.offline, args.check)
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Extraction dependency setup failed: {error}\n")
    print(f"Extraction sources verified: {args.output}")


if __name__ == K.S_MAIN_ACB5:
    main()

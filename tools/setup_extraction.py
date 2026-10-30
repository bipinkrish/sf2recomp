#!/usr/bin/env python3
"""Fetch pinned, checksum-verified extraction-tool sources into an ignored folder."""
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
LOCK = ROOT / "tools/extraction-dependencies.json"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def installed(output, pins):
    try:
        record = json.loads((output / ".sf2-sources.json").read_text())
        return record["dependencies"] == pins and all(
            (output / name).is_file() and digest(output / name) == expected
            for name, expected in record["files"].items()
        )
    except (OSError, ValueError, KeyError):
        return False


def prepare(output, archives, offline=False, check=False):
    pins = json.loads(LOCK.read_text())
    if installed(output, pins):
        return
    if check:
        raise ValueError("Extraction sources missing or changed; run tools/build_extraction_tools.sh")
    archives.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Verify every archive and stage all sources before replacing any old source.
    with tempfile.TemporaryDirectory(prefix=".sf2-extraction-", dir=output.parent) as temporary:
        stage = Path(temporary)
        for item in pins:
            archive = archives / f"{item['name']}-{item['revision']}.tar.gz"
            if not archive.is_file():
                if offline:
                    raise ValueError(f"Offline source archive missing: {archive}")
                partial = archive.with_suffix(".part")
                url = f"https://codeload.github.com/{item['repository']}/tar.gz/{item['revision']}"
                try:
                    subprocess.run([
                        "curl", "--fail", "--silent", "--show-error", "--location",
                        "--proto", "=https", "--tlsv1.2", "--connect-timeout", "20",
                        "--max-time", "180", "--retry", "2", url, "-o", str(partial),
                    ], check=True)
                    if digest(partial) != item["sha256"]:
                        raise ValueError(f"Source archive checksum mismatch: {item['name']}")
                    partial.replace(archive)
                finally:
                    partial.unlink(missing_ok=True)
            if digest(archive) != item["sha256"]:
                raise ValueError(f"Source archive checksum mismatch: {archive}")
            unpack = stage / ("unpack-" + item["name"])
            unpack.mkdir()
            with tarfile.open(archive) as source:
                source.extractall(unpack, filter="data")
            roots = list(unpack.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f"Unexpected archive layout: {item['name']}")
            destination = stage / "ready" / item["destination"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(roots[0], destination, dirs_exist_ok=True, symlinks=True)
        ready = stage / "ready"
        files = {str(p.relative_to(ready)): digest(p) for p in ready.rglob("*") if p.is_file()}
        output.mkdir(parents=True, exist_ok=True)
        # Roll back publication if either directory cannot be installed.
        moved = []
        published = []
        try:
            for name in ("hactool", "nstool"):
                old = output / name
                if old.exists():
                    old.rename(stage / ("previous-" + name))
                    moved.append(name)
                (ready / name).rename(old)
                published.append(name)
            record = stage / "sources.json"
            record.write_text(json.dumps({"dependencies": pins, "files": files}, indent=2) + "\n")
            record.replace(output / ".sf2-sources.json")
        except BaseException:
            for name in published:
                shutil.rmtree(output / name)
            for name in moved:
                (stage / ("previous-" + name)).rename(output / name)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify installed sources, no downloads")
    parser.add_argument("--offline", action="store_true", help="use verified cached archives only")
    parser.add_argument("--output", type=Path, default=ROOT / "tools/third_party")
    parser.add_argument("--archives", type=Path, default=Path(os.environ.get(
        "SF2_SOURCE_ARCHIVES", ROOT / "build/dependencies/archives")))
    args = parser.parse_args()
    try:
        prepare(args.output, args.archives, args.offline, args.check)
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Extraction dependency setup failed: {error}\n")
    print(f"Extraction sources verified: {args.output}")


if __name__ == "__main__":
    main()

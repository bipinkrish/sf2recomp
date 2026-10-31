#!/usr/bin/env python3
"""Build the pinned official Il2CppDumper source into ignored local tools."""
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
COMMIT = "8a521b9c180cf13499253f0818cbc729dca767cb"  # official v6.7.46
SHA256 = "a23ba541d2b085c5c1ca94c9c72cf217b88c8600589c4554285778ae4f4df2a8"
URL = f"https://codeload.github.com/Perfare/Il2CppDumper/tar.gz/{COMMIT}"


def installed():
    target = ROOT / "tools/il2cpp"
    try:
        record = json.loads((target / ".sf2-build.json").read_text(encoding="utf-8"))
        required = {"Il2CppDumper.dll", "Mono.Cecil.dll", "Il2CppDumper.deps.json", "Il2CppDumper.runtimeconfig.json"}
        return (record["commit"] == COMMIT and record["source_sha256"] == SHA256
                and required <= record["files"].keys()
                and all(digest(target / name) == value for name, value in record["files"].items()))
    except (OSError, ValueError, KeyError):
        return False


def build():
    dotnet = dotnet_sdk()
    cache = ROOT / "build/tools/il2cpp"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "source.tar.gz"
    if not archive.exists():
        print("Downloading official Il2CppDumper v6.7.46 source...", flush=True)
        with urllib.request.urlopen(URL, timeout=60) as source:
            data = source.read()
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise ValueError("Il2CppDumper source checksum mismatch; nothing installed.")
        archive.write_bytes(data)
    if digest(archive) != SHA256:
        raise ValueError(f"Unexpected source archive; remove {archive} and retry.")
    # Build from a fresh verified tree so stale or locally added source files do
    # not enter the next publish. No shell or Windows apphost is needed.
    with tempfile.TemporaryDirectory(prefix=".build-", dir=cache) as temporary:
        stage = Path(temporary)
        with tarfile.open(archive) as source:
            source.extractall(stage, filter="data")
        project = stage / f"Il2CppDumper-{COMMIT}/Il2CppDumper/Il2CppDumper.csproj"
        output = stage / "publish"
        run(dotnet, "publish", project, "-c", "Release", "-f", "net6.0", "-p:TargetFrameworks=net6.0",
            "-p:UseAppHost=false", "--self-contained", "false", "--nologo", "-o", output)
        required = {"Il2CppDumper.dll", "Mono.Cecil.dll", "Il2CppDumper.deps.json", "Il2CppDumper.runtimeconfig.json"}
        files = {path.name: digest(path) for pattern in ("*.dll", "*.deps.json", "*.runtimeconfig.json")
                 for path in output.glob(pattern)}
        if not required <= files.keys():
            raise ValueError("Il2CppDumper publish did not produce all required assemblies/runtime manifests.")
        target = ROOT / "tools/il2cpp"
        target.mkdir(parents=True, exist_ok=True)
        for name in files:
            pending = target / (name + ".tmp")
            shutil.copy2(output / name, pending)
            pending.replace(target / name)
        marker = target / ".sf2-build.json"
        pending = marker.with_suffix(".tmp")
        pending.write_text(json.dumps({"commit": COMMIT, "source_sha256": SHA256, "files": files}, indent=2) + "\n",
                           encoding="utf-8")
        pending.replace(marker)
    print("Built local tools/il2cpp/Il2CppDumper.dll from verified source.")


def ensure():
    if not installed():
        build()
    return ROOT / "tools/il2cpp/Il2CppDumper.dll"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ensure", action="store_true", help="reuse an intact build of the pinned source")
    args = parser.parse_args(argv)
    try:
        ensure() if args.ensure else build()
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"IL2CPP tool build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

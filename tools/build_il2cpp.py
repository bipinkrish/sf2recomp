#!/usr/bin/env python3
"""Build the pinned official Il2CppDumper source into ignored local tools."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
COMMIT = "8a521b9c180cf13499253f0818cbc729dca767cb"  # official v6.7.46
SHA256 = "a23ba541d2b085c5c1ca94c9c72cf217b88c8600589c4554285778ae4f4df2a8"
URL = f"https://codeload.github.com/Perfare/Il2CppDumper/tar.gz/{COMMIT}"


def main():
    dotnet = os.environ.get("DOTNET", "dotnet")
    if not shutil.which(dotnet):
        raise SystemExit("Install the .NET 8 SDK before building Il2CppDumper.")
    cache = ROOT / "build/tools/il2cpp"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "source.tar.gz"
    if not archive.exists():
        print("Downloading official Il2CppDumper v6.7.46 source...", flush=True)
        with urllib.request.urlopen(URL, timeout=60) as source:
            data = source.read()
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise SystemExit("Il2CppDumper source checksum mismatch; nothing installed.")
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"Unexpected source archive; remove {archive} and retry.")
    with tarfile.open(archive) as source:
        source.extractall(cache, filter="data")
    project = cache / f"Il2CppDumper-{COMMIT}/Il2CppDumper/Il2CppDumper.csproj"
    output = cache / "publish"
    subprocess.run([dotnet, "publish", str(project), "-c", "Release", "-f", "net6.0",
                    "-p:TargetFrameworks=net6.0", "--self-contained", "false", "-o", str(output)], check=True)
    target = ROOT / "tools/il2cpp"
    for pattern in ("*.dll", "*.deps.json", "*.runtimeconfig.json"):
        for path in output.glob(pattern):
            shutil.copy2(path, target / path.name)
    print("Built local tools/il2cpp/Il2CppDumper.dll. Source and dependencies remain under build/tools/il2cpp.")


if __name__ == "__main__":
    main()

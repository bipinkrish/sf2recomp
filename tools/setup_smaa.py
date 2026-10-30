#!/usr/bin/env python3
"""Prepare checksum-pinned upstream SMAA files and the C shader include."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = "71c806a838bdd7d517df19192a20f0c61b3ca29d"
BASE_URL = f"https://raw.githubusercontent.com/iryoku/smaa/{REVISION}"
FILES = {
    "SMAA.hlsl": "4db53d92ef0b45661f8450303589ffabce93c352cae4282a6f32dd2b80efbeb9",
    "LICENSE.txt": "2b848b67ba50ac40b53e11cf1ccdaee2a190ca83b4652cba695a5f1ce81f52ae",
    "Textures/AreaTex.h": "1933bf43ce86ba71f7ed5f0fa94b148610c634dc3395b9bc67a817d51ee73af2",
    "Textures/SearchTex.h": "1fac315ab5f87b60083c9b8387af17f056204d3d1bf21500f2acc5b3e8c1a37a",
}


def verified(path, digest):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest


def shader_include(shader):
    # Upstream shader comments use Latin-1. Normalize the CRLF source exactly
    # as the formerly vendored C string, preserving the compiled shader text.
    text = shader.decode("latin-1").replace("\r\n", "\n")
    return "".join(json.dumps(line + "\n") + "\n" for line in text.splitlines())


def prepare(output, check=False):
    if check:
        if not all(verified(output / name, digest) for name, digest in FILES.items()):
            raise ValueError("SMAA missing or changed; run tools/setup_runtime.sh --fetch-smaa")
        expected = shader_include((output / "SMAA.hlsl").read_bytes())
        include = output / "smaa_hlsl.inc"
        if not include.is_file() or include.read_text() != expected:
            raise ValueError("SMAA shader include missing or outdated; run tools/setup_runtime.sh --fetch-smaa")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    source = os.environ.get("SMAA_SOURCE_DIR")
    with tempfile.TemporaryDirectory(prefix=".smaa-", dir=output.parent) as temporary:
        stage = Path(temporary)
        for name, digest in FILES.items():
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            cached = output / name
            if verified(cached, digest):
                shutil.copyfile(cached, target)
            elif source:
                shutil.copyfile(Path(source) / name, target)
            else:
                subprocess.run([
                    "curl", "--fail", "--silent", "--show-error", "--location",
                    "--proto", "=https", "--tlsv1.2", "--connect-timeout", "20",
                    "--max-time", "180", "--retry", "2", f"{BASE_URL}/{name}",
                    "-o", str(target),
                ], check=True)
            if not verified(target, digest):
                raise ValueError(f"SMAA checksum mismatch: {name}")
        (stage / "smaa_hlsl.inc").write_text(shader_include((stage / "SMAA.hlsl").read_bytes()))
        (stage / "SOURCE.txt").write_text(f"SMAA revision {REVISION}\n{BASE_URL}\n")
        output.mkdir(parents=True, exist_ok=True)
        for path in stage.rglob("*"):
            if path.is_file():
                target = output / path.relative_to(stage)
                target.parent.mkdir(parents=True, exist_ok=True)
                # Avoid recompilation when setup runs again without changes.
                if not target.is_file() or target.read_bytes() != path.read_bytes():
                    path.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify local files without downloads")
    parser.add_argument("--output", type=Path, default=ROOT / "third_party/smaa")
    args = parser.parse_args()
    try:
        prepare(args.output, args.check)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"SMAA setup failed: {error}\n")
    print(f"SMAA verified: {REVISION}")


if __name__ == "__main__":
    main()

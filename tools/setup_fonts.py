#!/usr/bin/env python3
"""Prepare checksum-pinned, OFL-licensed shared-font substitutes."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FONT_DIRECTORY = ROOT / "deps/share/fonts"
SANS_REV = "ffebf8c1ee449e544955a7e813c54f9b73848eac"
CJK_REV = "523d033d6cb47f4a80c58a35753646f5c3608a78"
SANS_URL = f"https://raw.githubusercontent.com/notofonts/noto-fonts/{SANS_REV}"
CJK_URL = f"https://raw.githubusercontent.com/notofonts/noto-cjk/{CJK_REV}"
FILES = {
    "NotoSans-Regular.ttf": (f"{SANS_URL}/hinted/ttf/NotoSans/NotoSans-Regular.ttf",
                             "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5"),
    "NotoSans-LICENSE.txt": (f"{SANS_URL}/LICENSE",
                             "0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb"),
    "NotoSansCJKsc-Regular.otf": (f"{CJK_URL}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
                                "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"),
    "NotoSansCJK-LICENSE.txt": (f"{CJK_URL}/LICENSE",
                                "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"),
}


def verified(path, digest):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest


def prepare(output=FONT_DIRECTORY, check=False):
    if check:
        if (not (output / "SOURCE.txt").is_file() or
                not all(verified(output / name, digest) for name, (_, digest) in FILES.items())):
            raise ValueError("Bundled fonts missing or changed; run tools/setup_fonts.py")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    source = Path(os.environ["SF2_FONT_SOURCE_DIR"]) if os.environ.get("SF2_FONT_SOURCE_DIR") else FONT_DIRECTORY
    with tempfile.TemporaryDirectory(prefix=".fonts-", dir=output.parent) as temporary:
        stage = Path(temporary)
        for name, (url, digest) in FILES.items():
            target = stage / name
            if verified(output / name, digest):
                shutil.copyfile(output / name, target)
            elif verified(source / name, digest):
                shutil.copyfile(source / name, target)
            elif os.environ.get("SF2_FONT_SOURCE_DIR"):
                raise ValueError(f"Font checksum mismatch or missing offline source: {name}")
            else:
                subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                                "--proto", "=https", "--tlsv1.2", "--connect-timeout", "20",
                                "--max-time", "180", "--retry", "2", url, "-o", str(target)], check=True)
            if not verified(target, digest):
                raise ValueError(f"Font checksum mismatch: {name}")
        (stage / "SOURCE.txt").write_text("Open-licensed fallback fonts (SIL OFL 1.1)\n" +
            "".join(f"{name}\n{url}\nSHA256 {digest}\n" for name, (url, digest) in FILES.items()))
        output.mkdir(parents=True, exist_ok=True)
        for path in stage.iterdir():
            target = output / path.name
            if not target.is_file() or target.read_bytes() != path.read_bytes():
                path.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=FONT_DIRECTORY)
    parser.add_argument("--check", action="store_true", help="verify local files without downloads")
    args = parser.parse_args()
    try:
        prepare(args.output, args.check)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Font setup failed: {error}\n")
    print("Bundled Noto fallback fonts verified")


if __name__ == "__main__":
    main()

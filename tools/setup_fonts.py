#!/usr/bin/env python3
"""Prepare checksum-pinned, OFL-licensed shared-font substitutes."""
from _sf2_tool_constants import SETUP_FONTS as K
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FONT_DIRECTORY = ROOT / K.S_DEPS_SHARE_FONTS_5A92
SANS_REV = K.SANS_REV
CJK_REV = K.CJK_REV
SANS_URL = f"https://raw.githubusercontent.com/notofonts/noto-fonts/{SANS_REV}"
CJK_URL = f"https://raw.githubusercontent.com/notofonts/noto-cjk/{CJK_REV}"
FILES = {
    K.S_NOTOSANS_REGULAR_TTF_8737: (f"{SANS_URL}/hinted/ttf/NotoSans/NotoSans-Regular.ttf",
                             K.S_B85C38ECEA8A7CFB39C24E395A40_248B),
    K.S_NOTOSANS_LICENSE_TXT_5F00: (f"{SANS_URL}/LICENSE",
                             K.S_0DAB92D0544F7B233403F14B84A6_35C0),
    K.S_NOTOSANSCJKSC_REGULAR_OTF_5E25: (f"{CJK_URL}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
                                K.S_2C76254F6FC379FDDFCE0A7E84FB_D58D),
    K.S_NOTOSANSCJK_LICENSE_TXT_6796: (f"{CJK_URL}/LICENSE",
                                K.S_6A73F9541C2DE74158C0E7CF6B0A_95A5),
}


def verified(path, digest):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest


def prepare(output=FONT_DIRECTORY, check=False):
    if check:
        if (not (output / K.S_SOURCE_TXT_5804).is_file() or
                not all(verified(output / name, digest) for name, (_, digest) in FILES.items())):
            raise ValueError(K.S_BUNDLED_FONTS_MISSING_OR_CHA_B366)
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    source = Path(os.environ[K.S_SF2_FONT_SOURCE_DIR_3992]) if os.environ.get(K.S_SF2_FONT_SOURCE_DIR_3992) else FONT_DIRECTORY
    with tempfile.TemporaryDirectory(prefix=K.S_FONTS_8685, dir=output.parent) as temporary:
        stage = Path(temporary)
        for name, (url, digest) in FILES.items():
            target = stage / name
            if verified(output / name, digest):
                shutil.copyfile(output / name, target)
            elif verified(source / name, digest):
                shutil.copyfile(source / name, target)
            elif os.environ.get(K.S_SF2_FONT_SOURCE_DIR_3992):
                raise ValueError(f"Font checksum mismatch or missing offline source: {name}")
            else:
                subprocess.run([K.S_CURL_F8AE, K.S_FAIL_91BC, K.S_SILENT_90C6, K.S_SHOW_ERROR_194D, K.S_LOCATION_CAAB,
                                K.S_PROTO_0ADB, K.S_HTTPS_0B80, K.S_TLSV1_2_EC63, K.S_CONNECT_TIMEOUT_A6CF, K.S_20_1C3F,
                                K.S_MAX_TIME_7021, K.S_180_38B2, K.S_RETRY_04A1, K.S_2_5283, url, K.S_O_010C, str(target)], check=True)
            if not verified(target, digest):
                raise ValueError(f"Font checksum mismatch: {name}")
        (stage / K.S_SOURCE_TXT_5804).write_text(K.S_OPEN_LICENSED_FALLBACK_FONTS_DDFC +
            K.S_EMPTY_6F49.join(f"{name}\n{url}\nSHA256 {digest}\n" for name, (url, digest) in FILES.items()))
        output.mkdir(parents=True, exist_ok=True)
        for path in stage.iterdir():
            target = output / path.name
            if not target.is_file() or target.read_bytes() != path.read_bytes():
                path.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, default=FONT_DIRECTORY)
    parser.add_argument(K.S_CHECK_464A, action=K.S_STORE_TRUE_2C29, help=K.S_VERIFY_LOCAL_FILES_WITHOUT_D_6012)
    args = parser.parse_args()
    try:
        prepare(args.output, args.check)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Font setup failed: {error}\n")
    print(K.S_BUNDLED_NOTO_FALLBACK_FONTS__2659)


if __name__ == K.S_MAIN_ACB5:
    main()

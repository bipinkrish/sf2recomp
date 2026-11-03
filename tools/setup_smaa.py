#!/usr/bin/env python3
"""Prepare checksum-pinned upstream SMAA files and the C shader include."""
from _sf2_tool_constants import SETUP_SMAA as K
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = K.REVISION
BASE_URL = f"https://raw.githubusercontent.com/iryoku/smaa/{REVISION}"
FILES = {
    K.S_SMAA_HLSL_4525: K.S_4DB53D92EF0B45661F8450303589_0926,
    K.S_LICENSE_TXT_FEEC: K.S_2B848B67BA50AC40B53E11CF1CCD_1757,
    K.S_TEXTURES_AREATEX_H_6716: K.S_1933BF43CE86BA71F7ED5F0FA94B_AB35,
    K.S_TEXTURES_SEARCHTEX_H_79AF: K.S_1FAC315AB5F87B60083C9B8387AF_B814,
}


def verified(path, digest):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest


def shader_include(shader):
    # Upstream shader comments use Latin-1. Normalize the CRLF source exactly
    # as the formerly vendored C string, preserving the compiled shader text.
    text = shader.decode(K.S_LATIN_1_88CB).replace(K.S_R_N_350F, K.S_N_AD65)
    return K.S_EMPTY_6F49.join(json.dumps(line + K.S_N_AD65) + K.S_N_AD65 for line in text.splitlines())


def prepare(output, check=False):
    if check:
        if not all(verified(output / name, digest) for name, digest in FILES.items()):
            raise ValueError(K.S_SMAA_MISSING_OR_CHANGED_RUN__BE0B)
        expected = shader_include((output / K.S_SMAA_HLSL_4525).read_bytes())
        include = output / K.S_SMAA_HLSL_INC_B168
        if not include.is_file() or include.read_text() != expected:
            raise ValueError(K.S_SMAA_SHADER_INCLUDE_MISSING__7D32)
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    source = os.environ.get(K.S_SMAA_SOURCE_DIR_E372)
    with tempfile.TemporaryDirectory(prefix=K.S_SMAA_92D1, dir=output.parent) as temporary:
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
                    K.S_CURL_F8AE, K.S_FAIL_91BC, K.S_SILENT_90C6, K.S_SHOW_ERROR_194D, K.S_LOCATION_CAAB,
                    K.S_PROTO_0ADB, K.S_HTTPS_0B80, K.S_TLSV1_2_EC63, K.S_CONNECT_TIMEOUT_A6CF, K.S_20_1C3F,
                    K.S_MAX_TIME_7021, K.S_180_38B2, K.S_RETRY_04A1, K.S_2_5283, f"{BASE_URL}/{name}",
                    K.S_O_010C, str(target),
                ], check=True)
            if not verified(target, digest):
                raise ValueError(f"SMAA checksum mismatch: {name}")
        (stage / K.S_SMAA_HLSL_INC_B168).write_text(shader_include((stage / K.S_SMAA_HLSL_4525).read_bytes()))
        (stage / K.S_SOURCE_TXT_5804).write_text(f"SMAA revision {REVISION}\n{BASE_URL}\n")
        output.mkdir(parents=True, exist_ok=True)
        for path in stage.rglob(K.S_EMPTY_15A7):
            if path.is_file():
                target = output / path.relative_to(stage)
                target.parent.mkdir(parents=True, exist_ok=True)
                # Avoid recompilation when setup runs again without changes.
                if not target.is_file() or target.read_bytes() != path.read_bytes():
                    path.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_CHECK_464A, action=K.S_STORE_TRUE_2C29, help=K.S_VERIFY_LOCAL_FILES_WITHOUT_D_6012)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, default=ROOT / K.S_THIRD_PARTY_SMAA_8DBE)
    args = parser.parse_args()
    try:
        prepare(args.output, args.check)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"SMAA setup failed: {error}\n")
    print(f"SMAA verified: {REVISION}")


if __name__ == K.S_MAIN_ACB5:
    main()

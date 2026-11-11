"""Read the single tool-version source used by native code and release tags."""
from _sf2_tool_constants import VERSION as K
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read():
    text = (ROOT / K.S_RUNTIME_VERSION_H_D348).read_text(encoding=K.S_UTF_8_5383)
    version = re.search(K.S_DEFINE_SF2RECOMP_VERSION_202B, text, re.MULTILINE)[1]
    if not re.fullmatch(K.S_D_D_D_A_ZA_Z0_9_5C56, version):
        raise ValueError(K.S_SF2RECOMP_VERSION_MUST_BE_A__6D55)
    values = {K.S_VERSION_25EB: version}
    text = (ROOT / K.S_RUNTIME_LAN_PROTOCOL_H_0A0B).read_text(encoding=K.S_UTF_8_5383)
    for field in (K.S_PROTOCOL_E72E, K.S_COMPATIBILITY_56E9):
        values[K.S_LAN_D817 + field] = int(re.search(rf"^#define SF2RECOMP_LAN_{field.upper()} (\d+)$", text, re.MULTILINE)[1])
    return values


if __name__ == K.S_MAIN_ACB5:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_CHECK_TAG_0950)
    parser.add_argument(K.S_JSON_5AD0, action=K.S_STORE_TRUE_2C29)
    parser.add_argument(K.S_WINDOWS_AF25, action=K.S_STORE_TRUE_430C, help=K.S_PRINT_THE_FOUR_NUMERIC_PE_VE_06B6)
    args = parser.parse_args()
    data = read()
    if args.check_tag and args.check_tag != K.S_V_DEAF + data[K.S_VERSION_25EB]:
        parser.error(f"tag must be v{data[K.S_VERSION_25EB]}, matching runtime/version.h")
    if args.windows:
        fields = re.match(K.S_R_D_D_D_8DC2, data[K.S_VERSION_25EB]).groups()
        print(K.S_EMPTY_08B5.join((*fields, K.S_0_9808)))
    else:
        print(json.dumps(data) if args.json else data[K.S_VERSION_25EB])

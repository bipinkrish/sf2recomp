#!/usr/bin/env python3
"""Read official NACP metadata and prepare the user's local game icon."""
from _sf2_tool_constants import GAME_METADATA as K
import argparse
import json
from pathlib import Path
import shutil

APP_ID = K.APP_ID
LANGUAGES = (
    K.S_AMERICANENGLISH_BD2C, K.S_BRITISHENGLISH_7B66, K.S_JAPANESE_5F42, K.S_FRENCH_1D2B, K.S_GERMAN_1A38,
    K.S_LATINAMERICANSPANISH_BAFD, K.S_SPANISH_ECEA, K.S_ITALIAN_B671, K.S_DUTCH_5674, K.S_CANADIANFRENCH_6CAD,
    K.S_PORTUGUESE_B834, K.S_RUSSIAN_F61E, K.S_KOREAN_D9E8, K.S_TRADITIONALCHINESE_D981, K.S_SIMPLIFIEDCHINESE_A3FE,
    K.S_BRAZILIANPORTUGUESE_4BA2,
)


def nacp_text(data):
    return data.split(K.B_B_X00_38ED, 1)[0].decode(K.S_UTF_8_5383)


def parse_nacp(data):
    if len(data) < K.N_12400_FF21:
        raise ValueError(K.S_CONTROL_NACP_IS_TRUNCATED_4733)
    titles = []
    for index, language in enumerate(LANGUAGES):
        offset = index * K.N_768_F7B8
        title = nacp_text(data[offset:offset + K.N_512_94F8])
        if title:
            titles.append({K.S_TITLE_BED6: title, K.S_PUBLISHER_DF8D: nacp_text(data[offset + K.N_512_94F8:offset + K.N_768_F7B8]),
                           K.S_LANGUAGE_A56A: language})
    version = nacp_text(data[K.N_12384_3E0D:K.N_12400_FF21])
    if not titles or not version:
        raise ValueError(K.S_CONTROL_NACP_LACKS_AN_OFFICI_B1C8)
    return {**titles[0], K.S_DISPLAY_VERSION_3D6E: version, K.S_APPLICATION_ID_968E: APP_ID}


def write_game_metadata(control, destination, source):
    """Copy raw official metadata and convert its matching icon without resizing."""
    from PIL import Image
    control, destination = Path(control), Path(destination)
    nacp = control / K.S_CONTROL_NACP_C2FD
    info = parse_nacp(nacp.read_bytes())
    icon = control / f"icon_{info[K.S_LANGUAGE_A56A]}.dat"
    if not icon.is_file():
        candidates = sorted(control.glob(K.S_ICON_DAT_08B0))
        if not candidates:
            raise ValueError(K.S_THE_SELECTED_CONTROL_NCA_HAS_F092)
        icon = candidates[0]
    destination.mkdir(parents=True, exist_ok=True)
    with Image.open(icon) as image:
        if image.format not in (K.S_JPEG_E58B, K.S_PNG_FBBD):
            raise ValueError(K.S_THE_OFFICIAL_ICON_IS_NOT_A_S_3DB6)
        image.convert(K.S_RGB_4A24).save(destination / K.S_ICON_PNG_9A4E, format=K.S_PNG_FBBD)
    shutil.copy2(nacp, destination / K.S_CONTROL_NACP_C2FD)
    info.update(control_source=source, icon_source=icon.name)
    (destination / K.S_GAME_INFO_JSON_44B1).write_text(json.dumps(info, ensure_ascii=False, indent=K.N_2_D473) + K.S_N_AD65, encoding=K.S_UTF_8_5383)
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_CONTROL_6ED8, type=Path, help=K.S_EXTRACTED_CONTROL_NCA_ROMFS__D468)
    parser.add_argument(K.S_DESTINATION_7AA5, type=Path, help=K.S_LOCAL_UPDATED_GAME_DIRECTORY_F359)
    parser.add_argument(K.S_SOURCE_7756, choices=(K.S_PATCH_408C, K.S_APPLICATION_2B15), default=K.S_PATCH_408C)
    args = parser.parse_args()
    try:
        info = write_game_metadata(args.control, args.destination, args.source)
        print(f"Official metadata: {info[K.S_TITLE_BED6]} {info[K.S_DISPLAY_VERSION_3D6E]} ({info[K.S_CONTROL_SOURCE_9556]})")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Metadata preparation failed: {error}\n")


if __name__ == K.S_MAIN_ACB5:
    main()

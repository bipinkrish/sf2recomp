#!/usr/bin/env python3
"""Read official NACP metadata and prepare the user's local game icon."""
import argparse
import json
from pathlib import Path
import shutil

APP_ID = "org.sf2recomp.ShadowFight2"
LANGUAGES = (
    "AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
    "LatinAmericanSpanish", "Spanish", "Italian", "Dutch", "CanadianFrench",
    "Portuguese", "Russian", "Korean", "TraditionalChinese", "SimplifiedChinese",
    "BrazilianPortuguese",
)


def nacp_text(data):
    return data.split(b"\0", 1)[0].decode("utf-8")


def parse_nacp(data):
    if len(data) < 0x3070:
        raise ValueError("control.nacp is truncated")
    titles = []
    for index, language in enumerate(LANGUAGES):
        offset = index * 0x300
        title = nacp_text(data[offset:offset + 0x200])
        if title:
            titles.append({"title": title, "publisher": nacp_text(data[offset + 0x200:offset + 0x300]),
                           "language": language})
    version = nacp_text(data[0x3060:0x3070])
    if not titles or not version:
        raise ValueError("control.nacp lacks an official title or display version")
    return {**titles[0], "display_version": version, "application_id": APP_ID}


def write_game_metadata(control, destination, source):
    """Copy raw official metadata and convert its matching icon without resizing."""
    from PIL import Image
    control, destination = Path(control), Path(destination)
    nacp = control / "control.nacp"
    info = parse_nacp(nacp.read_bytes())
    icon = control / f"icon_{info['language']}.dat"
    if not icon.is_file():
        candidates = sorted(control.glob("icon_*.dat"))
        if not candidates:
            raise ValueError("the selected control NCA has no official icon")
        icon = candidates[0]
    destination.mkdir(parents=True, exist_ok=True)
    with Image.open(icon) as image:
        if image.format not in ("JPEG", "PNG"):
            raise ValueError("the official icon is not a supported JPEG/PNG image")
        image.convert("RGB").save(destination / "icon.png", format="PNG")
    shutil.copy2(nacp, destination / "control.nacp")
    info.update(control_source=source, icon_source=icon.name)
    (destination / "game-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path, help="extracted control NCA RomFS directory")
    parser.add_argument("destination", type=Path, help="local updated game directory")
    parser.add_argument("--source", choices=("Patch", "Application"), default="Patch")
    args = parser.parse_args()
    try:
        info = write_game_metadata(args.control, args.destination, args.source)
        print(f"Official metadata: {info['title']} {info['display_version']} ({info['control_source']})")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Metadata preparation failed: {error}\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Regenerate the Windows icon from the project's original PNG artwork."""
from _sf2_tool_constants import PREPARE_ICON as K
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    with Image.open(ROOT / K.S_ASSETS_SF2RECOMP_PNG_B232) as source:
        icon = source.convert(K.S_RGBA_6607)
        icon.save(ROOT / K.S_ASSETS_SF2RECOMP_ICO_D724, format=K.S_ICO_8925,
                                    sizes=[(size, size) for size in (K.N_16_B17E, K.N_24_C235, K.N_32_E29C, K.N_48_9801, K.N_64_A68B, K.N_128_2747, K.N_256_51E8)])
        # White transparent artwork needs a backplate on light README themes.
        badge = Image.new(K.S_RGBA_6607, icon.size, K.S_232A36_D57D)
        badge.alpha_composite(icon)
        badge.convert(K.S_RGB_FDFD).save(ROOT / K.S_ASSETS_SF2RECOMP_README_PNG_6BA4)


if __name__ == K.S_MAIN_8B1C:
    main()

#!/usr/bin/env python3
"""Prepare the supported combined SF2 base/update NSP using user-supplied keys."""
from _sf2_tool_constants import EXTRACT as K
import argparse
import importlib.util
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from game_metadata import write_game_metadata
from build_extraction_tools import ensure_tools
from host_tools import ROOT, dotnet_environment, dotnet_sdk, host_target, run

TOOLS = Path(__file__).resolve().parent
TITLE_IDS = {K.S_APPLICATION_2B15: K.N_72245610706624512_4125, K.S_PATCH_408C: K.N_72245610706626560_7016}
UPDATE_BUILD_ID = K.UPDATE_BUILD_ID


@dataclass(frozen=True)
class Entry:
    offset: int
    size: int


def pfs0_entries(path):
    """Validate names and ranges before handing this container to native tools."""
    size = path.stat().st_size
    with path.open(K.S_RB_81FE) as source:
        header = source.read(K.N_16_B17E)
        if len(header) != K.N_16_B17E or header[:K.N_4_4B22] != K.B_B_PFS0_D3B7:
            raise ValueError(K.S_EXPECTED_AN_UNCOMPRESSED_PFS_7FCE)
        _, count, strings_size, _ = struct.unpack(K.S_4SIII_6C63, header)
        data_start = K.N_16_B17E + count * K.N_24_C235 + strings_size
        if count > K.N_10000_39E5 or strings_size > K.N_16_B17E * K.N_1024_E39E * K.N_1024_E39E or data_start > size:
            raise ValueError(K.S_INVALID_NSP_FILE_TABLE_4CF0)
        table, strings = source.read(count * K.N_24_C235), source.read(strings_size)
        entries = {}
        portable_names = set()
        for index in range(count):
            offset, length, name_offset, _ = struct.unpack_from(K.S_QQII_8879, table, index * K.N_24_C235)
            if name_offset >= len(strings) or K.B_B_X00_38ED not in strings[name_offset:]:
                raise ValueError(K.S_INVALID_NSP_FILENAME_9DFD)
            name = strings[name_offset:strings.index(0, name_offset)].decode(K.S_UTF_8_5383)
            if (not name or name in (K.S_EMPTY_277A, K.S_EMPTY_E059) or any(c in name for c in K.S_EMPTY_3C14)
                    or any(ord(c) < K.N_32_E29C for c in name) or name.endswith((K.S_EMPTY_277A, K.S_EMPTY_730D))
                    or PureWindowsPath(name).is_reserved() or name.casefold() in portable_names):
                raise ValueError(K.S_NSP_CONTAINS_AN_UNSAFE_OR_DU_B672)
            portable_names.add(name.casefold())
            if data_start + offset + length > size:
                raise ValueError(K.S_NSP_CONTAINS_AN_OUT_OF_RANGE_0619)
            entries[name] = Entry(data_start + offset, length)
    return entries


def select_programs(path, entries):
    """This version supports one combined base + v131072 bundle with XML metadata."""
    selected = {}
    with path.open(K.S_RB_81FE) as source:
        for name, entry in entries.items():
            if not name.endswith(K.S_CNMT_XML_EF83):
                continue
            if entry.size > K.N_1024_E39E * K.N_1024_E39E:
                raise ValueError(K.S_CNMT_XML_IS_TOO_LARGE_4392)
            source.seek(entry.offset)
            meta = ET.fromstring(source.read(entry.size))
            kind = meta.findtext(K.S_TYPE_5892)
            if kind not in TITLE_IDS:
                continue
            if int(meta.findtext(K.S_ID_D271, K.S_0_59A9), 0) != TITLE_IDS[kind]:
                raise ValueError(K.S_BUNDLE_CONTAINS_A_DIFFERENT__DDCE)
            if kind in selected:
                raise ValueError(K.S_BUNDLE_CONTAINS_MULTIPLE_BAS_72D8)
            if kind == K.S_PATCH_408C and int(meta.findtext(K.S_VERSION_44A1, K.S_1_2D21)) != K.N_131072_F1DE:
                raise ValueError(K.S_UNSUPPORTED_UPDATE_THIS_PIPE_73F0)
            programs = [content.findtext(K.S_ID_D271, K.S_EMPTY_6F49) for content in meta.findall(K.S_CONTENT_3988)
                        if content.findtext(K.S_TYPE_5892) == K.S_PROGRAM_1564]
            if len(programs) != 1 or not re.fullmatch(K.S_0_9A_FA_F_32_0357, programs[0]):
                raise ValueError(K.S_CNMT_MUST_IDENTIFY_EXACTLY_O_4225)
            filename = programs[0].lower() + K.S_NCA_8098
            if filename not in entries:
                raise ValueError(K.S_CNMT_PROGRAM_NCA_IS_MISSING__C5B4)
            tickets = [name for name in entries if name.lower().startswith(f"{TITLE_IDS[kind]:016x}")
                       and name.endswith(K.S_TIK_71B5)]
            if len(tickets) != 1:
                raise ValueError(f"expected exactly one {kind} ticket in the bundle")
            selected[kind] = filename, tickets[0]
    if set(selected) != set(TITLE_IDS):
        raise ValueError(K.S_SUPPORTED_INPUT_IS_A_COMBINE_29B4)
    return selected



def select_control(path, entries):
    """Prefer update control content; use base only when update declares none."""
    controls = {}
    with path.open(K.S_RB_81FE) as source:
        for name, entry in entries.items():
            if not name.endswith(K.S_CNMT_XML_EF83):
                continue
            if entry.size > K.N_1024_E39E * K.N_1024_E39E:
                raise ValueError(K.S_CNMT_XML_IS_TOO_LARGE_4392)
            source.seek(entry.offset)
            meta = ET.fromstring(source.read(entry.size))
            kind = meta.findtext(K.S_TYPE_5892)
            if kind not in TITLE_IDS:
                continue
            if int(meta.findtext(K.S_ID_D271, K.S_0_59A9), 0) != TITLE_IDS[kind]:
                raise ValueError(K.S_CONTROL_METADATA_BELONGS_TO__DCAF)
            ids = [content.findtext(K.S_ID_D271, K.S_EMPTY_6F49) for content in meta.findall(K.S_CONTENT_3988)
                   if content.findtext(K.S_TYPE_5892) == K.S_CONTROL_317A]
            if not ids:
                continue
            if len(ids) != 1 or kind in controls or not re.fullmatch(K.S_0_9A_FA_F_32_0357, ids[0]):
                raise ValueError(K.S_CNMT_MUST_IDENTIFY_AT_MOST_O_1171)
            filename = ids[0].lower() + K.S_NCA_8098
            if filename not in entries:
                raise ValueError(K.S_CNMT_CONTROL_NCA_IS_MISSING__C4EE)
            controls[kind] = filename
    for kind in (K.S_PATCH_408C, K.S_APPLICATION_2B15):
        if kind in controls:
            return kind, controls[kind]
    raise ValueError(K.S_BUNDLE_LACKS_OFFICIAL_CONTRO_827F)


def publish(stage, game):
    """Replace both output directories, rolling back if a rename fails."""
    moved, installed = [], []
    try:
        for name in (K.S_NSP_DDC6, K.S_EXTRACTED_E3E0):
            target = game / name
            if target.exists():
                target.rename(stage / (K.S_OLD_3788 + name))
                moved.append(name)
            (stage / name).rename(target)
            installed.append(name)
    except OSError:
        for name in reversed(installed):
            shutil.rmtree(game / name)
        for name in reversed(moved):
            (stage / (K.S_OLD_3788 + name)).rename(game / name)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_NSP_DDC6, type=Path)
    parser.add_argument(K.S_KEYS_C7A3, type=Path, help=K.S_YOUR_LOCAL_PROD_KEYS_NEVER_U_1652)
    parser.add_argument(K.S_GAME_DIR_5F13, type=Path, default=TOOLS.parent / K.S_GAME_CF8E)
    parser.add_argument(K.S_VALIDATE_ONLY_9863, action=K.S_STORE_TRUE_2C29, help=K.S_CHECK_THE_PACKAGED_LAYOUT_WI_A468)
    args = parser.parse_args(argv)
    previous_mask = None
    try:
        nsp, keys, game = args.nsp.resolve(strict=True), args.keys.resolve(strict=True), args.game_dir.resolve()
        if not keys.is_file():
            raise ValueError(K.S_KEYS_MUST_BE_A_READABLE_FILE_72C3)
        entries = pfs0_entries(nsp)
        programs = select_programs(nsp, entries)
        control_kind, control_filename = select_control(nsp, entries)
        if args.validate_only:
            print(K.S_SUPPORTED_SF2_BASE_V131072_B_B842)
            return 0
        if importlib.util.find_spec(K.S_PIL_17DB) is None:
            raise ValueError(K.S_INSTALL_ICON_CONVERSION_DEPE_576E)
        from release_support import helper
        merger_command = helper(K.S_BKTR_7B22)
        if merger_command is None:
            dotnet = dotnet_sdk()
            merger = ROOT / K.S_BUILD_TOOLS_BKTR_9696 / host_target() / K.S_SF2BKTR_DLL_19A0
            run(dotnet, K.S_BUILD_0BA9, TOOLS / K.S_BKTR_SF2BKTR_CSPROJ_54B3, K.S_C_3F7E, K.S_RELEASE_D1CF, K.S_NOLOGO_5926,
                K.S_P_USEAPPHOST_FALSE_8237, K.S_O_010C, merger.parent, env=dotnet_environment())
            merger_command = [dotnet, merger]
        gc, nt = ensure_tools()
        # Private staging keeps tickets/keys local, and failures preserve previous results.
        previous_mask = os.umask(K.N_63_DA4E)
        game.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=K.S_EXTRACT_3024, dir=game) as temporary:
            stage = Path(temporary)
            (stage / K.S_NSP_DDC6).mkdir()
            run(gc, K.S_T_0583, K.S_PFS0_254E, K.S_PFS0DIR_2D5E + str(stage / K.S_NSP_DDC6), nsp)
            run(sys.executable, TOOLS / K.S_EXTRACT_TITLEKEYS_PY_AF8C, K.S_KEYS_B749, keys, K.S_NSP_DIR_685F, stage / K.S_NSP_DDC6)
            base, update = stage / K.S_EXTRACTED_BASE_DE20, stage / K.S_EXTRACTED_UPDATE_B887
            base.mkdir(parents=True)
            update.mkdir()
            for kind, destination in ((K.S_APPLICATION_2B15, base), (K.S_PATCH_408C, update)):
                filename, ticket = programs[kind]
                run(nt, K.S_K_0017, keys, K.S_TIK_829C, stage / K.S_NSP_DDC6 / ticket,
                    K.S_PART0_563F, destination / K.S_EXEFS_00A0, stage / K.S_NSP_DDC6 / filename)
            with (update / K.S_EXEFS_MAIN_8CC5).open(K.S_RB_81FE) as source:
                main_header = source.read(K.N_96_7B1A)
            if main_header[:K.N_4_4B22] != K.B_B_NSO0_4F90 or main_header[K.N_64_A68B:K.N_80_4844].hex().upper() != UPDATE_BUILD_ID:
                raise ValueError(K.S_UNSUPPORTED_MAIN_NSO_BUILD_E_FCCB)
            for name in (K.S_MAIN_13FD, K.S_SUBSDK0_5E64, K.S_SDK_0065, K.S_RTLD_1F0C):
                run(gc, K.S_T_0583, K.S_NSO0_A4F9, K.S_DISABLEKEYWARNS_5C04, K.S_UNCOMPRESSED_1713 + str(update / K.S_EXEFS_00A0 / (name + K.S_UNCOMPRESSED_E1D5)),
                    update / K.S_EXEFS_00A0 / name)
            base_nca, base_ticket = programs[K.S_APPLICATION_2B15]
            for partition, name in ((K.S_PART1_F4FF, K.S_ROMFS_0A7E), (K.S_PART2_5ED4, K.S_LOGO_87B5)):
                run(nt, K.S_K_0017, keys, K.S_TIK_829C, stage / K.S_NSP_DDC6 / base_ticket, partition, base / name, stage / K.S_NSP_DDC6 / base_nca)
            environment = dict(dotnet_environment(), SF2BKTR_DUMP_OUT=str(update / K.S_ROMFS_IMG_6675))
            run(*merger_command, stage / K.S_NSP_DDC6 / programs[K.S_PATCH_408C][0], stage / K.S_NSP_DDC6 / base_nca,
                keys, stage / K.S_NSP_TITLE_KEYS_E998, env=environment)
            run(nt, K.S_T_0583, K.S_ROMFS_0A7E, K.S_X_8BDC, update / K.S_ROMFS_0A7E, update / K.S_ROMFS_IMG_6675)
            if not (update / K.S_ROMFS_DATA_MANAGED_METADATA__F282).is_file():
                raise ValueError(K.S_MERGED_ROMFS_LACKS_THE_REQUI_3E81)
            # The display version comes from the official NACP, not CNMT's ordinal.
            control = stage / K.S_CONTROL_6ED8
            run(nt, K.S_K_0017, keys, K.S_PART0_563F, control, stage / K.S_NSP_DDC6 / control_filename)
            info = write_game_metadata(control, update, control_kind)
            print(f"Official game metadata: {info[K.S_TITLE_BED6]} {info[K.S_DISPLAY_VERSION_3D6E]} ({control_kind})")
            publish(stage, game)
        print(K.S_EXTRACTION_COMPLETE_150C, game / K.S_EXTRACTED_UPDATE_B887)
        return 0
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        print(K.S_EXTRACTION_FAILED_D274, error, file=sys.stderr)
        return 1
    finally:
        if previous_mask is not None:
            os.umask(previous_mask)


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

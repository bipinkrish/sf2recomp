#!/usr/bin/env python3
"""Prepare the supported SF2 IL2CPP symbol map on native Linux or Windows."""
from _sf2_tool_constants import ANALYZE_IL2CPP as K
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from build_extraction_tools import ensure_tools
import build_il2cpp
from host_tools import ROOT, digest, dotnet_environment, dotnet_sdk, run

BUILD_ID = bytes.fromhex(K.S_E15FC665EC42684AA6EDD1AE0362_06CD).ljust(K.N_32_E29C, K.B_B_X00_38ED)


def prepare_symbols(game, output=None, force=False):
    game = Path(game).resolve(strict=True)
    output = Path(output or ROOT / K.S_GAME_IL2CPP_940A).resolve()
    if ROOT.is_relative_to(output) or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError(K.S_SYMBOL_OUTPUT_MUST_NOT_REPLA_9A5F)
    main = game / K.S_EXEFS_MAIN_8CC5
    metadata = game / K.S_ROMFS_DATA_MANAGED_METADATA__F282
    with main.open(K.S_RB_81FE) as stream:
        header = stream.read(K.N_96_7B1A)
    if len(header) != K.N_96_7B1A or header[:K.N_4_4B22] != K.B_B_NSO0_4F90 or header[K.N_64_A68B:K.N_96_7B1A] != BUILD_ID:
        raise ValueError(K.S_IL2CPP_ANALYSIS_REQUIRES_THE_312D)
    config_path = ROOT / K.S_TOOLS_IL2CPP_CONFIG_JSON_BC09
    config = json.loads(config_path.read_text(encoding=K.S_UTF_8_5383))
    if config.get(K.S_REQUIREANYKEY_F18A, True) or not config.get(K.S_GENERATESTRUCT_A89B, False):
        raise ValueError(K.S_IL2CPP_CONFIG_REQUIRES_REQUI_098E)
    inputs = {K.S_MAIN_13FD: digest(main), K.S_METADATA_01D2: digest(metadata), K.S_CONFIG_40B7: digest(config_path),
              K.S_DUMPER_COMMIT_B452: build_il2cpp.COMMIT, K.S_SYMBOL_MAPPER_2206: digest(ROOT / K.S_TOOLS_IL2CPP_MAP_PY_17D1), K.S_SCHEMA_9B05: 1}
    inputs[K.S_TOOL_CONSTANTS_61E8] = digest(Path(__file__).with_name(K.S_SF2_TOOL_CONSTANTS_PY_B537))
    stamp = output / K.S_SOURCE_JSON_4EDA
    if not force and stamp.is_file() and (output / K.S_SYM_MAIN_TXT_5D45).is_file():
        try:
            previous = json.loads(stamp.read_text(encoding=K.S_UTF_8_5383))
            if previous == {**inputs, K.S_SYMBOLS_EDCD: digest(output / K.S_SYM_MAIN_TXT_5D45)}:
                return output / K.S_SYM_MAIN_TXT_5D45
        except (OSError, ValueError):
            pass
    from release_support import helper
    dumper_command = helper(K.S_IL2CPP_38B2)
    if dumper_command is None:
        dotnet = dotnet_sdk()
        dumper_command = [dotnet, build_il2cpp.ensure()]
    hactool, _ = ensure_tools()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=K.S_SF2_ANALYSIS_D2F7, dir=output.parent) as temporary:
        stage = Path(temporary)
        nso = stage / K.S_MAIN_UNCOMPRESSED_2662
        run(hactool, K.S_T_0583, K.S_NSO0_A4F9, K.S_DISABLEKEYWARNS_5C04, K.S_UNCOMPRESSED_1713 + nso.as_posix(), main)
        if not nso.is_file():
            raise ValueError(K.S_HACTOOL_DID_NOT_PRODUCE_AN_U_0693)
        analysis = stage / K.S_ANALYSIS_2902
        analysis.mkdir()
        # Upstream catches some errors and returns zero. Require fresh, valid
        # output as well as subprocess success, and never allow a console pause.
        run(*dumper_command, nso, metadata, analysis, env=dotnet_environment(), stdin=subprocess.DEVNULL)
        script = analysis / K.S_SCRIPT_JSON_6126
        if not script.is_file():
            raise ValueError(K.S_IL2CPPDUMPER_PRODUCED_NO_SCR_281A)
        data = json.loads(script.read_text(encoding=K.S_UTF_8_SIG_EACA))
        if not isinstance(data.get(K.S_SCRIPTMETHOD_986B), list) or not data[K.S_SCRIPTMETHOD_986B]:
            raise ValueError(K.S_IL2CPPDUMPER_PRODUCED_NO_MET_1967)
        run(sys.executable, ROOT / K.S_TOOLS_IL2CPP_MAP_PY_17D1, K.S_INDIR_B906, analysis, K.S_NSO_DD57, nso,
            K.S_CODEREG_99C7, K.S_0X3BE0EA_2655, K.S_METAREG_19FA, K.S_0X3BE120_2D87)
        symbols = analysis / K.S_SYM_MAIN_TXT_5D45
        if not symbols.is_file() or not symbols.stat().st_size:
            raise ValueError(K.S_IL2CPP_SYMBOL_MAPPING_PRODUC_ED7D)
        inputs[K.S_SYMBOLS_EDCD] = digest(symbols)
        output.mkdir(parents=True, exist_ok=True)
        # Publish the symbol map last and its identity stamp after it. Dumper or
        # mapper failures above cannot replace a previous working symbol map.
        for artifact in sorted(analysis.iterdir(), key=lambda p: p.name == K.S_SYM_MAIN_TXT_5D45):
            if artifact.is_file():
                artifact.replace(output / artifact.name)
        pending = output / K.S_SOURCE_JSON_TMP_C913
        pending.write_text(json.dumps(inputs, indent=K.N_2_D473) + K.S_N_AD65, encoding=K.S_UTF_8_5383)
        pending.replace(stamp)
    return output / K.S_SYM_MAIN_TXT_5D45


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_GAME_CF8E, nargs=K.S_EMPTY_5DE5, type=Path, default=ROOT / K.S_GAME_EXTRACTED_UPDATE_E969)
    parser.add_argument(K.S_OUTPUT_5648, nargs=K.S_EMPTY_5DE5, type=Path, default=ROOT / K.S_GAME_IL2CPP_940A)
    parser.add_argument(K.S_FORCE_CA33, action=K.S_STORE_TRUE_2C29, help=K.S_REGENERATE_EVEN_WHEN_THE_INP_72CC)
    args = parser.parse_args(argv)
    try:
        print(K.S_SYMBOL_MAP_90F7, prepare_symbols(args.game, args.output, args.force))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"IL2CPP analysis failed: {error}", file=sys.stderr)
        return 1


if __name__ == K.S_MAIN_ACB5:
    raise SystemExit(main())

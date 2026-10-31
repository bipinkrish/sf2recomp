#!/usr/bin/env python3
"""Prepare the supported SF2 IL2CPP symbol map on native Linux or Windows."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from build_extraction_tools import ensure_tools
import build_il2cpp
from host_tools import ROOT, digest, dotnet_environment, dotnet_sdk, run

BUILD_ID = bytes.fromhex("e15fc665ec42684aa6edd1ae03624d96").ljust(32, b"\0")


def prepare_symbols(game, output=None, force=False):
    game = Path(game).resolve(strict=True)
    output = Path(output or ROOT / "game/il2cpp").resolve()
    if ROOT.is_relative_to(output) or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError("Symbol output must not replace the source checkout or overlap the extracted game.")
    main = game / "exefs/main"
    metadata = game / "romfs/Data/Managed/Metadata/global-metadata.dat"
    with main.open("rb") as stream:
        header = stream.read(0x60)
    if len(header) != 0x60 or header[:4] != b"NSO0" or header[0x40:0x60] != BUILD_ID:
        raise ValueError("IL2CPP analysis requires the supported SF2 update v131072 main NSO.")
    config_path = ROOT / "tools/il2cpp/config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("RequireAnyKey", True) or not config.get("GenerateStruct", False):
        raise ValueError("IL2CPP config requires RequireAnyKey=false and GenerateStruct=true for unattended analysis.")
    inputs = {"main": digest(main), "metadata": digest(metadata), "config": digest(config_path),
              "dumper_commit": build_il2cpp.COMMIT, "symbol_mapper": digest(ROOT / "tools/il2cpp_map.py"), "schema": 1}
    stamp = output / "source.json"
    if not force and stamp.is_file() and (output / "sym_main.txt").is_file():
        try:
            previous = json.loads(stamp.read_text(encoding="utf-8"))
            if previous == {**inputs, "symbols": digest(output / "sym_main.txt")}:
                return output / "sym_main.txt"
        except (OSError, ValueError):
            pass
    dotnet = dotnet_sdk()
    hactool, _ = ensure_tools()
    dumper = build_il2cpp.ensure()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sf2-analysis-", dir=output.parent) as temporary:
        stage = Path(temporary)
        nso = stage / "main.uncompressed"
        run(hactool, "-t", "nso0", "--disablekeywarns", "--uncompressed=" + nso.as_posix(), main)
        if not nso.is_file():
            raise ValueError("hactool did not produce an uncompressed main NSO.")
        analysis = stage / "analysis"
        analysis.mkdir()
        # Upstream catches some errors and returns zero. Require fresh, valid
        # output as well as subprocess success, and never allow a console pause.
        run(dotnet, dumper, nso, metadata, analysis, env=dotnet_environment(), stdin=subprocess.DEVNULL)
        script = analysis / "script.json"
        if not script.is_file():
            raise ValueError("Il2CppDumper produced no script.json; previous symbols have been preserved.")
        data = json.loads(script.read_text(encoding="utf-8-sig"))
        if not isinstance(data.get("ScriptMethod"), list) or not data["ScriptMethod"]:
            raise ValueError("Il2CppDumper produced no methods; previous symbols have been preserved.")
        run(sys.executable, ROOT / "tools/il2cpp_map.py", "--indir", analysis, "--nso", nso,
            "--codereg", "0x3BE0EA", "--metareg", "0x3BE120")
        symbols = analysis / "sym_main.txt"
        if not symbols.is_file() or not symbols.stat().st_size:
            raise ValueError("IL2CPP symbol mapping produced no symbols.")
        inputs["symbols"] = digest(symbols)
        output.mkdir(parents=True, exist_ok=True)
        # Publish the symbol map last and its identity stamp after it. Dumper or
        # mapper failures above cannot replace a previous working symbol map.
        for artifact in sorted(analysis.iterdir(), key=lambda p: p.name == "sym_main.txt"):
            if artifact.is_file():
                artifact.replace(output / artifact.name)
        pending = output / "source.json.tmp"
        pending.write_text(json.dumps(inputs, indent=2) + "\n", encoding="utf-8")
        pending.replace(stamp)
    return output / "sym_main.txt"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", nargs="?", type=Path, default=ROOT / "game/extracted/update")
    parser.add_argument("output", nargs="?", type=Path, default=ROOT / "game/il2cpp")
    parser.add_argument("--force", action="store_true", help="regenerate even when the input identity cache matches")
    args = parser.parse_args(argv)
    try:
        print("Symbol map:", prepare_symbols(args.game, args.output, args.force))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"IL2CPP analysis failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/bin/sh
# Regenerate the IL2CPP symbol map (roadmap step 3) from an extracted game.
#
# Requires: dotnet on PATH (plain .NET 8+ works; we roll forward the net6.0 build),
# inputs produced by tools/extract.sh.
#
# Outputs into game/il2cpp/ (git-ignored): dump.cs, il2cpp.h, script.json,
# stringliteral.json, and — from tools/il2cpp_map.py —
# sym_main.txt + anchors.txt.
set -eu

D="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$#" -gt 2 ]; then
    echo "Usage: $0 [extracted-game-directory] [output-symbol-directory]" >&2
    exit 2
fi
GAME=${1:-"$D/game/extracted/update"}
OUT=${2:-"$D/game/il2cpp"}
NSO="$GAME/exefs/main.uncompressed"
META="$GAME/romfs/Data/Managed/Metadata/global-metadata.dat"
[ -f "$D/tools/il2cpp/Il2CppDumper.dll" ] || {
    echo "Build the analysis tool first: python3 tools/build_il2cpp.py" >&2
    exit 1
}
[ -f "$NSO" ] && [ -f "$META" ] || {
    echo "Extract the supported game first: tools/extract.sh <nsp> <prod.keys>" >&2
    exit 1
}
mkdir -p "$OUT"

# net6.0 assembly; .NET 8/10 runtimes satisfy it with roll-forward.
: "${DOTNET_ROLL_FORWARD:=Major}"
export DOTNET_ROLL_FORWARD

"${DOTNET:-dotnet}" "$D/tools/il2cpp/Il2CppDumper.dll" "$NSO" "$META" "$OUT"

# Console output includes the two anchors, e.g.
#   CodeRegistration : 3925210   (0x3BE0EA)
#   MetadataRegistration : 3925280 (0x3BE120)
python3 "$D/tools/il2cpp_map.py" --indir "$OUT" --nso "$NSO" --codereg 0x3BE0EA --metareg 0x3BE120

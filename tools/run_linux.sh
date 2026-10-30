#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} == --software ]]; then
    shift
    export VK_DRIVER_FILES=${SF2_LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
    [[ -r $VK_DRIVER_FILES ]] || { echo "Software Vulkan ICD not found: $VK_DRIVER_FILES" >&2; exit 1; }
    export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-x11}
    export LARECOMP_VK_NO_GMEM=1
fi
if (( $# > 1 )); then
    echo "Usage: $0 [--software] [extracted-game-directory]" >&2
    exit 2
fi
game=${1:-"$root/game/extracted/update"}
game=$(realpath -- "$game")
[[ -f $game/exefs/main && -d $game/romfs ]] || { echo "Expected exefs/main and romfs under $game" >&2; exit 1; }
[[ -x $root/build/sf2 ]] || { echo "Build first: make -C '$root' -j8" >&2; exit 1; }
export LARECOMP_DATA_HOME=${LARECOMP_DATA_HOME:-"$root/build/run"}
export LARECOMP_DATA_ROOT=${LARECOMP_DATA_ROOT:-"$root/build/run"}
export SF2_CONFIG
SF2_CONFIG=$(realpath -m -- "${SF2_CONFIG:-$root/sf2recomp.ini}")
mkdir -p -- "$LARECOMP_DATA_HOME" "$LARECOMP_DATA_ROOT/build/frames"
exec "$root/build/sf2" "$game"

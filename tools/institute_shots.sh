#!/usr/bin/env bash
# MAP-2: pictures of the institute scene on desktop (each storey from the play camera, the roof, the exterior).
#   tools/institute_shots.sh [BUILD_DIR] [OUT_DIR]     (default build/institute, docs/autotest/institute)
# The build's default scene does not matter: --scene institute loads it. Screenshots land in the pref path first.
set -eu
cd "$(dirname "$0")/.."
BIN=${1:-build/institute}/mobilegame
OUT=${2:-docs/autotest/institute}
PREF="$HOME/Library/Application Support/docvex/mobilegame"
[[ -d "$PREF" ]] || PREF="$HOME/.local/share/docvex/mobilegame"
mkdir -p "$OUT"
shot() { # name, extra args...
    local name=$1
    shift
    "$BIN" --offline --profile instshots --scene institute --markers 0 "$@" --screenshot-at "5:$name" --quit-after 6 \
        > "$OUT/$name.log" 2>&1 || true
    cp "$PREF/$name.png" "$OUT/$name.png" 2>/dev/null || cp "$PREF/instshots-$name.png" "$OUT/$name.png"
    rm -f "$OUT/$name.log"
    echo "$OUT/$name.png"
}
shot inst_yard --spawn 0,-0.25,26
shot inst_g_lobby --spawn 0,0,4
shot inst_g_west --spawn -17.5,0,-6
shot inst_g_east --spawn 17.5,0,-9.5
shot inst_1_north --spawn -6,4,-9.5
shot inst_1_south --spawn 6,4,9.5
shot inst_2_north --spawn 6,8,-9.5
shot inst_2_south --spawn -8,8,9.5
shot inst_3_north --spawn -6,12,-9.5
shot inst_3_south --spawn 6,12,9.5
shot inst_roof --spawn 0,16,12.5
shot inst_lift --spawn -14.1,4,9.5
shot inst_exterior --cam 0,55,78,0,-35
shot inst_exterior_ne --cam 60,40,-60,135,-25

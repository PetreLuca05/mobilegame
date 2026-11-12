#!/bin/zsh
# LIB-1: --autotest library (desktop), the fresh-install variant, and the table + seats inside the free rect
# on the iPhone presets (both landscape directions) and the iPad (--autotest lobbylayout, a full lobby).
# Pictures into docs/autotest/library/. The two-instance join / kick runs in tools/friends2_test.sh.
# usage: tools/library_test.sh [OUT]
cd "$(dirname "$0")/.."
OUT=${1:-build/library_test}
mkdir -p "$OUT" docs/autotest/library
GAME=build/desktop/mobilegame
PREF="$HOME/Library/Application Support/docvex/mobilegame"
RC=0
run() { # NAME ARGS...
  local n=$1; shift
  timeout 300 "$@" > "$OUT/$n.log" 2>&1
  if grep -q "AUTOTEST PASSED" "$OUT/$n.log"; then echo "PASS $n"; else echo "FAIL $n: $(grep -E 'FAILED at|ERROR: autotest' "$OUT/$n.log" | head -1 | sed 's/.*ERROR: //')"; RC=1; fi
}
run library $GAME --autotest library
run library_reduced $GAME --ui-motion reduced --autotest library
rm -f "$PREF/libfresh-"* 2>/dev/null
run library_fresh $GAME --profile libfresh --frontend welcome --autotest library
for W in iphone iphone-flip ipad; do
  run lobbylayout_$W env MG_WINDOW=$W $GAME --autotest lobbylayout
done
for f in library_menu library_joined library_locker library_welcome_walk library_move_000 library_move_025 library_move_050 library_move_075 library_move_100; do
  [ -f "$PREF/$f.png" ] && cp "$PREF/$f.png" docs/autotest/library/
done
mkdir -p docs/autotest/library/step7
for f in library_travel_out library_travel_pan library_travel_arrive library_plane_out library_plane_in library_plane_landed; do
  [ -f "$PREF/$f.png" ] && cp "$PREF/$f.png" docs/autotest/library/step7/
done
python3 tools/png_montage.py docs/autotest/library/library_move.png --cols 5 --scale 3 docs/autotest/library/library_move_{000,025,050,075,100}.png && echo "sheet docs/autotest/library/library_move.png"
exit $RC

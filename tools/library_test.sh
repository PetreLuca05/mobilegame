#!/bin/zsh
# LIB-1: --autotest library (desktop), the fresh-install variant, and the table + seats inside the free rect
# on the iPhone presets (both landscape directions) and the iPad (--autotest lobbylayout, a full lobby).
# Pictures into docs/autotest/library/. The two-instance join / kick runs in tools/friends2_test.sh.
# usage: tools/library_test.sh [OUT]
cd "$(dirname "$0")/.."
setopt null_glob # a pattern with no file is empty (else zsh aborts the whole rm)
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
# §8 / §15.11: Maps during Ieși's pan finishes the trip at once, the move starts from the pan's camera (the run ends on the map)
run library_maps_mid_trip $GAME --autotest library maps-mid-trip
# §8 / §15.11: the game starting, and Reduse switched on, in the middle of the Maps move and of the Back move (--lib-move-demo)
move_cut() { # NAME MODE WANT...
  local n=$1 m=$2; shift 2
  timeout 60 $GAME --profile libmovecut --frontend menu --lib-move-demo --lib-move-cut $m --quit-after 15 > "$OUT/$n.log" 2>&1
  local bad=""
  for w in "$@"; do grep -q "$w" "$OUT/$n.log" || bad="$bad [$w]"; done
  if grep -q "ERROR" "$OUT/$n.log"; then bad="$bad [an ERROR line]"; fi
  if [ -z "$bad" ]; then echo "PASS $n"; else echo "FAIL $n: missing$bad"; RC=1; fi
}
move_cut library_cut_start start "move to the map cut short" "frontend: LOADING" "frontend: PLAY"
move_cut library_cut_start_back start-back "move back cut short" "frontend: LOADING" "frontend: PLAY"
move_cut library_cut_reduce reduce "Reduse mid-move: the move to the map ends at once" "frontend: MAP" "move to the map done"
move_cut library_cut_reduce_back reduce-back "Reduse mid-move: the move back ends at once" "move back done"
# §11: a fresh install (the walk-in, Username, the Creator's Locker), then the same profile again (no second walk-in); the first
# launch on the iPhone presets and the iPad too. Pictures into docs/autotest/library/step8/.
mkdir -p docs/autotest/library/step8
onb_pics() { # SUFFIX
  for f in library_onb_welcome_walk library_onb_welcome library_onb_username library_onb_creator library_onb_closing; do
    [ -f "$PREF/$f.png" ] && cp "$PREF/$f.png" "docs/autotest/library/step8/$f$1.png"
  done
  rm -f "$PREF"/library_onb_*.png
}
rm -f "$PREF"/library_onb_*.png "$PREF/libfresh-"* 2>/dev/null
run library_fresh $GAME --profile libfresh --frontend welcome --autotest library
onb_pics ""
run library_fresh_again $GAME --profile libfresh --frontend welcome --lib-walked --autotest library
rm -f "$PREF"/library_onb_*.png
for W in iphone iphone-flip ipad; do
  rm -f "$PREF/libfresh_$W-"* 2>/dev/null
  run library_fresh_$W env MG_WINDOW=$W $GAME --profile libfresh_$W --frontend welcome --autotest library
  if [ "$W" = iphone ]; then onb_pics _iphone; else rm -f "$PREF"/library_onb_*.png; fi
done
# §11 the account's table bay (user decision): moves only on the first open after a cold start or a game, at my own table, once
rm -f "$PREF/rebay-"* "$PREF/rebaycold-"* 2>/dev/null
run rebay $GAME --profile rebay --frontend menu --autotest rebay
run rebay_cold $GAME --profile rebaycold --frontend menu --autotest rebay --rebay-cold
rm -f "$PREF/rebaygame-"* 2>/dev/null
run rebay_game $GAME --profile rebaygame --frontend menu --autotest rebay --rebay-game # a real game into the institute between the opens
for W in iphone iphone-flip ipad; do
  run lobbylayout_$W env MG_WINDOW=$W $GAME --autotest lobbylayout
done
for f in library_menu library_joined library_locker library_move_000 library_move_025 library_move_050 library_move_075 library_move_100; do
  [ -f "$PREF/$f.png" ] && cp "$PREF/$f.png" docs/autotest/library/
done
mkdir -p docs/autotest/library/step7
for f in library_travel_out library_travel_pan library_travel_arrive library_plane_out library_plane_in library_plane_landed; do
  [ -f "$PREF/$f.png" ] && cp "$PREF/$f.png" docs/autotest/library/step7/
done
python3 tools/png_montage.py docs/autotest/library/library_move.png --cols 5 --scale 3 docs/autotest/library/library_move_{000,025,050,075,100}.png && echo "sheet docs/autotest/library/library_move.png"
exit $RC

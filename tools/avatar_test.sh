#!/bin/zsh
# LIB-1 A1: --autotest avatar, then the gallery pictures into docs/autotest/avatar/ (desktop).
# usage: tools/avatar_test.sh [OUT]   (OUT: logs, default build/avatar_test)
cd "$(dirname "$0")/.."
OUT=${1:-build/avatar_test}
mkdir -p "$OUT" docs/autotest/avatar
GAME=build/desktop/mobilegame
PREF="$HOME/Library/Application Support/docvex/mobilegame"
DOC=docs/autotest/avatar
RC=0

$GAME --offline --autotest avatar > "$OUT/avatar.log" 2>&1
if grep -q "AUTOTEST PASSED" "$OUT/avatar.log"; then echo "PASS avatar"; else echo "FAIL avatar: $(grep FAILED "$OUT/avatar.log" | head -1)"; RC=1; fi

shot() { # NAME GALLERY SPAWN CAM
  $GAME --offline --scene hospital --play new --avatar-gallery "$2" --spawn "$3" --cam "$4" --autotest screenshot "av_$1" > "$OUT/$1.log" 2>&1
  if [ -f "$PREF/av_$1.png" ]; then cp "$PREF/av_$1.png" "$DOC/$1.png"; echo "shot $1"; else echo "FAIL shot $1"; RC=1; fi
}

# the option sheet: one close-up per row (1 hair styles, 2 suits, 3 extras, 4 skins, 5 hair colours, 6 bodies)
for N in 1 2 3 4 5 6; do
  Z=$(python3 -c "print(13 + 1.6 * (6 - $N) + 4.8)")
  shot options_row$N options$N -10,0.1,-6 9.7,2.0,$Z,0,-13 # above the yard fence
done
python3 tools/png_montage.py $DOC/options.png --cols 2 --scale 2 $DOC/options_row{1,2,3,4,5,6}.png && rm $DOC/options_row?.png && echo "sheet $DOC/options.png"

shot ages ages -10,0.1,-6 8.6,2.6,19.6,0,-16
shot poses poses -10,0.1,-6 10.2,1.4,18.4,0,-8
shot book book -10,0.1,-6 9.7,2.4,22.5,0,-10 # BOOK-ENGINE S4b: the pack kinds, talking, the scrub cap
shot seat_waiting_chair seats 10,0.1,-6 0.9,1.25,-1.7,-90,-12
shot seat_reception_chair seats 10,0.1,-6 -1.6,1.1,-4.6,90,-8
shot seat_yard_bench seats 10,0.1,-6 -1.6,1.3,12,90,-14
shot bed_side bed 8,4.1,-5 -1.4,4.8,-0.25,90,-6
shot bed_above bed 8,4.1,-5 -1.2,5.9,-0.2,90,-38
# the x-ray silhouette: the player behind the reception desk (the desk hides the lower body)
$GAME --offline --play new --spawn -2.6,0.1,-4.9 --cam -2.6,0.8,-1.6,0,-2 --autotest screenshot av_xray > "$OUT/xray.log" 2>&1 && cp "$PREF/av_xray.png" $DOC/xray_behind_desk.png && echo "shot xray_behind_desk"
exit $RC

#!/bin/zsh
# FRIENDS-1 end to end on desktop: three instances (host a, guests b and c, plus a fourth
# account through mg_nettest), real taps, guest accounts. Screenshots: docs/autotest/friends/.
# Usage: tools/friends_test.sh OUTDIR
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/mg_ft_out}
export MG_FT_SYNC=/tmp/mg_ft
GAME=build/desktop/mobilegame
PREFS="$HOME/Library/Application Support/docvex/mobilegame"
rm -rf "$MG_FT_SYNC" "$OUT"; mkdir -p "$MG_FT_SYNC" "$OUT" docs/autotest/friends

front() { osascript -e 'tell application "System Events" to set frontmost of (first process whose name is "mobilegame") to true' 2>/dev/null; }

# role profile window x
run() {
  MG_WINDOW=$3 $GAME --profile $2 --autotest friends $1 --window-pos $4,60 --quit-after 900 > $OUT/$1.log 2>&1
}
check() { grep -q "AUTOTEST PASSED" $OUT/$1.log; }
report() { if check $1; then echo "PASS $1"; else echo "FAIL $1: $(grep -E 'FAILED' $OUT/$1.log | head -1 | cut -c28-200)"; fi }

for p in fta ftb ftc; do
  if ! grep -q '"onboarding_done":[[:space:]]*true' "$PREFS/$p-prefs.json" 2>/dev/null; then
    $GAME --profile $p --autotest onboarding --window-pos 100,60 --quit-after 170 > $OUT/onboard_$p.log 2>&1
    check onboard_$p || { echo "FAIL onboarding $p"; exit 1; }
  fi
done
build/desktop/mg_nettest profile fta Ana 1 3 0 > /dev/null 2>&1
build/desktop/mg_nettest profile ftb Bogdan 2 2 1 > /dev/null 2>&1
build/desktop/mg_nettest profile ftc Clara 3 4 2 > /dev/null 2>&1
build/desktop/mg_nettest profile ftd Dan 0 5 4 > /dev/null 2>&1

run b ftb ipad 700 &
PB=$!
run c ftc iphone 20 &
PC=$!
sleep 1; front
( while [ ! -f $MG_FT_SYNC/code_a ]; do sleep 1; done; build/desktop/mg_nettest friends ftd 40 "$(cat $MG_FT_SYNC/code_a)" > $OUT/d.log 2>&1 ) &
run a1 fta iphone 300; report a1
wait $PC; report c
if check a1; then
  run a2 fta iphone 300; report a2
  run a3 fta ipad 300; report a3
fi
wait $PB; report b
run offline fta iphone 300; report offline
grep -E "NPC worlds match|heads generated" $OUT/*.log | cut -c1-160
if grep -rnIE 'free.?roam|last_mode|last_room|MODE_PARTY|MODE_WORLD|room_code|--party|game/modes|mode_coop|mode_versus' game engine assets/strings*.json CMakeLists.txt tests | grep -v "^tools/friends_test"; then
  echo "FAIL grep gate"; GATE=1
else
  echo "PASS grep gate"
fi
for f in "$PREFS"/friends_*.png; do cp "$f" docs/autotest/friends/ 2>/dev/null; done
for r in a1 a2 a3 b c offline; do check $r || exit 1; done
exit ${GATE:-0}

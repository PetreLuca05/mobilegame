#!/bin/zsh
# FRIENDS-2 end to end on desktop: four instances, the lobby in the main menu (invite, lock, kick,
# start together, back to the menu, leave, panel timeout). Screenshots: docs/autotest/friends/.
# Usage: tools/friends2_test.sh OUTDIR
# MG_FT2_SALT_ROLE=mb: that role's library table hash is salted (MG_TEST_TABLE_HASH_SALT=1), so the same-table check (§15.3) must FAIL
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/mg_ft2_out}
export MG_FT_SYNC=/tmp/mg_ft2
GAME=build/desktop/mobilegame
PREFS="$HOME/Library/Application Support/docvex/mobilegame"
mkdir -p docs/autotest/friends "$OUT"
for f in "$MG_FT_SYNC"/*(N); do rm -f "$f"; done
mkdir -p "$MG_FT_SYNC"
run() { MG_TEST_TABLE_HASH_SALT=$([ "$1" = "${MG_FT2_SALT_ROLE:-}" ] && echo 1) MG_WINDOW=$3 $GAME --profile $2 --autotest friends $1 --window-pos $4,60 --quit-after 480 > $OUT/$1.log 2>&1 }
check() { grep -q "AUTOTEST PASSED" $OUT/$1.log }
report() { if check $1; then echo "PASS $1"; else echo "FAIL $1: $(grep -E 'FAILED' $OUT/$1.log | head -1 | cut -c28-200)"; fi }
for p in fta ftb ftc ftd; do
  if ! grep -q '"onboarding_done":[[:space:]]*true' "$PREFS/$p-prefs.json" 2>/dev/null; then
    $GAME --profile $p --autotest onboarding --window-pos 100,60 --quit-after 170 > $OUT/onboard_$p.log 2>&1
    check onboard_$p || { echo "FAIL onboarding $p"; exit 1; }
  fi
done
# every role starts with its lobby open: a run (this script or the other friends script) may have left one locked
for p in fta ftb ftc ftd; do
  [ -f "$PREFS/$p-prefs.json" ] && python3 -c "import json,sys; f=sys.argv[1]; d=json.load(open(f)); d['lobby_open']=True; json.dump(d,open(f,'w'))" "$PREFS/$p-prefs.json"
done
build/desktop/mg_nettest profile fta Ana 1 3 0 > /dev/null 2>&1
build/desktop/mg_nettest profile ftb Bogdan 2 2 1 > /dev/null 2>&1
build/desktop/mg_nettest profile ftc Clara 3 4 2 > /dev/null 2>&1
build/desktop/mg_nettest profile ftd Dan 0 5 4 > /dev/null 2>&1
run mb ftb iphone 40 & PB=$!
run mc ftc iphone 80 & PC=$!
run md ftd iphone 120 & PD=$!
run ma fta iphone 160; report ma
wait $PB; report mb
wait $PC; report mc
wait $PD; report md
for f in "$PREFS"/friends_*.png; do cp "$f" docs/autotest/friends/ 2>/dev/null; done
for r in ma mb mc md; do check $r || exit 1; done

#!/bin/zsh
# WORLD-2 on the desktop: --autotest worldmap2 (steps 1-6), then a fresh launch that must draw the
# vehicle the stored bag says (step 5). Usage: tools/worldmap2_test.sh [extra args, e.g. --window-pos 100,100]
cd "$(dirname "$0")/.."
bin=build/desktop/mobilegame
$bin --autotest worldmap2 --quit-after 200 "$@" > /tmp/mg_wm2_a.log 2>&1
grep -E "autotest worldmap2|AUTOTEST" /tmp/mg_wm2_a.log | sed 's/.*\] //'
grep -q "AUTOTEST PASSED" /tmp/mg_wm2_a.log || { echo "FAIL worldmap2"; exit 1 }
$bin --autotest worldmap2 relaunch --quit-after 30 "$@" > /tmp/mg_wm2_b.log 2>&1
grep -E "autotest worldmap2|AUTOTEST" /tmp/mg_wm2_b.log | sed 's/.*\] //'
grep -q "AUTOTEST PASSED" /tmp/mg_wm2_b.log || { echo "FAIL worldmap2 relaunch"; exit 1 }
echo "PASS worldmap2 (+ relaunch)"

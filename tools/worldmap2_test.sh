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
# a reinstall: the device's prefs go, the account stays; the account's ride_state brings the bag back
prof=wm2r$$
printf '{"onboarding_done": true, "auth_provider": "guest"}' > "$HOME/Library/Application Support/docvex/mobilegame/$prof-prefs.json" # straight to the menu
$bin --autotest worldmap2 --profile $prof --quit-after 200 "$@" > /tmp/mg_wm2_c.log 2>&1
grep -q "AUTOTEST PASSED" /tmp/mg_wm2_c.log || { echo "FAIL worldmap2 (profile $prof)"; exit 1 }
sleep 2 # the network thread has finished its writes at exit
rm -f "$HOME/Library/Application Support/docvex/mobilegame/$prof-prefs.json"
$bin --autotest worldmap2 relaunch --profile $prof --quit-after 40 "$@" > /tmp/mg_wm2_d.log 2>&1
grep -E "ride bag from the account|autotest worldmap2|AUTOTEST" /tmp/mg_wm2_d.log | sed 's/.*\] //'
grep -q "AUTOTEST PASSED" /tmp/mg_wm2_d.log || { echo "FAIL worldmap2 after a reinstall"; exit 1 }
echo "PASS worldmap2 (+ relaunch, + reinstall from the account)"

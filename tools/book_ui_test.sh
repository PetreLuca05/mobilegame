#!/usr/bin/env bash
# PARKED (user decision 2026-11-14): the missions and question panels are parked, this script is in no suite and not run.
# BOOK-ENGINE S8: --autotest book_ui (the script player, every item type answered wrong then right) on the default window
# and the iPhone, iPhone-flipped and iPad presets, on the synthetic test fixture (generated into the build folder: no book
# content in the repository); pictures stay in the build folder (they show the fixture's placeholder text). macOS desktop.
#   tools/book_ui_test.sh [LOGDIR]
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/desktop/mobilegame}
FIXTURE=${FIXTURE:-$(dirname "$BIN")/fixture}
PROFILE=${PROFILE:-bookui}
LOGS=${1:-build/book_ui_logs}
PREF="$HOME/Library/Application Support/docvex/mobilegame"
OUT=${OUT:-$(dirname "$BIN")/book_ui_pictures}
mkdir -p "$OUT" "$LOGS"
if ! grep -qs '"onboarding_done":[[:space:]]*true' "$PREF/$PROFILE-prefs.json"; then
    "$BIN" --autotest onboarding --profile "$PROFILE" > "$LOGS/onboarding.log" 2>&1 || true
fi
status=0
for win in default iphone iphone-flip ipad; do
    log="$LOGS/book_ui_$win.log"
    rm -f "$PREF"/book_"$win"_*.png
    if [[ $win == default ]]; then
        env -u MG_WINDOW "$BIN" --autotest book_ui --profile "$PROFILE" --offline --content "$FIXTURE" > "$log" 2>&1 || true
    else
        MG_WINDOW=$win "$BIN" --autotest book_ui --profile "$PROFILE" --offline --content "$FIXTURE" > "$log" 2>&1 || true
    fi
    if grep -q "AUTOTEST PASSED" "$log"; then
        echo "book_ui $win: PASS  $(grep -o 'window [a-z-]*: .*' "$log" | tail -1)"
    else
        echo "book_ui $win: FAIL  $(grep -E 'FAILED|ERROR' "$log" | tail -2 | tr '\n' ' ')"
        status=1
    fi
    for f in "$PREF"/book_"$win"_*.png; do
        [[ -e $f ]] && sips -Z 1200 "$f" --out "$OUT/$(basename "$f")" > /dev/null
    done
done
exit $status

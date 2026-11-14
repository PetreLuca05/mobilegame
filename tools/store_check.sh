#!/usr/bin/env bash
# The App Store / Play rule end to end: configure + build a desktop MG_STORE=ON build (build/desktop_store), then run
# --autotest ui in it (no --store-config: the compile-time flag alone) and require the store rule's PASS line
# (Settings registers no Developer widget and draws none of its text).
#
#   tools/store_check.sh [OUTDIR]     # logs in OUTDIR (default build/desktop_store/store_check)
#   MG_WINDOW=iphone tools/store_check.sh
#
# The runtime twin of this check is --autotest ui --store-config in any desktop build.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=build/desktop_store
OUT="${1:-$BUILD/store_check}"
mkdir -p "$OUT"
cmake -S . -B "$BUILD" -G Ninja -DMG_PLATFORM=desktop -DMG_STORE=ON > "$OUT/configure.log"
grep -q '^MG_STORE:BOOL=ON' "$BUILD/CMakeCache.txt" || { echo "FAIL: $BUILD is not configured with MG_STORE=ON" >&2; exit 1; }
cmake --build "$BUILD" 2>&1 | tee "$OUT/build.log" | tail -1
if grep -qE 'warning:|error:' "$OUT/build.log"; then
    echo "FAIL: the MG_STORE build has warnings or errors (see $OUT/build.log)" >&2
    exit 1
fi
LOG="$OUT/ui${MG_WINDOW:+_$MG_WINDOW}.log"
"$BUILD/mobilegame" --autotest ui > "$LOG" 2>&1 || true
grep -E 'PASS store rule|AUTOTEST (PASSED|FAILED)|FAILED at' "$LOG" || true
if grep -q 'AUTOTEST PASSED' "$LOG" && grep -q 'PASS store rule' "$LOG" && grep -q 'store configuration: no Developer section' "$LOG"; then
    echo "PASS: MG_STORE build: no Developer section in Settings ($LOG)"
else
    echo "FAIL: MG_STORE build (see $LOG)" >&2
    exit 1
fi

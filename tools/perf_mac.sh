#!/bin/zsh
# Mac perf runs that count: the binary's own label must be HEAD on a clean tree, and every result must carry it.
#   tools/perf_mac.sh OUT SCENARIO [RUNS] [SECONDS] [-- extra game args]
# Builds build/desktop_rel (Release), refuses a dirty tree (outside docs/) or a binary whose embedded MG_BUILD_COMMIT is not HEAD,
# runs `--autotest perf SCENARIO SECONDS` RUNS times (default 2 x 60 s) and keeps OUT/mac_SCENARIO_N.json only when its "commit"
# is HEAD and it is valid (on power, paced by the screen, the window in front, the refresh unchanged). Exit 1 if any run does not count.
cd "$(dirname "$0")/.."
OUT=$1; SC=$2; RUNS=${3:-2}; SECS=${4:-60}
[[ -n $OUT && -n $SC ]] || { echo "usage: tools/perf_mac.sh OUT SCENARIO [RUNS] [SECONDS] [-- args]"; exit 2; }
shift $(( $# < 4 ? $# : 4 )); [[ ${1:-} == -- ]] && shift
EXTRA=("$@")
fail() { echo "perf_mac: $*" >&2; exit 1; }
git diff --quiet HEAD -- . ':!docs' || fail "tracked files differ from HEAD: commit first (a dirty build's numbers belong to no commit)"
HEAD=$(git rev-parse --short=7 HEAD)
[[ -d build/desktop_rel ]] || cmake -S . -B build/desktop_rel -G Ninja -DMG_PLATFORM=desktop -DCMAKE_BUILD_TYPE=Release > /dev/null || fail "configure"
cmake --build build/desktop_rel > /dev/null || fail "build"
BIN=build/desktop_rel/mobilegame
strings "$BIN" | grep -qx "$HEAD" || fail "the binary is labelled $(strings "$BIN" | grep -E '^[0-9a-f]{7}(-dirty)?$' | head -1), HEAD is $HEAD"
mkdir -p "$OUT"
# macOS paces the game to the screen only while it is the active app: launched from a terminal or an editor it starts behind, so
# bring it to the front before the recording starts (the settle period) and again a few seconds later
run_front() {
  "$@" &
  local pid=$!
  for t in 2 4 7; do
    sleep 2
    local gp=$(pgrep -n -f "$BIN --autotest perf" || true)
    [[ -n $gp ]] && osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $gp) to true" > /dev/null 2>&1
  done
  wait $pid
}
PREF="$HOME/Library/Application Support/docvex/mobilegame"
RC=0
for i in $(seq 1 $RUNS); do
  echo "perf_mac: $SC run $i of $RUNS starts now (about $(( SECS + 15 )) s; keep the game window in front)"
  rm -f "$PREF/perf_$SC.json"
  run_front timeout $(( SECS + 120 )) "$BIN" --autotest perf "$SC" "$SECS" --quit-after $(( SECS + 40 )) "${EXTRA[@]}" > "$OUT/mac_${SC}_$i.log" 2>&1
  J="$OUT/mac_${SC}_$i.json"
  cp "$PREF/perf_$SC.json" "$J" 2>/dev/null || { echo "perf_mac: run $i wrote no result"; RC=1; continue; }
  python3 - "$J" "$HEAD" <<'PY' || RC=1
import json, sys
j = json.load(open(sys.argv[1])); head = sys.argv[2]
if j.get("commit") != head:
    print(f"perf_mac: {sys.argv[1]}: labelled {j.get('commit')}, HEAD is {head}: does not count"); sys.exit(1)
if not j.get("valid"):
    fr = max(j.get("frames", 1), 1)
    print(f"perf_mac: {sys.argv[1]}: invalid ({j.get('invalid_why')}; covered {100.0 * j.get('window_occluded_frames', 0) / fr:.2f} %, "
          f"inactive {100.0 * j.get('window_unfocused_frames', 0) / fr:.2f} %, {j.get('fps', 0):.1f} fps): does not count"); sys.exit(1)
c = j["counters"]; s = {x["name"]: x for x in j["scopes"]}
print(f"perf_mac: {sys.argv[1]}: {head}, {j['display_hz_start']:.0f} Hz, {j['fps']:.1f} fps, draws {c['draws']['avg']:.1f}/{c['draws']['max']:.0f}, "
      f"tris {c['tris']['avg']/1000:.1f}k/{c['tris']['max']/1000:.1f}k, frame {s['frame']['avg']:.3f} ms, render {s['render']['avg']:.3f} ms, "
      f"{j['resident_mb_max']} MB, {j['heap_allocs']} allocs, {j['dropped']} dropped, {len(j['late_frames'])} late, "
      f"covered {100.0 * j.get('window_occluded_frames', 0) / max(j['frames'], 1):.2f} %, inactive {100.0 * j.get('window_unfocused_frames', 0) / max(j['frames'], 1):.2f} % (limit 0.5 %)")
PY
done
exit $RC

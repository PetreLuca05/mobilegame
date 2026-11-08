#!/bin/zsh
# Runs --autotest perf on the connected devices and records the results.
#   tools/perf_run.sh OUTDIR [--devices "ipad iphone"] [--scenarios "yard lobby corridor_f2 stress200"]
#                     [--seconds N] [--gpu] [--no-json] [--tag NAME] [--extra "app args"]
# Fails (non-zero, naming the file) if a run produced no new output: every file
# must be newer than its run, carry that run's id and the commit at HEAD.
# Never falls back to an earlier file.
set -u
cd "$(dirname "$0")/.."
OUT=$1; shift
DEVICES=(ipad iphone); SCEN=(yard lobby corridor_f2 stress200); SECONDS_OVERRIDE=0; GPU=0; JSON=1; TAG=""; EXTRA=()
while (( $# )); do
  case $1 in
    --devices) DEVICES=(${=2}); shift 2 ;;
    --scenarios) SCEN=(${=2}); shift 2 ;;
    --seconds) SECONDS_OVERRIDE=$2; shift 2 ;;
    --gpu) GPU=1; shift ;;
    --no-json) JSON=0; shift ;;
    --tag) TAG=_$2; shift 2 ;;
    --extra) EXTRA=(${=2}); shift 2 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
fail() { echo "PERF RUN FAILED: $*" >&2; exit 1 }
COMMIT=$(git rev-parse --short=7 HEAD)
git diff --quiet HEAD -- . ':!docs' || fail "tracked files differ from HEAD ($COMMIT): commit, build and install first"
mkdir -p "$OUT"
BID=ro.docvex.mobilegame
device_name() { [[ $1 == ipad ]] && echo "iPad (4)" || echo "iPhone" }
device_udid() { [[ $1 == ipad ]] && echo 00008101-001318910A44001E || echo 00008150-0008552802A3C01C }
pull() { # device source dest
  rm -f "$3"
  xcrun devicectl device copy from --device "$(device_name $1)" --domain-type appDataContainer --domain-identifier $BID \
    --source "Documents/$2" --destination "$3" >/dev/null 2>&1
}
check_file() { # path start
  [[ -s $1 ]] || fail "$1: no file produced by this run"
  (( $(stat -f %m "$1") >= $2 )) || fail "$1: older than the run start (stale)"
}
check_stamp() { # path run_id
  read -r got_id got_commit _ < "$1"
  [[ $got_id == "$2" ]] || fail "$1: run id $got_id, expected $2"
  [[ $got_commit == "$COMMIT" ]] || fail "$1: built from $got_commit, HEAD is $COMMIT (install this commit)"
}
for dev in $DEVICES; do
  name=$(device_name $dev)
  for s in $SCEN; do
    secs=60; [[ $s == stress200 ]] && secs=180; (( SECONDS_OVERRIDE > 0 )) && secs=$SECONDS_OVERRIDE
    if (( JSON )); then
      id=$(date +%s)$RANDOM
      start=$(date +%s)
      limit=$(( secs + 40 ))
      log="$OUT/${dev}_$s$TAG.log"
      xcrun devicectl device process launch --console --terminate-existing --device "$name" $BID \
        --autotest perf $s $secs --quit-after $limit --perf-run-id $id $EXTRA > "$log" 2>&1 &
      pid=$!; (sleep $(( limit + 30 )); kill $pid 2>/dev/null) &
      watchdog=$!
      wait $pid; kill $watchdog 2>/dev/null
      grep -q "AUTOTEST PASSED" "$log" || { tail -5 "$log" >&2; fail "$log: the app did not finish the run" }
      dest="$OUT/${dev}_$s$TAG.json"
      pull $dev perf_$s.json "$dest"
      check_file "$dest" $start
      python3 - "$dest" "$id" "$COMMIT" <<'PY' || exit 1
import json,sys
d=json.load(open(sys.argv[1]))
if d.get("run_id")!=sys.argv[2]: sys.exit(f"PERF RUN FAILED: {sys.argv[1]}: run id {d.get('run_id')}, expected {sys.argv[2]}")
if d.get("commit")!=sys.argv[3]: sys.exit(f"PERF RUN FAILED: {sys.argv[1]}: built from {d.get('commit')}, HEAD is {sys.argv[3]}")
PY
      rm -f "$log"
      echo "$dev $s: ok ($dest)"
      sleep 15
    fi
    if (( GPU )); then
      id=$(date +%s)$RANDOM
      udid=$(device_udid $dev)
      base="$OUT/${dev}_$s${TAG}_gpu"
      xcrun devicectl device process launch --terminate-existing --device "$name" $BID --quit-after 1 >/dev/null 2>&1
      sleep 8
      rm -rf "$base.trace" "$base.stamp"
      start=$(date +%s)
      timeout 120 xcrun xctrace record --template 'Metal System Trace' --device $udid --time-limit 10s --output "$base.trace" \
        --launch -- $BID --autotest perf $s 30 --quit-after 45 --perf-run-id $id $EXTRA > "$base.log" 2>&1
      [[ -e "$base.trace/Trace1.run" ]] || { tail -5 "$base.log" >&2; fail "$base.trace: xctrace recorded nothing" }
      pull $dev perf_stamp.txt "$base.stamp"
      check_file "$base.stamp" $start
      check_stamp "$base.stamp" $id
      timeout 200 xcrun xctrace export --input "$base.trace" --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]' > "$base.xml" 2>/dev/null
      timeout 100 xcrun xctrace export --input "$base.trace" --xpath '/trace-toc/run[@number="1"]/data/table[@schema="gpu-performance-state-intervals"]' > "$base.st.xml" 2>/dev/null
      check_file "$base.xml" $start
      check_file "$base.st.xml" $start
      python3 tools/perf_gpu_summary.py "$base.xml" "$base.st.xml" "$dev" "$s" "$COMMIT" > "$base.json" || fail "$base.xml: could not read the trace"
      cat "$base.json"
      rm -rf "$base.trace" "$base.xml" "$base.st.xml" "$base.log" "$base.stamp"
      sleep 15
    fi
  done
done

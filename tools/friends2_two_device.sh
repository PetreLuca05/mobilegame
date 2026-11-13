#!/bin/zsh
# FRIENDS-2 on two devices: the iPad hosts its menu lobby, this Mac is invited. Needs the app installed on the
# iPad. Friend request both ways, join with approval, decline, timeout, kick, host-leave, and the
# NPC hash compared at every tick both sides logged. Usage: tools/friends2_two_device.sh OUTDIR
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/mg_2d}
mkdir -p "$OUT"
DEV="iPad (4)"; BID=ro.docvex.mobilegame; GAME=build/desktop/mobilegame; P=dg_two
ipad() { xcrun devicectl device process launch --console --terminate-existing --device "$DEV" "$@"; }

ipad $BID --frontend menu --autotest friends d_code --quit-after 60 > $OUT/ipad_code.log 2>&1
IPAD_CODE=$(grep -oE 'my code [0-9A-Z]{6}' $OUT/ipad_code.log | head -1 | awk '{print $3}')
$GAME --profile $P --frontend menu --autotest friends d_code --window-pos 100,60 --quit-after 60 > $OUT/mac_code.log 2>&1
MAC_CODE=$(grep -oE 'my code [0-9A-Z]{6}' $OUT/mac_code.log | head -1 | awk '{print $3}')
echo "ipad $IPAD_CODE mac $MAC_CODE"
[ -n "$IPAD_CODE" ] && [ -n "$MAC_CODE" ] || { echo "FAIL could not read the codes"; exit 1; }

ipad -e "{\"MG_FT_PEER\":\"$MAC_CODE\"}" $BID --frontend menu --autotest friends d2h --quit-after 600 > $OUT/ipad.log 2>&1 &
PI=$!
for i in {1..120}; do grep -q "my code" $OUT/ipad.log 2>/dev/null && break; sleep 1; done
MG_FT_PEER=$IPAD_CODE $GAME --profile $P --frontend menu --autotest friends d2g --window-pos 100,60 --quit-after 600 > $OUT/mac.log 2>&1
MR=0; grep -q "AUTOTEST PASSED" $OUT/mac.log || MR=1
wait $PI
IR=0; grep -q "AUTOTEST PASSED" $OUT/ipad.log || IR=1
for s in mac ipad; do
  if grep -q "AUTOTEST PASSED" $OUT/$s.log; then echo "PASS $s"; else echo "FAIL $s: $(grep -E 'FAILED at' $OUT/$s.log | tail -1 | cut -c1-200)"; echo "  last: $(grep -E 'autotest friends .*: op' $OUT/$s.log | tail -1 | cut -c1-200)"; fi
done
python3 - $OUT/ipad.log $OUT/mac.log <<'PY'
import re,sys
def load(p): return {int(a):b for a,b in re.findall(r"npc-sync: tick (\d+) hash ([0-9a-f]+)", open(p,errors="ignore").read())}
a,b=load(sys.argv[1]),load(sys.argv[2]); both=sorted(set(a)&set(b)); bad=[t for t in both if a[t]!=b[t]]
print(f"NPC hash: {len(both)} common ticks, {len(bad)} differ", "PASS" if both and not bad else "FAIL", both[:6])
PY
python3 - $OUT/ipad.log $OUT/mac.log <<'PY'
import re,sys
def h(p):
    m=re.findall(r"library table hash ([0-9a-f]{8}) \(bay (\d+), (\d+) studying; parts ([0-9a-f ]+)\)", open(p,errors="ignore").read())
    return m[-1] if m else None
a,b=h(sys.argv[1]),h(sys.argv[2])
if not a or not b:
    print("table hash: FAIL (a side logged none)", a, b); sys.exit(1)
print(f"table hash: iPad {a[0]} (bay {a[1]}, {a[2]} studying), Mac {b[0]} (bay {b[1]}, {b[2]} studying):", "PASS the same table" if a[0]==b[0] else "FAIL")
if a[0]!=b[0]:
    names=["seat order","covers","lamp","clutter","members"]; pa,pb=a[3].split(),b[3].split()
    print("  differs in:", ", ".join(n for n,x,y in zip(names,pa,pb) if x!=y)); sys.exit(1)
PY
TR=$?
exit $((MR+IR+TR))

#!/usr/bin/env python3
"""metal-gpu-intervals + gpu-performance-state-intervals exports -> one JSON line:
GPU frame ms, GPU-busy %, effective ms = ms x busy, dominant clock state (last 5 s)."""
import json
import re
import sys
import xml.etree.ElementTree as ET

iv_path, st_path, device, scenario, commit = sys.argv[1:6]
root = ET.parse(iv_path).getroot()
ids = {el.attrib["id"]: el for el in root.iter() if "id" in el.attrib}
res = lambda e: ids[e.attrib["ref"]] if "ref" in e.attrib else e
iv = []
for row in root.iter("row"):
    c = [res(x) for x in row]
    if c[10].attrib.get("fmt", "").startswith("Mobilegame") and c[5].text == "0":
        a = int(c[0].text)
        iv.append((a, a + int(c[1].text), c[3].text if c[3].tag != "sentinel" else None))
if not iv:
    sys.exit("no Mobilegame GPU intervals in the trace")
end = max(b for _, b, _ in iv)
iv = sorted(x for x in iv if x[0] >= end - 5e9)
busy, ca, cb, frames = 0, None, None, set()
for a, b, f in iv:
    frames.add(f)
    if cb is None or a > cb:
        if cb is not None:
            busy += cb - ca
        ca, cb = a, b
    else:
        cb = max(cb, b)
busy += cb - ca
nf = len([f for f in frames if f]) or 1
span = (iv[-1][1] - iv[0][0]) / 1e9
ms, pct = busy / 1e6 / nf, busy / (span * 1e9)
text, names, tot = open(st_path).read(), {}, {}
for m in re.finditer(r"<row>(.*?)</row>", text):
    r = m.group(1)
    st = re.search(r'<gpu-performance-state id="(\d+)" fmt="([^"]+)"', r)
    if st:
        names[st.group(1)] = st.group(2)
        nm = st.group(2)
    else:
        rf = re.search(r'<gpu-performance-state ref="(\d+)"', r)
        nm = names.get(rf.group(1), "?") if rf else "?"
    d = re.search(r'<duration id="\d+"[^>]*>(\d+)</duration>', r)
    if d:
        tot[nm] = tot.get(nm, 0) + int(d.group(1))
dom = max(tot, key=tot.get) if tot else "?"
share = 100 * tot.get(dom, 0) / (sum(tot.values()) or 1)
print(json.dumps({"device": device, "scenario": scenario, "commit": commit, "fps": round(nf / span), "gpu_frame_ms": round(ms, 2),
                  "busy_pct": round(100 * pct), "effective_ms": round(ms * pct, 2), "clock": dom, "clock_share_pct": round(share)}))

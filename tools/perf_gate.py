#!/usr/bin/env python3
"""The permanent performance gate (docs/PERF_RULES.md).

  tools/perf_gate.py NEW_DIR [--reference docs/perf/after]

Reads the JSON that tools/perf_run.sh wrote (<device>_<scenario>.json and
<device>_<scenario>_gpu.json) and checks every budget, then compares with the
reference run: anything more than 10 % worse fails. Exit code 1 on any FAIL or
on a missing file; missing files are never skipped."""
import json
import os
import sys

DEVICES = ["ipad"] # the iPad Air 4 is the reference; the iPhone is no longer tested
PRE_FRIENDS_PASSES = 4  # main, two blur (the frosted pause button, e99c268), UI: FRIENDS-1 may not add any
SCENARIOS = ["yard", "lobby", "corridor_f2", "stress200", "npcs", "lift_panel", "interact_npc", "lobby4", "menu", "worldmap", "worldmap_travel", "island", "worldmap_voyage"]
# WORLD-1 estimates (iPad Air 4); a measurement over 2x an estimate fails. Draws counted by the renderer include the
# HUD batch: <= 16 world + 1 x-ray (a cube hides the parked ambulance) + 1 HUD = 18.
WORLD_LIMITS = {"worldmap": {"gpu_effective_ms": 1.5, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "worldmap_travel": {"gpu_effective_ms": 1.0, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "island": {"gpu_effective_ms": 1.0, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "worldmap_voyage": {"gpu_effective_ms": 2.5, "render_ms": 0.25, "cpu_tick_ms": 0.05}}
WORLD_MAX_DRAWS = 18
TOLERANCE = 1.10
ABS_NOISE = {"cpu_tick_ms": 0.1, "npc_ms": 0.1, "render_ms": 0.1, "gpu_effective_ms": 0.2, "draws": 5, "memory_mb": 5}
# name: (budget, stress200 only)
BUDGETS = {"cpu_tick_ms": (5.0, False), "npc_ms": (2.0, True), "render_ms": (2.0, False), "gpu_effective_ms": (8.0, False),
           "draws": (150, False), "heap_allocs": (0, False), "memory_mb": (300, False), "dropped": (0, False)}


def load(path, problems):
    if not os.path.exists(path):
        problems.append(f"MISSING {path}")
        return None
    with open(path) as f:
        return json.load(f)


def metrics(d, g):
    s = {x["name"]: x for x in d["scopes"]}
    avg = lambda n: s.get(n, {}).get("avg", 0.0)
    m = {"cpu_tick_ms": avg("tick") + avg("frame"), "npc_ms": avg("npc") + avg("npc.roster"), "render_ms": avg("render"), "draws": d["counters"]["draws"]["avg"],
         "heap_allocs": d["heap_allocs"], "memory_mb": d["resident_mb_max"], "dropped": d["dropped"], "thermal": d["thermal_worst"]}
    m["gpu_effective_ms"] = g["effective_ms"] if g else None
    return m


def main():
    new = sys.argv[1]
    ref = sys.argv[sys.argv.index("--reference") + 1] if "--reference" in sys.argv else "docs/perf/after"
    problems, compare = [], ref != new and os.path.isdir(ref)
    for dev in DEVICES:
        for sc in SCENARIOS:
            d = load(f"{new}/{dev}_{sc}.json", problems)
            g = load(f"{new}/{dev}_{sc}_gpu.json", problems)
            if not d or not g:
                continue
            m = metrics(d, g)
            if sc in ("lift_panel", "interact_npc") and d.get("panel_samples", 0) < 0.9 * d["frames"]:
                problems.append(f"{dev} {sc}: the panel was up in only {d.get('panel_samples', 0)} of {d['frames']} frames")
            if sc == "lobby4" and d["counters"]["passes"]["max"] > PRE_FRIENDS_PASSES:
                problems.append(f"{dev} {sc}: {d['counters']['passes']['max']:.0f} passes, the pre-FRIENDS baseline is {PRE_FRIENDS_PASSES}")
            for name, (budget, stress_only) in BUDGETS.items():
                if stress_only and sc != "stress200":
                    continue
                ok = m[name] <= budget
                print(f"{'PASS' if ok else 'FAIL'} {dev:6} {sc:12} {name:18} {m[name]:9.2f} budget {budget}")
                if not ok:
                    problems.append(f"{dev} {sc}: {name} {m[name]:.2f} over budget {budget}")
            if sc in WORLD_LIMITS: # the estimates are the map's own tick and the submit (encode + sort), not the whole frame
                scope = {x["name"]: x["avg"] for x in d["scopes"]}
                m = dict(m, cpu_tick_ms=scope.get("tick", 0.0), render_ms=scope.get("render.encode", 0.0) + scope.get("render.sort", 0.0))
                if d["counters"]["draws"]["max"] > WORLD_MAX_DRAWS:
                    problems.append(f"{dev} {sc}: {d['counters']['draws']['max']:.0f} draws, the design allows {WORLD_MAX_DRAWS} (16 world + x-ray + HUD)")
                for name, est in WORLD_LIMITS[sc].items():
                    print(f"{'PASS' if m[name] <= 2 * est else 'FAIL'} {dev:6} {sc:12} {name:18} {m[name]:9.2f} estimate {est} (fails over 2x)")
                    if m[name] > 2 * est:
                        problems.append(f"{dev} {sc}: {name} {m[name]:.2f} is over 2x the estimate {est}")
            if m["thermal"] > 1:
                problems.append(f"{dev} {sc}: thermal state {m['thermal']} above fair")
            if compare:
                # a scenario newer than the reference has nothing to regress against; the new run is still required
                has_ref = os.path.exists(f"{ref}/{dev}_{sc}.json")
                rd, rg = (load(f"{ref}/{dev}_{sc}.json", problems), load(f"{ref}/{dev}_{sc}_gpu.json", problems)) if has_ref else (None, None)
                if rd and rg:
                    r = metrics(rd, rg)
                    for name, noise in ABS_NOISE.items():
                        if m[name] > r[name] * TOLERANCE and m[name] - r[name] > noise:
                            problems.append(f"{dev} {sc}: {name} regressed {r[name]:.2f} -> {m[name]:.2f}")
    for p in problems:
        print("FAIL", p)
    print("PERF GATE", "FAILED" if problems else "PASSED")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

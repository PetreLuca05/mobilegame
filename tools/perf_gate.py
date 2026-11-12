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
MENU_TO_MAP_DRAWS_MAX = 60  # the Maps / Back move, the worst frame (user decision 2026-11-12: 60, not 46 + 16)
MENU_TRIS_STOP_K = 75.0  # the menu at rest (LIB-1 square hall: estimate 58 k)
SCENARIOS = ["yard", "lobby", "corridor_f2", "stress200", "npcs", "lift_panel", "interact_npc", "lobby4", "menu", "menu_to_map", "ui_juice", "worldmap", "worldmap_travel", "island", "worldmap_voyage", "institute", "book"]
# WORLD-1 estimates (iPad Air 4); a measurement over 2x an estimate fails. Draws counted by the renderer include the
# HUD batch: <= 16 world + 1 x-ray (a cube hides the parked ambulance) + 1 HUD = 18.
WORLD_LIMITS = {"worldmap": {"gpu_effective_ms": 1.5, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "worldmap_travel": {"gpu_effective_ms": 1.0, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "island": {"gpu_effective_ms": 1.0, "render_ms": 0.15, "cpu_tick_ms": 0.05},
                "worldmap_voyage": {"gpu_effective_ms": 2.5, "render_ms": 0.25, "cpu_tick_ms": 0.05}}
WORLD_MAX_DRAWS = 18
WORLD_MAX_DRAWS_VOYAGE = 38  # WORLD-2: both islands for part of a voyage (32) + vehicle 4 + effects 1 + HUD 1
# UI-1 estimates (fail over 2x), every scenario: tween + effect update, UI submit (instance upload); the UI is one draw
UI_LIMITS = {"ui_update_ms": 0.05, "ui_submit_ms": 0.15}
UI_MAX_DRAWS = 3
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
            if d.get("valid") is False: # refused on battery, or the display refresh changed during the recording
                problems.append(f"{dev} {sc}: run invalid ({d.get('invalid_why', '')})")
                continue
            if d.get("paced_below_refresh"):
                print(f"WARN {dev:6} {sc:12} {d.get('fps', 0):.1f} fps under a {d.get('display_hz_start', 0):.0f} Hz refresh (the OS paced the window down?)")
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
                cap = WORLD_MAX_DRAWS_VOYAGE if sc == "worldmap_voyage" else WORLD_MAX_DRAWS
                if d["counters"]["draws"]["max"] > cap:
                    problems.append(f"{dev} {sc}: {d['counters']['draws']['max']:.0f} draws, the design allows {cap}")
                for name, est in WORLD_LIMITS[sc].items():
                    print(f"{'PASS' if m[name] <= 2 * est else 'FAIL'} {dev:6} {sc:12} {name:18} {m[name]:9.2f} estimate {est} (fails over 2x)")
                    if m[name] > 2 * est:
                        problems.append(f"{dev} {sc}: {name} {m[name]:.2f} is over 2x the estimate {est}")
            scope = {x["name"]: x["avg"] for x in d["scopes"]}
            ui = {"ui_update_ms": scope.get("ui.anim", 0.0) + scope.get("ui.fx", 0.0), "ui_submit_ms": scope.get("render.ui", 0.0)}
            for name, est in UI_LIMITS.items():
                print(f"{'PASS' if ui[name] <= 2 * est else 'FAIL'} {dev:6} {sc:12} {name:18} {ui[name]:9.3f} estimate {est} (fails over 2x)")
                if ui[name] > 2 * est:
                    problems.append(f"{dev} {sc}: {name} {ui[name]:.3f} is over 2x the estimate {est}")
            fc = d["counters"]
            if "fade_violations" in fc and fc["fade_violations"]["max"] > 0: # the blended toon pipeline: the Maps move's shell only
                problems.append(f"{dev} {sc}: {fc['fade_violations']['max']:.0f} whole-object fade draws refused (only the Maps move may fade)")
            if "fade_draws" in fc and fc["fade_draws"]["max"] > 0 and sc != "menu_to_map":
                problems.append(f"{dev} {sc}: {fc['fade_draws']['max']:.0f} whole-object fade draws outside the Maps move")
            if sc == "menu_to_map" and "draws" in fc:
                dm = fc["draws"]["max"]
                print(f"{'PASS' if dm <= MENU_TO_MAP_DRAWS_MAX else 'FAIL'} {dev:6} {sc:12} {'draws_max':18} {dm:9.0f} limit {MENU_TO_MAP_DRAWS_MAX}")
                if dm > MENU_TO_MAP_DRAWS_MAX:
                    problems.append(f"{dev} {sc}: {dm:.0f} draws in a frame of the move, over {MENU_TO_MAP_DRAWS_MAX}")
            if sc == "menu" and "tris" in fc: # LIB-1 square hall: 58 k at rest, stop line 75 k (user decision 2026-11-12)
                t = fc["tris"]["avg"] / 1000.0
                print(f"{'PASS' if t <= MENU_TRIS_STOP_K else 'FAIL'} {dev:6} {sc:12} {'tris_k':18} {t:9.1f} estimate 58 (stop line {MENU_TRIS_STOP_K})")
                if t > MENU_TRIS_STOP_K:
                    problems.append(f"{dev} {sc}: {t:.1f} k triangles at rest, over the {MENU_TRIS_STOP_K} k stop line")
            if "ui_draws" in d["counters"] and d["counters"]["ui_draws"]["max"] > UI_MAX_DRAWS:
                problems.append(f"{dev} {sc}: {d['counters']['ui_draws']['max']:.0f} UI draws, UI-1 allows {UI_MAX_DRAWS}")
            print(f"INFO {dev:6} {sc:12} passes max {d['counters']['passes']['max']:.0f}")
            if "fps" in d: # compare like with like: every run states the screen it was paced by
                print(f"INFO {dev:6} {sc:12} refresh {d.get('display_hz_start', 0):.0f} Hz, {d['fps']:.1f} fps"
                      + (" (not paced by the screen)" if d.get("paced_above_refresh") or d.get("display_asleep_s") or d.get("window_occluded_frames") else ""))
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

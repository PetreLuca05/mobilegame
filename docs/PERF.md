# Performance report (PERF-1)

Reference device: iPad Air 4 (A14). Everything here is a Release build with the profiler on, measured with `tools/perf_run.sh`.
GPU figures are Instruments Metal System Trace: frame ms, GPU-busy %, effective ms = ms x busy %, dominant clock state. Raw files: `docs/perf/`.
Budgets and standing rules: [PERF_RULES.md](PERF_RULES.md).

**Verdict:** the 200-NPC stress scene meets every budget on the iPad Air 4 for a 5-minute run, thermal state nominal (commit `6c46299`, MSAA 2x).

## Sign-off decisions (PERF-1 signed off at `f19cace`)

- **MSAA 2x ships; the hot-device path drops it to off. Final.**
- Sub-target misses are accepted as debt, not failures; see "Audit status and deferred work".
- The gate (`--gate`) fails on any heap allocation in the frame loop except those made while saving a screenshot, which are counted separately (`heap_allocs_screenshot`). Debug builds count allocations and do not abort.
- iPhone testing stopped at `f19cace`. If the iPhone comes back as a test device, the first task is a dropped-frame trace.

## iPad Air 4: before and after

Before is the baseline at `c4eafa3` (MSAA 4x, 180 s stress). After is `6c46299` (MSAA 2x; stress run 300 s).

| Scenario | Metric | Before | After | Budget |
| --- | --- | --- | --- | --- |
| yard | CPU tick + game frame ms | 0.20 | 0.16 | 5.0 |
| yard | Render submission ms (p95) | 1.43 (1.85) | 1.03 (1.40) | 2.0 |
| yard | GPU ms / busy % / effective ms | 5.06 / 30 / 1.54 | 4.82 / 29 / 1.4 | 8.0 effective |
| yard | Draw calls | 109 | 68 | 150 |
| yard | Triangles (k) | 50 | 50 | - |
| yard | Render passes | 3 | 2 | 2 |
| yard | Heap allocations in the frame loop | 0 | 0 | 0 |
| yard | Memory MB | 220 | 154 | 300 |
| yard | Dropped frames | 0 | 0 | 0 |
| yard | Worst thermal state | 0 | 0 | 1 (fair) |
| lobby | CPU tick + game frame ms | 0.20 | 0.18 | 5.0 |
| lobby | Render submission ms (p95) | 1.38 (1.80) | 1.10 (1.50) | 2.0 |
| lobby | GPU ms / busy % / effective ms | 4.81 / 29 / 1.39 | 4.9 / 29 / 1.44 | 8.0 effective |
| lobby | Draw calls | 122 | 67 | 150 |
| lobby | Triangles (k) | 50 | 50 | - |
| lobby | Render passes | 3 | 2 | 2 |
| lobby | Heap allocations in the frame loop | 0 | 0 | 0 |
| lobby | Memory MB | 219 | 155 | 300 |
| lobby | Dropped frames | 0 | 0 | 0 |
| lobby | Worst thermal state | 0 | 0 | 1 (fair) |
| corridor_f2 | CPU tick + game frame ms | 0.26 | 0.17 | 5.0 |
| corridor_f2 | Render submission ms (p95) | 2.03 (2.55) | 1.09 (1.40) | 2.0 |
| corridor_f2 | GPU ms / busy % / effective ms | 6.32 / 38 / 2.40 | 4.86 / 29 / 1.42 | 8.0 effective |
| corridor_f2 | Draw calls | 253 | 87 | 150 |
| corridor_f2 | Triangles (k) | 59 | 59 | - |
| corridor_f2 | Render passes | 3 | 2 | 2 |
| corridor_f2 | Heap allocations in the frame loop | 0 | 0 | 0 |
| corridor_f2 | Memory MB | 219 | 157 | 300 |
| corridor_f2 | Dropped frames | 0 | 0 | 0 |
| corridor_f2 | Worst thermal state | 0 | 0 | 1 (fair) |
| stress200 | CPU tick + game frame ms | 0.47 | 0.41 | 5.0 |
| stress200 | NPC sim ms on the main thread | 0.02 | 0.20 | 2.0 |
| stress200 | Render submission ms (p95) | 5.86 (7.65) | 1.03 (1.45) | 2.0 |
| stress200 | GPU ms / busy % / effective ms | 6.70 / 40 / 2.71 | 4.9 / 29 / 1.44 | 8.0 effective |
| stress200 | Draw calls | 1978 | 88 | 150 |
| stress200 | Triangles (k) | 1008 | 179 | - |
| stress200 | Render passes | 3 | 2 | 2 |
| stress200 | Heap allocations in the frame loop | 429 | 0 | 0 |
| stress200 | Memory MB | 256 | 164 | 300 |
| stress200 | Dropped frames | 1 | 0 | 0 |
| stress200 | Worst thermal state | 0 | 0 | 1 (fair) |

All GPU figures were taken at the Minimum clock state (88-100 % of the time). The baseline dummies only followed paths; the after dummies also separate from each other, which is why the NPC cost rises.

## iPhone 17 Pro Max: last measurements

Testing on the iPhone stopped at `f19cace`, at the user's request. These are the last valid numbers, commit `6c46299`, MSAA 2x. Recorded as **FAIL**: 16 dropped frames in the 5-minute run. Recorded as **NOT VERIFIABLE**: the GPU stress scene and the determinism test.

| Scenario | Tick + frame ms | Render ms | GPU ms / busy % / effective | Draws | Memory MB | Dropped |
| --- | --- | --- | --- | --- | --- | --- |
| yard | 0.08 | 0.71 | 2.04 / 12 / 0.25 | 64 | 207 | 0 |
| lobby | 0.09 | 0.72 | 1.94 / 12 / 0.23 | 63 | 202 | 1 |
| corridor_f2 | 0.09 | 0.76 | 2.19 / 13 / 0.29 | 83 | 202 | 2 |
| stress200 | 0.32 | 0.83 | not measured | 87 | 212 | 16 |

The 5-minute stress run dropped 16 of 17,985 frames (none of 50 ms or more). The iPhone does not meet "at or under the iPad": its memory is higher (212 vs 164 MB in stress) and it dropped frames where the iPad dropped none.

## What each change bought (iPad, stress run)

| Step | Change | Result |
|---|---|---|
| perf 1 | Instrumentation | Baseline: render 5.86 ms, sort 2.72 ms, encode 2.67 ms, 1,978 draws |
| perf 2 | Job system, spatial hash, hot/cold NPC data, staggered rates, separation | NPC sim 0.02 -> 0.12 ms: the sim now does more; no render change |
| perf 3a | Auto-instancing, radix sort, opaque/dithered split | Render 5.86 -> 1.52 ms, sort 2.67 -> 0.16 ms, encode 2.71 -> 0.42 ms, draws 1,969 -> 166, heap allocs 346 -> 0, GPU effective 2.71 -> 2.12 ms |
| perf 3b | Static merge per storey, LOD, frustum cull, present inside the UI pass | Draws 166 -> 89, triangles 962k -> 187k, passes 3 -> 2, render 1.52 -> 1.09 ms, GPU effective 2.12 -> 1.48 ms |
| perf 4 | Release the load-time scratch pages (about 27-30 MB) | Part of the memory drop below |
| perf 5 | MSAA 2x, mesh buffers 12 -> 6 MB | Memory 239 -> 164 MB on the iPad |

Without instancing, the desktop stress scene goes from 91 to 294 draws (`--no-instancing`).

## MSAA decision

Rule: at most 15 % of the 8 ms effective budget (1.2 ms) on the iPad at the 200-NPC scene, the thermal path can drop it, and 4x may not push the iPhone over 300 MB. Measured on `f8a0163`, stress scene, 40 s:

| Samples | iPad effective GPU ms | iPad footprint MB | iPhone effective GPU ms | iPhone footprint MB |
|---|---|---|---|---|
| 4x | 1.51 | 210 | 0.32 | 340 |
| 2x | 1.40 | 168 | 0.45 | 300 (see note) |
| off | 1.42 | 141 | 0.27 | 184 |

**Decision: ship 2x** (`GFX_DEFAULT_MSAA`). GPU cost is within the rule at every level (differences of 0.1 ms are within noise), but 4x puts the iPhone at 340 MB. The thermal path drops it to off at state "serious".

Note: the 2x and off rows were switched from a 4x start at runtime, which inflates the iPhone figures. With 2x as the default from launch, the 5-minute run peaked at 212 MB on the iPhone and 164 MB on the iPad.

## Memory breakdown (stress run, MB, commit `6c46299`, 2x)

| Item | iPad | iPhone |
| --- | --- | --- |
| MSAA colour | 18.9 | 18.8 |
| Depth (MSAA) | 18.9 | 18.8 |
| Scene texture | 9.4 | 9.4 |
| Blur targets | 1.2 | 1.2 |
| Swapchain (estimate: 3 drawables) | 44.3 | 43.3 |
| UI atlas | 0.2 | 0.2 |
| Mesh buffers reserved (used 2.0) | 6.0 | 6.0 |
| UI buffers | 1.3 | 1.3 |
| Instance buffers | 2.5 | 2.5 |
| Allocator arena in use (includes nav and NPC) | 19.5 | 19.5 |
| Rest: Metal driver, binary, OS | 42.2 | 90.5 |
| **Footprint** | **164.4** | **211.5** |

Inside the arena: nav grids 7.6 MB, NPC storage 0.9 MB; audio is in the arena and in the rest. Load-time peak was 49.0 MB; those pages are now given back. The merged static meshes are not duplicated (2.0 MB used).

## Remaining hot spots (iPad, stress run, ms per frame)

| Scope | Avg | p95 | Max |
| --- | --- | --- | --- |
| render | 1.03 | 1.45 | 2.74 |
| render.encode | 0.27 | 0.40 | 1.78 |
| tick | 0.23 | 0.45 | 1.87 |
| npc | 0.20 | 0.40 | 1.87 |
| frame | 0.19 | 0.30 | 1.33 |
| npc.draw | 0.05 | 0.15 | 1.13 |
| render.sort | 0.03 | 0.10 | 0.60 |
| kinematic | 0.02 | 0.05 | 1.07 |
| npc.cull | 0.02 | 0.05 | 0.54 |
| render.cull | 0.01 | 0.05 | 0.58 |

`render` contains `render.sort` and `render.encode`; `frame` and `tick` contain their nested scopes. About 0.7 ms of `render` is outside those two scopes (instance building and upload, UI encoding, submit); it is the largest item left and is not broken down yet.

## Audit status and deferred work

| Item | Status | Cost / reason |
|---|---|---|
| NPC hot/cold layout | Done, `npc_hot_t` is 64 bytes or less, double-buffered | The hot data is an array of one-cache-line structs rather than per-field arrays |
| Spatial hash | Done, O(n) counting sort | Used by NPC separation; markers and name tags have too few entries to need it today |
| Update rates | Done for steering (1, 3, 6 ticks) | Package re-evaluation arrives with M7 |
| Path queue | Done: separate NPC queue, 600 cells and 2 paths per tick, 32-entry cell cache; player queue 1 ms per frame, 16-entry room cache | |
| Job system | Done, 6 threads on the iPad | At 400 NPCs one thread takes 0.057 ms per tick and 6 take 0.062 ms: dispatch costs as much as it saves until the per-NPC work grows in M7 |
| Determinism | Passes on the iPad and desktop | LOD rates depend on the viewer, so two players with different views can drift; M7 accepts drift for now |
| Render thread priority | Done, log shows 45 on the iPad | |
| Heap allocations | 0 in every recorded run | Counted on the HUD and in the JSON. Debug builds do not abort; `--gate` fails on any allocation outside a screenshot |
| Vertex-shader avatar animation | **Deferred** (accepted) | Avatars are instanced with matrices composed on the CPU. `npc.draw` is the cost it would save: under 0.2 ms per frame with 200 NPCs; expected saving about half of that, and it needs a part id per instance. Candidate when the fidelity work starts |
| Avatar draws | **Done** (FRIENDS-1): the x-ray silhouettes were the part that scaled with players, 24 uninstanced draws each (49 rim + 49 fill of 163 in `lobby4`). They now have instanced pipelines, like the toon, outline, unlit and ground-sprite parts: one draw per mesh and kind across all avatars and NPCs, with colour per instance. `lobby4` on the Mac: 162.7 -> 96.7 draws (max 102), same 659 instances | Player count no longer scales the draw count; each distinct part mesh on screen still costs one draw per kind |
| Static world draws | **Deferred debt:** 68 in the yard scene including UI, target 25 or fewer | Tree canopies and text remain separate; the 150 and 100 limits are met. Revisit with chunked static geometry when the map changes |
| Unattributed iPad render time | **Deferred:** about 0.7 ms of `render` outside the sort and encode scopes | Break it down the next time a perf pass runs |
| Menu avatar preview | Still an offscreen render target | Menu only, no gameplay cost; M8 replaces it with a viewport in the main pass |
| NPC preview head draws | **Deferred** (accepted at M8 sign-off): +14.3 draws against an estimate of 3 or fewer, no measurable GPU cost | The head is drawn part by part in its own inset. If draw count ever becomes the constraint: merge the parts into one mesh per look |
| Light bake | Skipped by decision | Desktop only: 136 ms for 31,744 vertices, 1 ms cached, streaky floors because the meshes have too few vertices. `MG_BAKE` stays off |

## Fidelity headroom

| Candidate | Cost | Basis |
|---|---|---|
| MSAA 4x instead of 2x | about 0.1 ms effective GPU on the iPad, +42 MB | Measured |
| Baked lighting | Not pursued | User decision |
| Outlines, particles, more props | Not measured | No implementation to measure yet |

The iPad has about 6.5 ms of effective GPU budget and about 1 ms of render submission budget left at the 200-NPC scene.

## Open risks

- `stress200` covers the current hospital only; a larger map needs its own pass through the gate.
- The GPU ran at the Minimum clock in every trace, so effective ms is the meaningful number, and a heavier scene will raise the clock.
- The fence-based "GPU approx" in the JSON includes queueing; use the Instruments figures.
- Thermal downgrade was exercised with `--force-thermal 2` on desktop. No device reached the "serious" state in these runs.

## Not verifiable from the command line

These need the Xcode GUI on a Mac with the iPad connected. Menu names can differ slightly between Xcode versions; I could not run these steps myself.

**Shader profile (toon fragment shader)**
1. Run `MG_IOS_TEAM_ID=<team id> tools/run_ios.sh --open`, pick the iPad as the destination and press Run.
2. Walk to a busy view, then in the debug bar click the Metal icon (Capture GPU Workload) and capture one frame.
3. When the capture opens, in the Debug navigator choose the Performance view and profile the capture.
4. Expand the main render pass, select an instanced draw using `toon_inst.frag`, then open the fragment shader from Bound Resources.
5. Read the per-line cost; check that there is no texture fetch and that the arithmetic is light.

**Overdraw (floor 2 corridor, roof faded)**
1. Run the app from Xcode with `--spawn -10.4,8.1,-3` added under Product > Scheme > Edit Scheme > Run > Arguments.
2. Capture one GPU frame as above.
3. In the capture, select the main render pass and open its colour attachment in the viewer.
4. Look for the overdraw display in the attachment viewer's options or the performance counters for the pass, and check that faded walls are the only layered areas.

## M7 (NPCs), iPad Air 4, commit `6451a0b`

Perf gate passed on all five scenarios (raw files in `docs/perf/m7/`). The 47-NPC roster ran 5 minutes: 0 dropped frames, 0 heap allocations, thermal nominal.

| Metric | NPC roster | Budget |
|---|---|---|
| NPC sim on the main thread | 0.08 ms | 2.0 |
| CPU tick + game frame | 0.28 ms | 5.0 |
| Render submission | 1.04 ms | 2.0 |
| GPU frame / busy / effective (Minimum clock) | 4.75 ms / 29 % / 1.36 ms | 8.0 effective |
| Draw calls | 85 | 150 |
| Memory | 154 MB | 300 |

Real NPC sim, 1 thread against 6, iPad (`--autotest npcs-bench`, positions identical): 44 NPCs 0.008 / 0.013 ms per tick, 200 NPCs 0.021 / 0.027 ms. Dispatch costs more than it saves at these sizes, so the job system gives no speed-up yet.

Decision: the dispatch stays, with a threshold (`JOBS_MIN_NPCS` 100): NPC steering and culling run on one thread below 100 NPCs and on the job system above it.

Every NPC updates every tick (viewer-based update rates would make clients differ): 0.08 ms for the 47-NPC roster on the iPad.

Two late-frame allocation sources were removed for this gate: uploads now use a fixed ring of preallocated buffers, and SDL's GPU pools are filled at startup. The gate now also fails on dropped frames. `docs/perf/after/` gained the `npcs` scenario as its reference.

## M8 (interaction, layout), iPad Air 4, commit `aff8719`

Perf gate passed on all seven scenarios, including the new `lift_panel` and `interact_npc` (raw files in `docs/perf/after/`, panels-off runs in `docs/perf/m8_panels_off/`). Dominant GPU clock state: Minimum in every run.

| Panel cost | Panels on | Panels off | Delta | Estimate | 2x limit |
|---|---|---|---|---|---|
| Lift panel, GPU effective | 1.99 ms (5.74 ms, 35 %) | 1.73 ms (5.36 ms, 32 %) | +0.26 ms | +0.15-0.25 | 0.50 |
| Lift panel, draws (Mac, counter patched for the run) | 80 | 75 | +5 | <= 4 | 8 |
| NPC preview head, GPU effective | 1.41 ms (4.84 ms, 29 %) | 1.42 ms (4.86 ms, 29 %) | none measurable | +0.08-0.12 | 0.24 |
| NPC preview head, draws | 100.3 | 86.0 | +14.3 | <= 3 | 6, **exceeded** |
| Scan + registry, CPU (p95 frame) | 0.05 ms | - | - | <= 0.03 per scan | 0.06 |

The head is drawn part by part in its own inset (head, hair, outlines, eyes, nose, mouth), not instanced, hence the draws. Lift-panel draw figures up to and including `aff8719` (the iPad's 67) exclude the panel: `GfxStats.draws` did not count insets drawn before the world. Fixed in `3f29211`. True number on the iPad, rerun at `1e0b900` (after the lift panel and frosted pause button changes): 76 draws, 4 passes (the frosted pause disc makes the two blur passes run on every play frame), 0 dropped, GPU 5.32 ms, 32 % busy, 1.70 ms effective, clock Minimum 99 %.

Font atlas: 512x512 -> 1024x512, +256 KB; no separate texture budget, resident memory 151 MB against 300.

## Reproducing

```
CONFIG=Release MG_IOS_TEAM_ID=<team id> tools/run_ios.sh
tools/perf_run.sh docs/perf/run1 --gpu --gate
```

## FRIENDS-1 (desktop sanity only)

`lobby4` (3 fake guests, join panel up, 200 dummies) on the Mac, new camera, labelled sanity, not the gate: 163 draws average, 168 max before the x-ray instancing, 96.7 average, 102 max after (budget 150, an iPad budget; target 110), 4 passes, 0 heap allocations in the frame, 1 dropped frame of 1800, 200 MB resident. The iPad Air 4 was unavailable, so neither the gate nor the generation-frame GPU cost is measured.

Steady state the four passes are main, two blur (the frosted pause disc, since `e99c268`) and UI; head icons add none. Accepted at FRIENDS-1 review as the known cost of the frosted button: the `lobby4` gate asserts no new passes against that pre-FRIENDS baseline of 4. If the blur ever has to go, it goes, but not in this milestone. Heads are generated at most 2 per frame in one extra offscreen pass, only while the queue is not empty.

The three-instance friends test generated 6 heads into 6 cells, longest placeholder 1 frame.

## Rebaseline after the fixed camera (e034fd2, iPad Air 4)

`docs/perf/after/` now holds this run: all scenarios at the current camera (FOV 40, distance 19, angle 55), with the frosted pause disc. Older files were from `aff8719` (`1e0b900` for `lift_panel`). Every GPU run's dominant clock state was Minimum.

| Scenario | Draws old -> new | Passes | Render ms | GPU effective ms | Memory MB |
|---|---|---|---|---|---|
| `yard` | 68.0 -> 64.0 | 2 -> 4 | 1.06 -> 1.21 | 1.37 -> 1.72 | 152 -> 156 |
| `lobby` | 67.5 -> 63.5 | 2 -> 4 | 1.07 -> 1.31 | 1.16 -> 1.63 | 151 -> 152 |
| `corridor_f2` | 87.1 -> 83.1 | 2 -> 4 | 1.11 -> 1.36 | 1.30 -> 1.81 | 151 -> 153 |
| `stress200` | 89.2 -> 86.6 | 2 -> 4 | 1.09 -> 1.25 | 1.37 -> 2.01 | 161 -> 168 |
| `npcs` | 89.1 -> 89.2 | 2 -> 4 | 1.09 -> 1.38 | 1.29 -> 1.90 | 155 -> 154 |
| `lift_panel` | 76.0 -> 72.0 | 4 -> 4 | 1.40 -> 1.37 | 1.70 -> 1.71 | 153 -> 154 |
| `interact_npc` | 100.3 -> 93.3 | 2 -> 4 | 1.73 -> 1.38 | 1.41 -> 1.93 | 154 -> 154 |
| `lobby4` | new: 95.4 | 4 | 1.64 | 1.91 | 162 |

Against the old baseline, GPU effective and render time rise 10 to 55 % in the scenarios that gained the two blur passes and the wider view; `lift_panel` already had both blur passes, and moved within noise. All of it is inside the budgets (GPU 8 ms, render 2 ms). Draws fall everywhere, from the instanced x-ray.

Gate against the new baseline: passed. `corridor_f2` dropped 1 frame on its first run and 0 on its one rerun, which is the file kept here.


## Full gate at 22cd389 (iPad Air 4)

`docs/perf/after/` now holds this run, all eight scenarios from one commit, gated against the e034fd2 baseline. Gate: passed. `stress200` failed on its first run (3 heap allocations, 1 dropped frame) and was clean on its one rerun, which is the file kept. GPU clock Minimum for 97-100 % of every run.

| Scenario | Draws | Render ms | GPU frame ms / busy % / effective ms | Memory MB |
|---|---|---|---|---|
| `yard` | 64.0 | 1.21 | 5.31 / 32 / 1.70 | 157 |
| `lobby` | 63.5 | 1.20 | 5.19 / 31 / 1.62 | 158 |
| `corridor_f2` | 83.1 | 1.28 | 5.49 / 33 / 1.82 | 162 |
| `stress200` | 86.6 | 1.33 | 5.75 / 35 / 1.99 | 169 |
| `npcs` | 89.8 | 1.34 | 5.62 / 34 / 1.90 | 159 |
| `lift_panel` | 72.0 | 1.30 | 5.30 / 32 / 1.69 | 160 |
| `interact_npc` | 97.3 | 1.39 | 5.81 / 35 / 2.03 | 162 |
| `lobby4` | 95.4 | 1.30 | 5.69 / 34 / 1.95 | 161 |

## Menu scenario (87b762a, iPad Air 4)

`--autotest perf menu`: hospital, roaming NPCs (48 alive), hero, same budgets as gameplay. Gate: passed on its one rerun; the first run had 1 dropped frame and 4 small allocations (24-56 bytes) inside `render`, the same signature as the first `stress200` run. 76.2 draws (110 max), render 0.99 ms, GPU 6.14 ms / 37 % busy / 2.27 ms effective at Minimum clock (98 %), 172 MB.

## FRIENDS-2 full gate at 3681449 (iPad Air 4)

`docs/perf/after/` now holds this run, nine scenarios from one commit, gated against 22cd389. Gate: passed. `yard`, `lobby`, `corridor_f2` (render or GPU over 10 %) and `stress200` (1 dropped frame) failed on the first run and were within budget on their one rerun, which are the files kept. GPU clock Minimum for 96-99 % of every run.

| Scenario | Draws | Render ms | GPU frame ms / busy % / effective ms | Memory MB |
|---|---|---|---|---|
| `yard` | 64.0 | 1.28 | 5.19 / 31 / 1.62 | 159 |
| `lobby` | 63.5 | 1.27 | 5.17 / 31 / 1.61 | 158 |
| `corridor_f2` | 83.1 | 1.32 | 5.48 / 33 / 1.81 | 158 |
| `stress200` | 86.6 | 1.37 | 5.74 / 35 / 1.99 | 169 |
| `npcs` | 89.1 | 1.43 | 5.63 / 34 / 1.91 | 160 |
| `lift_panel` | 72.0 | 1.36 | 5.35 / 32 / 1.73 | 158 |
| `interact_npc` | 94.8 | 1.41 | 5.87 / 35 / 2.07 | 159 |
| `lobby4` | 95.4 | 1.40 | 5.85 / 35 / 2.06 | 163 |
| `menu` (4 players) | 76.5 | 0.85 | 6.19 / 37 / 2.30 | 169 |

Four avatars in the menu against one (87b762a): +0.3 draws, +31.7 k triangles (223.7 k -> 255.4 k), +57 instances, GPU effective 2.27 -> 2.30 ms.

## WORLD-1: the island map (2026-11-10)

Design: one main pass + UI. World draws: sea 1, island 1, road 1, props 7 (one instanced draw per group), hospitals 1, rings 1, ambulance body, wheels, roof lights, contact disc = 16; +1 x-ray of the body on frames where a placeholder cube hides the parked ambulance (5 of 9 stops lie north of their hospital). The renderer's draw counter also counts the HUD batch, so the numbers below read 17 / 18. Prop transforms are built once at load, sorted into an 8 x 5 grid, and cells are culled against the frustum.

iPad Air 4, `--autotest perf` 60 s, commit 9a9cbb3 (CPU and memory only; the GPU traces were cut off by the device link, see the open issue below). `worldmap_travel` from that commit never drove (it opened in the overview); fixed in 5811809, not yet remeasured on the iPad.

| Scenario | Draws (avg / max) | Triangles | Heap allocs | Dropped | Memory MB | Tick + frame ms | Render ms (encode) |
|---|---|---|---|---|---|---|---|
| worldmap (overview) | 17 / 17 | 28.5 k | 0 | 0 | 153 | 0.002 + 0.183 | 0.71 (0.085) |
| island (parked, card up) | 18 / 18 | 15.8 k | 0 | 0 | 173 | 0.003 + 0.138 | 1.08 (0.088) |

Estimates against measurements: tick 0.05 ms -> 0.002-0.003; render submit 0.15 ms -> encode 0.085-0.088 (the `render` scope, 0.71-1.08, also holds the drawable wait; `yard` reads 1.28 on the same scope); memory below the hospital scene: 153 MB overview against 159 (`yard`), but 173 parked with the card up (not yet explained: the same meshes, plus the card's blur panel). GPU (estimate <= 1.5 ms overview, <= 1.0 ms zoomed): not measured.

Mac (desktop, labelled sanity, not the gate), 30 s, after the fix: worldmap 17 draws / 28.5 k tris, worldmap_travel 16.9 avg (18 max) / 18.5 k tris (grid culling), island 18 / 19.1 k; 0 heap allocations; tick 0.001 ms; render encode 0.016-0.017 ms; 1-2 dropped frames per run, as `yard` (1) on the same machine. Scene switch: map load 1.6-2.1 ms on first open, open 1.8-2.3 ms.

## Open issues

- **First-run allocations and first-run regressions (trace task).** Since FRIENDS-2 the first run of a scenario sometimes has a few small allocations (24-56 bytes) inside `render` and a dropped frame, or reads over 10 % worse on render / GPU, and is clean on its one rerun (`stress200`, `menu`, then `yard`, `lobby`, `corridor_f2` at 3681449). Treated as one bug, for the allocation / Instruments trace task; not closed by the reruns.
- **Device link drops during the gate (2026-11-10).** The iPad (linked over the network) dropped `devicectl --console` sessions about 30 s in, three times; `tools/perf_run.sh --no-console` launches detached and still checks run id, mtime and commit. The full gate for WORLD-1 has not run: device testing now needs the user's permission per run.

## WORLD-1 / WORLD-2 sign-off (1be3e40)

iPad Air 4, GPU at Minimum clock 91-99 %. Zoomed-in map (`island`, `worldmap_travel`): 6.44 ms frame, 39 % busy, **2.50 ms effective**, against a WORLD-1 estimate of 1.0 ms. Decision: **2.5 ms is the new estimate** for the zoomed-in map (the overview stays 1.5, measured 1.05; the voyage 2.5, measured 0.89). The cost is full-screen toon-lit terrain and props at 2x MSAA where the overview is mostly unlit sea; the hospital card's blur panel was 0.2 ms of it and is gone (flat card, 1 pass, 155 MB).

Deferred: cheaper terrain shading when zoomed in on an island, for when the real hospital models land.

## UI-1 "Jelly Pop" audit (eaba2e5, 2026-11-10)

Data in `docs/perf/ui1/`. The iPad went offline to Instruments after 6 GPU traces (xctrace recorded nothing twice on `interact_npc`), so by the standing rule the remaining scenarios ran on this Mac (Release desktop build, `mac_*.json`): CPU, UI and pass numbers only, no GPU.

iPad Air 4, before (`docs/perf/after`, frosted panels) -> after; GPU = frame ms / busy % / effective ms, all at Minimum clock:

| Scenario | Passes max | render ms | UI update / submit ms | GPU before -> after |
|---|---|---|---|---|
| yard | 4 -> 2 | 1.28 -> 1.02 | 0.009 / 0.102 | 5.19/31/1.62 -> 4.78/29/1.38 |
| lobby | 4 -> 2 | 1.27 -> 1.09 | 0.010 / 0.117 | 5.17/31/1.61 -> 4.64/28/1.30 |
| corridor_f2 | 4 -> 2 | 1.32 -> 1.10 | 0.010 / 0.116 | 5.48/33/1.81 -> 4.94/30/1.46 |
| stress200 | 4 -> 2 | 1.37 -> 1.10 | 0.009 / 0.105 | 5.74/35/1.99 -> 5.20/31/1.63 |
| npcs | 4 -> 2 | 1.43 -> 1.11 | 0.009 / 0.105 | 5.63/34/1.91 -> 5.29/32/1.68 |
| lift_panel | 4 -> 2 | 1.36 -> 1.15 | 0.010 / 0.122 | 5.35/32/1.73 -> 4.83/29/1.40 |
| interact_npc (no GPU trace) | 4 -> 2 | 1.41 -> 1.15 | 0.009 / 0.106 | - |

The 2 remaining passes on the iPad are the world's own (main pass + the upscaling UI pass at render scale < 1); the 2 blur passes are gone. 1 UI draw everywhere, 0 heap allocations, no dropped frames, thermal nominal. CPU tick within noise of the reference (+0.01..0.03 ms, under the 0.1 ms noise floor).

Mac (Release): `interact_npc`, `lobby4`, `menu`, `ui_juice`, `worldmap`, `worldmap_travel`, `island`, `worldmap_voyage`: 1 pass, 1 UI draw, 0 allocations, UI update <= 0.005 ms, UI submit 0.029-0.033 ms; `ui_juice` peaks at ~290 UI instances. `lobby4` was 4 passes on the iPad before UI-1 (blur); on the Mac it is 1. Not measured on the iPad this round: `lobby4`, `menu`, `ui_juice`, the four map scenarios, and therefore the "full menu of cards <= 0.4 ms GPU" estimate and the `worldmap_travel` pass re-check. `docs/perf/after` is not replaced (the gate was not complete).

UI-1 sign-off (1d4a6ed): accepted with the iPad GPU numbers for `menu`, `lobby4`, `ui_juice` and the four map scenarios still open; they are measured in the UI-1b gate (one device session), and `docs/perf/after/` is replaced only when all 15 scenarios are in. The drop-out: after 6 traces Instruments listed the iPad under "Devices Offline" while devicectl still saw it, and xctrace hung until its 120 s kill. `tools/perf_run.sh` now waits up to 10 min for the device (devicectl, and Instruments when `--gpu`), retries a trace 3 times and a failed launch once, and `--resume` continues a run from the first scenario OUTDIR does not yet have from this build; it no longer falls back to the Mac.

## UI-1b stick audit (194b6e9, 2026-11-10, iPad Air 4, CPU side)

Instruments listed the iPad as offline for the whole session (devicectl still reached it), so this round has every scenario's JSON but no GPU trace; `docs/perf/after/` is not replaced. Data in `docs/perf/ui1b/` (Mac Release copies of yard / lobby / interact_npc / lobby4 as `mac_*`).

| Scenario | tick ms (ref -> now) | render ms | passes | UI update / submit ms | UI instances |
|---|---|---|---|---|---|
| yard | 0.175 -> 0.183 | 1.28 -> 0.97 | 4 -> 2 | 0.009 / 0.096 | 73 |
| lobby | 0.180 -> 0.202 | 1.27 -> 1.00 | 4 -> 2 | 0.010 / 0.101 | 73 |
| corridor_f2 | 0.168 -> 0.191 | 1.32 -> 1.04 | 4 -> 2 | 0.009 / 0.098 | 73 |
| stress200 | 0.471 -> 0.479 | 1.37 -> 1.09 | 4 -> 2 | 0.009 / 0.098 | 73 |
| npcs | 0.296 -> 0.320 | 1.43 -> 1.12 | 4 -> 2 | 0.010 / 0.102 | 76 |
| lift_panel | 0.190 -> 0.192 | 1.36 -> 1.09 | 4 -> 2 | 0.009 / 0.112 | 67 |
| interact_npc | 0.311 -> 0.297 | 1.41 -> 1.12 | 4 -> 2 | 0.009 / 0.100 | 96 |
| lobby4 | 0.498 -> 0.514 | 1.40 -> 1.10 | 4 -> 2 | 0.010 / 0.103 | 205 |
| menu | 0.287 -> 0.310 | 0.85 -> 0.80 | 1 -> 1 | 0.008 / 0.095 | 245 |
| ui_juice | 0.338 (new) | 0.81 | 1 | 0.015 / 0.099 | 298 |
| worldmap | 0.220 (new ref) | 0.65 | 1 | 0.008 / 0.095 | 322 |
| worldmap_travel | 0.153 -> 0.191 | 1.18 -> 0.65 | **4 -> 1** | 0.010 / 0.105 | 142 |
| island | 0.148 -> 0.184 | 0.59 -> 0.63 | 1 -> 1 | 0.009 / 0.100 | 157 |
| worldmap_voyage | 0.115 -> 0.121 | 0.61 -> 0.65 | 1 -> 1 | 0.009 / 0.102 | 24 |

References: `docs/perf/after` (pre-UI-1) for the hospital and menu scenarios, `docs/perf/world2` for the map ones. Every scenario: 1 UI draw, 0 heap allocations, no dropped frames, thermal nominal; tick changes are all under the 0.1 ms noise floor. The play scenarios carry the new stick (6 instances idle; the old hint was 2): +3 UI instances, no draw or pass. The `worldmap_travel` "4 passes" from WORLD-1 is gone for good (1 pass). Still open, needs Instruments: GPU for all 14 + `ui_juice`, then `docs/perf/after/` is replaced (`tools/perf_run.sh docs/perf/ui1b --gpu --resume --gate` continues this run).

Instruments drop-out, diagnosed (2026-11-10): the iPad was on Wi-Fi only (`devicectl device info details`: `transportType: localNetwork`, pairing `paired`, Developer Mode `enabled`). devicectl works through CoreDevice's network tunnel; xctrace needs the usbmux link, which over Wi-Fi depends on Bonjour and drops whenever the iPad sleeps, hence "Devices Offline" and hung traces. Fix: gates run with the iPad on USB, unlocked, trusted, Auto-Lock Never. `perf_run.sh --gpu` now records a 60 s smoke trace first and refuses to start the gate if it records nothing, naming the link.

Instruments drop-out, part 2: on USB the iPad still fell out of `xctrace list devices` whenever its CoreDevice tunnel went idle, and came back on any devicectl call. `perf_run.sh` now wakes the device (`devicectl device info details`) before every Instruments check, the smoke trace and each trace.

## TILT-1 + A1 (6686bab, Mac only)

No device runs until after LIB-1 (standing rule). Release desktop build, `--autotest perf` on all 14 scenarios with `--tilt-sine` (1 Hz, full strength); data in `docs/perf/tilt1/mac_*.json`. Every scenario: 1 pass, 1 UI draw, 0 heap allocations, draws unchanged against the UI-1 / UI-1b Mac runs (e.g. yard 68.0, lobby4 98.2, menu 74.5, ui_juice 74.5, worldmap_voyage 23.0). CPU tick within the Mac's run-to-run noise (under the 0.1 ms floor). The tilt is one uniform and a float4 per UI instance (96 -> 112 bytes; ~300 instances = +5 KB per frame upload). iPad CPU + GPU: in the combined gate after LIB-1.

## LIB-1 + A1 (Mac only; the iPad gate comes after LIB-1)

Mac Release, `--autotest perf SCENARIO 60`, `docs/perf/lib1a1/` (A1) and `docs/perf/lib1/` (the library steps). Per step against the
step before; 1 pass and 0 heap allocations everywhere unless noted.

| Step | Scenario | Draws avg (max) | Triangles avg | Frame / render ms avg | Note |
|---|---|---|---|---|---|
| A1 | stress200 | 89.5 -> 71.7 | 372 k -> 87 k | | the new avatar (TILT-1 numbers before) |
| A1 | lobby4 | 98.2 -> 75.3 | 362 k -> 80 k | | |
| A1 | interact_npc | 97 -> 74.9 | 212 k -> 59 k | | |
| 1-2 | menu | 74.5 -> 17.3 (19) | 257 k -> 10 k | 0.111 -> 0.079 / 0.235 -> 0.19 | the hospital backdrop -> the library, the hero studying |
| 3 | menu | 17.3 -> 23.0 (24) | 10 k -> 14.7 k | 0.079 -> 0.085 / 0.19 -> 0.195 | the 4-player demo lobby seated |
| 4 | menu | 23.0 -> 23.6 (24) | 14.7 k -> 22.6 k | 0.085 -> 0.084 / 0.195 -> 0.18 | six NPCs, the cart |
| 5 | menu | 23.6 (24) | 22.6 k | 0.084 -> 0.094 / 0.18 -> 0.205 | the Locker only (run-to-run noise) |
| 6 | menu | 23.6 (24) | 22.6 k | 0.098 / 0.208 | resident 180 -> 191 MB: the map preloaded |

**Memory baselines for later comparisons:** step 5 = **180 MB**, step 6 (the map preloaded) = **191 MB** (Mac menu, resident max).
The menu's LIB-1 allowance is +16 MB over step 5. An earlier report quoted 191 MB as step 5: that record was measured at step 6.

| 6 | menu_to_map | 26.1 (40) | 27.8 k (36.9 k) | 0.058 / 0.175 | new: the move looped; the estimate was <= 45 draws |

Estimates (LIB-1 section 7): menu <= 22 draws / ~18 k triangles: 23.6 / 22.6 k (1.07x / 1.25x, under the 2x stop rule); the move
<= 45 draws: 40 at most. `menu_to_map` first showed 7 allocations in 2 frames (SDL's per-command-buffer uniform-buffer lists growing
on the move's busier frames): the prewarm now fills 6 uniform blocks per pooled command buffer; one `prefs.json` write per Back
(cJSON) is exempt like the map's one load. GPU numbers come from the combined iPad gate.

### The square hall (2 x 2 bays, mg-library 10), Mac Release, `docs/perf/lib1/`

| Scenario | Draws max | Triangles max | Passes | Allocations | Resident MB | Note |
|---|---|---|---|---|---|---|
| menu (before this round, 35be897) | 21 | 61.6 k | 1 | 0 | 211 | 26 avatars at LOD0, pools 12 + 6 MB |
| menu (now) | 27 | 47.4 k | 1 | 0 | 194 | NPC LOD, pools 5 + 2 MB, meshes welded |
| menu `--lib-no-nav` (A/B) | 27 | 47.4 k | 1 | 0 | 198 | the straight-leg runner |
| yard | 51 | 34.8 k | 1 | 0 | 199 | vertex pool 3.5 / 5 MB, index 0.9 / 2 MB |
| lobby4 | 69 | 75.8 k | 1 | 0 | 199 | vertex pool 3.5 / 5 MB |
| worldmap | 23 | 63.0 k | 1 | 0 | 185 | |

- **Estimate, rooms (replaces the 18 k of LIB-1 §7):** the four bays are **33.3 k triangles** (room_0..3: 7.5 / 9.7 / 7.7 / 8.5 k),
  all drawn in the menu because the back rooms show through the open doors. Props ~3.3 k (10 chairs, books, mugs, pencils, 4 doors,
  the cart).
- **Per-part triangles** are measured since this round: the perf JSON has `tris_rooms`, `tris_props`, `tris_avatars`, `tris_other`
  (`gfx_set_category` around a group of draws; the UI counts as other).
- **Measured per part (menu, Mac, this round):** rooms 33.3 k, props 3.0 k, avatars 9.9 k average / 11.4 k max, other (UI) 0.3 k;
  draws 27 average / 39 max at rest; `menu_to_map` 28 / 52, 0 dropped frames.
- **Estimate, the Maps / Back move (replaces the <= 45 of the first LIB-1 text): 52 draws**, limit 60 (as for travelling).
  The NPC LOD rule is by size on screen (full above 0.215 of the view's height, coarse below 0.195, a 0.25 s crossfade);
  during the Maps move the LODs come from the menu's resting camera.
- **Avatars:** LOD0 is ~950 triangles (the suit body 419, head 80, hair 90-120, eyes 2 x 48, hands 2 x 80; glasses / mask +108 / +117),
  LOD1 ~154. Rule: NPCs in another bay or under 60 pt tall on screen are LOD1, of the rest the 4 nearest the camera LOD0; my table
  always LOD0. Menu avatars: 25 k -> ~11 k (8 at LOD0, 21 at LOD1; measured total minus rooms and props). The LOD1 parts are their
  own instanced batches: draws 21 -> 27.
- **Memory against step 5 (180 MB; the 191 MB record is step 6):** 211 -> 194 MB = +14. The step-6 map preload is +11, the mesh pools
  (5 + 2 against 4 + 2 MB) +1, the perm arena (the hall's nav grid and its request queue, the larger MenuLibrary) +2. Every triangle soup
  is welded at upload (`mesh_upload_welded`: identical position + normal + colour): the hall 105,840 -> 82,006 vertices; the vertex
  pool peaks at 3.5 MB (yard, lobby4) of 5.
- **NPC pathing:** each NPC leg is an A* request on the hall grid through the request queue, serviced once a frame within 0.5 ms
  (one search always runs); the next leg is searched while the NPC waits at its post. Menu frame 0.081 ms (straight legs) -> 0.083 ms;
  `lib.nav` 0.001 ms average, 0.14 ms max (one search); 0 heap allocations (lib_nav uses its own scratch).

### Renderer changes of aedb310 (the seamless Maps move), checked outside the move (Mac Release, `docs/perf/lib1_render/`)

- **Dual light:** a branch on a uniform. The second light set is evaluated only when `u_sun2_dir.w` (the mix) is above 0, and
  the game sets it above 0 only on the Maps / Back move's frames; at rest in the menu, on the map and in every hospital scene the
  mix is 0 and every fragment takes the one-set path (the same for the whole draw: no divergence). Mac, cfbdcc1 -> aedb310 (render
  submit = encode + sort): stress200 0.049-0.050 -> 0.050 ms, 62.0 draws, 67.1 k tris both; lobby4 0.064 -> 0.065, 66.2 draws,
  65.1 k; yard 0.043 -> 0.045, 51 draws, 34.8 k; menu 0.022-0.028 -> 0.022-0.024. The Mac reports no GPU time (`gpu_main_ms_approx`
  0). If the iPad trace shows a cost at rest from the shader's larger body, the second set goes to a pipeline variant used only
  on the move.
- **Blended toon pipeline:** `gfx_allow_fade` is on only around the move's library draw; a GFX_MAT_DEPTH / GFX_MAT_TOON_FADE draw
  outside it is dropped, logged once and counted (`fade_violations`); `tools/perf_gate.py` fails on any violation and on any fade
  draw outside `menu_to_map`. Checked: a fade injected at rest reads 1 violation per frame; `menu_to_map` 2 fade draws, 0 violations.
- **Sort key** (bucket 3 bits, front-to-back depth in 1/128 m): yard, corridor and the lobby captured on both builds: the only
  differing pixels are the player's idle pose (x 820-867, y 362-412), as between two runs of one build; draws and triangles equal.
- **Memory:** the menu's footprint peak was 192-205 MB run to run with our own memory identical; the spread was Metal keeping the
  mesh uploads' freed staging buffers as reclaimable driver memory (IOAccelerator, 5-12 MB) for a while after the map's preload.
  `gfx_settle_uploads` waits for the GPU once after each batch of uploads (start, the map preload, the hospital: 0.2-0.8 ms on the
  Mac). Five menu runs: 193 / 191 / 194 / 193 / 193 MB (min 191, median 193, max 194; the gate is 196). `perf: footprint` lines log
  any new peak during a run with its time.
- **LOD crossfade at rest:** with one NPC held mid-crossfade for the whole run (`--lib-lod-fade N`) the peak was 44-48 draws: the 39
  had not included it. A crossfade now dithers only body, head and hair; eyes, hands and accessories swap at its middle
  (`AvatarPose.small_own`). Held crossfade: 36 max; normal runs 26.8 avg / 33 max.
- **Dropped frames:** single dropped frames show up in some 60 s Mac runs on both builds (cfbdcc1 stress200: 2), with no allocation;
  not explained yet.

### Notes for the combined iPad gate

- The dual-light path and the blended shell need a GPU trace of the Maps move on the iPad (Metal System Trace): the Mac cannot show
  tile-GPU cost. Also check the menu at rest for any cost of the larger shader body (the second set is behind a uniform branch).

### Library menu triangles (wall-aware bays, 93d2a65; user decision 2026-11-12)

- Mac, `--log-frame-stats`, menu at rest, 60 s: triangles 61.1 k -> 57.9 k average (rooms 33.3 -> 31.1 k, props 19.1 -> 18.3 k,
  avatars 8.5 -> 8.3 k), draws 33.2 -> 33.0 (max 35.9). The back bays are hidden 19 % / 9 % of the time (the front doors are open
  the rest). Pixel-identical (`--lib-still --lib-wall-ab`: 0 changed pixels on every bay and window).
- Accepted: estimate **58 k** at rest (was 26 k, written for the first small room before the hall became four rooms), stop line
  **75 k** (`tools/perf_gate.py`). No doorway culling of people and particles: 57.9 k is below `stress200` (87 k+, every iPad budget
  passed); draws, allocations and memory are within budget.
- The combined iPad gate's GPU traces include the **menu at rest** and the **Maps move** (`menu_to_map`); they decide whether the
  menu needs more work.



## LIB-1 step 11: Mac perf audit (2026-11-13)

Release desktop build, `tools/perf_mac.sh` (the binary's embedded commit = HEAD on a clean tree; the game window in front; every run
below is valid: on power, paced by the 60 Hz screen, the window covered or inactive 0.00 % of the frames). Data in
`docs/perf/lib1_step11/`. The iPad numbers and GPU traces are deferred to the combined gate (docs/PERF_RULES.md lists what it traces).

| Run | Commit | Hz / fps | Covered / inactive | Draws avg / max | Triangles avg / max | Frame / render ms | Memory | Allocs | Dropped (late) |
|---|---|---|---|---|---|---|---|---|---|
| menu_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.198 / 0.210 | 202 MB | 0 | 0 (0) |
| menu_2 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.192 / 0.209 | 202 MB | 0 | 0 (0) |
| menu_3 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.195 / 0.207 | 201 MB | 0 | 0 (0) |
| menu_4 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.194 / 0.206 | 203 MB | 0 | 0 (0) |
| menu_5 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.193 / 0.210 | 201 MB | 0 | 0 (0) |
| menu_final_1 | d4e0c3a | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.202 / 0.207 | 201 MB | 0 | 0 (0) |
| menu_final_2 | d4e0c3a | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.7k / 56.7k | 0.200 / 0.208 | 203 MB | 0 | 0 (0) |
| menu_to_map_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 32.5 / 56 | 58.7k / 84.8k | 0.127 / 0.195 | 205 MB | 0 | 0 (0) |
| menu_to_map_final | d4e0c3a | 60 / 60.0 | 0.00 % / 0.00 % | 32.5 / 56 | 58.7k / 84.8k | 0.131 / 0.189 | 205 MB | 0 | 0 (0) |
| menu_travel_1 | d4e0c3a | 60 / 60.0 | 0.00 % / 0.00 % | 35.8 / 43 | 59.6k / 70.4k | 0.214 / 0.220 | 203 MB | 0 | 0 (0) |
| menu_travel_2 | d4e0c3a | 60 / 60.0 | 0.00 % / 0.00 % | 35.8 / 43 | 59.6k / 70.4k | 0.208 / 0.215 | 204 MB | 0 | 0 (0) |
| ui_juice_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 36.1 / 41 | 54.9k / 56.8k | 0.200 / 0.207 | 202 MB | 0 | 0 (0) |
| yard_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 51.0 / 51 | 34.8k / 34.8k | 0.084 / 0.184 | 208 MB | 0 | 0 (0) |
| lobby_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 50.5 / 51 | 34.2k / 34.2k | 0.089 / 0.187 | 208 MB | 0 | 0 (0) |
| corridor_f2_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 70.1 / 73 | 44.0k / 44.0k | 0.093 / 0.206 | 208 MB | 0 | 0 (0) |
| stress200_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 62.0 / 83 | 67.1k / 112.2k | 0.169 / 0.233 | 208 MB | 0 | 0 (0) |
| npcs_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 65.1 / 83 | 48.0k / 63.4k | 0.114 / 0.211 | 211 MB | 0 | 1 (0) |
| lift_panel_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 59.0 / 59 | 34.7k / 34.7k | 0.091 / 0.199 | 208 MB | 0 | 0 (0) |
| interact_npc_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 63.8 / 65 | 50.1k / 52.1k | 0.112 / 0.218 | 208 MB | 0 | 0 (0) |
| lobby4_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 66.2 / 69 | 65.1k / 75.9k | 0.178 / 0.233 | 208 MB | 0 | 0 (0) |
| worldmap_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 23.0 / 23 | 63.0k / 63.0k | 0.143 / 0.219 | 199 MB | 0 | 0 (0) |
| worldmap_travel_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 22.9 / 24 | 53.0k / 55.2k | 0.115 / 0.186 | 198 MB | 0 | 0 (0) |
| island_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 24.0 / 24 | 53.5k / 53.5k | 0.116 / 0.186 | 198 MB | 0 | 0 (0) |
| worldmap_voyage_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 28.9 / 33 | 61.6k / 68.9k | 0.090 / 0.194 | 199 MB | 0 | 0 (0) |
| institute_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 49.1 / 63 | 59.3k / 78.9k | 0.111 / 0.212 | 232 MB | 0 | 0 (0) |
| book_1 | 416cbf6 | 60 / 60.0 | 0.00 % / 0.00 % | 59.5 / 63 | 78.9k / 78.9k | 0.115 / 0.222 | 235 MB | 0 | 0 (0) |

Re-runs of `npcs` on d4e0c3a: 0 dropped both times (`mac_npcs_rerun_*.json`).

**Budgets on the Mac:** 0 heap allocations per frame in every run after the fix below; the menu at rest 36.1 / 41 draws (≤ 46) and
54.7 k triangles (≈ 58 k, stop line 75 k); the Maps move at most 56 draws (limit 60); the §8 trip (`menu_travel`) at most 43 draws
(limit 60; the staged worst case `--lib-worst-trip` 45). Dropped frames: 0 in every run but one: `npcs` (416cbf6) dropped one 32.7 ms
frame at 10.18 s in the hospital with nothing noted during it (the frame notes list every expensive one-off); it did not recur in two
re-runs. It stays on the tracked late-frame bug.

**Fixed in this step:** `menu_travel` counted 48 heap allocations (12 on each of 4 frames): `gfx_mesh_update` created a GPU transfer
buffer per call, and the dressing is re-built on each join and leave. One staging buffer is now reserved at load (0.14 MB):
0 allocations (d4e0c3a). Every dropped frame (over 1.5 refresh intervals) is now recorded with its notes in `dropped_frames`.

**The memory gap (5 MB over step 6's 197 MB): not a growth.** The 197 MB run was one the OS left unpaced (about 120 fps, the window
behind the editor: not comparable). Measured the same way today, `menu` on each step's merge commit (`memory_history/`):

| Commit | Step | Footprint |
|---|---|---|
| c578fc3 | 6 | 209 / 206 MB (two runs) |
| 6d4def4 | 7 | 202 MB |
| 58e0b41 | 8 | 201 MB |
| 45a8329 | 9 | 202 MB |
| 1ae4217 | 10 | 201 MB |
| 416cbf6 / d4e0c3a | 11 | 201-203 MB (seven runs) |

Per part, every tracked allocation is identical from step 6 to now: MSAA colour 10.0, depth 10.0, swapchain (estimate) 15.1, UI atlas
2.0, UI buffers 7.0, instance buffers 3.0, mesh pool used 3.6 + 0.7 (reserved 12 + 4), the permanent arena 13.4 used / 15.7 peak (96
reserved, untouched pages), NPC data 0.9 MB; the update staging buffer added 0.14 MB. The step 6 -> 7 drop of about 4-7 MB is outside
those parts; most likely (not measured part by part, the old JSON had no field for them) the removed book pack's resident figure
texture and its 2 MB figure staging buffer, which the JSON did not list then
(`memory_mb` now lists `figure`, `figure_staging` and `update_staging`), plus the driver's own share, which moves 1-3 MB run to run.

### Menu memory allowance and the dropped / late wording (user decision, 2026-11-13)

- The menu's 196 MB allowance (step 5's 180 + 16) is retired: measured before the hall became four rooms and in an unpaced run. The
  menu's Mac allowance is **210 MB**, measured paced, against 201-203 MB at step 11 (`tools/perf_mac.sh` fails above it). The global
  300 MB budget is unchanged and is the one the iPad gate enforces.
- **dropped** = a frame over 25 ms (1.5 refresh intervals at 60 Hz); **late** = a frame over 50 ms (a subset of dropped, the tracked
  late-frame bug). The perf JSON has both (`dropped`, `late`, each dropped frame with its notes in `dropped_frames`); every perf table
  from now on shows both columns. The JSON's interval buckets keep their old names: `interval_16ms` up to 25 ms, `interval_33ms`
  25-42 ms, `interval_50ms_plus` over 42 ms.
- The `npcs` dropped frame of step 11 (32.7 ms at 10.18 s, nothing noted, once in three runs) stays a failure of the tracked
  late-frame kind; the iPad gate runs `npcs` five times and reports dropped frames per run with their notes (if the iPad shows it too,
  it is the first thing to fix).


## Combined iPad gate (2026-11-13, iPad Air 4, build b9236bd)

`tools/perf_run.sh --gpu --gate` (Metal System Trace per scenario; the link dropped twice and the run resumed; the remainder ran from a
worktree at b9236bd after main moved on). Data in `docs/perf/gate_2026-11-13/`. The iOS build of b9236bd: 0 warnings.

| Scenario | fps | Draws avg / max | Triangles avg | CPU tick / render ms | GPU frame / busy / effective (clock) | Memory | Allocs | Dropped / late |
|---|---|---|---|---|---|---|---|---|
| yard | 60 | 48.0 / 48 | 34.7k | 0.052 / 0.986 | 5.41 ms / 33 % / **1.76 ms** (Minimum 99 %) | 180 MB | 0 | 0 / 0 |
| lobby | 60 | 47.5 / 48 | 34.0k | 0.053 / 0.958 | 5.21 ms / 31 % / **1.64 ms** (Minimum 99 %) | 178 MB | 0 | 0 / 0 |
| corridor_f2 | 60 | 67.1 / 69 | 43.8k | 0.043 / 1.063 | 5.31 ms / 32 % / **1.7 ms** (Minimum 99 %) | 181 MB | 0 | 0 / 0 |
| stress200 | 60 | 59.8 / 79 | 60.7k | 0.249 / 1.043 | 5.26 ms / 32 % / **1.67 ms** (Minimum 98 %) | 180 MB | 0 | 0 / 0 |
| npcs | 60 | 62.4 / 82 | 47.1k | 0.139 / 1.028 | 5.11 ms / 31 % / **1.57 ms** (Minimum 98 %) | 180 MB | 0 | 0 / 0 |
| lift_panel | 60 | 56.0 / 56 | 34.5k | 0.045 / 1.003 | 5.33 ms / 32 % / **1.71 ms** (Minimum 98 %) | 181 MB | 0 | 0 / 0 |
| interact_npc | 60 | 60.0 / 62 | 48.6k | 0.136 / 1.058 | 5.26 ms / 32 % / **1.67 ms** (Minimum 98 %) | 179 MB | 0 | 0 / 0 |
| lobby4 | 60 | 64.4 / 69 | 56.9k | 0.272 / 1.100 | 5.19 ms / 31 % / **1.62 ms** (Minimum 97 %) | 179 MB | 0 | 0 / 0 |
| menu | 60 | 37.0 / 41 | 56.8k | 0.010 / 0.680 | 7.44 ms / 45 % / **3.33 ms** (Minimum 81 %) | 180 MB | 0 | 0 / 0 |
| menu_to_map | 60 | 32.8 / 57 | 57.5k | 0.011 / 0.625 | 7.61 ms / 46 % / **3.48 ms** (Minimum 65 %) | 186 MB | 0 | 0 / 0 |
| menu_travel | 60 | 32.3 / 42 | 46.6k | 0.006 / 0.683 | 7.24 ms / 44 % / **3.16 ms** (Minimum 83 %) | 183 MB | 0 | 0 / 0 |
| ui_juice | 60 | 37.0 / 41 | 56.9k | 0.010 / 0.679 | 7.22 ms / 43 % / **3.14 ms** (Minimum 76 %) | 181 MB | 0 | 0 / 0 |
| worldmap | 60 | 23.0 / 23 | 63.0k | 0.013 / 0.688 | 5.18 ms / 31 % / **1.61 ms** (Minimum 99 %) | 182 MB | 0 | 0 / 0 |
| worldmap_travel | 60 | 22.7 / 24 | 51.1k | 0.013 / 0.606 | 7.48 ms / 45 % / **3.36 ms** (Minimum 93 %) | 182 MB | 0 | 0 / 0 |
| island | 60 | 24.0 / 24 | 50.2k | 0.007 / 0.643 | 7.48 ms / 45 % / **3.37 ms** (Minimum 91 %) | 181 MB | 0 | 0 / 0 |
| worldmap_voyage | 60 | 24.8 / 28 | 54.1k | 0.013 / 0.624 | 4.23 ms / 25 % / **1.08 ms** (Minimum 99 %) | 184 MB | 0 | 0 / 0 |
| institute | 60 | 46.1 / 60 | 59.1k | 0.099 / 1.028 | 2.73 ms / 16 % / **0.45 ms** (Minimum 98 %) | 205 MB | 0 | 0 / 0 |
| book | 60 | 56.5 / 60 | 78.7k | 0.082 / 1.073 | 3.55 ms / 21 % / **0.76 ms** (Minimum 99 %) | 203 MB | 0 | 0 / 0 |

- **`npcs` x5** (the Mac's step 11 frame): 60 fps, 0 dropped, 0 late in all five runs (`npcs_x5/`).
- **Same table on two devices:** iPad 8f5714ae, Mac 8f5714ae (bay 3, 2 studying): the same (`two_device/`). The NPC-hash line has
  nothing to compare in the institute (it declares 0 NPCs).
- **Gate verdict: FAILED** on: memory regressed +20 MB against the reference (3681449) in yard / lobby / corridor_f2 / npcs /
  lift_panel / interact_npc (158-160 -> 178-181 MB; the mesh pools grew 6 -> 16 MB and the UI atlas / buffers 3.4 MB; the 300 MB
  budget passes); `menu` CPU tick 0.29 -> 0.42 and GPU 2.30 -> 3.33 ms effective against the reference, which was the old menu with
  the hospital behind it (not like for like; LIB-1's own estimate is 1.7 ms at rest: 1.96x); `worldmap`, `worldmap_travel` and
  `island` at 23-24 draws against WORLD-1's 18, and `worldmap_travel` / `island` at 3.36 / 3.37 ms GPU effective against the 1.0
  estimate (first iPad measurement of these scenarios). Every scenario is inside the 4 ms GPU target and the 300 MB budget.
- `docs/perf/after/` is not replaced (the gate did not pass).

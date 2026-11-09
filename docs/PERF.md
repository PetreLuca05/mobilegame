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

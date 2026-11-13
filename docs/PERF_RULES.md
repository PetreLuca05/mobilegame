# Performance rules

Standing rules for every milestone. Numbers and history are in [PERF.md](PERF.md).

## The gate

- Every milestone ends with a perf step: `--autotest perf` on all scenarios (`lift_panel`, `interact_npc`, `yard`, `lobby`, `corridor_f2`,
  `stress200`) on the iPad Air 4 (and desktop for a labelled sanity check), compared with `docs/perf/after/`, reported
  PASS / FAIL per budget.
- A milestone is not signed off if any budget is exceeded or regresses by more than 10 %.
- Each prompt's done checklist gets: `[ ] --autotest perf within budget on iPad Air 4; docs/perf/ updated.`
- Never commit an optimisation without its measured delta in PERF.md.
- The Gastro institute (INST-LIGHT) is fully lit and textured: every institute pixel samples the pattern texture (one R8 2D array
  sample) and runs the reference light set, and its glass is blended. Measured on the Mac only so far (CPU `render` scope, draws);
  the combined iPad gate must include a GPU trace (Instruments, Metal System Trace) of the institute scenario.
- Words: **dropped** = a frame over 25 ms, **late** = over 50 ms (a subset); every perf table shows both columns.
- The combined iPad gate also runs: `npcs` five times, dropped frames per run with their notes (the step 11 Mac frame); the
  two-instance same-table hash with the iPad as one instance; an iOS build of the gate's commit checked for warnings.
- The library menu (LIB-1): the combined iPad gate must include GPU traces of the menu at rest (`menu`, ≈ 58 k triangles, stop
  line 75 k in `tools/perf_gate.py`) and of the Maps move (`menu_to_map`).

## Budgets (iPad Air 4, 60 fps, 200 NPCs)

| Area | Budget | Headroom goal |
|---|---|---|
| CPU main thread per tick (sim + input + net + UI) | 5.0 ms | 3.0 ms |
| of which NPC sim | 2.0 ms | job-parallel, 1.0 ms on the main thread |
| CPU render submission (cull, sort, encode) | 2.0 ms | 1.0 ms |
| GPU frame (main pass + UI) | 8.0 ms | 6.0 ms |
| Draw calls per frame | 150 | 100 |
| Heap allocations inside the frame loop | 0 | 0 |
| Resident memory | 300 MB | - |
| Dropped frames in a 5-minute stress run | 0 at steady state, thermal state at most "fair" | - |

GPU numbers follow the rule in CLAUDE.md: frame ms,
GPU-busy %, effective ms = ms x busy %, dominant clock state.

## Code

- New per-entity systems use hot/cold arrays (`npc_hot_t` at most one cache line), the spatial hash
  (`engine/world/spatial.c`), staggered update rates and the job system (`engine/core/jobs.c`). Exceptions need a
  line in PERF.md stating the cost.
- All neighbour queries go through the spatial hash. Never O(n^2).
- Parallel simulation reads last tick and writes this tick; anything that touches a queue or render state stays on
  the main thread, in index order. `--autotest determinism` must keep passing.
- NPC paths use `NAV_CLASS_NPC` (cell cap and path cap per tick, never the clock). The player uses `NAV_CLASS_PLAYER`.
- No doubles, no `pow`, no string work in the tick; squared distances where they are enough.
- Zero heap allocations in the frame loop. The HUD line turns red and `--autotest perf` reports any.

## Rendering

- New meshes go through `gfx_draw`: same mesh and pipeline become one instanced draw automatically. Static geometry
  is merged per storey and material at load; use `gfx_draw_range` for parts that fade separately.
- New shaders are checked for `clip` (opaque pipelines must have none), precision (`min16float` for colours, ramps
  and fades) and arithmetic cost.
- No new render passes without a measured justification. The frame is one main pass plus one UI pass; depth is never
  stored.
- Draws with fade between 0 and 1 use the dithered pipelines; fully faded ones (hide at 0.98 or more) are dropped.

# BOOK-ENGINE — the Gastro Institute and the book in the game (MAP-2, then BOOK-ENGINE)

You are the engine session on the Mac. You implement the BOOK-1 branch inside `main` and replace the first Gastro hospital
placeholder (`books.json`: level `gastro`, `map_id: "institute"`, `scene: "hospital_v1"`) with the institute and its book.
This prompt supersedes `content_private/gastro/ENGINE_PROMPT.md` wherever they differ; that file stays the reference for the
pack layout (its S2), the item types and widgets (S5), the avatar mapping table (S4b) and the measured content figures.

## Preconditions (check first, stop and report if one fails)

- LIB-1 is signed off and the combined post-LIB-1 iPad gate has passed (`docs/perf/after/` is the post-LIB-1 run).
- `content_private/gastro/` exists on this Mac with the engine bundle: `gastro.mgpack` (v4), `gastro.mgpack.h`,
  `mgpack_read.c`, `institute.map`, `model_nav.json`, `fig/` (18 crops + `crops.json`), `ENGINE_PROMPT.md`. The folder is
  git-ignored and must never be committed: the repository is public and the pack holds the book's text.

## Decisions (defaults set by the author; follow them)

1. The BOOK-1 branch comes in as a merge commit (no rebase, no history rewrite of either side).
2. The institute replaces `hospital_v1` for level `gastro`. `game/hospital.c` keeps compiling (devroom, tests, perf
   scenarios that use it) but is not built when the level is `gastro`.
3. Lobbies: the people come from the pack, so every player sees the same institute; staging (a presenter walking to a
   step, a patient lying down) is local to each player; book progress is per account. Missions work in a lobby, per player.
4. Progress: the fact bitset (419 bytes) + case state (5 bytes) per account, stored locally in a per-account file and on
   Supabase in a new table `book_progress`; Settings → Erase progress also resets the book.
5. The font atlas grows by the 11 glyphs the pack uses (below).
6. CI gains a Linux job: build + ctest + headless desktop autotests on the dummy pack.
7. Figures: ASTC 4×4 on Apple and Android, RGBA8 fallback on desktop Linux (dev builds only).
8. Final-exam question counts and scoring: constants marked TODO, the author sets them.

## Rules that hold for the whole milestone

- Real content only from `content_private/gastro/`; when it is absent the build embeds the dummy pack
  (`content/gastro_demo/gastro.mgpack`) and the game shows a small „conținut demo” chip. A shipped build fails without the
  real pack. CI and the public repository only ever see the dummy pack.
- The leak guard (`tools/book/leakguard.py`, run by `tools/precommit.sh`) runs in the pre-commit hook next to the build;
  it refuses any staged path under `content_private/` and any run of ≥ 12 book words. Never `--no-verify`.
- No JSON parsing at run time, no heap allocation in the frame loop, fixed-size records read in place.
- Every CLAUDE.md rule holds (arenas, gfx.h only, strings in `assets/strings_ro.json` with `strings.json` as fallback,
  landscape, safe areas, Jelly Pop tokens, haptics through `ui_haptic`, wip commits before branch operations).
- The milestone ends with the perf audit on the iPad Air 4 against `docs/perf/after/`: FAIL on any exceeded budget or a
  regression above 10 %.

## Inputs

- Branch `claude/festive-johnson-2qrowe` (head a107b67 or later; 29+ commits behind `main`). Public, git-safe parts:
  - `tools/book/` (the content pipeline; the engine needs only `packbin.py`, `make_dummy_pack.py`, `leakguard.py`,
    `meshgen` is already on main);
  - `content/gastro/`: `institute.map` (rooms, doors, windows, corridors, lifts, 1,020 furniture records + 105 door
    leaves, 201 book props, 33 NPC points), `model_nav.json` (the walk grid the map was checked on: 0.25 m cells, walkable
    and reachable bitsets per storey, door and room reports), `prop_kinds.json` (the 15 book prop kinds and the mesh
    families), `mgpack_read.c` + `gastro.mgpack.h` (the reference reader), `institute_geometry.js` (the model's geometry
    code: shell, furniture builders, lifts, roof, site — the specification for the C scene builder), `ENGINE_PROMPT.md`,
    `STATE.md`, `leakguard.txt`;
  - `content/gastro_demo/gastro.mgpack` + `.h` (the dummy pack, invented filler);
  - `.gitignore` (`content_private/`), the leak guard block in `tools/precommit.sh`, the BOOK-1 section of CLAUDE.md.
- The 3D reference page (look and behaviour of the institute and the mission play): https://claude.ai/artifact/QQsqMd8f4bNsZLYpH8m31C
  (private to the author; ask for screenshots if you cannot open it).

## Phase 0 — bring the branch into main

1. `git fetch origin claude/festive-johnson-2qrowe` and merge it into `main` (merge commit). Resolve conflicts keeping
   everything LIB-1 did: CLAUDE.md (main's text + the branch's BOOK-1 section, corrected: the reader is v4, the pack has 32
   table slots), `tools/precommit.sh` (LIB-1's hook + the leak guard block), `.gitignore` (+ `content_private/`).
2. Linux build: `M_PI` in `engine/audio/audio_sdl.c` is undeclared under `-std=c11`; add `_DEFAULT_SOURCE` for Linux in
   CMake (no change to `audio_sdl.c` needed). The pre-commit build must be green without a hand-edited cache.
3. Checks: `git ls-files | grep -c content_private` = 0; `python3 -I tools/book/leakguard.py repo content/gastro/leakguard.txt .`
   clean; `cc -include content/gastro_demo/gastro.mgpack.h content/gastro/mgpack_read.c -o /tmp/mgr && /tmp/mgr content/gastro_demo/gastro.mgpack`
   prints `problems=0`; the same on `content_private/gastro/gastro.mgpack`.
4. Fix the dummy pack's one mismatch: its 6 rooms (`demo_r1..6`) do not exist in `institute.map`. Change
   `tools/book/make_dummy_pack.py` to take the rooms, furniture, book props and slots tables from the public
   `content/gastro/institute.map`, place its synthetic steps on real props of 6 real rooms (2 storeys), and choose stand
   points within 2.0 m of a reachable cell of `content/gastro/model_nav.json`. Text stays invented filler. Regenerate,
   check with `mgpack_read.c`, commit through the leak guard.

Do not run `content/gastro/check.py` or `tools/book/make.sh`: they need the book's text (content-pack zip, not here).
The content is validated in the BOOK-1 session; you validate the pack with the reader's checks.

## Phase 1 — MAP-2: the institute scene from `institute.map`, furniture from data only

The engine has no map loader today: the scene is compiled in (`MG_SCENE`), `books.json`'s `scene` field is only logged.

1. **Map compiler (build time).** A Python tool turns `institute.map` into fixed-size binary records (rooms, doors, door
   leaves, windows, corridors, lifts and landings, stairs, slots, book props, NPC points, furniture), embedded like the
   other assets; no text parsing at run time. Embedded arrays are unaligned `unsigned char`: emit them `_Alignas(8)` or
   read with `memcpy`.
2. **Scene builder in C** (`game/institute.c` on the `scene.h` interface): port the rules of
   `content/gastro/institute_geometry.js` — walls with door and window gaps, wall pieces for the occlusion fade (south side
   of each room), slabs with the atrium void, stairwells (collide as one box, as today), lift towers and cars, roof, site,
   the entrance canopy. Same dimensions (`FH` 4 m), same colours (`COL`), toon materials, the minimal renderer. Storeys
   0–3 plus the roof; the floor fade per storey; interiors lit like the rest of the scene.
3. **Furniture from data only.** Every piece comes from a furniture record (id, kind, x, z, yaw, w, d, h, mount): meshes
   per kind ported from the page's builders (`PROP`, `furnish`, `furnishBase`, `extras`, `corridorItem`), parameterised by
   the record's footprint; **merged at load per storey and material** (PERF_RULES: static geometry), not one instanced
   draw per kind (that would add 43–70 draws on a storey). Colliders from the records (free and wall mounts; floor mounts
   are rugs and are walked on; door leaves swing open when someone is near). Book props (15 kinds, `prop_kinds.json`) are
   placed the same way from their records.
4. **Engine limits the institute exceeds — raise them:** `COL_MAX_BOXES` 1024 (need ≥ 1,500 with walls),
   `ROOMS_MAX` / `ROOM_DOORS_MAX` 48 (need 85 / 86+), `NAV_MAX_GRIDS` 4 (need 5: G, 1, 2, 3, roof), the fade slots
   (storeys 0–2 + roof today; need 0–3 + roof), `visited_rooms` (its bit index wraps at storey 4), the interact and NPC
   spatial grids (x −40..40, z −30..30: the building is ±26 × ±18, the site larger), markers' storeys (−1..2 today) and the
   floor badge (0..2 today; need 0..3 and R).
5. **Lifts:** three lifts (A, B, C in `institute.map`) with 5 landings each (G, 1, 2, 3, R). Today there is one
   `Elevator` per scene with `ELEV_MAX_FLOORS` 4 and one car in the nav bake: make it an array, a 5-button panel, cars in
   the nav bake, autonav across storeys through the nearest lift.
6. **Nav bake at the game's capsule (0.35 m).** Acceptance against `content/gastro/model_nav.json` (same 0.25 m cells and
   origin): every door opening holds ≥ 2 walkable cells across its width (the grid supports 1.2 m doors and wider; the
   institute's toilets were widened from 0.9 m for this); every door has a free 1.0 m approach on both sides; every room's
   floor is reachable from the entrance with all furniture solid (the 4 stairwells are exempt). Report every difference
   between your grid and the reference.
7. **The level uses it.** `books.json`: `gastro` → `"scene": "institute"` (= its `map_id`, so the card no longer says
   „Hartă provizorie”). Build the scene when the level loads; do not keep `hospital_v1` resident. The renderer has no mesh
   free and `ensure_world` builds once into the perm arena: give level scenes their own arena / pool reset, and check the
   GPU pools (vertex 5 MB with ~1.5 MB free after LIB-1, index 2 MB) — the institute's shell and merged furniture must fit
   or the pools grow, measured.
8. **Saves.** Save v7 adds the scene id; a v6 save of level `gastro` (hospital coordinates) continues at the institute's
   entrance. The old hospital's ambient roster (M7) does not run in the institute.
9. **Tests:** `--autotest institute` (walk to every room through the nav grid, ride every lift to every landing, the door
   and reach checks above, screenshots of each storey and the exterior in `docs/autotest/institute/`), a perf scenario
   `institute` (busiest storey, view from the atrium) in `game/perf_run.c`, `tools/perf_gate.py`, `tools/perf_run.sh`.

**Stop and report after Phase 1**: screenshots, the grid comparison, draws / memory / pools measured on the Mac, then
continue with Phase 2 unless the author says otherwise.

## Phase 2 — BOOK-ENGINE: the pack, the missions, the player, the progress

1. **Pack embed and loader.** CMake copies `content_private/gastro/gastro.mgpack` (or the dummy) into `${MG_GEN_DIR}` as
   `content/gastro.mgpack` and embeds it aligned to 8. The loader runs the same checks as `mgpack_read.c` (magic,
   version 4, record sizes, every index and string offset, each required script in exactly one mission step) once at load
   and reads records in place. `MG_CONTENT_DEMO=1` marks the dummy build; the game shows „conținut demo” (a Jelly Pop chip
   that avoids the account chip's perf text and the left 45 % stick zone). Shipped builds: `tools/archive_ios.sh` and
   `tools/build_aab.sh` pass an explicit `-DMG_REQUIRE_REAL_CONTENT=ON`, and configure fails if the real pack is missing.
2. **Text.** Regenerate the font atlas with „ ” – ’ › → − † α β γ (10,413 occurrences in the pack; the demo chip uses „ ”).
   UI strings in `assets/strings_ro.json` (+ English fallback in `strings.json`). Text helpers draw single lines today:
   add word wrap; the scrolling card emits only its visible lines (the UI cap is 8,192 instances per frame).
3. **People from the pack's `people` table only** (112: 50 cast posts + 62 background; 22 walking; ≤ 36 on one storey).
   `npc_spawn` cannot place a given look at a given position, facing and pose, so add a placed-person path: look from the
   record (mapping table in ENGINE_PROMPT S4b, corrected: `AVATAR_EXTRA_HAT` is a sun hat, so the surgeon's scrub cap is
   new; set `accent` for the badge / cross colour), pose stand / sit / lie (lying is fixed to a 0.6 m mattress: offset
   `feet.y` for other bed heights), LIB-1's LOD rules, instanced with the avatar batches, only the view storey drawn. The 90
   static people are placed once and never path; the 22 walkers use the existing NPC steering and path queue. People are
   outside the deterministic M7 sim (the sim never reads the player); staging is local. New: the talking hand gesture
   (build on LIB-1's hand / carry fields).
4. **Interaction and markers (reuse).** Register only the view storey's people. The objective's presenter gets
   `IA_PRIORITY` and facing-free, and the 2 Hz flags pass (`wants_chat`) must not overwrite it; cast are always talkable,
   background people by their `talk` flag; others greet (2.5 s card, nod). `IA_MAX` is 320: book props are not
   interactables (the book is reached only through NPCs). One `MARKER_OBJECTIVE` on the objective NPC, storeys 0–3.
5. **Script player (Jelly Pop).** The card with the presenter's name in its header; teach steps (atoms with page numbers),
   the 8 item types (simplu, multiplu, card, cloze, sort, match, order, label — sort / match / order need new tap-to-place
   widgets), result rows, explanations, review notes, „De verificat” / „Text ascuns” chips. At most 2 more UI draws (the
   gate allows 3 in total and the frame already uses 1). Text scrolls, never cut.
6. **Figures.** Build time: the 18 crops (`content_private/gastro/fig/`, max 1,386 px) → ASTC 4×4 sRGB (`astcenc`),
   shipped as separate bundle files (≈ 9.7 MB on disk), RGBA8 on desktop Linux. Run time: only the figure on screen is
   resident (≤ 1.6 MB on the GPU), uploaded through a preallocated 2 MB staging buffer, freed when the card closes; label
   boxes from the pack's figure-label table, drawn as UI text over the image. `gfx.h` has no texture API and `ui.frag`
   samples only the font and head atlases: add a texture slot for one figure.
7. **Missions (new).** Arcs → cases → quests → steps from the pack. The objective pill (case, who, room, storey, steps
   done), the dialogue card (portrait: a new path, head icons are keyed by `NetSkin` and cannot hold cast colours), the
   journal screen (opened from a new „Misiuni” row of the pause card; the pause card has no pages: the journal is its own
   screen with arc tabs, case cards, stages, Track / „Du-mă acolo”). Staging: the presenter walks (same storey, in view) or
   is placed at the step's stand point; a case step's patient lies or sits at the step's bed / seat. **No locks**: any case
   can be accepted from its giver or the journal, any stage of an accepted case played in any order; the only gates are a
   case closing when its steps are done and a chapter exam opening when its rooms are complete (the final simulation when
   every room is complete).
8. **Progress (per fact).** A step is done when every fact its script covers is set; quests, cases and arcs derive from
   that; plus one accepted + closed bit pair per case (5 bytes). Local: a per-account file in the pref path (atomic write
   from a fixed buffer when a step ends, never through cJSON prefs in the frame loop; `prefs.json` is per device and would
   leak bits into the next account). Server: table `book_progress` (`user_id uuid primary key references public.profiles(id)
   on delete cascade`, `book text`, `facts bytea`, `cases bytea`, `updated_at`), RPCs `book_progress_merge` (bitwise OR,
   newer case state wins) and `book_progress_reset` (Erase progress), RLS owner-only, a rule in `merge_ticket_redeem`
   (`link_test.sql` enforces it). Feed `world_level_completion` for `gastro` from the learned facts.
9. **Load-time data check, no run-time snapping.** On the engine's own grid every presenter's stand point and every
   patient's bed / seat is within 2.0 m of a reachable cell. If any fails, list them and stop: the BOOK-1 session fixes the
   data (known: 11 book props fail a 0.7 m standing square — 6 are behind pharmacy counters, none is an interaction target).
10. **Tests.** `--autotest book` (dummy pack in CI, real pack locally): accept case 1, autonav to each presenter, play each
    step with the keys from the pack, close the case with its giver, check the next case is tracked and that a wrong
    answer leaves its facts unset; `--autotest book_ui` (one item of every type, a figure, the journal, the dialogue card on
    iPhone, iphone-flip and iPad windows; screenshots in `docs/autotest/book/`); `link_test.sql` and a new
    `book_progress_test.sql`; the CI job from decision 6.

## Budgets (PERF-1, iPad Air 4) — estimates to replace with measurements

| Budget | Estimate | Risk |
|---|---|---|
| Draws ≤ 150 (gate on the average) | play today 64–95; furniture merged per storey/material +5–10; people share the avatar batches (each part mesh is one draw per kind) | per-kind furniture instancing would give 110–140: do not |
| Tick ≤ 5 ms | 22 walkers (~1.7 µs each) + the path queue + the 10 Hz scan < 0.2 ms; 90 static: 0; M7 roster off | low |
| Render submit ≤ 2 ms | one storey: merged statics + ≤ 36 avatars < 0.3 ms | low |
| GPU effective ≤ 8 ms | ~96k furniture triangles + ≤ 36 avatars (LOD0 ~950 triangles within 22 m) + fades and x-ray | atrium / glass overdraw: measure |
| Memory ≤ 300 MB | pack 2.5 MB read in place, meshes ~1.2 MB, nav grids ~0.4 MB, one figure ≤ 1.6 MB: +6–12 MB | the GPU vertex / index pools, not the total |
| Frame allocations 0 | records in place, fixed buffers | progress saves and figure uploads unless preallocated |

## Report at the end

Measured figures for each budget on the Mac and the iPad Air 4 (GPU frame ms, busy %, effective ms, dominant clock state),
the grid comparison, the load-time data check, screenshots, the files changed, and anything the BOOK-1 session must fix in
the data.

## Done checklist

- [ ] the BOOK-1 branch merged into `main`; leak guard in the hook; `content_private/` never tracked
- [ ] the dummy pack uses `institute.map`'s rooms; public build and CI green on it, „conținut demo” shown
- [ ] level `gastro` loads the institute built from `institute.map` (furniture from data only, merged per storey/material); `hospital_v1` not resident
- [ ] nav at 0.35 m: every door ≥ 2 cells, every room reachable (stairwells exempt), 3 lifts × 5 landings
- [ ] the real pack from `content_private/gastro/` loads with the reader's checks; a shipped build fails without it
- [ ] the book is reached only through missions; the 112 people from the pack; no locks beyond the two gates
- [ ] progress per fact + 5-byte case state, local per account and on Supabase; Erase progress resets it
- [ ] `--autotest institute`, `--autotest book`, `--autotest book_ui` pass; `link_test.sql` and `book_progress_test.sql` pass
- [ ] `--autotest perf` within budget; `docs/perf/` updated

# BOOK-1 engine prompt — the Gastro content pack in the game

Paste everything below the line into a Claude Code session on `PetreLuca05/mobilegame`. Figures are measured from the
pack that this prompt ships with (`tools/book/packbin.py`), not estimated, unless marked "estimate".

---

You are implementing **BOOK-1 in the engine**: the Gastro chapter of the Kumar textbook, already turned into a content pack
(facts → items → scripts → rooms of the institute map), becomes playable. Read `CLAUDE.md`, `docs/PERF_RULES.md`,
`game/interact.h`, `docs/UI.md`, `content/gastro/README.md`, `content/gastro/STATE.md` and `tools/book/packbin.py` first.

## What the pack holds (measured)

- {FACTS} facts, {ITEMS} items ({ITEM_TYPES}), {SCRIPTS} scripts ({SCRIPT_KINDS}); {REQ_SCRIPTS} required.
- 50 topics in 45 rooms of `content/gastro/institute.map` (MAP-2 layout v3), 5 chapters; {PROPS} new interactable props
  (`iprop`, {NMODELS} models: {MODELS}) and {SLOTS} new slots (`islot`); per storey: {PROPS_STOREY}.
- {FIGS} figures (`fig/<id>.png`, 200 dpi crops; `fig/figures.json` gives each label's box for `label` items).
- Compiled: `gastro.mgpack` = **{PACK_BYTES} bytes** ({PACK_MB} MB): fixed-size little-endian records, one UTF-8 string
  table, u32 offsets (`gastro.mgpack.h` holds the structs). Per-account progress = one bit per fact = **{BITSET} bytes**.

## Rules that do not change

- Every fact is taught in its room; a room is complete when every fact of its topics has been answered correctly at least
  once (an item answered correctly sets the bits of its `fact_ids`; for multiplu only a fully correct answer counts). A
  chapter exam (Sala Comisiei) opens when every room of the chapter is complete. Review scripts (round, garda, exams) never
  count toward completion.
- Show the book as printed: items flagged `needs_review` (errata) or `unverified_ocr` (hidden text) show a small marker and
  their `review_note`; never rewrite pack text at run time.
- The repository is public: no text of the book may be committed (the pre-commit hook refuses 12-word runs). The pack and
  figures are never copied into `assets/`.

## Steps

### S0. Prerequisite: the institute scene (MAP-2)

`assets/world/books.json` still points Gastro at the stand-in scene `hospital_v1`. BOOK-1 needs the institute map loaded as
a scene (rooms, doors, slots, storeys, lifts). If MAP-2 is not done, stop after S2 and say so; do not fake the rooms in
`hospital_v1`. The map parser must accept the two new records: `iprop <floor> <room> <id> <model> <x> <z> <yaw> "<label>"`
and `islot <floor> <room> <id> <kind> <x> <z> <yaw>` (x east, z south, yaw 0 faces +z).

### S1. Build: a private content location, compiled at build time

- **Open question for the author (propose, do not decide):** where the pack lives. Proposal: a private repository (or
  private bucket) holding `gastro-pack.zip`, pinned in `content/CONTENT.lock` (path | sha256 | source, same format and
  checker as `assets/ASSETS.lock` / `tools/assets_lock.py`), fetched at configure time with a token from the environment.
- CMake: when `content/src/gastro-pack.zip` (or an unpacked `content/gastro/` with `atoms.jsonl`) is present and matches the
  lock, run `content/gastro/check.py` (must pass) and `tools/book/packbin.py` into `${MG_GEN_DIR}/content/gastro.mgpack` and add
  it and the figure atlas to `MG_EMBED_FILES`. When absent (public CI), embed a **stub pack** generated from the public
  files (ids, rooms, props, counts; placeholder strings) so the public build and tests stay green.
- No JSON parsing of the pack at run time; `packbin.py` is the only producer of the binary.

### S2. Loader: zero-copy, zero allocation per frame

- `game/pack.c` / `pack.h`: `pack_open(const void *blob, size_t size)` checks magic, version and that every table fits
  (offset + count × size ≤ size, record sizes equal the `_Static_assert`ed struct sizes), then keeps typed pointers into
  the embedded blob. Strings are `blob + strings.offset + off`. Lookups by index; topic → contiguous fact / item / script
  ranges (the compiler sorts by topic). One `perm` arena allocation for the derived per-room tables; nothing in the frame.
- ctest `test_pack.c`: open the stub pack and a corrupted copy (bad magic, short table) — the second must be refused.

### S3. Props and slots: instanced, one draw per model per visible storey

- Procedural meshes for the {NMODELS} prop models (`tools/meshgen.py` `.mgmesh`, vertex colours, flat normals, ≤ 300
  triangles each), merged nothing: each prop is an instance, so one `gfx_draw` per model per visible storey
  (at most {MAX_MODELS_STOREY} models on one storey). Storey-tagged like the scene (floor fade, x-ray rules unchanged).
- Slots are positions only (NPC stands); NPC packages may use them like `rooms.h` slots.

### S4. Interactions: reuse the registry and its 10 Hz scan

- Each script becomes an interactable (`ia_add`, a new kind `IA_SCRIPT` or `IA_BUTTON` with a script ref) at its prop or
  slot, label = script title, `on_interact` opens the script player. Register only the scripts of the **current storey**
  (most on one storey: {MAX_REQ_STOREY} required + the optional ones of that storey), re-registered on storey change; this
  keeps the registry under `IA_MAX` (320) with NPCs and markers. The scan, ranges (2.0 / 2.5 m) and the panel are unchanged.
- A room's station shows a marker until the room is complete (existing `markers.c`).

### S5. Script player and item widgets (UI-1 Jelly Pop)

- One modal card (scrim, no blur pass) that plays the steps in order: `predict` (an item before teaching; wrong answers are
  fine), `teach` (the atoms' text with the printed page; figures from the atlas), `check` (items). Widgets per item type:
  simplu (5 options, one tap), multiplu (5 toggles + confirm), cloze (a chip row of candidate tokens from the same item and
  its siblings; the gap fills), card (flip), sort (drag chips into 2–3 bins), match (two columns), order (drag to reorder),
  label (the figure with blank boxes from `figures.json`; drag labels). After an answer: the explanation (it quotes the
  book sentence and page) and, for multiplu, each false option's `why`.
- All widgets from `ui_widgets.c` / `k_ui` tokens; targets ≥ 44 pt; strings from the pack (Romanian), chrome from
  `assets/strings.json`. One UI batch: the card must not add more than 3 UI draws (UI-1 gate).

### S6. Progress: a bitset per account

- Local: `prefs.json` gains `book` {pack id, fact bitset base64, per-item last result}; written atomically on change
  (throttled to 1 s), on background and exit.
- Supabase: table `fact_progress (owner uuid references auth.users, pack text, bits bytea, updated_ms bigint)` with
  owner-only RLS and RPC `fact_progress_merge(p_pack, p_bits)` = bitwise OR with the stored bits (monotone: no conflict).
  **Merge coverage rule:** name `public.fact_progress` in `merge_ticket_redeem` with an explicit rule (OR the two accounts'
  bits); `supabase/tests/link_test.sql` must pass. Migration `<timestamp>_fact_progress.sql`.
- Pack versions: the bitset is keyed by fact index; ship `fact id → index` in the pack and remap a saved bitset by id when
  the pack id changes (facts removed → bits dropped, new facts → 0).

### S7. Rooms, chapters, exams

- Room complete = popcount over its topics' fact ranges equals their size. Chapter complete = all its rooms. The chapter exam
  (Sala Comisiei) and the final simulation (Sala de examen) draw from item pools (`scripts.json` `pool`); **question counts
  and scoring of the final simulation are the author's to set: leave them as constants marked TODO and ask.**
- Round (nurse station) and garda (emergency bays) draw from completed rooms, oldest-answered facts first.

### S8. Tests

- `--autotest book`: offline, walk to every room of chapter 1, open each required script, answer every item with the key
  from the pack, check the room completes and the chapter-1 exam opens; then one wrong answer leaves its facts unset.
- `--autotest book_ui`: one item of every type on iPhone, iphone-flip and iPad windows (screenshots).
- Pack loader ctest (S2); `link_test.sql` for the merge rule (S6).

## Estimated cost against the PERF-1 budgets (iPad Air 4)

| Area | Budget | BOOK-1 estimate | How |
|---|---|---|---|
| Draw calls per frame | 150 | +{MAX_MODELS_STOREY} at most on one storey (one per prop model), +≤3 UI while a card is open | instancing, one UI batch |
| CPU tick | 5.0 ms | < 0.02 ms: ≤ 80 more entries in the 10 Hz interaction scan through the spatial hash; progress is a bit set | estimate, measure |
| CPU render submission | 2.0 ms | + instance building for ≤ {PROPS} props (< 0.05 ms) | estimate, measure |
| Memory | 300 MB | pack {PACK_MB} MB (embedded, read in place) + figure atlas ≤ {FIG_MB} MB RGBA8 (2048² pages; ASTC 4×4 would be a quarter) + derived tables < 0.1 MB | measured / estimate |
| Heap allocations in the frame loop | 0 | 0: fixed-size player state, arena tables built at load | |
| GPU frame | 8.0 ms | props are small toon meshes; the card is one SDF batch | measure with Instruments |

## Perf audit (last step)

Run `tools/perf_run.sh OUT --gpu --gate` on the iPad Air 4 with a clean tree and HEAD installed, add a `book` scenario
(chapter-1 rooms, a card open) to `--autotest perf`, compare with `docs/perf/after/`, record the measured deltas in
`docs/PERF.md` and replace every estimate above with its measurement. Any budget exceeded or regressed by more than 10 %
blocks sign-off.

## Done checklist

- [ ] S0–S8 done, or stopped at S2 with MAP-2 named as the blocker
- [ ] `content/gastro/check.py` passes in the build; the public build uses the stub pack and stays green
- [ ] the content location is proposed in `content/README` and left as an open question for the author
- [ ] `--autotest book` and `--autotest book_ui` pass; pack ctest passes; `link_test.sql` passes
- [ ] `--autotest perf` within budget; `docs/perf/` updated

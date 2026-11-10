# BOOK-1 state — Gastro book inside the institute map

Resume from these files alone (the zip = `content/gastro/` + `content/src/` + `tools/book/`). Source: `Gastro.pdf`,
22 PDF pages, printed 1147, 1162–1166, 1172–1176, 1198–1208. Rebuild: `tools/book/make.sh content/gastro` (23 s; ends
with the whole-book `check.py`, the review sheets and `ENGINE_PROMPT.md`).

## Phase

| phase | state |
|---|---|
| A extract | done and accepted (check-in 1); the author's review of errata and hidden text runs through the two review sheets |
| B facts / C items / D scripts | all 50 topics done (style v2, check-in 2) |
| E proof | `check.py` (whole book) passes 100 %: the four ledgers, the style rules, typos not tested, verbatim gaps / labels, prop kinds, placement, interactables per room, script length, and the reference C reader on `gastro.mgpack` |
| close-out §1–§8 | done; see "Close-out" below and `report.md` |
| F hand-over | `ENGINE_PROMPT.md`: MAP-2 first (`learn1b-quest1-map2-prompt.md`), then BOOK-ENGINE; engine work not started (author's decision) |

## Measured (build + check.py)

- Text layer 18,151 words, all in atoms; 1,828 hidden (PDF p. 2: 314, 6: 137, 7: 505, 11: 283, 12: 194, 22: 395), every one
  under a white rectangle of the PDF.
- `cleaned.txt` 127,030 characters, 108,419 non-whitespace, all in 657 atoms; 1,194 sentences, 700 number tokens.
- 3,349 facts; 3,478 items (cloze 1,452, multiplu 720, simplu 623, card 456, sort 74, order 67, match 65, label 21);
  every fact tested by ≥ 2 item types and by ≥ 1 simplu / multiplu; items per fact min 2, median 2, max 6; multiplu correct
  counts 2: 159, 3: 416, 4: 145.
- 389 scripts: 382 required as played (280 as authored; 96 longer than 15 steps are stored as 198 parts „Partea N” on the
  same prop) + 7 optional review / exam pools (counts and scoring of the exams are the author's to set).
- `institute.map`: 201 props in 15 kinds (13 existing in MAP-2 v3, 2 new: microscope, iv_stand), 34 NPC points; all pass
  the placement check; interactables per room min 1, median 6, max 9.
- 134 errata: contradiction 9, cross-reference 9, missing target 4, contents mismatch 8, terminology 23, layout 6,
  illegible 1, typo 74. 247 facts / 457 items `needs_review`; 514 facts / 600 items `unverified_ocr` (hidden text).
- 18 figures, 177 labels, every label boxed on its crop.
- `gastro.mgpack` v2: 2,355,308 bytes; string table 1,818,519 bytes; longest string 1,776 bytes; largest script
  `s-barrett_dx-praga-p1` (12 steps, 25,117 string bytes); progress bitset 419 bytes per account.

## Close-out (author's message 3)

- §1 typos shown, not tested: 72 items changed (69 answer fields to the intended form with the printed wording in the
  explanation, one of them a cloze turned into a card; 3 cloze gaps moved off the misprint) + 1 new card; 2 facts whose only
  testable token is a misprint (`typo_only_facts.json`, for the author).
- §2 answers verbatim: cloze gaps and label answers are exact substrings (check.py); 637 other answers not verbatim
  (inflection 560, reworded 77), listed in `report.md` (appendix) and `inflections.json`.
- §3 props: 155 placement failures found and fixed (114 wall-mounted props off their wall, 38 free props < 0.3 m from a
  wall, 3 outside their room) by `tools/book/placeprops.py`; visible kinds per storey ≤ 18.
- §4 script length: as authored min 2, median 12, max 42; as played min 2, median 10, max 15.
- §5 review sheets: `review_errata.csv` 134 rows, `review_hidden.csv` 590 rows; `check.py --apply-review` applies them.
- §6 hook: `git config core.hooksPath tools/hooks` (README); installed in the working clone, the desktop build is green.
  On Linux the build needs `-D_DEFAULT_SOURCE` for `M_PI` in `engine/audio/audio_sdl.c` (not changed by BOOK-1).
- §7 `ENGINE_PROMPT.md` regenerated (order, prop kinds, binary layout, reference reader, measured sizes, perf audit).

## Corrections round (author's message 4)

- §1 typo errata in two classes: 74 typo errata, class A (spelling only) 16, class B 58. 59 items reverted to the printed
  wording; 2 tests moved by hand; 4 items kept as printed with the misprint as their test (needs_review); 2 facts whose only
  testable token is a misprint. `review_errata.csv` has `printed`, `intended`, `class`, `use_intended` (yes for A, no for B);
  `--apply-review` honours it.
- §2 short keyed answers: every word printed in its source sentence or an inflection of one (check.py, `wordrule.py`);
  reworded 39 before, 5 after (3 items kept on purpose, needs_review). The previous 77: 62 shortened (61 verbatim spans), 9
  inflections, 6 reworded.
- §3 props against the institute model (the "Gastro Institute 3D" page): its own furniture is rasterised
  (`model_occupancy.json`, from `content/src/institute_model_page.html`); 234 props / NPC points, 0 placement failures,
  9 anchored to furniture the model already has (`on_model`). 183 moved from their authored position, 53 by more than
  1.0 m (report.md §3; `plan/0–3.png`, the reviewer's Plan tabs). The book plays inside the model page.
- §4 `ENGINE_PROMPT.md`: measured text lengths (stem 287, option / answer 621, explanation 1,681 characters); text
  scrolls, never cut; the Linux `M_PI` error in `engine/audio/audio_sdl.c` noted for the engine step (file untouched).

## Furniture as data (author's message 5)

- `content/src/institute.map` holds every piece of furniture the institute model places: 1,020 `prop` records (82 kinds:
  id, kind, room, position, rotation, footprint w × d, height, wall / free / floor) and 105 door leaves (hinge, swing). The
  9 `on_model` anchors name furniture ids; their position comes from the record. `tools/book/model_furniture.js` exports
  the page's furniture (`model_furniture.json`); `check.py` fails unless page and file place the same pieces (ids, kinds,
  positions within 1 cm), and the placement check runs against the file's records (234 objects, 0 failures).
- The 3D page still builds its furniture in code (it does not read the file); the equality check in `make.sh` guards it. To
  change furniture: change the page, then `node tools/book/model_furniture.js PAGE OUT --map content/src/institute.map`.
- Model page findings: 9 of its pieces reach past their room outline into a wall (6 cabinets, a lab bench, the two outer
  library shelves); they are the page's geometry, recorded as they are.
- Visible kinds per storey (book + furniture): G 69, 1: 64, 2: 61, 3: 60; meshes with shared families (`mesh_groups`): 43,
  40, 40, 34. Pack v3 adds the furniture table (36-byte rows); prop / slot rows point to their furniture row.

## Topics done (all 50; per-topic counts in report.md)

## Next

1. Author: fill `review_errata.csv` / `review_hidden.csv` → `check.py content/gastro --apply-review` → fix the listed
   topic modules → `make.sh`.
2. MAP-2 builds the institute scene with the BOOK-1 props and slots; then BOOK-ENGINE (`ENGINE_PROMPT.md`).
3. Exam question counts and scoring: the author's.

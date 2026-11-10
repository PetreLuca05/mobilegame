# content/gastro — BOOK-1: the Gastro chapter inside the institute map

The game teaches a whole textbook chapter (Kumar & Clark, ch. 32 Gastroenterologie, Romanian edition: printed pages
1147, 1162–1166, 1172–1176, 1198–1208) inside the institute map (MAP-2). Every printed character is taught somewhere.

## What is in git and what is not

This repository is public, so **no text of the book is committed**. Git holds the recipe:

| in git | what |
|---|---|
| `layout/pNN.txt` | reading order and structure of each PDF page (DSL in `LAYOUT_GUIDE.md`) |
| `rules.json` | the global OCR rules (the per-page fixes `fixes/pNN.json` transcribe figure text, so they travel in the zip) |
| `topic_rules.json` | how each atom is assigned to one of the 50 topics |
| `gastro-quests.json` | the quest table (public copy: heading lists as atom ids) |
| `institute.map` | MAP-2 layout v3 + the `iprop` / `islot` records the scripts use (each prop names a kind) |
| `prop_kinds.json` | the prop kinds: model → kind, footprint, wall / free mount, existing in MAP-2 or new |
| `mgpack_read.c`, `gastro.mgpack.h` | reference C reader of the binary pack and its structs (`check.py` builds and runs it) |
| `check.py` | the proof: fails unless every ledger is 100 % |
| `STATE.md`, `QUESTIONS.md` | where the work stands; the author's decisions (one line each) |
| `LAYOUT_GUIDE.md`, `AUTHORING_GUIDE.md` | how pages were laid out and proofread; how topics are written (style rules) |
| `ENGINE_PROMPT.md` | Phase F: the prompt for the engine work (binary pack, progress bitset, perf budget) |
| `leakguard.txt` | hashes of every 12-word run of the book (no text); the pre-commit hook refuses a commit that holds one |
| `../../tools/book/` | the pipeline (extract, build, atoms, numbers, figures, compile, report, reviewer) |

Not in git (generated, or the book's own text — delivered as a zip): `fixes/`, `gastro.mgpack`, `cleaned.txt`, `atoms.jsonl`, `facts.jsonl`,
`items.jsonl`, `scripts.json`, `coverage.json`, `numbers.csv`, `errata.json`, `report.md`, `fig/`, `topics/`, `notes/`,
the review sheets `review_errata.csv` / `review_hidden.csv` and what applying them writes (`errata_decisions.json`,
`hidden_confirmed.json`, `review_applied.json`), `inflections.json`, `typo_only_facts.json`, `typo_changes.json`,
`placement_moves.json`, `pack_measure.json`, the topic sources `tools/book/topics/*.py`, `tools/book/data/`, and
`content/src/` (the PDF).

## The pre-commit hook (install once per clone)

```
git config core.hooksPath tools/hooks
```

`tools/hooks/pre-commit` runs `tools/precommit.sh`: the book leak guard on the staged files (no run of 12 or more
consecutive words of the book), then the desktop build of exactly what is staged (needs `rsync`, `cmake`, `ninja` and the
submodules). Never commit with `--no-verify`.

## Rebuild

Needs poppler (`pdftotext`, `pdftoppm`), Python 3 with Pillow, numpy, pdfplumber.

```
mkdir -p content/src && cp /path/to/Gastro.pdf content/src/
tools/book/make.sh content/gastro
python3 content/gastro/check.py content/gastro            # final gate (all topics)
python3 content/gastro/check.py content/gastro --topics intro
python3 content/gastro/check.py content/gastro --leak .   # every tracked file and the branch history: no book text
```

`check.py` (whole book) also proves the map side: every `iprop` kind is in `prop_kinds.json` (≤ 12 new kinds, ≤ 30
visible kinds per storey), every prop and NPC point passes the placement check, no room has more than 12 interactables,
no required script has more than 15 steps (longer ones are stored as parts „Partea 1, 2, …” on the same prop), and the
reference C reader accepts `gastro.mgpack`. `tools/book/placeprops.py content/gastro` moves any prop or slot that fails
placement to the nearest valid spot and rewrites its line in the topic module (then run `make.sh`).

## Author review: errata and hidden text

`make.sh` writes two sheets, sorted by the number of items each row touches:

- `review_errata.csv` — one row per erratum; fill `decision` with `keep` (taught as printed, with the note — the default),
  `not_an_error` (the note goes, the items stop flagging it) or `correct: <reading>` (the text is changed to that reading).
- `review_hidden.csv` — doubtful tokens of the hidden text (fixes made without print to confirm them, odd characters,
  words never printed in the visible text); fill `ok` with `y` (the best reading is right) or the right reading.

Then:

```
python3 content/gastro/check.py content/gastro --apply-review
```

applies the decisions through `fixes/review.json` (+ `fixes.json`), the fix files' `status` and `errata_decisions.json`
(+ `errata.json`), rebuilds the text (build, atoms, errata; not the topics) and lists the atoms that changed, the facts and
items that must be rebuilt and their topics (`review_applied.json`). Fix those topic modules, then `make.sh`.

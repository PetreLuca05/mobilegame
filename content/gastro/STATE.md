# BOOK-1 state — Gastro book inside the institute map

Resume from these files alone (the zip = this folder + `tools/book/` + `src/`). Source: `Gastro.pdf`, 22 PDF pages,
printed 1147, 1162–1166, 1172–1176, 1198–1208.

## Phase

| phase | state |
|---|---|
| A extract | **done**, waiting for the author's review of `numbers.csv` and the hidden text (check-in 1) |
| B facts / C items / D scripts | **topic `intro` done end to end**, waiting for approval of the question style (check-in 2); the other 49 topics start after it |
| E proof | `check.py` passes: ledger 0 (words) and 1 (characters) for the whole book, ledgers 2–4 for `intro` |
| F hand-over | not started (engine prompt after all topics) |

## Measured (from the build and check.py)

- Text layer: 18,151 words, all used by atoms (1,827 under the white rectangles, PDF pages 2, 6, 7, 11, 12, 22).
- `cleaned.txt`: 127,030 characters, 108,419 non-whitespace, all inside the 657 atoms; 1,194 sentences.
- Hidden text: 112 atoms, 12,884 characters, `unverified_ocr`.
- OCR fixes: 289 page fixes + 2 global rules (32 fixes in hidden text, marked unverified); 18 figures cropped, each with
  caption and label group; 61 errata; `numbers.csv` 1,318 rows.
- Topics: every atom has one of the 50 topics; `intro`: 43 facts, 42 items (simplu 6, multiplu 7, cloze 16, card 11,
  sort 2), 6 required scripts, 4 new props in `g_amfiteatru`.

## Topics done (facts → items → scripts, check passes)

| topic | room | facts | items | scripts |
|---|---|---|---|---|
| intro | g_amfiteatru | 43 | 42 | 6 |

## Next

1. Apply the author's answers to `QUESTIONS.md` (numbers / hidden text corrections go into `fixes/pNN.json`).
2. Topics in book order (quest order inside each chapter): cuprins, oral_gingii, oral_salivare, brge_epi … constipatia.
   Author each in `tools/book/topics/<topic>.py` (see `intro.py`), `compile.py`, then `check.py --topics …`.
   Facts must pass (ledger 2) before items, items (ledger 3) before scripts (ledger 4).
3. Final gate `check.py` with no `--topics`; reviewer page republished with no red; Phase F engine prompt.

## How to rebuild

`tools/book/make.sh PACK_DIR` (see README.md). Individual steps: build.py → atoms.py → numbers.py → figs.py →
quests.py → errata.py → compile.py → report.py → check.py; reviewer page: `tools/book/reviewer.py PACK OUTDIR`.

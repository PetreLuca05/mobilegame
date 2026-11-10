# BOOK-1 state — Gastro book inside the institute map

Resume from these files alone (the zip = `content/gastro/` + `content/src/` + `tools/book/`). Source: `Gastro.pdf`,
22 PDF pages, printed 1147, 1162–1166, 1172–1176, 1198–1208. Rebuild: `tools/book/make.sh content/gastro` (16 s).

## Phase

| phase | state |
|---|---|
| A extract | done and accepted (author's check-in 1); author's numbers / hidden-text review runs in parallel — corrections arrive as `fixes/pNN.json` entries |
| B facts / C items / D scripts | **all 50 topics done** (style v2 approved at check-in 2) |
| E proof | `check.py` (whole book, no `--topics`) **passes 100 %** on every ledger and rule; `check.py --leak .` clean on the public repo |
| F hand-over | `ENGINE_PROMPT.md` written (engine work not started); `gastro.mgpack` compiled by `tools/book/packbin.py` |

## Measured (build + check.py)

- Text layer 18,151 words, all in atoms; 1,828 hidden (PDF p. 2: 314, 6: 137, 7: 505, 11: 283, 12: 194, 22: 395), every one
  under a white rectangle of the PDF.
- `cleaned.txt` 127,030 characters, 108,419 non-whitespace, all in 657 atoms; 1,194 sentences, 700 number tokens.
- 3,349 facts; 3,477 items (cloze 1,453, multiplu 720, simplu 623, card 454, sort 74, order 67, match 65, label 21);
  every fact tested by ≥ 2 item types and by ≥ 1 simplu / multiplu (0 exam-exempt); items per fact min 2, median 2,
  max 6 (per topic: min 2 everywhere, median 2–3); multiplu correct counts 2: 159, 3: 416, 4: 145.
- 287 scripts: 280 required (station 50, inspect 107, read 33, case 23, colleague 23, dispense 20, directory 14, sort 6,
  sequence 4) + 7 optional review / exam pools (4 chapter exams, round, garda, final simulation — counts and scoring are
  the author's to set). 201 new props, 34 new slots in `institute.map`, listed per room.
- 134 errata (61 at Phase A + 73 found while authoring): contradiction 9, cross-reference 9, missing target 4, contents
  mismatch 8, terminology 23, layout 6, illegible 1, typo 74. 247 facts / 453 items flagged `needs_review`; 514 facts /
  600 items `unverified_ocr` (hidden text).
- 18 figures, 177 labels, every label boxed on its crop (`fig/figures.json`).
- `gastro.mgpack` 2,320,628 bytes; progress bitset 419 bytes per account.

## Topics done (all 50; per-topic counts in report.md)

## Next

1. Author's corrections to numbers / hidden text → `fixes/pNN.json` → `make.sh` → `check.py` lists stale atoms / facts /
   items → fix those topic modules only.
2. Engine: `ENGINE_PROMPT.md` (needs the MAP-2 institute scene; content location is an open question for the author).
3. Final exam simulation: question counts and scoring from the author.

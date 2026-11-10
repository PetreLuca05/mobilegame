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
| `institute.map` | MAP-2 layout v3 + the `iprop` records the scripts use |
| `check.py` | the proof: fails unless every ledger is 100 % |
| `STATE.md`, `QUESTIONS.md` | where the work stands; the author's decisions (one line each) |
| `LAYOUT_GUIDE.md`, `AUTHORING_GUIDE.md` | how pages were laid out and proofread; how topics are written (style rules) |
| `ENGINE_PROMPT.md` | Phase F: the prompt for the engine work (binary pack, progress bitset, perf budget) |
| `leakguard.txt` | hashes of every 12-word run of the book (no text); the pre-commit hook refuses a commit that holds one |
| `../../tools/book/` | the pipeline (extract, build, atoms, numbers, figures, compile, report, reviewer) |

Not in git (generated, or the book's own text — delivered as a zip): `fixes/`, `gastro.mgpack`, `cleaned.txt`, `atoms.jsonl`, `facts.jsonl`,
`items.jsonl`, `scripts.json`, `coverage.json`, `numbers.csv`, `errata.json`, `report.md`, `fig/`, `topics/`, `notes/`,
the topic sources `tools/book/topics/*.py`, `tools/book/data/`, and `content/src/` (the PDF).

## Rebuild

Needs poppler (`pdftotext`, `pdftoppm`), Python 3 with Pillow, numpy, pdfplumber.

```
mkdir -p content/src && cp /path/to/Gastro.pdf content/src/
tools/book/make.sh content/gastro
python3 content/gastro/check.py content/gastro            # final gate (all topics)
python3 content/gastro/check.py content/gastro --topics intro
```

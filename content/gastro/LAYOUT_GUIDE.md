# BOOK-1 Phase A — page layout + proofreading guide

Working dir for everything below: the parent of the pack directory, which holds `src/` (the PDF, `words.json`), `render/`
and `build/` (in the repo: `content/`, with the pack in `content/gastro`; in a scratch session the scratch root with the
pack in `pack/`). Run commands from there with `python3 -I`; the tools are in `tools/book/` (adjust the paths below).

## What this is for

`src/Gastro.pdf` is 22 PDF pages of a Romanian medical textbook chapter (Kumar & Clark ch. 32). A game
teaches it for the Romanian residency exam. **Every printed character is exam material**: body text,
headings, box rows, tables, figure captions, the labels inside figures, credits, running headers, page
numbers, reading lists, and the OCR text that sits under white rectangles (hidden text). Nothing may be
dropped, summarised or "corrected" away. The book is taught exactly as printed: do **not** fix the book's
own mistakes (contradictions, wrong cross-references, odd wording); only fix **OCR** errors, and note the
book's own oddities in your notes file.

PDF page -> printed page: 1->1147, 2..6->1162..1166, 7..11->1172..1176, 12..22->1198..1208.

## Inputs and tools

- `src/words.json`: the text layer (poppler) as pages -> blocks -> lines -> words, each word with bbox (PDF
  points, origin top-left), `ink` (0 = no ink under the word in the rendered page = **hidden** text), font, size.
- `render/p-NN.png`: page renders at 150 dpi (1 pt = 150/72 px).
- `python3 -I tools/book/dumpblocks.py N` — every block of PDF page N: id, bbox, line count, hidden/total words, font, text.
- `python3 -I tools/book/dumplines.py N b012 b013` — lines of the given blocks with index, x, y, H = hidden.
- `python3 -I tools/book/dumpwords.py N y0 y1 [x0 x1]` — words in a band with coordinates (for `r(...)` sources).
- `python3 -I tools/book/crop.py N x0 y0 x1 y1 crops/NAME.png [dpi]` — crop a region (PDF points) at high dpi
  (220–300) and look at it with the Read tool. Use this for proofreading: never proofread from the 150-dpi render.
- `python3 -I tools/book/build.py pack --pages N,M --out build/aNN.json` — builds atoms for all pages that have a
  layout, reports **unused words** (must be 0 for your pages) and **fix count mismatches** (must be 0).
- `python3 -I tools/book/show.py build/aNN.json N M` — prints the atoms of pages N, M (⇥ = cell tab, ⏎ = newline).

Do **not** edit anything under `tools/` or other pages' files. If the DSL cannot express something, work
around it with `r(...)` sources and fixes, and say so in your notes.

## Your outputs (per PDF page NN, two digits)

1. `pack/layout/pNN.txt` — the page in reading order (DSL below).
2. `pack/fixes/pNN.json` — every OCR correction for that page (format below).
3. `pack/notes/pNN.md` — notes: errata candidates (the book's own contradictions / wrong cross-references / missing
   targets, with the exact printed words), doubtful tokens in hidden text, list of figures (id, bbox, what it shows),
   anything you were unsure about.

Look at `pack/layout/p01.txt`, `p02.txt`, `pack/fixes/p01.json`, `p02.json` first: they are the reference.

## Layout DSL (one segment per line; `#` starts a comment)

```
<kind> <source>... [key=value ...] [flags]
```
Sources: `b012` (whole block), `b012:3-7` (lines 3..7), `b012:5-` (line 5 to end), `b012:4` (one line),
`r(x0,y0,x1,y1)` (every not-yet-used word whose centre is in the rectangle, from any block, regrouped into lines),
`|` (cell separator in `row` / `toc`). Words are consumed in file order; a word can be used only once; **every
word of the page must be used by exactly one segment** (the build reports unused words).

Kinds:
| kind | atom kind | use |
|---|---|---|
| `hdr` | running_header | running header text + page number (+ the chapter tab "32", added by a fix). Give it `name=hdrPPPP` (printed page). |
| `h1`..`h6` | heading | headings; they set the heading path for what follows (path is global across pages) |
| `ctx lvl=N text="..."` | (none) | set a heading level without an atom, only when the heading is not printed on these pages |
| `p` | paragraph / list_item | text flow; auto-split into paragraphs at first-line indents and at lines starting with • or - |
| `li` | list_item | lists: one atom per bullet item; "-" sub-items get the parent bullet in their path |
| `bt` / `bh` / `be` | heading | box title (opens a box: e.g. `Cadranul 32.11 ...`), box sub-heading, box end (no source) |
| `row` | box_row / table_row | one table row: `row b017 \| b018 \| b019` (cells; a cell may have several sources) |
| `cap` | figure_caption | the full caption `Fig. 32.10 ...` (add `fig=fig_32_10`) |
| `lab` | figure_labels | the labels printed inside a figure (add `fig=fig_32_10 name=lab_32_10`) |
| `fig id=fig_32_10 bbox=x0,y0,x1,y1` | (none) | the figure's crop box in PDF points (the drawing/photo + its labels, not the caption) |
| `toc` | contents_line | contents-list line (page 1 only) |
| `ref` | reference | reading-list entries; use `split=` with the line index where each entry starts |
| `by` | byline | author line |
| `decor` | (none) | **only** OCR noise from graphics that are not text: box icons ("i", "■"), dotted rules, arrows read as `--->`, stray marks. Add `why="..."`. Never use it for printed words, numbers or letters. |

Keys / flags: `name=` (needed when a fix targets the segment), `split=i,j` (line indices inside the segment where a new
atom starts), `keep` (split only at `split=`), `nosplit` (one atom), `cont` (the first atom continues the previous
text atom — a paragraph broken by a column / page / box), `frag=start|end|both` (hidden passages that begin or end
mid-sentence), `fig=` (link caption / labels to a figure), `path=` (extra path element).

### Reading order

- Main text: left column top to bottom, then right column; a paragraph broken by a column or page break continues
  with `cont` on the segment that holds its continuation (check the previous page's last paragraph!).
- A box (`Cadranul ...`), figure or table goes **right after the paragraph that first cites it** if that paragraph is on
  the same page or earlier; otherwise at the end of the page's main flow (before the running header of the next page is
  irrelevant — the header goes first on each page). The box's heading path is the path where it is placed.
- Running header first on every page (`hdr`), then everything else.

### Heading levels (by printed style; the path is global, so levels must be consistent)

- `h1` chapter title (page 1 only: "32 Gastroenterologie").
- `h2` ALL-CAPS section in a band: FARINGELE ȘI ESOFAGUL, STOMACUL ȘI DUODENUL, BOLILE INFLAMATORII INTESTINALE,
  COLONUL ȘI RECTUL, GLANDELE SALIVARE.
- `h3` mixed-case title in a grey/coloured band or the big mixed-case section title: Boala de reflux gastro-esofagian,
  Gingiile, Dinții, Tulburările de motilitate, Gastrita și gastropatia, Infecția cu Helicobacter pylori, Boala Crohn,
  Colita ulcerativă, Colita microscopică, Constipația ... (if a title is in a band like these, it is h3).
- `h4` coloured bold heading (green / orange / dark-red bold, mid size): Epidemiologie și fiziopatologie, Tablou clinic,
  Tratament, Diagnostic și investigații paraclinice, Acalazia, Globusul ...
- `h5` smaller coloured / blue heading below an h4: "Medicamente utilizate".
- `h6` black bold heading below that: "Preparatele pe bază de alginat", "Inhibitorii pompei de protoni".
When unsure, compare with the styles already used on pages 1–3 (`render/p-02.png`, `render/p-03.png`) and keep
the hierarchy logical (a sub-heading must be deeper than its parent). Put your decisions in your notes.

### Boxes and tables

`bt` title, then rows (`row a | b | c`, header row included, empty first cell allowed: `row | b015 | b016`) or list
items (`li ...`) or `p`, sub-headings `bh`, then `be`. Inside a cell, lines that start at the cell's left margin begin a new
entry (hanging indent) automatically. Two-column bullet lists inside a box: one `li` segment with the sources in reading
order (left column then right column).

### Figures

For every figure: `fig id=fig_32_NN bbox=...` (whole drawing incl. its labels and panel letters), `cap ... fig=fig_32_NN`,
and `lab r(...) fig=fig_32_NN name=lab_32_NN` consuming all the OCR words inside the figure area. Then in the fixes
replace the whole label atom with your transcription from the image (`"find": "*"`): every printed label, panel letter
((A), (B)), axis title, tick value, legend and number inside the figure, in reading order, separated by a TAB (`\t`);
multi-line labels joined with a space. If the figure has no OCR words, still write `lab fig=... name=...` (no source)
and fill it with `"find": "*"`. Photos with no printed text: still a `lab` with the panel letters.

## Fixes (`pack/fixes/pNN.json`, a JSON list)

```json
{"id": "p03-01", "pdf_page": 3, "find": "BAGE", "replace": "BRGE", "count": 4, "why": "known OCR error R→A (image: BRGE)"}
{"id": "p03-02", "pdf_page": 3, "seg": "lab_32_10", "find": "*", "replace": "Fornix\tCardia\t...", "why": "figure labels transcribed from the image"}
{"id": "p06-09", "pdf_page": 6, "find": "Sjiigren", "replace": "Sjögren", "layer": "under_blank", "status": "unverified", "why": "hidden; OCR ii for ö"}
```
- `find` is matched in the atoms of that PDF page (the text you see in show.py, after diacritic normalisation:
  ș ț already comma-below; soft hyphens already removed). `count` = exact number of replacements on that page
  (default 1; the build fails the fix otherwise). Make `find` long enough to be unique.
- Already handled globally, do not fix: ş/ţ → ș/ț, numeric ranges 10-20 → 10–20 (rule R1), closing quote " → ” after „ (rule R2).
- Known OCR patterns: BAGE→BRGE, ATSEI→RTSEI, DR8*0103→DRB*0103, Prebioticul→Probioticul, `800 11g`→`800 µg`,
  `2 0-50%`→`20–50%`, 18D→IBD, barita!→baritat, `{` `}` for `(` `)`, `0` for `D`, `!` for `l` or `ț`, `I`/`l` confusion,
  `Ph` for `pH`, `�` = unreadable glyph, lost diacritics in headings (SI→ȘI), missing bullets, missing words, joined words
  (STOMACULSIDUODENUL), extra spaces inside words. Typographic emphasis (bold/italic) is not text: ignore it.
- Hidden text (ink = 0) cannot be checked against the image: fix only what is certain from the word itself
  (garbled letters, lost diacritics) and mark it `"layer": "under_blank", "status": "unverified"`; list every doubtful
  token (even ones you did not change) in your notes.
- Text printed in the image but missing from the text layer (chapter tab "32", a dropped word, a dropped line): add it
  with a fix whose `find` is a neighbouring anchor and `replace` = anchor + the added text.

## Proofreading procedure (for every visible region of the page)

Crop the region at 220–300 dpi in pieces about 200 pt tall (one column at a time), print the atoms with show.py, and
compare **word by word**, including numbers, units, %, doses, signs (<, >, ±), Greek letters, superscripts (H2,
13C, CD4), italics names, punctuation inside numbers (1,4 vs 1.4), and diacritics. Every difference is a fix.
Then rebuild and re-read the corrected atoms against the crop once more. A page is done when: 0 unused words,
0 fix mismatches, every atom re-read against the image, labels of every figure transcribed, notes written.

# BOOK-1 Phases B–D — authoring guide (facts → items → scripts, one topic at a time)

Scratch root `S=/tmp/claude-0/-home-user-mobilegame/efc69998-e2ae-5f1c-9689-c296999c2d7f/scratchpad`; run everything from
`$S` with `python3 -I`. The reference topic is **`tools/book/topics/intro.py`** — read it completely before you start;
the author approved its style. API: `tools/book/content.py` (read the docstrings).

## What the game is

Rezi teaches a Romanian textbook chapter (Kumar & Clark ch. 32, Gastroenterologie) for the residency exam
(rezidențiat). **Every character of the book is exam material.** Each topic is a room of the institute map; the player
must answer every fact of the room correctly at least once to complete it.

Three rules that never bend:
1. **The book is the only source.** No fact, number, dose, name or explanation from your own knowledge. Every correct
   answer and every explanation traces to a span of the atoms of your topic. Wrong options are book material too.
2. **Taught as printed.** Do not correct the book. Typos and contradictions are in `pack/errata.json`; facts that touch
   one are flagged automatically (`needs_review`), and you keep the printed wording. Hidden text (layer `under_blank`) is
   taught exactly as it stands (flag `unverified_ocr` is automatic). A passage that ends where the PDF ends
   (`fragment` set, or a section cut by a missing page) is taught as it stands: never write an item that claims the
   section is complete beyond what is printed.
3. **Report only what you measured** (counts from compile / check).

## Tools

```
python3 -I tools/book/topicatoms.py pack TOPIC        # your atoms, every sentence with its index, errata on them
python3 -I tools/book/roominfo.py pack ROOM            # room bounds, doors, windows, slots, props already placed
python3 -I tools/book/compile.py pack TOPIC --no-merge # run your module -> pack/topics/TOPIC.json (prints weak facts)
python3 -I pack/check.py pack --from-topics --topics TOPIC   # must end with CHECK PASSED: 100 %
```
Write only `tools/book/topics/<topic>.py` for your topics (and nothing else; other helpers work in parallel).

## Phase B — facts (`T.fact`)

`T.fact(key, atom, quote, claim, type, keys=[...])` — `quote` is the exact text span (unique in the atom; `occ=n` for the
n-th occurrence; an int = that sentence; `None` = whole atom). `claim` restates it in Romanian **in the book's own words**.
- **Sentence ledger:** every sentence of every atom (see topicatoms) lies inside at least one fact span → for every
  sentence write one *whole-sentence* fact (quote = the sentence index), then the component facts.
- **Numbers:** every number token of a sentence must appear in the claim (or `keys`) of a fact overlapping it. A sentence
  with four numbers → at least four facts (one per number / dose / %).
- **Lists** (bullets, enumerations inside a sentence, box columns): one fact per member + one *set* fact for the whole list.
- **Box rows** (`box_row`, cells separated by TAB, entries by newline): a fact per cell relation (row header ↔ each cell)
  + a whole-row fact. **Headings**: a structure fact (title, which section it opens, its page / what it is under).
- **Figure captions**: like paragraphs. **Figure labels** (`figure_labels`, labels separated by TAB): a whole-atom fact +
  one fact per label (what the label names in that figure, from the caption / text; if the book does not say what a label
  means, the fact is only "Fig. X shows the label «…»" — do not explain from your own knowledge).
- **References** (reading lists): facts for authors, title, journal, year, volume / pages (one per element) + whole entry.
- **Running headers / contents lines** (topic `cuprins`): the printed text and page; contents page numbers that differ from
  where the heading is printed are both facts (errata flag the contents line automatically).
- Types: statement, number, dose, name, definition, cause, sign, indication, contraindication, sequence, comparison,
  exception, set, member, structure, label, reference, example, detail.

## Phase C — items (approved style; check.py enforces it)

An item **tests** a fact only when its answer depends on it (keyed answer, a statement judged true/false, a correct option,
a gap, a card answer, an entry / pair / position). Wrong options never count.
- **Every fact is tested by ≥ 2 items of different types, and by ≥ 1 exam item** (`simplu` or `multiplu`).
  If a fact cannot get four wrong options from the book (page → section → topic), cover it with a `multiplu_tf` statement;
  only if even that is impossible, `T.exam_exempt(fact, reason)` (it is reported to the author).
- `T.simplu(key, q, stem, (correct_text, 'factkeys'), [(wrong, 'from_fact'), x4], note, (atom, sent))` — `q` is a
  question key `relation:subject` in ASCII (e.g. `factor_asociat:brge`, `doza:omeprazol_intretinere`). The correct text is
  registered as an answer to q for those facts; **a wrong option must not be an answer to q anywhere in the book** — if
  a list in your topic answers q, register every member with `T.answers(q, fact, text)`. Wrong options come from sibling
  facts (page → section → topic) and are themselves book text.
- `T.multiplu(key, q, stem, [(correct, 'facts'), ...2-4], [(wrong, 'fact that shows it is wrong'), ...], note, src)` —
  list style ("Care dintre următoarele sunt …"). Every wrong option names the fact whose sentence makes it wrong (quoted
  automatically with its page).
- `T.multiplu_tf(key, stem, [(statement, True, 'fact'), (statement, False, 'refuting fact'), x5], note, src)` — 2–4 true.
  A false statement is a book fact with one element swapped for a sibling's (subject, number, category) or the scope
  changed ("numai", "exclusiv"); it must not equal any fact's claim.
- **Vary multiplu correct counts:** in a topic with ≥ 3 multiplu items, 2, 3 and 4 correct each used.
- `T.cloze(key, 'facts', text_with_one____gap, [answer], note, src)` — the gap is the tested token (number, name, term,
  dose, unit), never a function word; **one gap per item** unless the sentence is a list (`list_gap=True`). The answer must
  appear in the source sentence.
- `T.card`, `T.sort(key, stem, groups, [(text, group, 'fact')])`, `T.match(key, stem, [(left, right, 'fact')])`,
  `T.order(key, 'facts', stem, sequence)`, `T.label(key, stem, 'fig_32_NN', [(label, 'fact')])` (figure with blanks).
- **No cues:** the correct option must not be the conspicuously longest; a content word of the stem must not reappear only
  in the correct option(s); options of one item share one form (all noun phrases, or all full statements ending with a
  period, or all numbers); grammar of the stem must fit every option (prefer stems ending in ":" with neutral agreement).
- Stems unique within the book; Romanian clinical register; patient framing ("Bărbat, 54 de ani…") allowed as wrapping
  when the answer rests only on the fact.
- `note` (optional) is a short explanation in Romanian; the source sentence and printed page are appended automatically
  (`src = (atom_id, sentence_index)`; sentence `None` = the whole atom, e.g. for box rows / labels).
- Item keys: `s01…` simplu, `m01…` multiplu, `z01…` cloze, `c01…` card, `t01…` sort, `p01…` match, `o01…` order, `l01…` label.

## Phase D — world (`T.prop`, `T.slot`, `T.script`)

The topic's room is fixed (see `pack/gastro-quests.json`); never move a topic or change a room id. Read
`roominfo.py pack ROOM`: bounds in metres, x east, **z south**, yaw 0 faces +z (south), 90 east, 180 north, 270 west.
- Add 2–6 interactable props (`T.prop(floor, room, '<topic>_<name>', model, x, z, yaw, 'Romanian label')`) and NPC slots
  (`T.slot`) inside the room: ≥ 0.4 m from walls, not within 1 m of a door centre or in front of a window, ≥ 1.2 m apart,
  not on existing slots. Models (reuse these names): `poster`, `wall_plaque`, `lectern`, `whiteboard`, `lightbox`
  (X-ray / figure viewer), `monitor`, `scope_tower`, `microscope`, `specimen_shelf`, `pharmacy_shelf`, `dispensing_counter`,
  `bed_chart`, `iv_stand`, `bookshelf`, `filing_cabinet`, `model_stomach`, `or_screen`, `lab_bench`, `breath_analyser`.
- Scripts (`T.script(sid, kind, room, title, steps, prop=… | slot=…, npc=…)`): steps are `('predict', item)`,
  `('teach', [atom ids])`, `('check', item)`. Kinds: **`station`** (exactly one per topic; prediction → teach → check, at the
  room's main slot from the map or your own slot), `case` (a patient chart at a bed / desk), `inspect` (one prop = one
  piece: a box, a figure on a lightbox, a poster), `dispense` (drugs, doses, order of escalation), `read` (exact wording),
  `sequence`, `sort`, `directory` (structure, page numbers), `colleague` (NPC dialogue).
- **Every fact of the topic must be tested by a step of a `required=True` script in the topic's room.** Spread the
  topic over the room: station = the core, the rest on the props, so covering a topic means exploring the room.
- Shared rooms: give each topic its own props / slots and its own station; check `roominfo.py` for what is already placed.

## Done means (per topic)

`compile.py … --no-merge` prints no weak facts and no facts without an exam item, and
`check.py pack --from-topics --topics <topic>` ends with `CHECK PASSED: 100 %`. Then the next topic.
Final reply: per topic the measured counts (facts, items by type, scripts, props, slots, exam_exempt facts) and anything
you could not do.

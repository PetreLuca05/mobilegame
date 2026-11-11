Decisions (author's answers of the Phase A check-in, and my choices where the author delegated them under the rule
"book as printed, nothing dropped, flag instead of correct; the option that keeps the most printed text and changes it
the least"). The author may overrule any line.

1. **Public repository** — author: stays public; no book text, fixes, questions or figures in any commit or in the branch
   history; content only in the zip. Done: branch history rewritten, leak guard (12-word runs) in check.py `--leak` and in
   the pre-commit hook.
2. **Check-in 1 (numbers, hidden text)** — author reviews in parallel; corrections arrive as fixes; `check.py` lists every
   atom, fact and item whose source text changed (stale section) so only those are rebuilt.
3. **Check-in 2 (question style)** — approved with §2 a–h; applied to `intro` and enforced by `check.py` for every topic.
4. **"mortalitate puțin mare"** — author: not an erratum; p. 1207 is taught as printed ("o mortalitate puțin mai mare").
5. **Passages ending on a full stop (Acalazia p. 1166, Gastropareza p. 1176)** — author: `fragment` stays null, taught as
   they stand; no item claims the section is complete beyond what is printed (rule in AUTHORING_GUIDE.md).
6. **Fig. 32.15 order** — author: the order printed in the figure. The figure prints its own step numbers 1–6, so the label
   group and the `order` items follow 1 → 6 (no `needs_review` needed).
7. **Not text (decor)** — choice: keep the 14 decor entries out of the atoms (box icons "i" / "■", the dotted rule between
   the contents columns, stray marks): none is a printed letter, number or word, so nothing printed is dropped and no title
   is changed. The illegible mark under panel B of Fig. 32.23 cannot be transcribed: kept out, and the figure's label items
   are flagged `needs_review` (erratum E62).
8. **Running headers** — choice: atoms of topic `cuprins`, taught at the atrium board with the contents list (page ↔ header
   text); nothing dropped or merged.
9. **Typography** — choice: numeric ranges with the en dash as printed (rule R1; not applied to hidden text; the 3 ranges
   printed with a hyphen are restored); superscripts / subscripts written inline (H2, 13C, B12, E1: every character kept,
   only its position lost); bold / italic not kept (no character lost).

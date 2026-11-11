#!/usr/bin/env python3
"""§2: every short keyed answer (simplu keyed option, list-style multiplu options, match pairs, sort entries, order steps,
cards of at most 6 words) that is not a verbatim span of its source, classified by wordrule.py: shortened (printed
words, printed order, one run), inflection (printed words, some inflected or reordered), reworded (a word the source does
not print; only items kept on purpose, flagged needs_review).
  inflections.py PACK -> PACK/inflections.json (+ counts)"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wordrule as W


def main(pack):
    A = {json.loads(l)['id']: json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')}
    F = {json.loads(l)['id']: json.loads(l) for l in open(os.path.join(pack, 'facts.jsonl'), encoding='utf-8')}
    I = [json.loads(l) for l in open(os.path.join(pack, 'items.jsonl'), encoding='utf-8')]
    book = W.norm('\n'.join(a['text'] for a in A.values()))
    out, n = [], 0
    for it in I:
        src = W.source_text(it, F, A)
        shown = [(x['used'], x['printed']) for x in it.get('typo_shown', [])]
        for path, txt in W.short_answers(it):
            n += 1
            k, miss = W.classify(txt, src, shown, book)
            if k != 'verbatim':
                out.append({'item': it['id'], 'type': it['type'], 'used_form': txt, 'kind': k, 'not_printed': miss})
    json.dump(out, open(os.path.join(pack, 'inflections.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    c = {}
    for o in out:
        c[o['kind']] = c.get(o['kind'], 0) + 1
    print(f'short answers {n}; not verbatim {len(out)}: {c}')


if __name__ == '__main__':
    main(sys.argv[1])

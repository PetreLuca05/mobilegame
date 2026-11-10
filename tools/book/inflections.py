#!/usr/bin/env python3
"""§2: answers of non-cloze items (keyed option, list-style multiplu option, pair, sort entry, order step, short card)
that are not verbatim in the book: for each, the printed form (closest span of the source atoms) and the used form.
  inflections.py PACK -> PACK/inflections.json (+ counts)"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import typoguard

W = re.compile(r'[0-9A-Za-zĂÂÎȘȚăâîșțöüéαβγ\-\']+')


def low(s):
    return s.lower().replace('ş', 'ș').replace('ţ', 'ț')


def stem(w):
    w = low(w)
    return w[:max(4, int(len(w) * 0.6))] if len(w) > 4 else w


def main(pack):
    A = {json.loads(l)['id']: json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')}
    F = {json.loads(l)['id']: json.loads(l) for l in open(os.path.join(pack, 'facts.jsonl'), encoding='utf-8')}
    I = [json.loads(l) for l in open(os.path.join(pack, 'items.jsonl'), encoding='utf-8')]
    out = []
    checked = 0
    for it in I:
        t = it['type']
        fields = []
        if t == 'simplu' or (t == 'multiplu' and it.get('style') == 'list'):
            fields = [o['t'] for o in it['options'] if o.get('ok')]
        elif t == 'match':
            fields = [p['left'] for p in it['pairs']] + [p['right'] for p in it['pairs']]
        elif t == 'sort':
            fields = [e['t'] for e in it['entries']]
        elif t == 'order':
            fields = list(it['sequence'])
        elif t == 'card' and len(W.findall(it['answer'])) <= 6:
            fields = [it['answer']]
        book = ' '.join(A[F[f]['atom']]['text'] for f in it['fact_ids'] if f in F) + ' ' + it['source']['quote']
        shown = {x['used']: x['printed'] for x in it.get('typo_shown', [])}
        for txt in fields:
            checked += 1
            probe = txt.rstrip('.')
            for used, printed in shown.items():
                probe = probe.replace(used, printed)
            if low(probe) in low(book):
                continue
            words = W.findall(probe)
            bw = [(m.group(0), m.start(), m.end()) for m in W.finditer(book)]
            hits = []
            for w in words:
                cand = [b for b in bw if stem(b[0]) == stem(w)] or [b for b in bw if low(b[0])[:4] == low(w)[:4] and len(w) >= 4]
                if cand:
                    hits.append(cand[0])
            kind = 'inflection' if words and len(hits) >= max(1, int(0.75 * len(words))) else 'reworded'
            printed = ''
            if hits:
                s0 = min(h[1] for h in hits)
                s1 = max(h[2] for h in hits)
                if s1 - s0 <= 3 * len(probe) + 20:
                    printed = book[s0:s1]
            out.append({'item': it['id'], 'type': t, 'printed_form': printed, 'used_form': txt, 'kind': kind})
    json.dump(out, open(os.path.join(pack, 'inflections.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    k = {}
    for o in out:
        k[o['kind']] = k.get(o['kind'], 0) + 1
    print(f'answers checked {checked}; not verbatim {len(out)}: {k}')


if __name__ == '__main__':
    main(sys.argv[1])

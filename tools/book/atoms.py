#!/usr/bin/env python3
"""BOOK-1 Phase A, step 2: build atoms -> cleaned.txt + atoms.jsonl (+ topic assignment).

Usage: atoms.py PACK
Reads PACK/../build/atoms_raw.json (from build.py, all pages), PACK/topic_rules.json, PACK/gastro-quests.json.
Writes PACK/cleaned.txt, PACK/atoms.jsonl.

cleaned.txt = every atom's text in reading order, atoms separated by a blank line. Inside an atom: table cells are
separated by TAB, entries inside a cell by a newline. src_span = [start, end) character offsets into cleaned.txt.
sent_spans = [start, end) offsets *inside the atom text*.
"""
import json, os, re, sys

PDF2PRINT = {1: 1147}
for i, p in enumerate(range(1162, 1167)): PDF2PRINT[2 + i] = p
for i, p in enumerate(range(1172, 1177)): PDF2PRINT[7 + i] = p
for i, p in enumerate(range(1198, 1209)): PDF2PRINT[12 + i] = p

# abbreviations after which a period does not end a sentence (lower-cased, without the period)
ABBR = {'ex', 'p', 'pp', 'fig', 'vs', 'nr', 'cf', 'dr', 'prof', 'al', 'et', 'aprox', 'max', 'min', 'resp', 'sp', 'spp',
        'i.v', 'iv', 's.c', 'p.o', 'i.m', 'mg', 'ml', 'cca', 'n', 'v', 'vol', 'ed', 'eds', 'st', 'dl', 'ș.a', 'etc'}
SENTENCE_KINDS = {'paragraph', 'list_item', 'figure_caption', 'byline'}


def split_sentences(t):
    """-> list of (start, end) spans covering every non-space character of t"""
    spans = []
    start = 0
    i = 0
    n = len(t)
    while i < n:
        ch = t[i]
        if ch in '.!?':
            j = i + 1
            while j < n and t[j] in '.”"\')]}':
                j += 1
            if j < n and t[j] == ' ':
                k = j
                while k < n and t[k] == ' ':
                    k += 1
                nxt = t[k] if k < n else ''
                m = re.search(r'([\w.\-]+)$', t[start:i])
                prev = m.group(1).lower() if m else ''
                is_abbr = prev in ABBR or prev.rstrip('.') in ABBR or re.fullmatch(r'[a-zăâîșț]', prev) is not None \
                    or (re.fullmatch(r'[A-ZĂÂÎȘȚ]{1,3}', m.group(1)) is not None and t[i] == '.' and nxt[:1].islower() if m else False)
                single_initial = m is not None and re.fullmatch(r'[A-Z]', m.group(1)) is not None
                if (nxt[:1].isupper() or nxt[:1].isdigit() or nxt[:1] in '„"(•–-') and not is_abbr and not single_initial:
                    spans.append((start, j))
                    start = k
                    i = k
                    continue
        i += 1
    if start < n and t[start:].strip():
        spans.append((start, n))
    # trim spaces
    out = []
    for a, b in spans:
        while a < b and t[a] == ' ':
            a += 1
        while b > a and t[b - 1] == ' ':
            b -= 1
        if a < b:
            out.append([a, b])
    return out


def assign_topics(atoms, rules):
    """rules: list of {"on": "id"|"fig"|"kind"|"path", "match": str, "topic": str}.
    Order of precedence: id, fig, kind, then path rules. A path rule "A > B > C" matches when A, B, C occur in that order
    in the atom's path (+ its own text for a heading); the most specific matching rule wins (more elements, then the one
    whose last element sits deepest in the path)."""
    by_id = {r['match']: r['topic'] for r in rules if r['on'] == 'id'}
    by_fig = {r['match']: r['topic'] for r in rules if r['on'] == 'fig'}
    by_kind = {r['match']: r['topic'] for r in rules if r['on'] == 'kind'}
    path_rules = [([x.strip() for x in r['match'].split(' > ')], r['topic']) for r in rules if r['on'] == 'path']

    def eq(el, m):
        return el == m or (m.endswith('*') and el.startswith(m[:-1]))

    def match(path, elems):
        i = 0
        last = first = -1
        for j, el in enumerate(path):
            if i < len(elems) and eq(el, elems[i]):
                if i == 0:
                    first = j
                i += 1
                last = j
        return (last, len(elems), first) if i == len(elems) else None

    for a in atoms:
        t = by_id.get(a['id'])
        if not t and a.get('fig'):
            t = by_fig.get(a['fig'])
        if not t and a['kind'] in by_kind:
            t = by_kind[a['kind']]
        if not t:
            path = list(a['path'])
            if a['kind'] == 'heading' and (not path or path[-1] != a['text']):
                path.append(a['text'])
            best = None
            for elems, tp in path_rules:
                key = match(path, elems)
                if key is not None:
                    if best is None or key > best[0]:
                        best = (key, tp)
            t = best[1] if best else None
        a['topic'] = t


def main(pack):
    raw = json.load(open(os.path.join(pack, '..', 'build', 'atoms_raw.json'), encoding='utf-8'))
    rules = json.load(open(os.path.join(pack, 'topic_rules.json'), encoding='utf-8')) if os.path.exists(os.path.join(pack, 'topic_rules.json')) else []
    atoms = []
    seq = {}
    cleaned = []
    pos = 0
    for r in raw:
        page = PDF2PRINT[r['pdf_page']]
        seq[page] = seq.get(page, 0) + 1
        text = r['_text']
        kind = r['kind']
        if kind == 'paragraph' and text[:1] in '•■':
            kind = 'list_item'
        hidden, words = r['_hidden'], r['_words']
        layer = 'visible' if hidden == 0 else ('under_blank' if hidden == words else 'mixed')
        if words == 0:
            layer = 'image_only'      # transcribed from the page image (figure labels, chapter tab ...)
        frag = r.get('frag')
        a = {
            'id': f'a{page}-{seq[page]:03d}',
            'pdf_page': r['pdf_page'], 'page': page,
            'pages': sorted(set(PDF2PRINT[p] for p in r['pdf_pages'])),
            'path': r['path'], 'kind': kind, 'layer': layer,
            'fragment': {'start': 'start', 'end': 'end', 'both': 'both'}.get(frag) if frag else None,
            'unverified_ocr': hidden > 0,
            'text': text,
            'in_box': r.get('in_box'), 'fig': r.get('fig'), 'lvl': r.get('lvl'),
            'segs': r['segs'],
        }
        a['src_span'] = [pos, pos + len(text)]
        a['sent_spans'] = split_sentences(text) if kind in SENTENCE_KINDS else ([[0, len(text)]] if text.strip() else [])
        cleaned.append(text)
        pos += len(text) + 2
        atoms.append(a)
    assign_topics(atoms, rules)
    with open(os.path.join(pack, 'cleaned.txt'), 'w', encoding='utf-8') as f:
        f.write('\n\n'.join(cleaned) + '\n')
    with open(os.path.join(pack, 'atoms.jsonl'), 'w', encoding='utf-8') as f:
        for a in atoms:
            f.write(json.dumps(a, ensure_ascii=False) + '\n')
    return atoms


if __name__ == '__main__':
    atoms = main(sys.argv[1])
    n = len(atoms)
    nt = sum(1 for a in atoms if a['topic'])
    print(f'atoms {n}, with topic {nt}, sentences {sum(len(a["sent_spans"]) for a in atoms)}')

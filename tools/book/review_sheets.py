#!/usr/bin/env python3
"""§5 review sheets for the author (book text: they travel in the zip, never in git).
  review_sheets.py PACK -> PACK/review_errata.csv, PACK/review_hidden.csv
review_errata.csv: id, kind, page, printed, note, items_touching, decision (empty), item_ids
  decision: keep | not_an_error | correct: <the reading to teach instead>
review_hidden.csv: pdf_page, page, token, sentence, best_reading, items_touching, ok (empty), source, item_ids
  ok: y (the best reading is right) | <the right reading>
Both sorted by items touched, most first. `check.py PACK --apply-review` applies a filled sheet."""
import collections, csv, json, os, re, sys

ODD = re.compile(r'(?=.*[A-Za-zĂÂÎȘȚăâîșț])(?:[0-9][A-Za-zĂÂÎȘȚăâîșț]|[A-Za-zĂÂÎȘȚăâîșț][0-9]|[/!{}�•\\|]|[a-zăâîșț][A-ZĂÂÎȘȚ][a-zăâîșț]|[a-zăâîșț]ii[a-kmo-zăâîșț]|[şţŞŢ])')


def load(pack):
    J = lambda n: [json.loads(l) for l in open(os.path.join(pack, n), encoding='utf-8')]
    return J('atoms.jsonl'), J('facts.jsonl'), J('items.jsonl'), json.load(open(os.path.join(pack, 'errata.json'), encoding='utf-8'))


def items_by_fact(items):
    m = collections.defaultdict(set)
    for it in items:
        for f in it['fact_ids']:
            m[f].add(it['id'])
    return m


def sentence_at(a, pos):
    for s, e in a.get('sent_spans', []):
        if s <= pos < e:
            return a['text'][s:e]
    return a['text']


def errata_rows(atoms, facts, items, errata):
    ibf = items_by_fact(items)
    rows = []
    for e in errata:
        touch = set()
        for f in facts:
            if f.get('needs_review') and re.search(r'(?<![0-9A-Z])' + e['id'] + r':', f['needs_review']):
                touch |= ibf[f['id']]
        for it in items:
            if any(x['erratum'] == e['id'] for x in it.get('typo_shown', [])) or e['id'] + ':' in (it.get('review_note') or '') \
                    or re.search(r'\b' + e['id'] + r'\b', it.get('review_note') or ''):
                touch.add(it['id'])
        rows.append({'id': e['id'], 'kind': e['kind'], 'page': e['page'], 'printed': e['printed'], 'note': e['issue'],
                     'items_touching': len(touch), 'decision': '', 'item_ids': ' '.join(sorted(touch))})
    rows.sort(key=lambda r: (-r['items_touching'], r['id']))
    return rows


def hidden_rows(pack, atoms, facts, items):
    fx = json.load(open(os.path.join(pack, 'fixes.json'), encoding='utf-8'))['fixes']
    ibf = items_by_fact(items)
    fb = collections.defaultdict(list)
    for f in facts:
        fb[f['atom']].append(f)
    hid = [a for a in atoms if a['layer'] in ('under_blank', 'mixed')]
    rows, seen = [], set()

    def touching(a, pos, n):
        t = set()
        for f in fb[a['id']]:
            if f['span'][0] < pos + n and pos < f['span'][1]:
                t |= ibf[f['id']]
        return t

    for f in fx:                                   # 1. every fix made in hidden text that no print can confirm
        if f.get('status') != 'unverified' or f['find'] == '*':
            continue
        for a in hid:
            if f['pdf_page'] != a['pdf_page'] or f['replace'] not in a['text']:
                continue
            pos = a['text'].find(f['replace'])
            while pos >= 0:
                key = (a['id'], pos)
                if key not in seen:
                    seen.add(key)
                    t = touching(a, pos, len(f['replace']))
                    rows.append({'pdf_page': f['pdf_page'], 'page': a['page'], 'token': f['find'], 'sentence': sentence_at(a, pos),
                                 'best_reading': f['replace'], 'items_touching': len(t), 'ok': '', 'source': f'fix {f["id"]}: {f.get("why", "")}',
                                 'item_ids': ' '.join(sorted(t)), 'atom': a['id'], 'pos': pos})
                pos = a['text'].find(f['replace'], pos + 1)
    for a in hid:                                  # 2. tokens that still look odd (digits in words, stray symbols, case)
        for m in re.finditer(r'\S+', a['text']):
            tok = m.group(0).strip('.,;:()„”"')
            if not tok or not ODD.search(tok) or re.fullmatch(r'[\d.,–%<>=/+-]+', tok):
                continue
            pos = a['text'].find(tok, m.start())
            if any(k[0] == a['id'] and abs(k[1] - pos) < 3 for k in seen):
                continue
            seen.add((a['id'], pos))
            t = touching(a, pos, len(tok))
            rows.append({'pdf_page': a['pdf_page'], 'page': a['page'], 'token': tok, 'sentence': sentence_at(a, pos),
                         'best_reading': tok, 'items_touching': len(t), 'ok': '', 'source': 'machine list: unusual characters, kept as read',
                         'item_ids': ' '.join(sorted(t)), 'atom': a['id'], 'pos': pos})
    # 3. words of the hidden text that the print never confirms (not printed anywhere in the visible text)
    W = re.compile(r"[A-Za-zĂÂÎȘȚăâîșțÀ-ÿ'’-]+")
    vis = {w.lower() for a in atoms if a['layer'] == 'visible' for w in W.findall(a['text'])}
    for a in hid:
        for m in W.finditer(a['text']):
            tok = m.group(0).strip("-'’")
            if not tok or tok.lower() in vis or any(k[0] == a['id'] and abs(k[1] - m.start()) < 3 for k in seen):
                continue
            seen.add((a['id'], m.start()))
            t = touching(a, m.start(), len(tok))
            rows.append({'pdf_page': a['pdf_page'], 'page': a['page'], 'token': tok, 'sentence': sentence_at(a, m.start()),
                         'best_reading': tok, 'items_touching': len(t), 'ok': '', 'source': 'not printed in the visible text: no print confirms it',
                         'item_ids': ' '.join(sorted(t)), 'atom': a['id'], 'pos': m.start()})
    rows.sort(key=lambda r: (-r['items_touching'], r['pdf_page'], r['pos']))
    return rows


ERR_COLS = ['id', 'kind', 'page', 'printed', 'note', 'items_touching', 'decision', 'item_ids']
HID_COLS = ['pdf_page', 'page', 'token', 'sentence', 'best_reading', 'items_touching', 'ok', 'source', 'item_ids', 'atom', 'pos']


def main(pack):
    atoms, facts, items, errata = load(pack)
    er = errata_rows(atoms, facts, items, errata)
    hr = hidden_rows(pack, atoms, facts, items)
    for name, cols, rows in (('review_errata.csv', ERR_COLS, er), ('review_hidden.csv', HID_COLS, hr)):
        p = os.path.join(pack, name)
        old = {}
        if os.path.exists(p):                      # keep decisions already written
            for r in csv.DictReader(open(p, encoding='utf-8-sig')):
                k = r.get('id') or (r.get('atom'), r.get('pos'))
                old[k] = r.get('decision') or r.get('ok') or ''
        with open(p, 'w', encoding='utf-8-sig', newline='') as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for r in rows:
                k = r.get('id') or (r.get('atom'), str(r.get('pos')))
                if old.get(k):
                    r['decision' if 'decision' in r else 'ok'] = old[k]
                w.writerow(r)
    print(f'review_errata.csv {len(er)} rows ({sum(1 for r in er if r["items_touching"])} touching items); '
          f'review_hidden.csv {len(hr)} rows ({sum(1 for r in hr if r["items_touching"])} touching items)')
    return er, hr


if __name__ == '__main__':
    main(sys.argv[1])

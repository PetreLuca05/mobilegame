#!/usr/bin/env python3
"""List the facts whose only testable token is a misprint (typo errata): their items stay, flagged needs_review.
  typofacts.py PACK -> PACK/typo_only_facts.json"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import typoguard
STOP = set('a ai al ale ca care cu de din după e este fi în la le lui mai o pe pentru prin sau se și un una unei unor unui sunt'.split())


def words(s):
    return [w for w in re.findall(r'[0-9A-Za-zĂÂÎȘȚăâîșțöüé\-]+', s) if w.lower() not in STOP and len(w) >= 3]


def main(pack):
    T = typoguard.load_tokens()
    E = {e['id']: e for e in json.load(open(os.path.join(pack, 'errata.json'), encoding='utf-8'))}
    out = []
    for l in open(os.path.join(pack, 'facts.jsonl'), encoding='utf-8'):
        f = json.loads(l)
        hits = [(eid, tok) for eid, toks in T.items() for tok, _ in toks if typoguard.find(tok, f['text'])]
        if not hits:
            continue
        rest = f['text']
        for _, tok in hits:
            rest = re.sub(r'(?<!\w)' + re.escape(tok) + r'(?!\w)', ' ', rest, flags=re.I)
        if not words(rest):
            out.append({'fact': f['id'], 'text': f['text'], 'errata': [h[0] for h in hits],
                        'note': 'its only testable token is a misprint: items kept, needs_review'})
    json.dump(out, open(os.path.join(pack, 'typo_only_facts.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('facts whose only testable token is a misprint:', len(out))
    for o in out:
        print(' ', o['fact'], repr(o['text'][:60]), o['errata'])


if __name__ == '__main__':
    main(sys.argv[1])

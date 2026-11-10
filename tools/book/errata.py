#!/usr/bin/env python3
"""errata.json from tools/book/data/errata_src.py: every printed snippet is located in the atoms (must be found)."""
import json, os, sys, importlib.util
HERE = os.path.dirname(os.path.abspath(__file__))


def main(pack):
    spec = importlib.util.spec_from_file_location('es', os.path.join(HERE, 'data', 'errata_src.py'))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    tspec = importlib.util.spec_from_file_location('tt', os.path.join(HERE, 'data', 'typo_tokens.py'))
    tm = importlib.util.module_from_spec(tspec); tspec.loader.exec_module(tm)
    cspec = importlib.util.spec_from_file_location('tc', os.path.join(HERE, 'data', 'typo_class.py'))
    cm = importlib.util.module_from_spec(cspec); cspec.loader.exec_module(cm)
    dp = os.path.join(pack, 'errata_decisions.json')          # the author's decisions (check.py --apply-review)
    dec = json.load(open(dp, encoding='utf-8')) if os.path.exists(dp) else {}
    old = {e['id']: e for e in json.load(open(os.path.join(pack, 'errata.json'), encoding='utf-8'))} if os.path.exists(os.path.join(pack, 'errata.json')) else {}
    out, missing = [], []
    for i, (kind, page, printed, issue, alt) in enumerate(m.E, 1):
        eid = f'E{i:02d}'
        hit = [a for a in atoms if printed in a['text'] and page in a['pages']] or [a for a in atoms if printed in a['text']]
        if not hit and alt:
            hit = [a for a in atoms if alt in a['text']]
        if not hit and dec.get(eid, {}).get('decision') == 'correct':   # the printed wording was replaced on the author's word
            hit = [a for a in atoms if a['id'] in old.get(eid, {}).get('atoms', [])]
        if not hit:
            missing.append((page, printed))
        out.append({'id': f'E{i:02d}', 'kind': kind, 'page': page, 'printed': printed, 'issue': issue,
                    'atoms': [a['id'] for a in hit], 'topics': sorted(set(a['topic'] for a in hit)),
                    'policy': 'taught as printed; items that rest on it carry flags ["needs_review"] and a one-line note'})
        if kind == 'typo':
            out[-1]['typo_tokens'] = [list(x) for x in tm.T.get(out[-1]['id'], [])]
            cls = 'A' if eid in cm.A else 'B'
            out[-1]['class'] = cls
            out[-1]['intended'] = ' | '.join(c for _, c in out[-1]['typo_tokens']) or cm.PUNCT.get(eid, '')
            out[-1]['use_intended'] = dec.get(eid, {}).get('use_intended', cls == 'A')
            out[-1]['policy'] = ('typo: shown with this note, never the test; '
                                 + ('class A (spelling only): answers may use the intended form' if cls == 'A' else
                                    'class B: answers keep the printed wording') + ('' if out[-1]['use_intended'] else ' (printed wording, no intended form)'))
        d = dec.get(eid)
        if d and d.get('decision') == 'not_an_error':
            out[-1]['withdrawn'] = True
            out[-1]['policy'] = 'withdrawn by the author: not an error; items no longer carry its note'
        elif d and d.get('decision') == 'correct':
            out[-1]['resolved'] = d['text']
            out[-1]['policy'] = f'corrected on the author\'s decision: the text now reads {d["text"]!r} (fix rv-{eid})'
        elif d and d.get('decision') == 'keep':
            out[-1]['author'] = 'keep'
    json.dump(out, open(os.path.join(pack, 'errata.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('errata', len(out), 'not located:', missing)


if __name__ == '__main__':
    main(sys.argv[1])

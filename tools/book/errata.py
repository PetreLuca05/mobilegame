#!/usr/bin/env python3
"""errata.json from tools/book/data/errata_src.py: every printed snippet is located in the atoms (must be found)."""
import json, os, sys, importlib.util
HERE = os.path.dirname(os.path.abspath(__file__))


def main(pack):
    spec = importlib.util.spec_from_file_location('es', os.path.join(HERE, 'data', 'errata_src.py'))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    out, missing = [], []
    for i, (kind, page, printed, issue, alt) in enumerate(m.E, 1):
        hit = [a for a in atoms if printed in a['text'] and page in a['pages']] or [a for a in atoms if printed in a['text']]
        if not hit and alt:
            hit = [a for a in atoms if alt in a['text']]
        if not hit:
            missing.append((page, printed))
        out.append({'id': f'E{i:02d}', 'kind': kind, 'page': page, 'printed': printed, 'issue': issue,
                    'atoms': [a['id'] for a in hit], 'topics': sorted(set(a['topic'] for a in hit)),
                    'policy': 'taught as printed; items that rest on it carry flags ["needs_review"] and a one-line note'})
    json.dump(out, open(os.path.join(pack, 'errata.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('errata', len(out), 'not located:', missing)


if __name__ == '__main__':
    main(sys.argv[1])

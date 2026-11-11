#!/usr/bin/env python3
"""Refresh gastro-quests.json from the atoms: each step's headings = the printed headings, box titles and figure
captions it owns; pages = the printed pages of its atoms. Prints what changed against src/gastro-quests.json."""
import json, os, re, sys


def main(pack):
    src = json.load(open(os.path.join(pack, '..', 'src', 'gastro-quests.json'), encoding='utf-8'))
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    changes = []
    for ch in src['chapters']:
        for st in ch['steps']:
            A = [a for a in atoms if a['topic'] == st['topic']]
            heads = []
            for a in A:
                if a['kind'] == 'heading':
                    heads.append(a['text'])
                elif a['kind'] == 'figure_caption':
                    heads.append(re.match(r'Fig\.?\s*[\d.]+', a['text']).group(0) if re.match(r'Fig\.?\s*[\d.]+', a['text']) else a['text'][:20])
            if not any(a['kind'] == 'heading' for a in A) and A:
                heads.insert(0, '(' + A[0]['kind'] + ' ' + A[0]['id'] + ', no heading printed)')
            pages = sorted(set(p for a in A for p in a['pages']))
            runs = []
            for p in pages:
                if runs and p == runs[-1][1] + 1:
                    runs[-1][1] = p
                else:
                    runs.append([p, p])
            pg = ', '.join(str(a0) if a0 == b0 else f'{a0}–{b0}' for a0, b0 in runs)
            old = st.get('headings_v1', st['headings'])
            gone = [h for h in old if h.split('/')[-1] not in heads and h not in heads]
            if gone or st['pages'] != pg:
                changes.append({'topic': st['topic'], 'old_pages': st['pages'], 'new_pages': pg, 'old_headings_not_printed_as_such': gone})
            st['headings_v1'] = old
            st['headings'] = heads
            st['pages'] = pg
            st['atoms'] = len(A)
    src['version'] = 2
    src['note'] = 'v2: headings and pages regenerated from the book atoms (BOOK-1); headings_v1 keeps the v1 table'
    json.dump(src, open(os.path.join(pack, 'gastro-quests.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    json.dump(changes, open(os.path.join(pack, '..', 'build', 'quest_changes.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('steps changed', len(changes))


if __name__ == '__main__':
    main(sys.argv[1])

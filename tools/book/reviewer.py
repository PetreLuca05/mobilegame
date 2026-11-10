#!/usr/bin/env python3
"""Reviewer page: each PDF page beside its text, every sentence coloured by the room that teaches it.
  reviewer.py PACK OUTDIR   -> OUTDIR/reviewer.html + OUTDIR/pages/pNN.jpg"""
import json, os, re, subprocess, sys, collections, html

PAGES = 22


def main(pack, outdir):
    os.makedirs(os.path.join(outdir, 'pages'), exist_ok=True)
    pdf = os.path.join(pack, '..', 'src', 'Gastro.pdf')
    for p in range(1, PAGES + 1):
        dst = os.path.join(outdir, 'pages', f'p{p:02d}')
        if not os.path.exists(dst + '.jpg'):
            subprocess.run(['pdftoppm', '-r', '110', '-jpeg', '-jpegopt', 'quality=72', '-f', str(p), '-l', str(p), '-singlefile', pdf, dst], check=True)
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    facts = [json.loads(l) for l in open(os.path.join(pack, 'facts.jsonl'), encoding='utf-8')] if os.path.exists(os.path.join(pack, 'facts.jsonl')) else []
    items = [json.loads(l) for l in open(os.path.join(pack, 'items.jsonl'), encoding='utf-8')] if os.path.exists(os.path.join(pack, 'items.jsonl')) else []
    scripts = json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts'] if os.path.exists(os.path.join(pack, 'scripts.json')) else []
    quests = json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))
    fixes = json.load(open(os.path.join(pack, 'fixes.json'), encoding='utf-8')) if os.path.exists(os.path.join(pack, 'fixes.json')) else {'fixes': []}
    topic = {}
    for ch in quests['chapters']:
        for st in ch['steps']:
            topic[st['topic']] = {'title': st['title'], 'room': st['room'], 'ch': ch['id'], 'step': st.get('step')}
    rooms = {}
    for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+"([^"]*)"', open(os.path.join(pack, 'institute.map'), encoding='utf-8').read(), re.M):
        rooms[m.group(2)] = {'floor': int(m.group(1)), 'name': m.group(3)}
    by_atom = collections.defaultdict(list)
    for f in facts:
        by_atom[f['atom']].append(f)
    item_by_fact = collections.defaultdict(set)
    for it in items:
        for fid in it['fact_ids']:
            item_by_fact[fid].add(it['type'])
    req_by_fact = collections.defaultdict(list)
    for sc in scripts:
        if sc.get('required'):
            for fid in sc['covers']:
                req_by_fact[fid].append(sc['title'])
    done_topics = set(f['topic'] for f in facts)
    data = []
    for a in atoms:
        sents = []
        for (s0, s1) in a['sent_spans']:
            cov = [f for f in by_atom.get(a['id'], []) if f['span'][0] <= s0 and f['span'][1] >= s1]
            ok = bool(cov) and all(len(item_by_fact[f['id']]) >= 2 and req_by_fact[f['id']] for f in cov)
            state = 'taught' if ok else ('red' if a['topic'] in done_topics or not a['topic'] else 'pending')
            where = sorted(set(t for f in cov for t in req_by_fact[f['id']]))
            sents.append([s0, s1, state, len(cov), where])
        t = topic.get(a['topic'], {})
        data.append({'id': a['id'], 'p': a['pdf_page'], 'pg': a['page'], 'k': a['kind'], 't': a['text'], 'tp': a['topic'],
                     'room': t.get('room'), 'ch': t.get('ch'), 'layer': a['layer'], 'frag': a['fragment'],
                     'path': ' › '.join(a['path'][1:]), 's': sents})
    fixcount = collections.Counter(f.get('pdf_page') for f in fixes['fixes'])
    meta = {'topics': topic, 'rooms': rooms, 'fixes': fixcount, 'chapters': [c['name'] for c in quests['chapters']],
            'stats': {'atoms': len(atoms), 'facts': len(facts), 'items': len(items), 'scripts': len(scripts),
                      'topics_done': sorted(done_topics)}}
    tpl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'reviewer_template.html'), encoding='utf-8').read()
    out = tpl.replace('/*DATA*/', 'const ATOMS=' + json.dumps(data, ensure_ascii=False) + ';\nconst META=' + json.dumps(meta, ensure_ascii=False) + ';')
    open(os.path.join(outdir, 'reviewer.html'), 'w', encoding='utf-8').write(out)
    print('reviewer.html', len(out) // 1024, 'KB')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

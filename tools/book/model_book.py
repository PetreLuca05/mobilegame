#!/usr/bin/env python3
"""Put the content pack into the "Gastro Institute 3D" page: book.js (props, NPC points, scripts, items, atoms, facts,
figure labels), fig/*.jpg, and index.html = the model page with the book layer (props drawn by kind, markers, a room /
prop card listing its scripts, a player for every item type, progress per fact in the viewer's browser).
  model_book.py PACK MODEL_HTML OUTDIR
The output holds the book's text: it is published as a private artifact, never committed."""
import json, os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def main(pack, model_html, out):
    J = lambda n: [json.loads(l) for l in open(os.path.join(pack, n), encoding='utf-8')]
    atoms, facts, items = J('atoms.jsonl'), J('facts.jsonl'), J('items.jsonl')
    scripts = [s for s in json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts'] if s.get('topic')]
    mp = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
    quests = json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))
    ch_of = {st['topic']: ch['id'] for ch in quests['chapters'] for st in ch['steps']}
    A = {a['id']: i for i, a in enumerate(atoms)}
    F = {f['id']: i for i, f in enumerate(facts)}
    I = {it['id']: i for i, it in enumerate(items)}
    anchors, AX = [], {}
    for m in re.finditer(r'^iprop\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"([^"]*)"(\s+on_model\s+\S+)?', mp, re.M):
        AX[m.group(3)] = len(anchors)
        anchors.append({'id': m.group(3), 't': 'p', 'f': int(m.group(1)), 'room': m.group(2), 'k': m.group(4), 'x': float(m.group(5)),
                        'z': float(m.group(6)), 'yaw': int(float(m.group(7))), 'label': m.group(8), 'onm': bool(m.group(9)), 's': []})
    for m in re.finditer(r'^islot\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)(\s+on_model\s+\S+)?', mp, re.M):
        AX[m.group(3)] = len(anchors)
        anchors.append({'id': m.group(3), 't': 'n', 'f': int(m.group(1)), 'room': m.group(2), 'k': m.group(4), 'x': float(m.group(5)),
                        'z': float(m.group(6)), 'yaw': int(float(m.group(7))), 'label': '', 'onm': bool(m.group(8)), 's': []})
    S = []
    for sc in scripts:
        a = sc.get('prop') or sc.get('slot')
        if a not in AX:                            # a map slot (desk, bed, board ...) or no anchor: the room's station
            a = 'st:' + sc['room']
            if a not in AX:
                AX[a] = len(anchors)
                anchors.append({'id': a, 't': 's', 'room': sc['room'], 'label': 'Stația camerei', 's': []})
        steps = []
        for st in sc['steps']:
            if st['stage'] == 'teach':
                steps.append([0] + [A[x] for x in st['atoms']] + ([-1] if st.get('repeat') else []))
            else:
                steps.append([1 if st['stage'] == 'predict' else 2, I[st['item']]])
        anchors[AX[a]]['s'].append(len(S))
        S.append({'id': sc['id'], 'title': sc['title'], 'kind': sc['kind'], 'room': sc['room'], 'topic': sc['topic'],
                  'ch': ch_of.get(sc['topic'], 0), 'part': sc.get('part', 1), 'parts': sc.get('parts', 1), 'npc': sc.get('npc', ''),
                  'a': AX[a], 'steps': steps, 'covers': [F[x] for x in sc['covers']]})
    for a in anchors:                              # a prop's or NPC's chapter colour = its first script's chapter
        a['ch'] = S[a['s'][0]]['ch'] if a['s'] else 0
    IT = []
    keep = ('type', 'stem', 'explanation', 'review_note', 'answers', 'answer', 'groups', 'sequence', 'figure')
    for it in items:
        d = {k: it[k] for k in keep if it.get(k) is not None}
        d['f'] = [F[x] for x in it['fact_ids']]
        d['flags'] = it.get('flags', [])
        if it.get('options'):
            d['options'] = [{'t': o['t'], 'ok': bool(o.get('ok')), **({'why': o['why']} if o.get('why') else {})} for o in it['options']]
        if it.get('entries'):
            d['entries'] = [{'t': e['t'], 'g': e['group']} for e in it['entries']]
        if it.get('pairs'):
            d['pairs'] = [[p['left'], p['right']] for p in it['pairs']]
        if it.get('labels'):
            d['labels'] = [l['t'] for l in it['labels']]
        IT.append(d)
    figs = {}
    os.makedirs(os.path.join(out, 'fig'), exist_ok=True)
    for fg in json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8')):
        im = Image.open(os.path.join(pack, fg['file'])).convert('RGB')
        w, h = im.size
        s = min(1.0, 1400 / w)
        im.resize((round(w * s), round(h * s)), Image.LANCZOS).save(os.path.join(out, 'fig', fg['id'] + '.jpg'), quality=86)
        figs[fg['id']] = {'w': w, 'h': h, 'labels': [[l['t']] + (l['bbox'] or [0, 0, 0, 0]) for l in fg.get('labels', [])]}
    topics = sorted({f['topic'] for f in facts})
    book = {'anchors': anchors, 'scripts': S, 'items': IT, 'atoms': [[a['text'], a['page'], a['kind']] for a in atoms],
            'facts': [topics.index(f['topic']) for f in facts], 'topics': topics, 'figs': figs}
    with open(os.path.join(out, 'book.js'), 'w', encoding='utf-8') as fo:
        fo.write('window.BOOK=' + json.dumps(book, ensure_ascii=False, separators=(',', ':')) + ';\n')
    # the page: the model as it is, plus the book layer
    html = open(model_html, encoding='utf-8').read()
    layer = open(os.path.join(HERE, 'model_book_layer.js'), encoding='utf-8').read()
    css = open(os.path.join(HERE, 'model_book_layer.css'), encoding='utf-8').read()
    def put(a, b, after=False):
        nonlocal html
        assert html.count(a) == 1, a[:60]
        html = html.replace(a, (a + b) if after else (b + a))
    def sub(a, b):
        nonlocal html
        assert html.count(a) == 1, a[:60]
        html = html.replace(a, b)
    put('</style>', css)
    put('  <div class="seg glass" id="views"></div>', '\n  <div id="bookChip" class="glass" hidden></div>', after=True)
    put(' <div id="toast" role="status"></div>', ' <div id="play" hidden><div class="pbox glass" role="dialog" aria-modal="true" aria-labelledby="pTitle"></div></div>\n')
    put('  <button id="tMesh">Copy building mesh</button>', '  <button id="tBook" aria-pressed="true">Book props</button>\n')
    put('<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>', '\n<script src="book.js"></script>', after=True)
    put("setView('all');requestAnimationFrame(frame);", layer + '\n')
    # hooks into the model's own code: book picks before room picks, the card, marker motion
    put("var hit=ray.intersectObjects(FL[view].picks,false)[0];if(hit)pick(hit.object.userData.r);",
        "var hb=bookHit(ray);if(hb!==null){pickAnchor(hb);return}")
    sub("  $('card').innerHTML=h}", "  $('card').innerHTML=h;bookRoomExtra()}")
    sub("function card(){var h;", "function card(){if(bookCard())return;var h;")
    put("  glassMat.uniforms.uTime.value=time;", "  bookFrame(time);\n")
    a = '85 rooms, 27 sizes</span>'
    assert html.count(a) == 1
    html = html.replace(a, '85 rooms · Kumar ch. 32 inside</span>')
    open(os.path.join(out, 'index.html'), 'w', encoding='utf-8').write(html)
    sz = os.path.getsize(os.path.join(out, 'book.js'))
    print(f'index.html {os.path.getsize(os.path.join(out, "index.html")):,} bytes; book.js {sz:,} bytes; {len(anchors)} anchors '
          f'({sum(1 for a in anchors if a["t"] == "p")} props, {sum(1 for a in anchors if a["t"] == "n")} NPC points, '
          f'{sum(1 for a in anchors if a["t"] == "s")} room stations); {len(S)} scripts; {len(IT)} items; {len(figs)} figures')


if __name__ == '__main__':
    main(*sys.argv[1:4])

#!/usr/bin/env python3
"""The test fixture: a small synthetic content pack in the pack format (v4), generated at build time into the build folder
(never stored in the repository). 2 arcs, 3 cases, 6 rooms of placeholder facts, items of every type (and one per type at a
large worst-case size), scripts, dialogue, a cast and people. Every text is invented filler made from nonsense syllables.
The map tables come from the building map (assets/world/institute/institute.map); the fixture adds its own invented
interaction props and topic records to a temporary copy of it (the repository's map has none). Stand points, patient places
and person posts are cells the building's reference walk grid (model_nav.json) marks reachable.
  make_fixture_pack.py OUT_DIR      -> OUT_DIR/fixture.mgpack (+ .h), OUT_DIR/fig/ (the figure crops: figpack.py packs them)
The engine tests load it with --content OUT_DIR (book_ui, missions, book, bookpeople) or read it directly (ctest book_pack)."""
import base64, importlib.util, json, math, os, random, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
PUBLIC = os.path.join(ROOT, 'assets', 'world', 'institute')


class Reach:
    """the reachable cells of model_nav.json (0.25 m, per storey, bit j * nx + i, low bit first)"""
    def __init__(self, path):
        d = json.load(open(path, encoding='utf-8'))
        g = d['grid']
        self.cs, self.x0, self.z0, self.nx, self.nz = g['cs'], g['x0'], g['z0'], g['nx'], g['nz']
        self.bits = [base64.b64decode(b) for b in g['reach']]
        self.exempt = set(d.get('exempt', []))

    def ok(self, f, i, j):
        k = j * self.nx + i
        return 0 <= i < self.nx and 0 <= j < self.nz and 0 <= f < len(self.bits) and (self.bits[f][k >> 3] >> (k & 7)) & 1

    def near(self, f, x, z, box, avoid=()):
        """the reachable cell centre inside box (x0, z0, x1, z1) nearest (x, z), not within 0.6 m of any point in avoid"""
        best, bd = None, 1e9
        i0, i1 = int((box[0] - self.x0) / self.cs), int((box[2] - self.x0) / self.cs)
        j0, j1 = int((box[1] - self.z0) / self.cs), int((box[3] - self.z0) / self.cs)
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                if not self.ok(f, i, j):
                    continue
                cx, cz = self.x0 + (i + .5) * self.cs, self.z0 + (j + .5) * self.cs
                if not (box[0] < cx < box[2] and box[1] < cz < box[3]) or any(math.hypot(cx - a, cz - b) < .6 for a, b in avoid):
                    continue
                dd = math.hypot(cx - x, cz - z)
                if dd < bd:
                    best, bd = (round(cx, 3), round(cz, 3)), dd
        if best is None:
            sys.exit(f'make_dummy_pack: no reachable cell on storey {f} in {box}')
        return best
SYL = ['ta', 'lo', 'mi', 're', 'du', 'na', 'vi', 'so', 'ke', 'pu', 'ra', 'ze', 'fo', 'li', 'ga', 'be']


def write_png(path, w, h, rgb):
    """an RGB PNG (no Pillow needed): rgb on a soft diagonal ramp, with a dark frame and two darker boxes where the labels go,
    so a block-compressed copy has edges and gradients to get wrong"""
    import struct, zlib
    def px(x, y):
        if x in (0, w - 1) or y in (0, h - 1) or 4 <= x < 20 and 4 <= y < 14 or 30 <= x < 50 and 20 <= y < 30:
            return bytes((40, 60, 90))
        k = (x + y) / (w + h)
        return bytes(int(c * (0.8 + 0.2 * k)) for c in rgb)
    raw = b''.join(b'\0' + b''.join(px(x, y) for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def filler(rng, n):
    """n invented words of 2-3 nonsense syllables"""
    return ' '.join(''.join(rng.choice(SYL) for _ in range(rng.randint(2, 3))) for _ in range(n))


# The real pack's worst case per item type, as numbers only (the game logs them at load: "book: worst cases ..."): the dummy
# pack carries one item of each type at that size, so CI plays the hardest layout without a word of the book.
# type: (options / entries / pairs / steps / labels / answers, longest answer bytes, stem bytes, explanation bytes)
WORST = {'simplu': (5, 102, 223, 632), 'multiplu': (5, 225, 189, 1776), 'card': (1, 643, 189, 1589), 'cloze': (6, 44, 294, 1615),
         'sort': (22, 101, 196, 1258), 'match': (7, 161, 106, 1449), 'order': (14, 181, 132, 1758), 'label': (13, 120, 148, 1579)}
WORST_TEACH_BYTES = 1724                 # the longest atom
WORST_FIG = (1386, 1233, 35)             # the biggest figure (w, h px) and the most label boxes on one figure
WORST_SORT_GROUPS = 5


def sized(rng, n, end='.'):
    """invented filler of exactly n bytes (ASCII), ending with `end`"""
    t = ''
    while len(t) < n + 4:
        t += (' ' if t else '') + ''.join(rng.choice(SYL) for _ in range(rng.randint(2, 4)))
    t = t[:n - len(end)].rstrip()
    t = (t + 'x' * n)[:n - len(end)] + end
    return t


def write_big_png(path, w, h, boxes):
    """a w x h RGB PNG: a soft ramp with a frame and bands, the label boxes as white boxes with dark outlines"""
    import struct, zlib
    dark, white = bytes((30, 40, 60)), bytes((255, 255, 255))
    rows = []
    for y in range(h):
        k = y / h
        if y < 3 or y >= h - 3:
            base = bytearray(dark * w)
        else:
            base = bytearray(bytes((int(226 + 20 * k), int(232 + 16 * k), int(238 + 10 * k))) * w)
            base[0:9] = dark * 3
            base[(w - 3) * 3:] = dark * 3
            if (y // 40) % 5 == 0:                       # horizontal bands, like a diagram's strata
                base[90:(w - 30) * 3] = bytes((205, 214, 226)) * (w - 60)
        for (x0, y0, x1, y1) in boxes:
            if y0 <= y <= y1:
                if y in (y0, y0 + 1, y1 - 1, y1):
                    base[x0 * 3:(x1 + 1) * 3] = dark * (x1 - x0 + 1)
                else:
                    base[x0 * 3:(x0 + 2) * 3] = dark * 2
                    base[(x1 - 1) * 3:(x1 + 1) * 3] = dark * 2
                    base[(x0 + 2) * 3:(x1 - 1) * 3] = white * (x1 - x0 - 3)
        rows.append(b'\0' + bytes(base))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def add_worst_case(items, atoms, scripts, topic, room, prop, tmp):
    """one item of each type at WORST, a teach atom at WORST_TEACH_BYTES on the big figure, one optional script per item (in no
    mission); returns the big figure's figures.json entry"""
    rng = random.Random(99)                              # its own stream: the rest of the dummy pack does not change
    tf = [f'f-{topic}-{j}' for j in range(4)]
    fw, fh, nbox = WORST_FIG
    cols = 5
    rows_n = (nbox + cols - 1) // cols
    boxes = []
    for i in range(nbox):                                # a grid of 150 x 34 px boxes over the figure
        c, r = i % cols, i // cols
        x0 = 40 + c * (fw - 80) // cols + 20
        y0 = 40 + r * (fh - 80) // rows_n + 30
        boxes.append((x0, y0, x0 + 150, y0 + 34))
    write_big_png(os.path.join(tmp, 'fig', 'fig_worst.png'), fw, fh, boxes)
    n_lab, lab_bytes = WORST['label'][0], WORST['label'][1]
    box_text = [sized(rng, 8 + (i * 7) % 30, '') for i in range(nbox)]
    box_text[n_lab - 1] = sized(rng, lab_bytes, '')       # one label at the longest label's length
    atoms.append({'id': 'a-worst-teach', 'text': sized(rng, WORST_TEACH_BYTES), 'page': 1, 'topic': topic, 'kind': 'paragraph', 'layer': 'visible',
                  'fragment': None, 'fig': 'fig_worst'})
    for ty, (n, ans, stem, expl) in WORST.items():
        iid = f'i-worst-{ty}'
        it = {'id': iid, 'topic': topic, 'type': ty, 'stem': sized(rng, stem), 'explanation': sized(rng, expl), 'fact_ids': tf[:2], 'flags': [],
              'difficulty': 2}
        A = lambda k: sized(rng, ans if k == 0 else max(12, ans // (2 + k % 3)))
        if ty in ('simplu', 'multiplu'):
            it['options'] = [{'t': A(k), 'ok': k == 0 or (ty == 'multiplu' and k in (1, 2)), 'fact': tf[0],
                              **({'why': sized(rng, 120)} if ty == 'multiplu' and k > 2 else {})} for k in range(n)]
        elif ty == 'cloze':
            words = [sized(rng, 30, '') for _ in range(n)]
            it['answers'] = [sized(rng, ans if g == 0 else 8 + g, '') for g in range(n)]
            body = ' ____ '.join(words) + ' ____.'
            if len(body) < stem:
                body += ' ' + sized(rng, stem - len(body) - 1)
            it['stem'] = body
            it['list_gap'] = True
        elif ty == 'card':
            it['answer'] = A(0)
        elif ty == 'sort':
            it['groups'] = [sized(rng, 10 + g, '') for g in range(WORST_SORT_GROUPS)]
            it['entries'] = [{'t': A(e), 'group': it['groups'][e % WORST_SORT_GROUPS], 'fact': tf[e % 4]} for e in range(n)]
        elif ty == 'match':
            it['pairs'] = [{'left': sized(rng, 60 if k == 0 else 20), 'right': sized(rng, ans - 60 if k == 0 else 30), 'fact': tf[k % 4]} for k in range(n)]
        elif ty == 'order':
            it['sequence'] = [A(k) for k in range(n)]
        elif ty == 'label':
            it['figure'] = 'fig_worst'
            it['labels'] = [{'t': box_text[k], 'fact': tf[k % 4]} for k in range(n)]
        items.append(it)
        scripts.append({'id': f's-worst-{ty}', 'topic': topic, 'room': room[0], 'kind': 'station', 'title': sized(rng, 40), 'required': False,
                        'part': 1, 'parts': 1, 'prop': prop, 'slot': None, 'npc': None,
                        'steps': [{'stage': 'teach', 'atoms': ['a-worst-teach']}, {'stage': 'check', 'item': iid}], 'covers': []})
    return {'id': 'fig_worst', 'file': 'fig/fig_worst.png', 'labels': [{'t': box_text[i], 'bbox': list(boxes[i])} for i in range(nbox)]}


def main(out):
    rng = random.Random(7)
    T = lambda n: filler(rng, n).capitalize() + '.'
    tmp = tempfile.mkdtemp(prefix='fixture_pack_')
    os.makedirs(os.path.join(tmp, 'fig'))
    mp_text = open(os.path.join(PUBLIC, 'institute.map'), encoding='utf-8').read()
    reach = Reach(os.path.join(PUBLIC, 'model_nav.json'))
    all_rooms = [(m.group(2), int(m.group(1)), *[float(m.group(i)) for i in (3, 4, 5, 6)])
                 for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"', mp_text, re.M)]
    ROOM_TYPES = ('exam', 'lab', 'office', 'meeting')         # rooms a fixture prop can stand in
    all_typed = [(m.group(2), int(m.group(1)), m.group(3)) for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)\s+(\S+)\s', mp_text, re.M)]
    usable = {rid for rid, f, ty in all_typed if ty in ROOM_TYPES}
    pick = []
    for f in (0, 1):                                          # 6 rooms of the building: 3 on the ground floor, 3 on storey 1
        cand = sorted(r for r in all_rooms if r[1] == f and r[0] in usable and r[0] not in reach.exempt)
        pick += [cand[k * len(cand) // 3] for k in range(3)]
    rooms = [(r[0], r[1], r[2], r[3], r[4], r[5]) for r in pick]  # (id, storey, x0, z0, x1, z1)
    topics = [f'demo_t{i + 1}' for i in range(6)]
    # the fixture's own interaction props and topic records (invented), one board per room near its centre
    rprops, extra_map = {}, []
    for t, r in zip(topics, rooms):
        cx, cz = (r[2] + r[4]) / 2, (r[3] + r[5]) / 2
        px, pz = reach.near(r[1], cx, cz, (r[2], r[3], r[4], r[5]))
        pid = f'fx_{r[0]}'
        rprops[r[0]] = [(r[0], pid, px, pz)]
        extra_map.append(f'iprop {r[1]} {r[0]} {pid} board {px} {pz} 0 "{filler(rng, 2)}"')
        extra_map.append(f'learn {r[1]} {r[0]} fixture topic {t}')
    atoms, facts, items, scripts = [], [], [], []
    ITYPES = ['simplu', 'multiplu', 'card', 'cloze', 'sort', 'match', 'order', 'label']
    k = 0
    for ti, (t, r) in enumerate(zip(topics, rooms)):
        tf = []
        for j in range(4):
            aid = f'a-{t}-{j}'
            atoms.append({'id': aid, 'text': T(10), 'page': 1, 'topic': t, 'kind': 'heading' if j == 0 else 'paragraph', 'layer': 'visible', 'fragment': None})
            fid = f'f-{t}-{j}'
            facts.append({'id': fid, 'claim': T(9), 'atom': aid, 'topic': t, 'page': 1, 'type': 'statement'})
            tf.append(fid)
        ti_items = []
        for j in range(2):
            ty = ITYPES[k % len(ITYPES)]
            k += 1
            iid = f'i-{t}-{j}'
            it = {'id': iid, 'topic': t, 'type': ty, 'stem': T(8), 'explanation': T(12), 'fact_ids': tf[j * 2:j * 2 + 2], 'flags': [], 'difficulty': 2}
            if ty in ('simplu', 'multiplu'):
                it['options'] = [{'t': T(4), 'ok': o == 0 or (ty == 'multiplu' and o == 1), 'fact': tf[0]} for o in range(4)]
            elif ty == 'cloze':
                it['stem'] = filler(rng, 4) + ' ____ ' + filler(rng, 3) + '.'
                it['answers'] = [filler(rng, 1)]
            elif ty == 'card':
                it['answer'] = T(5)
            elif ty == 'sort':
                it['groups'] = [filler(rng, 1), filler(rng, 1)]
                it['entries'] = [{'t': T(3), 'group': it['groups'][e % 2], 'fact': tf[0]} for e in range(4)]
            elif ty == 'match':
                it['pairs'] = [{'left': T(2), 'right': T(2), 'fact': tf[1]} for _ in range(3)]
            elif ty == 'order':
                it['sequence'] = [T(3) for _ in range(4)]
            elif ty == 'label':
                it['figure'] = 'fig_demo'
                it['labels'] = [{'t': filler(rng, 1), 'fact': tf[0]} for _ in range(2)]
            items.append(it)
            ti_items.append(iid)
        prop = rprops[r[0]][0][1]                             # a real book prop of the room
        for j in range(2):
            scripts.append({'id': f's-{t}-{j}', 'topic': t, 'room': r[0], 'kind': ['station', 'case'][j], 'title': T(4), 'required': True, 'part': 1, 'parts': 1,
                            'prop': prop if j == 0 else None, 'slot': None, 'npc': None,
                            'steps': [{'stage': 'teach', 'atoms': [f'a-{t}-{j * 2}', f'a-{t}-{j * 2 + 1}']}, {'stage': 'check', 'item': ti_items[j]}],
                            'covers': tf[j * 2:j * 2 + 2]})
    worst_figure = add_worst_case(items, atoms, scripts, topics[5], rooms[5], rprops[rooms[5][0]][0][1], tmp)
    J = lambda n, rows: open(os.path.join(tmp, n), 'w', encoding='utf-8').write('\n'.join(json.dumps(x, ensure_ascii=False) for x in rows) + '\n')
    J('atoms.jsonl', atoms)
    J('facts.jsonl', facts)
    J('items.jsonl', items)
    json.dump({'scripts': scripts}, open(os.path.join(tmp, 'scripts.json'), 'w', encoding='utf-8'))
    chapters = [{'id': 0, 'name': 'Demo A', 'sub': T(2), 'steps': [{'topic': t, 'room': r[0], 'title': T(3)} for t, r in zip(topics[:2], rooms[:2])]},
                {'id': 1, 'name': 'Demo B', 'sub': T(2), 'steps': [{'topic': t, 'room': r[0], 'title': T(3)} for t, r in zip(topics[2:], rooms[2:])]}]
    json.dump({'chapters': chapters}, open(os.path.join(tmp, 'gastro-quests.json'), 'w', encoding='utf-8'))
    open(os.path.join(tmp, 'institute.map'), 'w', encoding='utf-8').write(mp_text + '\n'.join(extra_map) + '\n')  # the building + the fixture's props

    write_png(os.path.join(tmp, 'fig', 'fig_demo.png'), 64, 48, (230, 236, 240))
    json.dump([{'id': 'fig_demo', 'file': 'fig/fig_demo.png', 'labels': [{'t': filler(rng, 1), 'bbox': [4, 4, 20, 14]}, {'t': filler(rng, 1), 'bbox': [30, 20, 50, 30]}]},
               worst_figure],
              open(os.path.join(tmp, 'fig', 'figures.json'), 'w', encoding='utf-8'))
    # missions: 2 arcs, 3 cases (A: rooms 1-2, B: 3-4, C: 5-6), a cast of 6, dialogue, people
    look = lambda kind, i: {'kind': kind, 'sex': 'fm'[i % 2], 'age': i % 3, 'skin': i % 4, 'hair': ['short', 'long', 'bun', 'bald', 'curly', 'ponytail'][i % 6],
                            'hair_color': '#4a2f1d', 'outfit': '#ffffff' if kind != 'patient' else '#9cc9e8', 'under': '#2bb3a3'}
    cast = []
    for i, (cid, kind, rid) in enumerate([('demo_chief', 'doctor', rooms[0][0]), ('demo_nurse', 'nurse', rooms[1][0]), ('demo_tech', 'tech', rooms[2][0]),
                                          ('demo_pharm', 'pharmacist', rooms[3][0]), ('demo_doc', 'doctor', rooms[4][0]), ('demo_patient', 'patient', rooms[5][0])]):
        cast.append({'id': cid, 'name': filler(rng, 2).title(), 'role': filler(rng, 2), 'post': rid, **look(kind, i)})
    RB = {r[0]: r for r in rooms}
    box = lambda r: (r[2], r[3], r[4], r[5])
    centre = lambda r: ((r[2] + r[4]) / 2, (r[3] + r[5]) / 2)
    def step(sc, pres, pat=None):
        r = RB[sc['room']]
        px, pz = rprops[r[0]][0][2], rprops[r[0]][0][3]       # the book prop the step is about
        stand = reach.near(r[1], px, pz, box(r))
        pt = reach.near(r[1], *centre(r), box(r), avoid=[stand])
        return {'script': sc['id'], 'kind': sc['kind'], 'presenter': pres, 'room': sc['room'], 'f': r[1], 'stand': [stand[0], stand[1], 0], 'at': [px, pz],
                'moved': '', 'line': T(6), 'after': [T(4), T(4)],
                'patient': {'cast': pat, 'x': pt[0], 'z': pt[1], 'yaw': 0, 'pose': 'lie', 'y': .62, 'seat': ''} if pat and sc['kind'] == 'case' else None}
    hosts = ['demo_chief', 'demo_nurse', 'demo_tech', 'demo_pharm', 'demo_doc', 'demo_chief']
    missions = []
    for mi, (ch, tps) in enumerate([(0, topics[:2]), (1, topics[2:4]), (1, topics[4:])]):
        qs = [{'topic': t, 'host': hosts[topics.index(t)], 'intro': [T(6)], 'done': T(5),
               'steps': [step(sc, hosts[topics.index(t)], 'demo_patient') for sc in scripts if sc['topic'] == t and sc['required']]} for t in tps]
        missions.append({'id': f'demo_m{mi + 1}', 'chapter': ch, 'title': filler(rng, 2).title(), 'case': T(7), 'giver': 'demo_chief', 'patient': 'demo_patient',
                         'ward': rooms[1][0], 'intro': [T(8), T(6)], 'outro': [T(6)], 'quests': qs})
    arcs = [{'chapter': c, 'title': f'Demo {"AB"[c]}', 'giver': 'demo_chief', 'intro': [T(7)], 'outro': [T(5)]} for c in (0, 1)]
    people, taken = [], {r[0]: [] for r in rooms}
    for c in cast:                                            # posts on reachable cells of their rooms
        r = RB[c['post']]
        at = reach.near(r[1], *centre(r), box(r), avoid=taken[r[0]])
        taken[r[0]].append(at)
        people.append({'cast': c['id'], 'kind': c['kind'], 'f': r[1], 'room': c['post'], 'x': at[0], 'z': at[1], 'yaw': 0, 'pose': 'stand', 'y': 0, 'mode': 'static'})
    for i in range(6):
        r = rooms[i]
        at = reach.near(r[1], r[2] + 1.5, r[5] - 1.0, box(r), avoid=taken[r[0]])
        taken[r[0]].append(at)
        people.append({'cast': None, 'kind': ['patient', 'visitor', 'nurse'][i % 3], 'look': {**look(['patient', 'visitor', 'nurse'][i % 3], i), 'seed': 1000 + i},
                       'f': r[1], 'room': r[0], 'x': at[0], 'z': at[1], 'yaw': 180,
                       'pose': 'sit' if i % 2 == 0 else 'stand', 'y': 0, 'mode': 'static' if i % 2 == 0 else 'walk', 'talk': i % 2 == 0})
    print('make_fixture_pack: rooms', ', '.join(f'{r[0]} (storey {r[1]})' for r in rooms))
    json.dump({'version': 1, 'note': 'synthetic test fixture', 'cast': cast, 'arcs': arcs, 'missions': missions, 'people': people,
               'greetings': {'doctor': [T(4)], 'nurse': [T(4)], 'patient': [T(4)], 'visitor': [T(4)]}},
              open(os.path.join(tmp, 'missions.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    spec = importlib.util.spec_from_file_location('packbin', os.path.join(ROOT, 'tools', 'book', 'packbin.py'))
    pb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pb)
    os.makedirs(out, exist_ok=True)
    pb.main(tmp, os.path.join(out, 'fixture.mgpack'))
    shutil.copytree(os.path.join(tmp, 'fig'), os.path.join(out, 'fig'), dirs_exist_ok=True)  # the figure crops travel next to the pack
    print('make_fixture_pack: wrote', os.path.join(out, 'fixture.mgpack'))
    shutil.rmtree(tmp)


if __name__ == '__main__':
    main(sys.argv[1])

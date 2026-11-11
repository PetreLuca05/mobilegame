#!/usr/bin/env python3
"""A small synthetic content pack in the real format (v4), for the public repository and CI: 2 arcs, 3 cases, 6 rooms of
placeholder facts, items of every type, scripts, dialogue, a cast and people. The map tables (rooms, furniture, book props,
slots) are the public content/gastro/institute.map itself; the 6 rooms are real rooms of it on 2 storeys, the scripts sit on
real book props there, and every stand point, patient place and person post is a cell content/gastro/model_nav.json marks
reachable (well within the 2.0 m rule). Every text is invented filler made from
nonsense syllables: no word of the book. It is written by the same packbin.py as the real pack, so the format cannot drift,
and checked with mgpack_read.c when a C compiler is present.
  make_dummy_pack.py OUT_DIR        -> OUT_DIR/gastro.mgpack, OUT_DIR/gastro.mgpack.h
The game shows "conținut demo" when it runs on this pack (ENGINE_PROMPT.md, S1)."""
import base64, importlib.util, json, math, os, random, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PUBLIC = os.path.join(HERE, '..', '..', 'content', 'gastro')


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
    """a plain RGB PNG of one colour (no Pillow needed)"""
    import struct, zlib
    raw = b''.join(b'\0' + bytes(rgb) * w for _ in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def filler(rng, n):
    """n invented words of 2-3 nonsense syllables"""
    return ' '.join(''.join(rng.choice(SYL) for _ in range(rng.randint(2, 3))) for _ in range(n))


def main(out):
    rng = random.Random(7)
    T = lambda n: filler(rng, n).capitalize() + '.'
    tmp = tempfile.mkdtemp(prefix='dummy_pack_')
    os.makedirs(os.path.join(tmp, 'fig'))
    mp_text = open(os.path.join(PUBLIC, 'institute.map'), encoding='utf-8').read()
    reach = Reach(os.path.join(PUBLIC, 'model_nav.json'))
    all_rooms = [(m.group(2), int(m.group(1)), *[float(m.group(i)) for i in (3, 4, 5, 6)])
                 for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"', mp_text, re.M)]
    iprops = [(m.group(1), m.group(2), float(m.group(3)), float(m.group(4)))
              for m in re.finditer(r'^iprop\s+-?\d+\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)', mp_text, re.M)]
    iprops = [(p[0], p[1], p[2], p[3]) for p in iprops]      # (room, prop id, x, z)
    with_props = {p[0] for p in iprops}
    pick = []
    for f in (0, 1):                                          # 6 real rooms: 3 on the ground floor, 3 on storey 1, with book props
        cand = sorted(r for r in all_rooms if r[1] == f and r[0] in with_props and r[0] not in reach.exempt)
        pick += [cand[k * len(cand) // 3] for k in range(3)]
    rooms = [(r[0], r[1], r[2], r[3], r[4], r[5]) for r in pick]  # (id, storey, x0, z0, x1, z1)
    rprops = {r[0]: [p for p in iprops if p[0] == r[0]] for r in rooms}
    topics = [f'demo_t{i + 1}' for i in range(6)]
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
    J = lambda n, rows: open(os.path.join(tmp, n), 'w', encoding='utf-8').write('\n'.join(json.dumps(x, ensure_ascii=False) for x in rows) + '\n')
    J('atoms.jsonl', atoms)
    J('facts.jsonl', facts)
    J('items.jsonl', items)
    json.dump({'scripts': scripts}, open(os.path.join(tmp, 'scripts.json'), 'w', encoding='utf-8'))
    chapters = [{'id': 0, 'name': 'Demo A', 'sub': T(2), 'steps': [{'topic': t, 'room': r[0], 'title': T(3)} for t, r in zip(topics[:2], rooms[:2])]},
                {'id': 1, 'name': 'Demo B', 'sub': T(2), 'steps': [{'topic': t, 'room': r[0], 'title': T(3)} for t, r in zip(topics[2:], rooms[2:])]}]
    json.dump({'chapters': chapters}, open(os.path.join(tmp, 'gastro-quests.json'), 'w', encoding='utf-8'))
    open(os.path.join(tmp, 'institute.map'), 'w', encoding='utf-8').write(mp_text)  # rooms, furniture, book props, slots: the real ones

    write_png(os.path.join(tmp, 'fig', 'fig_demo.png'), 64, 48, (230, 236, 240))
    json.dump([{'id': 'fig_demo', 'file': 'fig/fig_demo.png', 'labels': [{'t': filler(rng, 1), 'bbox': [4, 4, 20, 14]}, {'t': filler(rng, 1), 'bbox': [30, 20, 50, 30]}]}],
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
               'steps': [step(sc, hosts[topics.index(t)], 'demo_patient') for sc in scripts if sc['topic'] == t]} for t in tps]
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
    print('make_dummy_pack: rooms', ', '.join(f'{r[0]} (storey {r[1]})' for r in rooms))
    json.dump({'version': 1, 'note': 'synthetic demo content', 'cast': cast, 'arcs': arcs, 'missions': missions, 'people': people,
               'greetings': {'doctor': [T(4)], 'nurse': [T(4)], 'patient': [T(4)], 'visitor': [T(4)]}},
              open(os.path.join(tmp, 'missions.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    spec = importlib.util.spec_from_file_location('packbin', os.path.join(HERE, 'packbin.py'))
    pb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pb)
    os.makedirs(out, exist_ok=True)
    pb.main(tmp, os.path.join(out, 'gastro.mgpack'))
    cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
    reader = os.path.join(os.path.dirname(HERE), '..', 'content', 'gastro', 'mgpack_read.c')
    if len(sys.argv) > 2:
        reader = sys.argv[2]
    if cc and os.path.exists(reader):
        exe = os.path.join(tmp, 'mgpack_read')
        r = subprocess.run([cc, '-O1', '-include', os.path.join(out, 'gastro.mgpack.h'), reader, '-o', exe], capture_output=True, text=True)
        if r.returncode:
            sys.exit('mgpack_read.c does not build: ' + r.stderr[:400])
        r = subprocess.run([exe, os.path.join(out, 'gastro.mgpack')], capture_output=True, text=True)
        probs = [l for l in r.stdout.splitlines() if l.startswith('problems=')]
        print('mgpack_read:', probs[0] if probs else r.stdout[-200:], r.stderr[:300])
        if r.returncode or not probs or probs[0] != 'problems=0':
            sys.exit(1)
    shutil.rmtree(tmp)


if __name__ == '__main__':
    main(sys.argv[1])

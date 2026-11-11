#!/usr/bin/env python3
"""A small synthetic content pack in the real format (v4), for the public repository and CI: 2 arcs, 3 cases, 6 rooms of
placeholder facts, items of every type, scripts, dialogue, a cast and people. Every text is invented filler made from
nonsense syllables: no word of the book. It is written by the same packbin.py as the real pack, so the format cannot drift,
and checked with mgpack_read.c when a C compiler is present.
  make_dummy_pack.py OUT_DIR        -> OUT_DIR/gastro.mgpack, OUT_DIR/gastro.mgpack.h
The game shows "conținut demo" when it runs on this pack (ENGINE_PROMPT.md, S1)."""
import importlib.util, json, os, random, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SYL = ['ta', 'lo', 'mi', 're', 'du', 'na', 'vi', 'so', 'ke', 'pu', 'ra', 'ze', 'fo', 'li', 'ga', 'be']


def filler(rng, n):
    """n invented words of 2-3 nonsense syllables"""
    return ' '.join(''.join(rng.choice(SYL) for _ in range(rng.randint(2, 3))) for _ in range(n))


def main(out):
    rng = random.Random(7)
    T = lambda n: filler(rng, n).capitalize() + '.'
    tmp = tempfile.mkdtemp(prefix='dummy_pack_')
    os.makedirs(os.path.join(tmp, 'fig'))
    rooms = [(f'demo_r{i + 1}', i // 3, -12 + (i % 3) * 8, -6, -6 + (i % 3) * 8, 0) for i in range(6)]   # 6 rooms, 2 storeys
    types = ['exam', 'ward_room', 'lab', 'pharmacy', 'exam', 'ward_room']
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
        prop = f'demo_prop{ti + 1}'
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
    mp = []
    for (rid, f, x0, z0, x1, _), ty in zip(rooms, types):
        mp.append(f'room {f} {rid} {ty} {x0} {z0} {x1} {z0 + 6} "{filler(rng, 2)}"')
    for i, (rid, f, x0, z0, x1, _) in enumerate(rooms):
        mp.append(f'iprop {f} {rid} demo_prop{i + 1} board {x0 + 1} {z0 + .3} 0 "{filler(rng, 2)}"')
        mp.append(f'islot {f} {rid} demo_slot{i + 1} npc_stand {x0 + 3} {z0 + 3} 0')
        mp.append(f'prop {f} {rid} {rid}.desk.1 desk {x0 + 4} {z0 + 4} 0 1.8 0.8 1.15 free')
        mp.append(f'prop {f} {rid} {rid}.door_leaf.1 door_leaf {x0 + 3} {z0} 90 1.1 0.05 2.15 door opens 0 1')
    open(os.path.join(tmp, 'institute.map'), 'w', encoding='utf-8').write('\n'.join(mp) + '\n')
    from PIL import Image
    Image.new('RGB', (64, 48), (230, 236, 240)).save(os.path.join(tmp, 'fig', 'fig_demo.png'))
    json.dump([{'id': 'fig_demo', 'file': 'fig/fig_demo.png', 'labels': [{'t': filler(rng, 1), 'bbox': [4, 4, 20, 14]}, {'t': filler(rng, 1), 'bbox': [30, 20, 50, 30]}]}],
              open(os.path.join(tmp, 'fig', 'figures.json'), 'w', encoding='utf-8'))
    # missions: 2 arcs, 3 cases (A: rooms 1-2, B: 3-4, C: 5-6), a cast of 6, dialogue, people
    look = lambda kind, i: {'kind': kind, 'sex': 'fm'[i % 2], 'age': i % 3, 'skin': i % 4, 'hair': ['short', 'long', 'bun', 'bald', 'curly', 'ponytail'][i % 6],
                            'hair_color': '#4a2f1d', 'outfit': '#ffffff' if kind != 'patient' else '#9cc9e8', 'under': '#2bb3a3'}
    cast = []
    for i, (cid, kind, rid) in enumerate([('demo_chief', 'doctor', 'demo_r1'), ('demo_nurse', 'nurse', 'demo_r2'), ('demo_tech', 'tech', 'demo_r3'),
                                          ('demo_pharm', 'pharmacist', 'demo_r4'), ('demo_doc', 'doctor', 'demo_r5'), ('demo_patient', 'patient', 'demo_r6')]):
        cast.append({'id': cid, 'name': filler(rng, 2).title(), 'role': filler(rng, 2), 'post': rid, **look(kind, i)})
    RB = {r[0]: r for r in rooms}
    def step(sc, pres, pat=None):
        r = RB[sc['room']]
        return {'script': sc['id'], 'kind': sc['kind'], 'presenter': pres, 'room': sc['room'], 'f': r[1], 'stand': [r[2] + 2, r[3] + 2, 0], 'at': [r[2] + 1, r[3] + .3],
                'moved': '', 'line': T(6), 'after': [T(4), T(4)],
                'patient': {'cast': pat, 'x': r[2] + 4, 'z': r[3] + 3, 'yaw': 0, 'pose': 'lie', 'y': .62, 'seat': ''} if pat and sc['kind'] == 'case' else None}
    hosts = ['demo_chief', 'demo_nurse', 'demo_tech', 'demo_pharm', 'demo_doc', 'demo_chief']
    missions = []
    for mi, (ch, tps) in enumerate([(0, topics[:2]), (1, topics[2:4]), (1, topics[4:])]):
        qs = [{'topic': t, 'host': hosts[topics.index(t)], 'intro': [T(6)], 'done': T(5),
               'steps': [step(sc, hosts[topics.index(t)], 'demo_patient') for sc in scripts if sc['topic'] == t]} for t in tps]
        missions.append({'id': f'demo_m{mi + 1}', 'chapter': ch, 'title': filler(rng, 2).title(), 'case': T(7), 'giver': 'demo_chief', 'patient': 'demo_patient',
                         'ward': 'demo_r2', 'intro': [T(8), T(6)], 'outro': [T(6)], 'quests': qs})
    arcs = [{'chapter': c, 'title': f'Demo {"AB"[c]}', 'giver': 'demo_chief', 'intro': [T(7)], 'outro': [T(5)]} for c in (0, 1)]
    people = [{'cast': c['id'], 'kind': c['kind'], 'f': RB[c['post']][1], 'room': c['post'], 'x': RB[c['post']][2] + 3, 'z': RB[c['post']][3] + 2, 'yaw': 0,
               'pose': 'stand', 'y': 0, 'mode': 'static'} for c in cast]
    for i in range(6):
        r = rooms[i]
        people.append({'cast': None, 'kind': ['patient', 'visitor', 'nurse'][i % 3], 'look': {**look(['patient', 'visitor', 'nurse'][i % 3], i), 'seed': 1000 + i},
                       'f': r[1], 'room': r[0] if i % 2 == 0 else 'corridor%d' % r[1], 'x': r[2] + 1.5, 'z': r[3] + 4, 'yaw': 180,
                       'pose': 'sit' if i % 2 == 0 else 'stand', 'y': 0, 'mode': 'static' if i % 2 == 0 else 'walk', 'talk': i % 2 == 0})
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

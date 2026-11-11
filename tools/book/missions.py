#!/usr/bin/env python3
"""Missions: the book presented through NPCs. Reads the authored cast and cases (missions_src.json, zip only) and the
scripts, and writes missions.json: arc > mission (a hospital case) > quest (one topic, at its room's expert) > step (one
script) with the presenter, where they stand, where the patient lies or sits, and the line that opens the step.
  missions.py PACK            build PACK/missions.json and check it
  missions.py PACK --check    check only (check.py calls this)
Logic rules (author): nothing happens outdoors; a patient is seen only in a clinical room; treatment is chosen in a
pharmacy, ward, treatment / ICU / recovery bay, consult room or OR; every stand point is inside its room and clear of
furniture; every required script is in exactly one step, in book order. Text of the book never comes through here."""
import hashlib, importlib.util, json, math, os, re, sys

CLINICAL = {'exam', 'ward_room', 'treatment_bay', 'icu_bay', 'recovery', 'triage', 'or', 'imaging'}
PLACE_OK = {                                   # script kind -> room types where it can happen
    'case': CLINICAL | {'pharmacy'},
    'dispense': {'pharmacy', 'ward_room', 'treatment_bay', 'icu_bay', 'recovery', 'exam', 'or'},
}
INDOOR_ANY = {'exam', 'ward_room', 'treatment_bay', 'icu_bay', 'recovery', 'triage', 'or', 'imaging', 'pharmacy', 'lab',
              'meeting', 'office', 'reception', 'waiting', 'nurse_station', 'scrub', 'sterile'}
BEDS = {'bed': .62, 'exam_couch': .66, 'gurney': .72, 'or_table': .95}
SEATS = {'chair': .46, 'infusion_chair': .46, 'recliner': .46, 'dental': .5, 'alcove_seat': .46, 'sofa': .44,
         'corridor_chair': .46, 'auditorium_seat': .46, 'bench_seat': .46, 'corridor_bench': .46}
MAPSLOT = {'desk': ('desk', 'exam_desk', 'reading_desk'), 'bench': ('lab_bench', 'lab_island'), 'bed': ('bed', 'exam_couch', 'gurney'),
           'scope': ('trolley_tower', 'scope_cabinet'), 'or_table': ('or_table',), 'counter': ('dispensing_counter', 'station_counter', 'nurse_counter', 'reception_desk'),
           'chair': ('infusion_chair', 'recliner', 'dental', 'chair'), 'board': ('whiteboard_stand', 'wall_screen', 'info_screen'),
           'screen': ('wall_screen', 'desk_screen', 'monitor'), 'reading_desk': ('reading_desk', 'desk'), 'table': ('conference_table', 'side_table'), 'post': ('station_counter', 'nurse_counter')}
WALL_ONLY = {'wall_poster', 'clock', 'notice_poster', 'wall_screen', 'lift_sign', 'pharmacy_sign', 'coats', 'trophy'}

# the line a presenter says before a step, by script kind (role words fill in); picked by a hash of the script id
LINES = {
    'inspect': ['Vino lângă {a}. Uită-te atent, apoi te întreb.', 'Hai la {a}. Ce vezi aici contează pentru caz.',
                'Am pus {b} aici pentru tine. Uită-te cu atenție.', 'Oprește-te la {a}. Discutăm după.'],
    'read': ['Am pregătit lectura de azi. Citește, apoi verificăm.', 'Ia lectura asta și parcurge-o cu atenție.',
             'Înainte să mergem mai departe, citește aici.'],
    'station': ['Aici lucrăm cu adevărat. Hai să vedem ce știi.', 'Treci la post. Facem împreună pasul ăsta.',
                'Locul ăsta e al tău acum. Începem?'],
    'case': ['Pacientul e aici. Ascultă-l, apoi hotărăște.', 'Hai la pacient. Vreau să văd cum judeci cazul.',
             'Uite pacientul nostru. Spune-mi ce crezi.'],
    'case_self': ['Bună ziua, doctore. Mi-au spus că mă vedeți dumneavoastră azi.', 'Bună ziua. Puteți să-mi explicați ce am?',
                  'Doctore, aștept de ceva vreme. Mă ajutați?'],
    'dispense': ['Hai să alegem tratamentul. Ce îi dăm?', 'Dulapul cu medicamente e aici. Alege cu grijă.',
                 'Rețeta e pe tejghea. O verificăm împreună?'],
    'colleague': ['Ai un minut? Vreau să-ți arăt ceva.', 'Bine că ai venit. Am o întrebare pentru tine.',
                  'Stai puțin, colega. Discutăm cazul?'],
    'directory': ['Panoul ăsta te orientează. Citește-l întâi.', 'Uite lista. Te ajută să știi unde mergi.'],
    'sort': ['Am amestecat fișele. Le sortezi tu?', 'Pune-le în ordine cu mine.'],
    'sequence': ['Pașii sunt amestecați. Îi așezi în ordine?', 'Hai să punem etapele în ordinea bună.'],
}
OBJ = {'poster': ('afiș', 'afișul'), 'plaque': ('plăcuță', 'plăcuța'), 'board': ('tablă', 'tabla'), 'lightbox': ('negatoscop', 'negatoscopul'),
       'shelf': ('raft', 'raftul'), 'cabinet': ('dulap', 'dulapul'), 'counter': ('tejghea', 'tejgheaua'), 'bench': ('masa de lucru', 'masa de lucru'),
       'cart': ('cărucior', 'căruciorul'), 'monitor': ('monitor', 'monitorul'), 'scope_tower': ('turnul de endoscopie', 'turnul de endoscopie'),
       'model': ('mulaj', 'mulajul'), 'podium': ('pupitru', 'pupitrul'), 'microscope': ('microscop', 'microscopul'), 'iv_stand': ('stativ', 'stativul')}
PART = 'Continuăm de unde am rămas: partea {n}.'
AFTER_GOOD = ['Foarte bine. Mergem mai departe.', 'Excelent, asta e.', 'Exact așa. Bravo.']
AFTER_MIXED = ['Bine. Ce ai greșit revine mai târziu.', 'Nu-i rău. Mai trecem o dată prin asta.', 'Ai prins mare parte. Notează-ți restul.']


def pick(lst, key):
    return lst[int(hashlib.md5(key.encode()).hexdigest(), 16) % len(lst)]


def load_check(pack):
    spec = importlib.util.spec_from_file_location('mg_check', os.path.join(pack, 'check.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Space:
    """rooms, furniture, book props and NPC points of the map; stand-point search against them"""
    def __init__(self, pack, ck):
        mp = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
        self.rooms, self.iprops, self.islots = ck.parse_map(mp)
        self.types = {}
        for line in mp.splitlines():
            p = line.split()
            if len(p) > 3 and p[0] == 'room':
                self.types[p[2]] = p[3]
        self.kinds = json.load(open(os.path.join(pack, 'prop_kinds.json'), encoding='utf-8'))
        self.ck = ck
        self.objs = {rid: ck.room_objects(r, rid, self.iprops, self.islots, self.kinds) for rid, r in self.rooms.items()}
        self.furn = {rid: r.get('furn', []) for rid, r in self.rooms.items()}
        self.prop = {p[2]: p for p in self.iprops}
        self.slot = {s[2]: s for s in self.islots}
        self.taken = {}                         # room -> [(x, z)] NPC spots already used
        self.facts = [t for l in open(os.path.join(pack, 'facts.jsonl'), encoding='utf-8') for t in (json.loads(l).get('text') or '', json.loads(l).get('claim') or '')]

    def clear(self, rid, x, z, r=.3, ignore=()):
        R = self.rooms[rid]
        if not (R['x0'] + .35 <= x <= R['x1'] - .35 and R['z0'] + .35 <= z <= R['z1'] - .35):
            return False
        for name, rc, mount, _ in self.objs[rid]:
            if name in ignore or name.split('.')[1:2] and name.split('.')[1] in WALL_ONLY:
                continue
            if rc[0] - r < x < rc[2] + r and rc[1] - r < z < rc[3] + r:
                return False
        for d in R['doors']:                    # keep door approaches free (0.9 m inside the door)
            dx, dz, w, side = d
            ix, iz = {'N': (0, 1), 'S': (0, -1), 'E': (-1, 0), 'W': (1, 0)}[side]
            if abs((x - dx) * (1 - abs(ix)) + (z - dz) * (1 - abs(iz))) < w / 2 + .2 and 0 <= (x - dx) * ix + (z - dz) * iz < .9:
                return False
        for tx, tz in self.taken.get(rid, []):
            if math.hypot(tx - x, tz - z) < .75:
                return False
        return True

    def take(self, rid, x, z):
        self.taken.setdefault(rid, []).append((x, z))

    def beside(self, rid, x, z, yaw, w, d, name='', face=None):
        """a stand point next to an object: in front of it, off to one side, then the sides; yaw faces out of the object
        (or towards `face`)"""
        t = math.radians(yaw)
        fx, fz, rx, rz = math.sin(t), math.cos(t), math.cos(t), -math.sin(t)
        cands = []
        for v, u in ((d / 2 + .5, w / 2 + .1), (d / 2 + .5, -w / 2 - .1), (d / 2 + .55, 0), (0, w / 2 + .45), (0, -w / 2 - .45),
                     (d / 2 + .9, w / 2 + .3), (d / 2 + .9, -w / 2 - .3), (d / 2 + 1.2, 0), (d / 2 + .5, w / 2 + .6), (d / 2 + .5, -w / 2 - .6)):
            cands.append((x + v * fx + u * rx, z + v * fz + u * rz))
        for cx, cz in cands:
            if self.clear(rid, cx, cz, ignore=(name,)):
                fy = yaw if face is None else math.degrees(math.atan2(face[0] - cx, face[1] - cz))
                return round(cx, 2), round(cz, 2), round(fy) % 360
        return None

    def anywhere(self, rid, near=None):
        R = self.rooms[rid]
        best = None
        for i in range(1, 40):
            for j in range(1, 40):
                x = R['x0'] + (R['x1'] - R['x0']) * i / 40
                z = R['z0'] + (R['z1'] - R['z0']) * j / 40
                if self.clear(rid, x, z):
                    c = (x, z)
                    dd = math.hypot(x - near[0], z - near[1]) if near else math.hypot(x - (R['x0'] + R['x1']) / 2, z - (R['z0'] + R['z1']) / 2)
                    if best is None or dd < best[0]:
                        best = (dd, c)
        return None if best is None else (round(best[1][0], 2), round(best[1][1], 2), 0)

    def furniture(self, rid, kinds, near=None, used=()):
        L = [p for p in self.furn.get(rid, []) if p[1] in kinds and p[0] not in used]
        if near:
            L.sort(key=lambda p: math.hypot(p[2] - near[0], p[3] - near[1]))
        return L

    def anchor(self, sc):
        """(room, x, z, yaw, w, d, name) of the object a script is played at"""
        a = sc.get('prop') or sc.get('slot')
        if a in self.prop:
            p = self.prop[a]
            k = self.kinds['kinds'].get(p[3], {'w': .6, 'd': .6})
            return p[1], p[4], p[5], p[6], k['w'], k['d'], a
        if a in self.slot:
            s = self.slot[a]
            return s[1], s[4], s[5], s[6], .5, .5, a
        kinds = MAPSLOT.get(a or '', ())
        L = self.furniture(sc['room'], kinds)
        if L:
            p = L[0]
            return sc['room'], p[2], p[3], p[4], p[5], p[6], p[0]
        R = self.rooms[sc['room']]
        return sc['room'], (R['x0'] + R['x1']) / 2, (R['z0'] + R['z1']) / 2, 0, .4, .4, ''


def build(pack):
    ck = load_check(pack)
    SP = Space(pack, ck)
    src = json.load(open(os.path.join(pack, 'missions_src.json'), encoding='utf-8'))
    scripts = [s for s in json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts'] if s.get('topic')]
    order = [st['topic'] for ch in json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))['chapters'] for st in ch['steps']]
    by_topic = {}
    for s in scripts:
        by_topic.setdefault(s['topic'], []).append(s)
    cast = {c['id']: c for c in src['cast']}
    SN, PLACE = src.get('script_npc', {}), src.get('step_place', {})
    out_m = []
    for m in src['missions']:
        quests = []
        for q in m['quests']:
            steps = []
            for sc in by_topic[q['topic']]:
                if not sc.get('required', True):
                    continue
                room, ax, az, ayaw, aw, ad, aname = SP.anchor(sc)
                moved = sc['id'] in PLACE
                pres = SN.get(sc['id']) or q['host']
                patient = None
                if moved:                         # the step happens elsewhere (logic rule): at the right furniture there
                    room = PLACE[sc['id']]['room']
                    want = ('dispensing_counter', 'station_counter', 'nurse_counter', 'supply_cart', 'cabinet', 'tall_cabinet', 'shelf', 'desk', 'exam_desk') \
                        if sc['kind'] == 'dispense' else tuple(BEDS) + ('chair', 'infusion_chair', 'recliner')
                    L = SP.furniture(room, want)
                    if L:
                        p = L[0]
                        ax, az, ayaw, aw, ad, aname = p[2], p[3], p[4], p[5], p[6], p[0]
                    else:
                        R = SP.rooms[room]
                        ax, az, ayaw, aw, ad, aname = (R['x0'] + R['x1']) / 2, (R['z0'] + R['z1']) / 2, 0, .4, .4, ''
                rtype = SP.types[room]
                if sc['kind'] == 'case' and rtype in CLINICAL | {'pharmacy'}:
                    pc = m.get('patient')
                    if pres in cast and cast[pres]['kind'] == 'patient':
                        pc = pres
                    if pc:
                        used = [p for st in steps for p in ([st['patient']['seat']] if st.get('patient') and st['patient'].get('seat') else [])]
                        bed = SP.furniture(room, tuple(BEDS), (ax, az), used)
                        seat = SP.furniture(room, tuple(SEATS), (ax, az), used)
                        if bed:
                            p = bed[0]
                            patient = {'cast': pc, 'x': p[2], 'z': p[3], 'yaw': p[4], 'pose': 'lie', 'y': BEDS[p[1]], 'seat': p[0]}
                        elif seat:
                            p = seat[0]
                            patient = {'cast': pc, 'x': p[2], 'z': p[3], 'yaw': p[4], 'pose': 'sit', 'y': 0, 'seat': p[0]}
                        else:
                            sp = SP.beside(room, ax, az, ayaw, aw, ad, aname)
                            if sp:
                                patient = {'cast': pc, 'x': sp[0], 'z': sp[1], 'yaw': sp[2], 'pose': 'stand', 'y': 0, 'seat': ''}
                        if patient:
                            ax, az, ayaw, aw, ad, aname = patient['x'], patient['z'], patient['yaw'], 1.0, 2.1 if patient['pose'] == 'lie' else .6, patient['seat']
                if patient and pres == patient['cast']:
                    stand = (patient['x'], patient['z'], patient['yaw'])
                elif sc.get('slot') in SP.slot and not moved:
                    stand = (ax, az, ayaw)
                else:
                    stand = SP.beside(room, ax, az, ayaw, aw, ad, aname, face=(ax, az) if patient else None) or SP.anywhere(room, (ax, az))
                if not (patient and pres == patient['cast']):
                    SP.take(room, stand[0], stand[1])
                kind = sc['kind']
                obj = OBJ.get(SP.prop[sc['prop']][3], ('panou', 'panoul')) if sc.get('prop') in SP.prop else ('panou', 'panoul')
                key = 'case_self' if (patient and pres == patient['cast']) else kind
                line = PART.format(n=sc['part']) if sc.get('part', 1) > 1 else pick(LINES.get(key, LINES['station']), sc['id']).format(a=obj[0], b=obj[1])
                steps.append({'script': sc['id'], 'kind': kind, 'presenter': pres, 'room': room, 'f': SP.rooms[room]['floor'],
                              'stand': [round(stand[0], 2), round(stand[1], 2), int(stand[2]) % 360], 'at': [round(ax, 2), round(az, 2)],
                              'moved': PLACE[sc['id']]['why'] if moved else '', 'patient': patient, 'line': line,
                              'after': [pick(AFTER_GOOD, sc['id']), pick(AFTER_MIXED, sc['id'])]})
            quests.append({**q, 'steps': steps})
        out_m.append({**m, 'quests': quests})
    # idle posts: every cast member somewhere sensible in their post room (patients in a bed or a seat)
    posts = {}
    for c in src['cast']:
        rid = c['post']
        if c['kind'] == 'patient':
            ward = next((m.get('ward') for m in src['missions'] if m.get('patient') == c['id'] and m.get('ward')), None) or rid
            used = [p.get('seat') for p in posts.values()]
            bed = SP.furniture(ward, tuple(BEDS), None, used)
            seat = SP.furniture(ward, tuple(SEATS), None, used)
            if bed:
                p = bed[0]
                posts[c['id']] = {'room': ward, 'x': p[2], 'z': p[3], 'yaw': p[4], 'pose': 'lie', 'y': BEDS[p[1]], 'seat': p[0]}
                continue
            if seat:
                p = seat[0]
                posts[c['id']] = {'room': ward, 'x': p[2], 'z': p[3], 'yaw': p[4], 'pose': 'sit', 'y': 0, 'seat': p[0]}
                continue
            rid = ward
        work = SP.furniture(rid, ('desk', 'exam_desk', 'reading_desk', 'dispensing_counter', 'station_counter', 'nurse_counter',
                                  'reception_desk', 'lab_bench', 'lab_island', 'podium', 'lectern', 'trolley_tower', 'or_table'))
        sp = None
        for p in work:
            sp = SP.beside(rid, p[2], p[3], p[4], p[5], p[6], p[0])
            if sp:
                break
        sp = sp or SP.anywhere(rid)
        SP.take(rid, sp[0], sp[1])
        posts[c['id']] = {'room': rid, 'x': sp[0], 'z': sp[1], 'yaw': sp[2], 'pose': 'stand', 'y': 0, 'seat': ''}
    for c in posts.values():
        c['f'] = SP.rooms[c['room']]['floor']
    res = {'version': 1, 'note': 'built by tools/book/missions.py from missions_src.json + scripts.json + institute.map',
           'cast': src['cast'], 'arcs': src['arcs'], 'greetings': src.get('greetings', {}), 'posts': posts, 'missions': out_m}
    return res, scripts, order, SP


def check(res, scripts, order, SP):
    errs, notes = [], []
    req = [s['id'] for s in scripts if s.get('required', True)]
    seen = [st['script'] for m in res['missions'] for q in m['quests'] for st in q['steps']]
    dup = {x for x in seen if seen.count(x) > 1}
    miss = set(req) - set(seen)
    if dup:
        errs.append(f'scripts in more than one step: {sorted(dup)[:5]}')
    if miss:
        errs.append(f'required scripts in no mission: {len(miss)} e.g. {sorted(miss)[:5]}')
    topics = [q['topic'] for m in res['missions'] for q in m['quests']]
    if topics != order:
        errs.append('quests are not the 50 topics in book order')
    want = [s['id'] for t in order for s in scripts if s['topic'] == t and s.get('required', True)]
    if seen != want:
        errs.append('steps are not in book order')
    cast = {c['id'] for c in res['cast']}
    for m in res['missions']:
        for k in ('giver', 'patient'):
            if m.get(k) and m[k] not in cast:
                errs.append(f'{m["id"]}: unknown {k} {m[k]}')
        if m.get('ward') and SP.types.get(m['ward']) not in CLINICAL:
            errs.append(f'{m["id"]}: ward {m["ward"]} is not a clinical room')
        for q in m['quests']:
            if q['host'] not in cast:
                errs.append(f'{m["id"]}/{q["topic"]}: unknown host')
            for st in q['steps']:
                t = SP.types.get(st['room'])
                if st['presenter'] not in cast:
                    errs.append(f'{st["script"]}: unknown presenter {st["presenter"]}')
                if t not in INDOOR_ANY:
                    errs.append(f'{st["script"]}: happens in {st["room"]} ({t})')
                if st['kind'] in PLACE_OK and t not in PLACE_OK[st['kind']]:
                    errs.append(f'{st["script"]}: a {st["kind"]} step in a {t} room ({st["room"]})')
                if st['kind'] == 'case' and not st['patient']:
                    notes.append(f'{st["script"]}: case step without a patient in the room (chart review)')
                R = SP.rooms[st['room']]
                x, z = st['stand'][0], st['stand'][1]
                if not (R['x0'] < x < R['x1'] and R['z0'] < z < R['z1']):
                    errs.append(f'{st["script"]}: stand point outside its room')
    for cid, p in res['posts'].items():
        if SP.types.get(p['room']) == 'terrace':
            errs.append(f'{cid}: posted outdoors')
    # the lines frame the book, they never carry it: <= 160 characters, comma-below ș ț, no 8-word run of a fact
    lines = [('arc', l) for a in res['arcs'] for l in a['intro'] + a['outro']] + [(m['id'], l) for m in res['missions'] for l in m['intro'] + m['outro'] + [m['case']]]
    lines += [(m['id'] + '/' + q['topic'], l) for m in res['missions'] for q in m['quests'] for l in q['intro'] + [q['done']]]
    lines += [(st['script'], l) for m in res['missions'] for q in m['quests'] for st in q['steps'] for l in [st['line']] + st['after']]
    lines += [('greeting', l) for v in res.get('greetings', {}).values() for l in v]
    W = lambda t: re.sub(r'[^\w ]+', ' ', t.lower()).split()
    grams = set()
    for f in SP.facts:
        w = W(f)
        grams.update(' '.join(w[i:i + 8]) for i in range(len(w) - 7))
    for tag, l in lines:
        if len(l) > 160:
            errs.append(f'{tag}: line longer than 160 characters')
        if 'ş' in l or 'ţ' in l or 'Ş' in l or 'Ţ' in l:
            errs.append(f'{tag}: cedilla ş/ţ instead of comma-below ș/ț')
        w = W(l)
        if any(' '.join(w[i:i + 8]) in grams for i in range(len(w) - 7)):
            errs.append(f'{tag}: 8 or more words of a fact in a dialogue line')
    return errs, notes


def main(pack, *flags):
    res, scripts, order, SP = build(pack)
    errs, notes = check(res, scripts, order, SP)
    n = sum(len(q['steps']) for m in res['missions'] for q in m['quests'])
    moved = sum(1 for m in res['missions'] for q in m['quests'] for st in q['steps'] if st['moved'])
    pat = sum(1 for m in res['missions'] for q in m['quests'] for st in q['steps'] if st['patient'])
    print(f'missions: {len(res["missions"])} cases, {sum(len(m["quests"]) for m in res["missions"])} quests, {n} steps '
          f'(every required script once, book order: {"yes" if not errs else "NO"}); cast {len(res["cast"])}; '
          f'{pat} steps with the patient in a bed / seat; {moved} steps moved to a room that fits them; {len(notes)} chart reviews')
    for e in errs:
        print('  FAIL', e)
    if '--check' not in flags and not errs:
        with open(os.path.join(pack, 'missions.json'), 'w', encoding='utf-8') as fo:
            json.dump(res, fo, ensure_ascii=False, indent=1)
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))

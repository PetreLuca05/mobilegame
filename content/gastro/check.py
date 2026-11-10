#!/usr/bin/env python3
"""BOOK-1 content pack checker. Fails loudly (exit 1) unless every ledger and rule is 100 %.

  python3 check.py [PACK] [--topics t1,t2] [--from-topics] [--leak REPO]

Ledgers
  0 words      -> atoms   every word of the PDF text layer is used by exactly one atom (wordledger.json from the build)
  1 characters -> atoms   every non-whitespace character of cleaned.txt lies in exactly one atom; every atom has a topic
  2 sentences  -> facts   every sentence lies inside a fact span; every number in a sentence is named by a fact of it
  3 facts      -> items   every fact is TESTED by items of >= 2 types; tested = the item's answer depends on it (keyed
                          answer, statement judged true/false, correct option, gap, card, entry / pair / position);
                          appearing as a wrong option does not count
  4 facts      -> scripts every fact is tested by a required script in its topic's room; rooms / props exist in the map
Rules (author's check-in §2): every fact tested by >= 1 simplu/multiplu (or listed in exam_exempt.json, reported);
5 options; simplu 1 correct; multiplu 2-4 correct and, in a topic with >= 3 multiplu, 2, 3 and 4 each used; a wrong option
is not an answer to the same question key anywhere in the book; a false statement is no fact's claim; every false
multiplu option quotes the book sentence that makes it false with its page; cloze: one gap unless a list, gaps are not
function words and appear in the source; no length / repeated-word cue, options share one form; no duplicate stems;
every explanation quotes its source sentence and page.
Staleness: atoms changed since atoms.lock.json, facts whose span text changed, items whose source sentence changed are
listed (and fail) so only those are rebuilt after a fixes change.
--from-topics reads facts / items / scripts / props from topics/*.json (no merge needed; parallel authoring).
--leak REPO fails if a file tracked in REPO holds a run of >= 12 consecutive words of the book (leakguard.txt).
"""
import collections, hashlib, json, os, re, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXAM = {'simplu', 'multiplu'}
ITEM_TYPES = {'simplu', 'multiplu', 'card', 'match', 'order', 'label', 'cloze', 'sort'}
FUNCTION = set('''a ai al ale ar au ca care că ce cea cei cel cele cu da dar de dacă din după e ei el este fi fie i iar
în îi îl la le lui mai mult nu o or ori pe pentru prin până sa să se si și sunt un una unei unor unui va vor acest aceasta
această acestea aceste acești acel aceea acela atât cât cum când doar deci decât încă tot toate toți foarte'''.split())
STOP = FUNCTION | set('''următoarele următoarea următorul dintre conform textului textul text capitolului capitolul
afirmații afirmația afirmațiile corecte corectă corect este sunt care despre privind asociat asociată asociate
pacienții pacienților pacienți pacientul bolii boala boli tratamentul tratament'''.split())


def load_jsonl(p):
    return [json.loads(l) for l in open(p, encoding='utf-8') if l.strip()] if os.path.exists(p) else []


def ws(s):
    return re.sub(r'\s+', ' ', s).strip()


def norm(s):
    s = s.lower().replace('ş', 'ș').replace('ţ', 'ț')
    s = re.sub(r'[„”"«»().,;:!?…\-–—•\s]+', ' ', s)
    return s.strip()


def words_of(s):
    return [w for w in re.findall(r'[a-zăâîșț0-9]+', norm(s)) if len(w) >= 5 and w not in STOP]


NUM_RE = re.compile(r'\d+(?:[.,]\d+)*')


def answer_fields(it):
    """every text a candidate produces or picks as correct"""
    t = it['type']
    if t == 'cloze':
        return [(('answers', i), a) for i, a in enumerate(it['answers'])]
    if t in ('simplu', 'multiplu'):
        return [(('options', i), o['t']) for i, o in enumerate(it['options']) if o.get('ok')]
    if t == 'card':
        return [(('answer',), it['answer'])]
    if t == 'sort':
        return ([(('entries', i), e['t']) for i, e in enumerate(it['entries'])] + [(('groups', i), g) for i, g in enumerate(it['groups'])]
                + [(('entry groups', i), e['group']) for i, e in enumerate(it['entries'])])
    if t == 'match':
        return [(('pairs', i), p['left']) for i, p in enumerate(it['pairs'])] + [(('pairs', i), p['right']) for i, p in enumerate(it['pairs'])]
    if t == 'order':
        return [(('sequence', i), x) for i, x in enumerate(it['sequence'])]
    if t == 'label':
        return [(('labels', i), l['t']) for i, l in enumerate(it['labels'])]
    return []


def leak_scan(pack, repo):
    guard = set(l.strip() for l in open(os.path.join(pack, 'leakguard.txt')) if l.strip() and not l.startswith('#'))
    WORD = re.compile(r'[0-9a-zăâîșțáéíóúöüßçñµα-ω]+', re.I)
    skip = ('third_party/', 'shaders/compiled/', 'assets/fonts/', '.png', '.jpg', '.mgf', '.ttf', '.otf', '.pdf', '.zip', 'leakguard.txt')
    files = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True).stdout.split(b'\0')
    bad = []
    for f in files:
        f = f.decode()
        p = os.path.join(repo, f)
        if not f or any(x in f for x in skip) or not os.path.isfile(p) or os.path.getsize(p) > 5_000_000:
            continue
        try:
            t = open(p, encoding='utf-8').read()
        except UnicodeDecodeError:
            continue
        t = t.replace('ş', 'ș').replace('ţ', 'ț').replace('­', '')
        w = [x.lower() for x in WORD.findall(t)]
        for i in range(len(w) - 11):
            if hashlib.sha1(' '.join(w[i:i + 12]).encode()).hexdigest()[:16] in guard:
                bad.append(f'{f}: "{" ".join(w[i:i + 12])}"')
                break
    return bad


# ---------------------------------------------------------------------------------------------------------- placement
CELL = 0.1


def parse_map(mapsrc):
    """rooms {id: {floor, x0, z0, x1, z1, open, doors: [(x, z, w, side)], slots: [(kind, x, z, yaw)]}},
    iprops [(floor, room, id, kind, x, z, yaw, label)], islots [(floor, room, id, kind, x, z, yaw)]"""
    rooms, cur = {}, None
    for line in mapsrc.splitlines():
        line = line.split('#')[0].strip()
        m = re.match(r'room\s+(-?\d+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)', line)
        if m:
            cur = m.group(2)
            rooms[cur] = {'floor': int(m.group(1)), 'x0': float(m.group(3)), 'z0': float(m.group(4)), 'x1': float(m.group(5)),
                          'z1': float(m.group(6)), 'doors': [], 'slots': [], 'open': False}
            continue
        m = re.match(r'door\s+-?\d+\s+(\S+)\s+(\S+)\s+(\S+)\s+([NSEW])', line)
        if m and cur:
            rooms[cur]['doors'].append((float(m.group(1)), float(m.group(2)), float(m.group(3)), m.group(4)))
            continue
        m = re.match(r'slot\s+-?\d+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)', line)
        if m and cur:
            rooms[cur]['slots'].append((m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4))))
    for m in re.finditer(r'^prop\s+(-?\d+)\s+(\S+)\s+(\S+\.\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(wall|free|floor|door)', mapsrc, re.M):
        if m.group(2) in rooms:                   # furniture of the institute model, one record per piece
            rooms[m.group(2)].setdefault('furn', []).append((m.group(3), m.group(4), float(m.group(5)), float(m.group(6)), float(m.group(7)),
                                                             float(m.group(8)), float(m.group(9)), m.group(11)))
    iprops = [(int(a), b, c, d, float(e), float(f), float(g), h) for a, b, c, d, e, f, g, h in
              re.findall(r'^iprop\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"([^"]*)"', mapsrc, re.M)]
    islots = [(int(a), b, c, d, float(e), float(f), float(g)) for a, b, c, d, e, f, g in
              re.findall(r'^islot\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)', mapsrc, re.M)]
    return rooms, iprops, islots


def footprint(x, z, yaw, w, d):
    q = int(round(yaw / 90.0)) % 2
    hw, hd = (w / 2, d / 2) if q == 0 else (d / 2, w / 2)
    return (x - hw, z - hd, x + hw, z + hd)


def overlap(a, b, eps=0.02):
    return a[0] < b[2] - eps and b[0] < a[2] - eps and a[1] < b[3] - eps and b[1] < a[3] - eps


def room_objects(room, rid, iprops, islots, kinds):
    """every object of a room: (name, rect, mount, is_new)"""
    K, SK, MF = kinds['kinds'], kinds['slot_kinds'], kinds['map_slot_furniture']
    objs = []
    for fid, kind, x, z, yaw, w, d, mount in room.get('furn', []):
        if mount in ('free', 'wall'):             # rugs are walked on; door leaves: the door approach rule covers their swing
            objs.append((fid, footprint(x, z, yaw, w, d), mount, False))
    if not room.get('furn'):
        for kind, x, z, yaw in room['slots']:
            f = MF.get(kind)
            if f:
                objs.append((f'map slot {kind}@{x},{z}', footprint(x, z, yaw, f['w'], f['d']), 'free', False))
    for fl, r, pid, kind, x, z, yaw, _ in iprops:
        if r == rid:
            k = K.get(kind, {'w': 0.6, 'd': 0.6, 'mount': 'free'})
            objs.append((pid, footprint(x, z, yaw, k['w'], k['d']), k['mount'], True))
    for fl, r, sid, kind, x, z, yaw in islots:
        if r == rid:
            k = SK.get(kind, {'w': 0.5, 'd': 0.5})
            objs.append((sid, footprint(x, z, yaw, k['w'], k['d']), 'free', True))
    return objs


def load_occupancy(pack):
    """model_occupancy.json (tools/book/model_occupancy.js): the institute model's own furniture, 0.1 m cells per storey"""
    p = os.path.join(pack, 'model_occupancy.json')
    if not os.path.exists(p):
        return None
    d = json.load(open(p, encoding='utf-8'))
    grids = []
    for rows in d['storeys']:
        g = bytearray(d['nx'] * d['nz'])
        for j, r in enumerate(rows):
            for k in range(0, len(r), 2):
                g[j * d['nx'] + r[k]: j * d['nx'] + r[k] + r[k + 1]] = b'\x01' * r[k + 1]
        grids.append(g)
    return {'x0': d['x0'], 'z0': d['z0'], 'c': d['cell'], 'nx': d['nx'], 'nz': d['nz'], 'g': grids}


def occ_hits(occ, f, rc, shrink=0.02):
    """cells of the model furniture under rect rc on storey f"""
    if not occ or f < 0 or f >= len(occ['g']):
        return 0
    c, nx = occ['c'], occ['nx']
    i0, i1 = int((rc[0] + shrink - occ['x0']) / c), int((rc[2] - shrink - occ['x0']) / c)
    j0, j1 = int((rc[1] + shrink - occ['z0']) / c), int((rc[3] - shrink - occ['z0']) / c)
    g = occ['g'][f]
    return sum(g[j * nx + i] for j in range(max(0, j0), min(occ['nz'] - 1, j1) + 1) for i in range(max(0, i0), min(nx - 1, i1) + 1))


def on_model_ids(mapsrc):
    """props / NPC points that are a piece of the model's furniture (trailing on_model <furniture id>): {id: furniture id}"""
    return dict(re.findall(r'^i(?:prop|slot)\s+-?\d+\s+\S+\s+(\S+)\s.*\son_model\s+(\S+)\s*$', mapsrc, re.M))


def placement_check(rooms, iprops, islots, kinds, only=None, occ=None, onm=frozenset()):
    """-> (failures [(room, object, problem)], per-room standability grids are local)"""
    fails = []
    for rid, room in rooms.items():
        objs = room_objects(room, rid, iprops, islots, kinds)
        news = [o for o in objs if o[3] and (only is None or o[0] in only)]
        yaws = {pid: yaw for fl, r, pid, kind, x, z, yaw, _ in iprops if r == rid}
        if not news:
            continue
        ix0, iz0, ix1, iz1 = room['x0'] + 0.1, room['z0'] + 0.1, room['x1'] - 0.1, room['z1'] - 0.1
        approach = []
        for dx, dz, dw, side in room['doors']:
            if side in 'NS':
                zz = dz + (1.0 if abs(dz - room['z0']) < abs(dz - room['z1']) else -1.0)
                approach.append((dx - dw / 2, min(dz, zz), dx + dw / 2, max(dz, zz)))
            else:
                xx = dx + (1.0 if abs(dx - room['x0']) < abs(dx - room['x1']) else -1.0)
                approach.append((min(dx, xx), dz - dw / 2, max(dx, xx), dz + dw / 2))
        nx, nz = int(round((ix1 - ix0) / CELL)), int(round((iz1 - iz0) / CELL))
        blocked = [[False] * nx for _ in range(nz)]
        for _, rc, _, _ in objs:
            for j in range(max(0, int((rc[1] - iz0) / CELL)), min(nz, int((rc[3] - iz0) / CELL) + 1)):
                for i in range(max(0, int((rc[0] - ix0) / CELL)), min(nx, int((rc[2] - ix0) / CELL) + 1)):
                    blocked[j][i] = True
        if occ:                                    # the model's own furniture blocks standing too
            f = room['floor']
            for j in range(nz):
                for i in range(nx):
                    if not blocked[j][i]:
                        cx, cz = ix0 + (i + 0.5) * CELL, iz0 + (j + 0.5) * CELL
                        oi, oj = int((cx - occ['x0']) / occ['c']), int((cz - occ['z0']) / occ['c'])
                        if 0 <= oi < occ['nx'] and 0 <= oj < occ['nz'] and occ['g'][f][oj * occ['nx'] + oi]:
                            blocked[j][i] = True
        # standable: a 0.6 m square (6 x 6 cells) free of objects and inside the room
        S = [[0] * (nx + 1) for _ in range(nz + 1)]
        for j in range(nz):
            for i in range(nx):
                S[j + 1][i + 1] = S[j][i + 1] + S[j + 1][i] - S[j][i] + (1 if blocked[j][i] else 0)
        stand = [[False] * nx for _ in range(nz)]
        for j in range(3, nz - 3):
            for i in range(3, nx - 3):
                if S[j + 3][i + 3] - S[j - 3][i + 3] - S[j + 3][i - 3] + S[j - 3][i - 3] == 0:
                    stand[j][i] = True
        # reachable from the doors (or anywhere for an open room)
        seen = [[False] * nx for _ in range(nz)]
        q = []
        for j in range(nz):
            for i in range(nx):
                if stand[j][i]:
                    cx, cz = ix0 + (i + 0.5) * CELL, iz0 + (j + 0.5) * CELL
                    if room['open'] or not room['doors'] or any(a[0] - 0.4 <= cx <= a[2] + 0.4 and a[1] - 0.4 <= cz <= a[3] + 0.4 for a in approach):
                        seen[j][i] = True
                        q.append((j, i))
        while q:
            j, i = q.pop()
            for dj, di in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, b = j + dj, i + di
                if 0 <= a < nz and 0 <= b < nx and stand[a][b] and not seen[a][b]:
                    seen[a][b] = True
                    q.append((a, b))
        FRECT = {x[0]: footprint(x[2], x[3], x[4], x[5], x[6]) for x in room.get('furn', [])}
        news = [(n, FRECT.get(onm[n], rc) if n in onm else rc, m, i) for n, rc, m, i in news]   # an anchor is its furniture piece
        for name, rc, mount, _ in news:
            if name not in onm and not (rc[0] >= ix0 - 0.001 and rc[2] <= ix1 + 0.001 and rc[1] >= iz0 - 0.001 and rc[3] <= iz1 + 0.001):
                fails.append((rid, name, 'outside the room'))
                continue
            if name in onm:                        # the model's own podium / shelf / poster / chair: a furniture record of this room
                if onm[name] not in {x[0] for x in room.get('furn', [])}:
                    fails.append((rid, name, f'on_model {onm[name]} is not a furniture record of this room'))
                for other, orc, _, isnew in objs:
                    if other != name and isnew and other not in onm and overlap(rc, orc):
                        fails.append((rid, name, f'overlaps {other}'))
                        break
            else:
                if mount == 'free' and min(rc[0] - ix0, ix1 - rc[2], rc[1] - iz0, iz1 - rc[3]) < 0.3 - 0.001:
                    fails.append((rid, name, 'closer than 0.3 m to a wall'))
                if mount == 'wall':
                    back = {0: rc[1] - iz0, 1: rc[0] - ix0, 2: iz1 - rc[3], 3: ix1 - rc[2]}[int(round(yaws[name] / 90.0)) % 4]
                    if back > 0.05:
                        fails.append((rid, name, f'wall-mounted but {back:.2f} m off the wall behind it'))
                for a in approach:
                    if overlap(rc, a):
                        fails.append((rid, name, 'in a door opening or its 1.0 m approach'))
                        break
                for other, orc, _, _ in objs:
                    if other != name and overlap(rc, orc):
                        fails.append((rid, name, f'overlaps {other}'))
                        break
                if occ and occ_hits(occ, room['floor'], rc):
                    fails.append((rid, name, "overlaps the model's own furniture"))
            ok = False
            for j in range(max(0, int((rc[1] - 0.5 - iz0) / CELL)), min(nz, int((rc[3] + 0.5 - iz0) / CELL) + 1)):
                for i in range(max(0, int((rc[0] - 0.5 - ix0) / CELL)), min(nx, int((rc[2] + 0.5 - ix0) / CELL) + 1)):
                    if seen[j][i]:
                        cx, cz = ix0 + (i + 0.5) * CELL, iz0 + (j + 0.5) * CELL
                        gx = max(rc[0] - cx, 0, cx - rc[2]) - 0.3
                        gz = max(rc[1] - cz, 0, cz - rc[3]) - 0.3
                        if max(gx, gz) <= 0.2:
                            ok = True
                            break
                if ok:
                    break
            if not ok:
                fails.append((rid, name, 'no free 0.6 m standing cell beside it that connects to the door'))
    return fails


# ---------------------------------------------------------------------------------------------------------- review sheets
YES = {'y', 'yes', 'ok', 'da', 'd', '1', 'x', '✓'}


def tools_dir(pack):
    for d in (os.path.join(pack, '..', 'tools', 'book'), os.path.join(pack, '..', '..', 'tools', 'book')):
        if os.path.exists(os.path.join(d, 'build.py')):
            return os.path.abspath(d)
    sys.exit('tools/book not found next to the pack')


def apply_review(pack):
    """Read review_errata.csv / review_hidden.csv back: decisions go into errata_decisions.json (-> errata.json) and
    fixes/review.json (-> fixes.json); hidden-text confirmations into fixes/pNN.json (status verified) or
    hidden_confirmed.json. Then rebuild the text (build, atoms, errata; not the topics) and list what must be rebuilt."""
    import csv, subprocess
    pack = os.path.abspath(pack)
    atoms = load_jsonl(os.path.join(pack, 'atoms.jsonl'))
    A = {a['id']: a for a in atoms}
    errata = {e['id']: e for e in json.load(open(os.path.join(pack, 'errata.json'), encoding='utf-8'))}
    fdir = os.path.join(pack, 'fixes')
    rv_path = os.path.join(fdir, 'review.json')
    review = json.load(open(rv_path, encoding='utf-8')) if os.path.exists(rv_path) else []
    rv_ids = {f['id'] for f in review}
    dp = os.path.join(pack, 'errata_decisions.json')
    dec = json.load(open(dp, encoding='utf-8')) if os.path.exists(dp) else {}
    hc_path = os.path.join(pack, 'hidden_confirmed.json')
    confirmed = json.load(open(hc_path, encoding='utf-8')) if os.path.exists(hc_path) else []
    bad, log = [], collections.Counter()

    def page_count(pdf_page, s_):
        return sum(a['text'].count(s_) for a in atoms if a['pdf_page'] == pdf_page)

    ef = os.path.join(pack, 'review_errata.csv')
    use_changed = []
    for r in csv.DictReader(open(ef, encoding='utf-8-sig')) if os.path.exists(ef) else []:
        eid = r['id']
        u = (r.get('use_intended') or '').strip().lower()
        if u and eid in errata and errata[eid]['kind'] == 'typo':
            if u not in ('yes', 'no', 'da', 'nu', 'y', 'n'):
                bad.append(f'review_errata.csv {eid}: use_intended {u!r} is not yes / no')
            else:
                want = u in ('yes', 'da', 'y')
                if want != bool(errata[eid].get('use_intended')):
                    dec.setdefault(eid, {})['use_intended'] = want
                    use_changed.append(eid)
                    log['use_intended ' + ('yes' if want else 'no')] += 1
                elif eid in dec and 'use_intended' in dec[eid]:
                    dec[eid]['use_intended'] = want
        d = (r.get('decision') or '').strip()
        if not d:
            continue
        if eid not in errata:
            bad.append(f'review_errata.csv: unknown erratum {eid}')
            continue
        m = re.match(r'(?i)(correct|corect|corectează)\s*:\s*(.+)$', d)
        if d.lower() in ('keep', 'păstrează', 'pastreaza'):
            dec[eid] = {**{x: y for x, y in dec.get(eid, {}).items() if x == 'use_intended'}, 'decision': 'keep'}
        elif d.lower() in ('not_an_error', 'not an error', 'nu e eroare', 'nu este eroare'):
            dec[eid] = {**{x: y for x, y in dec.get(eid, {}).items() if x == 'use_intended'}, 'decision': 'not_an_error'}
        elif m:
            text = m.group(2).strip()
            dec[eid] = {**{x: y for x, y in dec.get(eid, {}).items() if x == 'use_intended'}, 'decision': 'correct', 'text': text}
            e = errata[eid]
            for pg in sorted({A[a]['pdf_page'] for a in e['atoms'] if a in A}):
                fid = f'rv-{eid}-p{pg:02d}'
                n = page_count(pg, e['printed'])
                if fid not in rv_ids and n:
                    review.append({'id': fid, 'pdf_page': pg, 'find': e['printed'], 'replace': text, 'count': n,
                                   'why': f'author review of {eid} ({e["kind"]}): {e["issue"]}', 'status': 'author'})
                    rv_ids.add(fid)
        else:
            bad.append(f'review_errata.csv {eid}: decision {d!r} is not keep / not_an_error / correct: <text>')
            continue
        log['errata ' + dec[eid]['decision']] += 1
    hf = os.path.join(pack, 'review_hidden.csv')
    page_fixes = {}
    for r in csv.DictReader(open(hf, encoding='utf-8-sig')) if os.path.exists(hf) else []:
        ok = (r.get('ok') or '').strip()
        if not ok:
            continue
        a = A.get(r['atom'])
        if not a:
            bad.append(f"review_hidden.csv: unknown atom {r['atom']}")
            continue
        fixid = r['source'][4:].split(':')[0] if r['source'].startswith('fix ') else None
        if ok.lower() in YES:
            if fixid:
                fn = os.path.join(fdir, f'{fixid[:3]}.json')
                L = page_fixes.setdefault(fn, json.load(open(fn, encoding='utf-8')))
                for f in L:
                    if f['id'] == fixid and f.get('status') == 'unverified':
                        f['status'] = 'verified'
                        f['verified_by'] = 'author review'
            elif not any(c['atom'] == r['atom'] and c['pos'] == int(r['pos']) for c in confirmed):
                confirmed.append({'atom': r['atom'], 'pos': int(r['pos']), 'token': r['token']})
            log['hidden confirmed'] += 1
        else:
            pos, best, sent = int(r['pos']), r['best_reading'], r['sentence']
            if a['text'][pos:pos + len(best)] != best:
                bad.append(f"review_hidden.csv {r['atom']}@{pos}: the text there is no longer {best!r} (sheet is stale: regenerate it)")
                continue
            s0 = a['text'].find(sent)
            if s0 < 0 or not (s0 <= pos < s0 + len(sent)):
                sent, s0 = best, pos
            new = sent[:pos - s0] + ok + sent[pos - s0 + len(best):]
            fid = f"rv-h-{r['atom']}-{pos}"
            if fid not in rv_ids:
                review.append({'id': fid, 'pdf_page': a['pdf_page'], 'find': sent, 'replace': new, 'count': page_count(a['pdf_page'], sent),
                               'layer': 'under_blank', 'status': 'author', 'why': f'author review of hidden text: {best!r} -> {ok!r}'})
                rv_ids.add(fid)
            log['hidden corrected'] += 1
    if bad:
        for b in bad:
            print('REVIEW', b)
        sys.exit(f'{len(bad)} problems in the review sheets: nothing applied')
    for fn, L in page_fixes.items():
        with open(fn, 'w', encoding='utf-8') as f:
            f.write('[\n' + ',\n'.join(' ' + json.dumps(x, ensure_ascii=False) for x in L) + '\n]\n')
    if review:
        json.dump(review, open(rv_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    json.dump(dec, open(dp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    json.dump(confirmed, open(hc_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('applied: ' + (', '.join(f'{k} {v}' for k, v in sorted(log.items())) or 'nothing (no decisions filled in)'))
    if not log:
        return
    # rebuild the text only, then list what changed against the lock the topics were written on
    T = tools_dir(pack)
    root = os.path.dirname(os.path.abspath(pack))
    for step in (['build.py', pack], ['atoms.py', pack], ['errata.py', pack]):
        r = subprocess.run([sys.executable, '-I', os.path.join(T, step[0]), step[1]], capture_output=True, text=True, cwd=root)
        if r.returncode:
            sys.exit(f'{step[0]} failed:\n{r.stdout}\n{r.stderr}')
        if step[0] == 'build.py':
            import shutil
            shutil.copy(os.path.join(root, 'build', 'fixes_merged.json'), os.path.join(pack, 'fixes.json'))
    lock = json.load(open(os.path.join(pack, 'atoms.lock.json'), encoding='utf-8'))
    import hashlib
    atoms2 = {a['id']: a for a in load_jsonl(os.path.join(pack, 'atoms.jsonl'))}
    stale_a = sorted(i for i, a in atoms2.items() if lock.get(i) != hashlib.sha1(a['text'].encode('utf-8')).hexdigest()[:16])
    facts = load_jsonl(os.path.join(pack, 'facts.jsonl'))
    items = load_jsonl(os.path.join(pack, 'items.jsonl'))
    F_ = {f['id']: f for f in facts}
    stale_f = [f['id'] for f in facts if f['atom'] not in atoms2 or atoms2[f['atom']]['text'][f['span'][0]:f['span'][1]] != f['text']]
    changed_e = [eid for eid, d in dec.items() if d.get('decision') in ('not_an_error', 'correct')] + use_changed
    e_facts = [f['id'] for f in facts if any(re.search(rf'(?<![0-9A-Z]){e}:', f.get('needs_review', '')) for e in changed_e)]
    sf = set(stale_f) | set(e_facts)
    E_tok = {eid: [t for t, _ in errata[eid].get('typo_tokens', [])] for eid in use_changed}
    stale_i = sorted(it['id'] for it in items if sf & set(it['fact_ids']) or
                     any(x['erratum'] in changed_e for x in it.get('typo_shown', [])) or
                     any(t in ' '.join(F_[f]['text'] for f in it['fact_ids'] if f in F_) for e in use_changed for t in E_tok[e]))
    topics = sorted({f['topic'] for f in facts if f['id'] in sf} | {it['topic'] for it in items if it['id'] in stale_i})
    out = {'atoms_changed': stale_a, 'facts_stale': stale_f, 'facts_errata_changed': e_facts, 'items_to_rebuild': stale_i,
           'topics_to_rebuild': topics}
    json.dump(out, open(os.path.join(pack, 'review_applied.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(f'must be rebuilt: {len(stale_a)} atoms changed; {len(stale_f)} facts whose text changed; {len(e_facts)} facts whose '
          f'erratum decision changed; {len(stale_i)} items; topics: {", ".join(topics) or "none"}')
    print('next: edit the topic modules of those topics where a fact quote no longer matches, then tools/book/make.sh PACK '
          '(compile + pack + check); review_applied.json has the lists')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pack = args[0] if args and os.path.isdir(args[0]) else HERE
    if '--apply-review' in sys.argv:
        return apply_review(pack)
    only = set(sys.argv[sys.argv.index('--topics') + 1].split(',')) if '--topics' in sys.argv else None
    from_topics = '--from-topics' in sys.argv
    E = collections.defaultdict(list)
    stats = {}

    cleaned = open(os.path.join(pack, 'cleaned.txt'), encoding='utf-8').read()
    atoms = load_jsonl(os.path.join(pack, 'atoms.jsonl'))
    A = {a['id']: a for a in atoms}
    quests = json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))
    mapsrc = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
    if from_topics:
        facts, items, scripts, props, slots, exempt = [], [], [], [], [], {}
        tdir = os.path.join(pack, 'topics')
        for fn in sorted(os.listdir(tdir)):
            if fn.endswith('.json'):
                d = json.load(open(os.path.join(tdir, fn), encoding='utf-8'))
                facts += d['facts']; items += d['items']; scripts += d['scripts']
                props += d.get('props', []); slots += d.get('slots', []); exempt.update(d.get('exam_exempt', {}))
    else:
        facts = load_jsonl(os.path.join(pack, 'facts.jsonl'))
        items = load_jsonl(os.path.join(pack, 'items.jsonl'))
        sj = os.path.join(pack, 'scripts.json')
        scripts = json.load(open(sj, encoding='utf-8'))['scripts'] if os.path.exists(sj) else []
        props, slots = [], []
        ex = os.path.join(pack, 'exam_exempt.json')
        exempt = json.load(open(ex, encoding='utf-8')) if os.path.exists(ex) else {}
    topic_room, topic_rooms, topic_ch = {}, collections.defaultdict(set), {}
    for ch in quests['chapters']:
        for st in ch['steps']:
            topic_room[st['topic']] = st['room']
            topic_rooms[st['topic']].add(st['room'])
            topic_ch[st['topic']] = ch['id']
    map_rooms = set(re.findall(r'^room\s+-?\d+\s+(\S+)', mapsrc, re.M))
    map_props = set(re.findall(r'^iprop\s+-?\d+\s+\S+\s+(\S+)', mapsrc, re.M)) | {p['id'] for p in props}
    map_slots = set(re.findall(r'^islot\s+-?\d+\s+\S+\s+(\S+)', mapsrc, re.M)) | {p['id'] for p in slots}

    def scope(t):
        return only is None or t in only

    # ---------------------------------------------------------------- ledger 0
    wl = os.path.join(pack, 'wordledger.json')
    if os.path.exists(wl):
        w = json.load(open(wl, encoding='utf-8'))
        stats['words'] = f"{w['used']}/{w['total']}"
        if w['unused'] or w['used'] != w['total']:
            E['L0 words'].append(f"{len(w['unused'])} PDF words in no atom, e.g. {w['unused'][:5]}")
    else:
        E['L0 words'].append('wordledger.json missing')

    # ---------------------------------------------------------------- ledger 1
    owner = [None] * len(cleaned)
    for a in atoms:
        s, e = a['src_span']
        if cleaned[s:e] != a['text']:
            E['L1 chars'].append(f"{a['id']}: src_span text differs from the atom text")
        for i in range(s, e):
            if owner[i] is not None:
                E['L1 chars'].append(f"char {i} in {owner[i]} and {a['id']}")
                break
            owner[i] = a['id']
    nonws = [i for i, c in enumerate(cleaned) if not c.isspace()]
    orphans = [i for i in nonws if owner[i] is None]
    if orphans:
        E['L1 chars'].append(f"{len(orphans)} non-whitespace characters in no atom")
    stats['characters'] = f'{len(nonws) - len(orphans)}/{len(nonws)}'
    for a in atoms:
        if not a.get('topic') or a['topic'] not in topic_room:
            E['L1 chars'].append(f"{a['id']}: no topic of the quest table")

    # ---------------------------------------------------------------- staleness
    lock_p = os.path.join(pack, 'atoms.lock.json')
    stale_atoms = []
    if os.path.exists(lock_p):
        lock = json.load(open(lock_p))
        for a in atoms:
            hsh = hashlib.sha1(a['text'].encode('utf-8')).hexdigest()[:16]
            if lock.get(a['id']) not in (None, hsh):
                stale_atoms.append(a['id'])
    stale_facts = [f['id'] for f in facts if f['atom'] not in A or A[f['atom']]['text'][f['span'][0]:f['span'][1]] != f['text']]

    def src_quote(it):
        a = A.get(it['source']['atom'])
        if not a:
            return None
        sent = it['source'].get('sent')
        if sent is None:
            return ws(a['text'])
        if sent >= len(a['sent_spans']):
            return None
        s0, s1 = a['sent_spans'][sent]
        return a['text'][s0:s1]
    stale_items = [it['id'] for it in items if src_quote(it) != it['source'].get('quote', src_quote(it))]
    for x in stale_atoms:
        E['stale (rebuild after a fixes change)'].append(f'atom {x} changed since the last compile')
    for x in stale_facts:
        E['stale (rebuild after a fixes change)'].append(f'fact {x}: its span text no longer matches the atom')
    for x in stale_items:
        E['stale (rebuild after a fixes change)'].append(f'item {x}: its source sentence changed')

    # ---------------------------------------------------------------- ledger 2
    F = {f['id']: f for f in facts}
    if len(F) != len(facts):
        E['L2 sentences'].append('duplicate fact ids')
    by_atom = collections.defaultdict(list)
    for f in facts:
        by_atom[f['atom']].append(f)
    sent_tot = sent_ok = num_tot = num_ok = 0
    chain = []
    for a in atoms:
        if not scope(a.get('topic')):
            continue
        fl = by_atom.get(a['id'], [])
        for si, (s0, s1) in enumerate(a['sent_spans']):
            sent_tot += 1
            cov = [f for f in fl if f['span'][0] <= s0 and f['span'][1] >= s1]
            over = [f for f in fl if f['span'][0] < s1 and f['span'][1] > s0]
            if cov:
                sent_ok += 1
            else:
                E['L2 sentences'].append(f"{a['id']} s{si} in no fact span: {a['text'][s0:s1][:70]!r}")
            for m in NUM_RE.finditer(a['text'][s0:s1]):
                num_tot += 1
                if any(m.group(0) in f['claim'] or m.group(0) in ' '.join(f.get('keys', [])) for f in over):
                    num_ok += 1
                else:
                    E['L2 sentences'].append(f"{a['id']} s{si}: number {m.group(0)!r} named by no fact")
            chain.append({'atom': a['id'], 'sentence': si, 'facts': [f['id'] for f in cov]})
    stats['sentences'] = f'{sent_ok}/{sent_tot}'
    stats['numbers'] = f'{num_ok}/{num_tot}'

    # ---------------------------------------------------------------- items
    ep = os.path.join(pack, 'errata.json')
    errata = json.load(open(ep, encoding='utf-8')) if os.path.exists(ep) else []
    typo_tokens = [(e['id'], tok, cor, bool(e.get('use_intended'))) for e in errata
                   if e.get('kind') == 'typo' and not e.get('withdrawn') and not e.get('resolved') for tok, cor in e.get('typo_tokens', [])]
    typo_kept = []
    answers = collections.defaultdict(set)          # q -> normalized answers (whole book)
    claims = set(norm(f['claim']) for f in facts)
    for f in facts:
        for q, ans in f.get('qa', []):
            answers[q].add(norm(ans))
    tested_by = collections.defaultdict(list)
    stems = collections.Counter()
    multi_counts = collections.defaultdict(list)
    for it in items:
        if not scope(it.get('topic')):
            for fid in it['fact_ids']:
                tested_by[fid].append(it)
            continue
        iid, t = it['id'], it['type']
        if t not in ITEM_TYPES:
            E['items'].append(f'{iid}: unknown type {t}')
        for fid in it['fact_ids']:
            if fid not in F:
                E['items'].append(f'{iid}: unknown fact {fid}')
            tested_by[fid].append(it)
        stems[ws(it['stem']).lower()] += 1
        # explanation quotes the source sentence + page
        q = src_quote(it)
        a = A.get(it['source']['atom'])
        if q is None or ws(q) not in ws(it.get('explanation', '')):
            E['items'].append(f'{iid}: explanation does not quote its source sentence')
        if a and f"p. {a['page']}" not in it.get('explanation', ''):
            E['items'].append(f"{iid}: explanation does not give the printed page p. {a['page']}")
        if t in EXAM:
            opts = it.get('options', [])
            corr = [o for o in opts if o.get('ok')]
            wrong = [o for o in opts if not o.get('ok')]
            if len(opts) != 5:
                E['items'].append(f'{iid}: {len(opts)} options (exam items need 5)')
            if sorted(it['correct']) != sorted(o['k'] for o in corr):
                E['items'].append(f'{iid}: correct keys do not match the options')
            if t == 'simplu' and len(corr) != 1:
                E['items'].append(f'{iid}: simplu with {len(corr)} correct')
            if t == 'multiplu':
                if not 2 <= len(corr) <= 4:
                    E['items'].append(f'{iid}: multiplu with {len(corr)} correct')
                multi_counts[it['topic']].append(len(corr))
            if len(set(norm(o['t']) for o in opts)) != len(opts):
                E['items'].append(f'{iid}: duplicate options')
            # tested facts follow the structure
            if t == 'simplu' or it.get('style') == 'list':
                exp = {o['fact'] for o in corr}
            else:
                exp = {o['fact'] for o in opts}
            if not exp <= set(it['fact_ids']):
                E['items'].append(f'{iid}: fact_ids miss facts its answer depends on')
            # (c) wrong for the whole book
            if t == 'simplu' or it.get('style') == 'list':
                qk = it.get('q')
                if not qk:
                    E['items'].append(f'{iid}: no question key q')
                for o in wrong:
                    if norm(o['t']) in answers.get(qk, set()):
                        E['items'].append(f"{iid}: wrong option {o['k']} is a correct answer to {qk} elsewhere in the book")
                for o in corr:
                    if norm(o['t']) not in answers.get(qk, set()):
                        E['items'].append(f"{iid}: correct option {o['k']} is not a registered answer to {qk}")
            else:
                for o in wrong:
                    if norm(o['t']) in claims:
                        E['items'].append(f"{iid}: false statement {o['k']} is a fact's claim")
            # (d) every false multiplu option quotes the sentence that makes it false
            if t == 'multiplu':
                for o in wrong:
                    why = o.get('why', '')
                    if not re.search(r'„.+” \(p\. \d{4}', why, re.S):
                        E['items'].append(f"{iid}: false option {o['k']} has no quoted book sentence with its page")
            # (f) cues and form
            lens = sorted((len(o['t']) for o in opts), reverse=True)
            if t == 'simplu' and len(corr[0]['t']) == lens[0] and lens[0] > 1.35 * lens[1] if corr else False:
                E['items'].append(f'{iid}: the correct option is much the longest (length cue)')
            if t == 'multiplu' and wrong and corr:
                mc = statistics.mean(len(o['t']) for o in corr)
                mw = statistics.mean(len(o['t']) for o in wrong)
                if mc > 1.6 * mw or mw > 1.6 * mc:
                    E['items'].append(f'{iid}: correct and wrong options differ in length (length cue)')
            sw = set(words_of(it['stem']))
            if corr and wrong:
                in_corr = set.intersection(*[set(words_of(o['t'])) for o in corr]) & sw
                in_wrong = set().union(*[set(words_of(o['t'])) for o in wrong])
                cue = in_corr - in_wrong
                if cue and not it.get('cue_ok'):
                    E['items'].append(f'{iid}: stem word(s) {sorted(cue)} repeated only in the correct option(s)')
            ends = set(o['t'].rstrip().endswith('.') for o in opts)
            digits = set(bool(re.search(r'\d', o['t'])) for o in opts)
            if len(ends) > 1:
                E['items'].append(f'{iid}: options mix full statements and phrases')
            if len(digits) > 1 and all(re.fullmatch(r'[\d.,–%<> ]+', o['t'].strip()) for o in opts if re.search(r'\d', o['t'])):
                E['items'].append(f'{iid}: some options are numbers and some are not')
        if t == 'cloze':
            g = it['stem'].count('____')
            if g != len(it['answers']):
                E['items'].append(f'{iid}: gaps != answers')
            if g > 1 and not it.get('list_gap'):
                E['items'].append(f'{iid}: {g} gaps in a sentence that is not a list')
            for ans in it['answers']:
                if norm(ans) in FUNCTION or (len(norm(ans)) < 2 and not norm(ans).isdigit()):
                    E['items'].append(f'{iid}: gap {ans!r} is a function word')
                if q and ans not in q:
                    E['items'].append(f'{iid}: gap {ans!r} is not an exact substring of the source sentence')
        if t == 'label':
            for l in it.get('labels', []):
                fa = A.get(F[l['fact']]['atom']) if l['fact'] in F else None
                if not fa or l['t'] not in fa['text']:
                    E['items'].append(f"{iid}: label {l['t']!r} is not an exact substring of the figure's printed labels")
        # §1 typos are shown, never the test. Gaps and labels never hold a misprint; another answer field may show it as
        # printed (class B) unless the misprint is the test itself (at least half of the answer's words); class A
        # (use_intended) answers carry the intended form; a class-B misprint is never replaced by its intended form.
        ftext = ' '.join(F[f]['text'] for f in it['fact_ids'] if f in F)
        for path_, txt in answer_fields(it):
            for eid, tok, cor, use in typo_tokens:
                hit = re.search(r'(?<!\w)' + re.escape(tok) + r'(?!\w)', txt, re.I)
                if hit and path_[0] in ('answers', 'labels'):
                    E['typos tested'].append(f'{iid}: {path_[0]} holds the misprint {tok!r} ({eid})')
                elif hit and use:
                    E['typos tested'].append(f'{iid}: {path_[0]} holds the class-A misprint {tok!r} ({eid}) instead of {cor!r}')
                elif hit:
                    share = len(re.findall(r'\w+', tok)) / max(1, len(re.findall(r'\w+', txt)))
                    if share >= 0.5:
                        if it.get('typo_kept'):
                            typo_kept.append(iid)
                        else:
                            E['typos tested'].append(f'{iid}: the misprint {tok!r} ({eid}) is the test ({share:.0%} of {path_[0]})')
                elif not use and tok in ftext and re.search(r'(?<!\w)' + re.escape(cor) + r'(?!\w)', txt) and cor not in ftext:
                    E['typos tested'].append(f'{iid}: {path_[0]} uses the intended form {cor!r} of class-B {eid} (the book prints {tok!r})')
        if it.get('typo_kept') and 'needs_review' not in it['flags']:
            E['typos tested'].append(f'{iid}: kept as printed with its misprint but not flagged needs_review')
        if t == 'sort':
            for e in it['entries']:
                if e['group'] not in it['groups']:
                    E['items'].append(f"{iid}: entry group {e['group']!r} is not one of the item's groups")
        if t in ('sort', 'match', 'label'):
            exp = {e['fact'] for e in it.get('entries', it.get('pairs', it.get('labels', [])))}
            if exp != set(it['fact_ids']):
                E['items'].append(f'{iid}: fact_ids differ from its entries')
    for s, n in stems.items():
        if n > 1:
            E['items'].append(f'duplicate stem x{n}: {s[:70]!r}')
    for tp, cnts in multi_counts.items():
        if not scope(tp):
            continue
        if len(cnts) >= 3 and not {2, 3, 4} <= set(cnts):
            E['items'].append(f'topic {tp}: multiplu correct counts {sorted(cnts)} (need 2, 3 and 4 each used)')

    # ---------------------------------------------------------------- ledger 3 + exam coverage
    per_topic = collections.defaultdict(list)
    tot = ok = 0
    noexam = []
    for f in facts:
        if not scope(f['topic']):
            continue
        tot += 1
        its = tested_by.get(f['id'], [])
        per_topic[f['topic']].append(len(its))
        if len(set(i['type'] for i in its)) >= 2:
            ok += 1
        else:
            E['L3 facts->items'].append(f"{f['id']}: tested by types {sorted(set(i['type'] for i in its))} (need >= 2)")
        if not any(i['type'] in EXAM for i in its):
            if f['id'] in exempt:
                noexam.append((f['id'], exempt[f['id']]))
            else:
                E['L3 facts->items'].append(f"{f['id']}: no simplu / multiplu tests it (and it is not in exam_exempt)")
    stats['facts tested by >= 2 types'] = f'{ok}/{tot}'
    topic_stats = {t: {'facts': len(v), 'min_items_per_fact': min(v) if v else 0,
                       'median_items_per_fact': statistics.median(v) if v else 0} for t, v in per_topic.items()}

    # ---------------------------------------------------------------- ledger 4
    item_by_id = {i['id']: i for i in items}
    req = collections.defaultdict(list)
    stations = collections.defaultdict(list)
    for sc in scripts:
        if sc.get('topic') is None:                 # optional review / exam pools (never count toward coverage)
            if sc.get('required') or sc['room'] not in map_rooms:
                E['scripts'].append(f"{sc['id']}: review script must be optional and in a map room")
            continue
        if not scope(sc.get('topic')):
            if sc.get('required'):
                for fid in sc['covers']:
                    req[fid].append(sc)
            continue
        if sc['room'] not in map_rooms:
            E['scripts'].append(f"{sc['id']}: room {sc['room']} not in institute.map")
        if sc.get('prop') and sc['prop'] not in map_props:
            E['scripts'].append(f"{sc['id']}: prop {sc['prop']} not in institute.map")
        if sc.get('slot') and sc['slot'] not in map_slots and not re.search(rf'^slot\s+-?\d+\s+{re.escape(sc["slot"])}\s', mapsrc, re.M):
            E['scripts'].append(f"{sc['id']}: slot {sc['slot']} not in institute.map")
        if sc['kind'] == 'station':
            stations[sc['topic']].append(sc['id'])
        tested = set()
        for st in sc['steps']:
            if 'item' in st:
                if st['item'] not in item_by_id:
                    E['scripts'].append(f"{sc['id']}: unknown item {st['item']}")
                else:
                    tested |= set(item_by_id[st['item']]['fact_ids'])
        for fid in sc['covers']:
            if fid not in tested:
                E['scripts'].append(f"{sc['id']}: covers {fid} but no step tests it")
            if sc.get('required'):
                req[fid].append(sc)
    rt = rok = 0
    for f in facts:
        if not scope(f['topic']):
            continue
        rt += 1
        if any(sc['room'] in topic_rooms[f['topic']] for sc in req.get(f['id'], [])):
            rok += 1
        else:
            E['L4 facts->scripts'].append(f"{f['id']}: no required script in {topic_room.get(f['topic'])}")
    stats['facts on the required path'] = f'{rok}/{rt}'
    if typo_kept:
        stats['items kept with a misprint as test'] = f'{len(set(typo_kept))} (needs_review, listed in report.md)'
    for t in topic_room:
        if scope(t) and any(f['topic'] == t for f in facts) and not stations.get(t):
            E['scripts'].append(f'topic {t} has no station')
    if only is None:
        for t in topic_room:
            if not any(f['topic'] == t for f in facts):
                E['L2 sentences'].append(f'topic {t}: no facts yet')

    # ---------------------------------------------------------------- map: prop kinds, placement, interactables
    if only is None and not from_topics:
        pkf = os.path.join(pack, 'prop_kinds.json')
        if not os.path.exists(pkf):
            E['map'].append('prop_kinds.json missing')
        else:
            PK = json.load(open(pkf, encoding='utf-8'))
            mrooms, ips, isl = parse_map(mapsrc)
            for p in ips:
                if p[3] not in PK['kinds']:
                    E['map'].append(f'iprop {p[2]}: kind {p[3]} not in prop_kinds.json')
            for p in isl:
                if p[3] not in PK['slot_kinds']:
                    E['map'].append(f'islot {p[2]}: kind {p[3]} not in prop_kinds.json')
            used = sorted({p[3] for p in ips})
            new = [k for k in used if PK['kinds'].get(k, {}).get('status') == 'new']
            stats['prop kinds (iprop)'] = f'{len(used)} ({len(used) - len(new)} existing, {len(new)} new: {", ".join(new)})'
            if len(new) > 12:
                E['map'].append(f'{len(new)} new prop kinds (max 12)')
            per = collections.defaultdict(set)
            for line in mapsrc.splitlines():
                m = re.match(r'prop\s+(-1)\s+(\S+)\s+-?[\d.]', line)     # yard props (the rooms' furniture is counted below)
                if m:
                    per[-1].add(m.group(2))
            for p in ips:
                if p[2] not in on_model_ids(mapsrc):
                    per[p[0]].add(p[3])
            for m in re.finditer(r'^prop\s+(-?\d+)\s+\S+\s+\S+\.\d+\s+(\S+)\s.*\s(wall|free|floor)\s*$', mapsrc, re.M):
                per[int(m.group(1))].add('furn:' + m.group(2))   # rooms and corridors
            gof = {k: g for g, ks in PK.get('mesh_groups', {}).items() for k in ks}
            mesh = {f: {gof.get(k.replace('furn:', '') if k.startswith('furn:') else 'book:' + k, k) for k in per[f]} for f in per}
            stats['visible kinds per storey'] = ', '.join(f'{f}: {len(per[f])}' for f in sorted(per))
            stats['meshes per storey (shared)'] = ', '.join(f'{f}: {len(mesh[f])}' for f in sorted(mesh)) + ' (mesh_groups in prop_kinds.json)'
            FR = [(m.group(2), (m.group(3), m.group(4), float(m.group(5)), float(m.group(6)), int(m.group(1)), 0, 0, m.group(11)))
                  for m in re.finditer(r'^prop\s+(-?\d+)\s+(\S+)\s+(\S+\.\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(wall|free|floor|door)', mapsrc, re.M)]
            stats['furniture (institute.map)'] = (f"{sum(1 for _, x in FR if x[7] != 'door')} pieces + {sum(1 for _, x in FR if x[7] == 'door')} door leaves, "
                                                  f"{len({x[1] for _, x in FR if x[7] != 'door'})} kinds")
            mf = os.path.join(pack, 'model_furniture.json')
            if not os.path.exists(mf):
                E['furniture'].append('model_furniture.json missing: run tools/book/model_furniture.js on the 3D page')
            else:                                  # the 3D page and the file must place the same furniture
                PG = {p['id']: p for p in json.load(open(mf, encoding='utf-8'))['pieces']}
                FM = {x[0]: (r, x) for r, x in FR}
                for i in sorted(set(PG) ^ set(FM)):
                    E['furniture'].append(f'{i}: ' + ('on the 3D page, not in institute.map' if i in PG else 'in institute.map, not on the 3D page'))
                for i in sorted(set(PG) & set(FM)):
                    a, (r, x) = PG[i], FM[i]
                    if a['kind'] != x[1] or a['room'] != r or abs(a['x'] - x[2]) > 0.01 or abs(a['z'] - x[3]) > 0.01:
                        E['furniture'].append(f"{i}: page {a['kind']} {a['x']},{a['z']} / map {x[1]} {x[2]},{x[3]}")
                stats['furniture page = file'] = 'yes (same ids, kinds, positions within 1 cm)' if not E.get('furniture') else 'NO'
            ONM = on_model_ids(mapsrc)
            stats['on the model furniture'] = f'{len(ONM)} props / NPC points are furniture records of the map'
            for rid, name, why in placement_check(mrooms, ips, isl, PK, onm=ONM):
                E['placement'].append(f'{rid} {name}: {why}')
            stats['placement (props + slots)'] = f'{len(ips) + len(isl)} checked, {len(E.get("placement", []))} failures'
            used_anchor = {sc.get('prop') for sc in scripts} | {sc.get('slot') for sc in scripts}
            for p in ips + isl:
                if p[2] not in used_anchor:
                    E['map'].append(f'{p[2]} ({p[1]}): no script uses it')
            anchors = collections.defaultdict(set)      # every point a player interacts with: new props / slots + map slots scripts use
            for p in ips + isl:
                anchors[p[1]].add(p[2])
            for sc in scripts:
                anchors[sc['room']].add(sc.get('prop') or sc.get('slot') or sc.get('npc') or 'trigger:' + sc.get('trigger', ''))
            cnt = {r: len(a) for r, a in anchors.items()}
            groups = collections.defaultdict(list)
            for sc in scripts:
                if sc.get('group'):
                    groups[sc['group']].append(sc)
            for g, P in groups.items():
                if sorted(x['part'] for x in P) != list(range(1, len(P) + 1)) or any(x['parts'] != len(P) for x in P):
                    E['script length'].append(f'{g}: parts not numbered 1..{len(P)}')
                if len({(x['room'], x.get('prop'), x.get('slot'), x.get('npc'), x['kind']) for x in P}) != 1:
                    E['script length'].append(f'{g}: parts not on the same prop / NPC')
            stats['split scripts'] = f'{len(groups)} scripts played in {sum(len(P) for P in groups.values())} parts'
            for r, n in cnt.items():
                if n > 12:
                    E['map'].append(f'room {r}: {n} interactables (max 12)')
            v = sorted(cnt.values())
            stats['interactables per room'] = f'min {v[0]}, median {v[len(v) // 2]}, max {v[-1]} ({len(v)} rooms)'
            # script length (one sitting): required scripts at most 15 steps
            lens = sorted((len(sc['steps']), sc['id'], sc['room']) for sc in scripts if sc.get('required'))
            for n, sid, r in lens:
                if n > 15:
                    E['script length'].append(f'{sid} ({r}): {n} steps (max 15 in one sitting; split into parts)')
            if lens:
                stats['steps per required script'] = f'min {lens[0][0]}, median {lens[len(lens) // 2][0]}, max {lens[-1][0]}'

    # ---------------------------------------------------------------- binary pack: the reference C reader must accept it
    if only is None and not from_topics:
        import shutil, subprocess, tempfile
        mg = os.path.join(pack, 'gastro.mgpack')
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not os.path.exists(mg) or not os.path.exists(mg + '.h'):
            E['pack'].append('gastro.mgpack (+ .h) missing: run tools/book/packbin.py PACK PACK/gastro.mgpack')
        elif not cc:
            stats['binary pack'] = 'not read (no C compiler)'
        else:
            with tempfile.TemporaryDirectory() as td:
                exe = os.path.join(td, 'mgpack_read')
                r = subprocess.run([cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-include', mg + '.h',
                                    os.path.join(pack, 'mgpack_read.c'), '-o', exe], capture_output=True, text=True)
                if r.returncode:
                    E['pack'].append('mgpack_read.c does not build: ' + r.stderr.strip()[:300])
                else:
                    r = subprocess.run([exe, mg], capture_output=True, text=True)
                    kv = dict(l.split('=', 1) for l in r.stdout.splitlines() if '=' in l)
                    if r.returncode:
                        E['pack'].append('reference reader rejects the pack: ' + r.stderr.strip()[:300])
                    want = {'facts': len(facts), 'items': len(items), 'scripts': sum(1 for x in scripts if x.get('topic')),
                            'figlabels': sum(len(fg.get('labels', [])) for fg in json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8'))),
                            'props': len(re.findall(r'^iprop\s', mapsrc, re.M)), 'slots': len(re.findall(r'^islot\s', mapsrc, re.M)),
                            'furniture': len(re.findall(r'^prop\s+-?\d+\s+\S+\s+\S+\.\d+\s.*\s(?:wall|free|floor)\s*$', mapsrc, re.M)),
                            'door_leaves': len(re.findall(r'^prop\s+-?\d+\s+\S+\s+\S+\.\d+\s.*\sdoor\s+opens\s', mapsrc, re.M))}
                    for k, v in want.items():
                        if str(v) != kv.get(k):
                            E['pack'].append(f'pack {k} = {kv.get(k)}, sources {v} (stale pack: rerun packbin.py)')
                    stats['binary pack'] = (f"{int(kv.get('bytes', 0)):,} bytes, v{kv.get('version')}, string table {int(kv.get('string_table_bytes', 0)):,} "
                                            f"bytes, longest string {kv.get('longest_string_bytes')} bytes, largest script "
                                            f"{kv.get('largest_script')} ({kv.get('largest_script_steps')} steps, {int(kv.get('largest_script_string_bytes', 0)):,} string bytes)")
                    json.dump(kv, open(os.path.join(pack, 'pack_measure.json'), 'w'), indent=1)

    # ---------------------------------------------------------------- §2 answers in the book's own words
    wp = os.path.join(pack, 'wordrule.py')
    if os.path.exists(wp):
        import importlib.util as _iu
        _sp = _iu.spec_from_file_location('wordrule', wp)
        WR = _iu.module_from_spec(_sp)
        _sp.loader.exec_module(WR)
        bookn = WR.norm('\n'.join(a['text'] for a in atoms))
        wc, kept = collections.Counter(), []
        for it in items:
            if not scope(it['topic']):
                continue
            src = WR.source_text(it, F, A)
            shown = [(x['used'], x['printed']) for x in it.get('typo_shown', [])]
            for path_, txt in WR.short_answers(it):
                k, miss = WR.classify(txt, src, shown, bookn)
                wc[k] += 1
                if k == 'reworded':
                    if it.get('kept_reworded'):
                        kept.append(it['id'])
                        if 'needs_review' not in it['flags']:
                            E['answer words'].append(f"{it['id']}: kept with words not printed but not flagged needs_review")
                    else:
                        E['answer words'].append(f"{it['id']}: {path_[0]} {txt[:60]!r} uses words not in its source sentence: {', '.join(miss[:5])}")
        stats['short answers'] = ', '.join(f'{k} {v}' for k, v in sorted(wc.items())) + (f'; kept with unprinted words {len(set(kept))} (needs_review)' if kept else '')
    else:
        E['answer words'].append('wordrule.py missing next to check.py')

    # ---------------------------------------------------------------- leak
    if '--leak' in sys.argv:
        repo = sys.argv[sys.argv.index('--leak') + 1]
        for b in leak_scan(pack, repo):
            E['leak (public repo)'].append(b)
        stats['leak check'] = 'clean' if not E.get('leak (public repo)') else 'FAIL'

    json.dump({'stats': stats, 'topics': topic_stats, 'no_exam_item': noexam,
               'stale': {'atoms': stale_atoms, 'facts': stale_facts, 'items': stale_items}, 'chain': chain},
              open(os.path.join(pack, 'coverage.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print('BOOK-1 check' + (f' (topics: {",".join(sorted(only))})' if only else ' (whole book)'))
    for k, v in stats.items():
        print(f'  {k:30s} {v}')
    if noexam:
        print(f'  facts without an exam item (exempt, listed in report.md): {len(noexam)}')
    bad = sum(len(v) for v in E.values())
    for k, v in E.items():
        print(f'FAIL {k}: {len(v)}')
        for e in v[:30]:
            print('   ', e)
        if len(v) > 30:
            print(f'    ... {len(v) - 30} more')
    if bad:
        print(f'CHECK FAILED: {bad} problems')
        sys.exit(1)
    print('CHECK PASSED: 100 %')


if __name__ == '__main__':
    main()

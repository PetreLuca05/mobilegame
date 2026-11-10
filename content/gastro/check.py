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


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pack = args[0] if args and os.path.isdir(args[0]) else HERE
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
                    if not re.search(r'„.+” \(p\. \d{4}', why):
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
                if norm(ans) in FUNCTION or len(norm(ans)) < 2:
                    E['items'].append(f'{iid}: gap {ans!r} is a function word')
                if q and norm(ans) not in norm(q):
                    E['items'].append(f'{iid}: gap {ans!r} not in the source sentence')
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
    for t in topic_room:
        if scope(t) and any(f['topic'] == t for f in facts) and not stations.get(t):
            E['scripts'].append(f'topic {t} has no station')
    if only is None:
        for t in topic_room:
            if not any(f['topic'] == t for f in facts):
                E['L2 sentences'].append(f'topic {t}: no facts yet')

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

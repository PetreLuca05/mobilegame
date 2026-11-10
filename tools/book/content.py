"""Authoring helpers for BOOK-1 Phases B-D (facts, items, scripts), one topic per module in tools/book/topics/.

v2 rules (author's check-in answers, §2):
- an item *tests* a fact only when its answer depends on it: the keyed answer (simplu), every statement judged true or
  false (multiplu statements), every correct option (multiplu list), a gap (cloze), a card answer, an entry / pair /
  position (sort / match / order). Wrong options never count. Ledger 3 counts tested facts only.
- every fact is tested by >= 1 exam item (simplu / multiplu) unless listed with T.exam_exempt(fact, reason).
- wrong options are wrong for the whole book: each simplu / multiplu-list has a question key `q`; facts register the
  answers they give to q (T.answers, and automatically the keyed answer of a simplu); check.py fails if a wrong option
  equals any registered answer of q anywhere in the book. A false multiplu statement must not equal any fact's claim.
- every false option of a multiplu carries the book's sentence that makes it false, with the page.
- cloze: one gap (the tested token) unless list=True.
Option letters are shuffled deterministically per item, so the key is not always A.
"""
import hashlib, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LETTERS = 'ABCDE'


def norm(s):
    s = s.lower().replace('ş', 'ș').replace('ţ', 'ț')
    s = re.sub(r'[„”"«»().,;:!?…\-–—•\s]+', ' ', s)
    return s.strip()



MAX_STEPS = 15      # one sitting; a longer required script is played as parts on the same prop / NPC (Partea 1, 2, ...)


def _split_steps(S):
    """Cut a step list into the fewest parts of <= MAX_STEPS. A part that starts in the middle of a teach block shows that
    teach step again first (a new sitting); cuts right before a teach step are preferred, then even sizes."""
    L = len(S)
    if L <= MAX_STEPS:
        return [S]
    last_teach = [None] * (L + 1)
    lt = None
    for i, st in enumerate(S):
        last_teach[i] = lt if st['stage'] != 'teach' else None
        if st['stage'] == 'teach':
            lt = st
    INF = (10 ** 9,)
    best = [INF] * (L + 1)
    cut = [0] * (L + 1)
    best[0] = (0, 0, 0)
    for j in range(1, L + 1):
        for i in range(max(0, j - MAX_STEPS), j):
            if best[i] == INF:
                continue
            rep = 1 if last_teach[i] is not None else 0
            size = j - i + rep
            if size > MAX_STEPS:
                continue
            c = (best[i][0] + 1, best[i][1] + rep, max(best[i][2], size))
            if c < best[j]:
                best[j], cut[j] = c, i
    bounds, j = [], L
    while j:
        bounds.append((cut[j], j))
        j = cut[j]
    out = []
    for i, j in reversed(bounds):
        P = list(S[i:j])
        if last_teach[i] is not None:
            P.insert(0, dict(last_teach[i], repeat=True))
        out.append(P)
    return out

class Topic:
    def __init__(self, topic_id, pack):
        self.topic = topic_id
        self.pack = pack
        self.atoms = {}
        for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8'):
            a = json.loads(l)
            self.atoms[a['id']] = a
        self.facts, self.items, self.scripts, self.props, self.slots = [], [], [], [], []
        self.exempt = {}
        self._f = {}
        ep = os.path.join(pack, 'errata.json')
        self.errata = json.load(open(ep, encoding='utf-8')) if os.path.exists(ep) else []

    # ------------------------------------------------------------------ facts
    def fact(self, key, atom, quote, claim, ftype, keys=(), qa=(), review=None, occ=None):
        """quote: exact substring of the atom text (unique, or pick the n-th occurrence with occ=n, 0-based), an int =
        sentence index, or None = whole atom."""
        a = self.atoms[atom]
        assert a['topic'] == self.topic, f'{key}: {atom} belongs to topic {a["topic"]}, not {self.topic}'
        t = a['text']
        if quote is None:
            span = [0, len(t)]
        elif isinstance(quote, int):
            span = list(a['sent_spans'][quote])
        else:
            i = t.find(quote)
            assert i >= 0, f'{key}: quote not in {atom}: {quote!r}'
            if occ is None:
                assert t.find(quote, i + 1) < 0, f'{key}: quote not unique in {atom} (use occ=n): {quote!r}'
            else:
                for _ in range(occ):
                    i = t.find(quote, i + 1)
                    assert i >= 0, f'{key}: occurrence {occ} of {quote!r} not in {atom}'
            span = [i, i + len(quote)]
        fid = f'f-{self.topic}-{key}'
        assert key not in self._f, f'duplicate fact key {key}'
        f = {'id': fid, 'topic': self.topic, 'atom': atom, 'span': span, 'text': t[span[0]:span[1]], 'claim': claim,
             'type': ftype, 'keys': list(keys), 'qa': [list(x) for x in qa], 'page': a['page']}
        notes = [review] if review else []
        for e in self.errata:                              # a fact whose span holds a printed erratum is flagged
            if atom in e['atoms'] and not e.get('withdrawn') and not e.get('resolved'):
                i = t.find(e['printed'])
                while i >= 0:                              # every occurrence of the printed snippet
                    if i < span[1] and i + len(e['printed']) > span[0]:
                        notes.append(f"{e['id']}: {e['issue']}")
                        break
                    i = t.find(e['printed'], i + 1)
        if notes:
            f['needs_review'] = ' | '.join(notes)
        if a.get('unverified_ocr'):
            f['unverified_ocr'] = True
        self._f[key] = f
        self.facts.append(f)
        return fid

    def answers(self, q, fkey, *answers):
        """register that fact fkey answers question key q with each of `answers`"""
        for ans in answers:
            if [q, ans] not in self._f[fkey]['qa']:
                self._f[fkey]['qa'].append([q, ans])

    def exam_exempt(self, fkey, reason):
        self.exempt[self._f[fkey]['id']] = reason

    def F(self, keys):
        if isinstance(keys, str):
            keys = keys.split()
        return [self._f[k]['id'] for k in keys]

    # ------------------------------------------------------------------ explanations
    def _sent(self, atom, sent):
        a = self.atoms[atom]
        if sent is None:
            return re.sub(r'\s+', ' ', a['text'])
        s0, s1 = a['sent_spans'][sent]
        return a['text'][s0:s1]

    def _pg(self, atom):
        a = self.atoms[atom]
        return 'p. ' + ', '.join(str(p) for p in a['pages'])

    def cite_fact(self, fkey):
        """the book's sentence that holds fact fkey, quoted with its page"""
        f = self._f[fkey]
        a = self.atoms[f['atom']]
        if a['kind'] == 'figure_labels' and f['span'] != [0, len(a['text'])]:
            fig = (a.get('fig') or '').replace('fig_', 'Fig. ').replace('_', '.')
            return f'„{f["text"]}” ({self._pg(f["atom"])}, eticheta din {fig})'
        s = None
        for i, (s0, s1) in enumerate(a['sent_spans']):
            if s0 <= f['span'][0] < s1 or (f['span'][0] <= s0 and f['span'][1] >= s1):
                s = i
                break
        return f'„{re.sub(r"\s+", " ", self._sent(f["atom"], s))}” ({self._pg(f["atom"])})'

    def _expl(self, src, note):
        atom, sent = src
        q = self._sent(atom, sent)
        return (note.strip() + ' ' if note else '') + f'Sursa: „{q}” ({self._pg(atom)}).', {'atom': atom, 'sent': sent, 'quote': q}

    def _item(self, key, itype, tested, stem, note, src, difficulty=2, flags=(), **kw):
        expl, source = self._expl(src, note)
        iid = f'i-{self.topic}-{key}'
        assert all(i['id'] != iid for i in self.items), f'duplicate item {key}'
        fl = list(flags)
        for fid in tested:
            f = next(x for x in self.facts if x['id'] == fid)
            if f.get('needs_review') and 'needs_review' not in fl:
                fl.append('needs_review')
                kw.setdefault('review_note', f['needs_review'])
            if f.get('unverified_ocr') and 'unverified_ocr' not in fl:
                fl.append('unverified_ocr')
        it = {'id': iid, 'topic': self.topic, 'type': itype, 'fact_ids': list(dict.fromkeys(tested)), 'stem': stem,
              'explanation': expl, 'source': source, 'difficulty': difficulty, 'flags': fl}
        it.update(kw)
        if kw.get('kept_reworded') and 'needs_review' not in it['flags']:   # §2: words the book does not print, kept on purpose
            it['flags'].append('needs_review')
            it.setdefault('review_note', 'answer not in the printed words: ' + kw['kept_reworded'])
        self._typo_guard(it)
        self.items.append(it)
        return iid

    def _typo_guard(self, it):
        """Typos are shown, never the test. A printed misprint in an answer field (keyed option, card answer, pair, sort
        entry / group, order step): class A errata (spelling only, use_intended yes) put the intended form in the answer;
        class B errata keep the printed wording. Either way the explanation names the printed wording and the erratum, and
        the item is flagged needs_review. Cloze gaps and labels are never rewritten (exact substrings: check.py fails them).
        check.py also fails an answer whose test is the misprint itself (the misprint is at least half of its words)."""
        import typoguard
        if not hasattr(self, '_typos'):
            off = {e['id'] for e in self.errata if e.get('withdrawn') or e.get('resolved')}
            self._typos = {k: v for k, v in typoguard.load_tokens().items() if k not in off}
            self._eissue = {e['id']: e['issue'] for e in self.errata}
            self._use = {e['id']: bool(e.get('use_intended')) for e in self.errata}
        shown = []
        ftext = ' '.join(f['text'] for f in self.facts if f['id'] in it['fact_ids'])
        for path, txt in typoguard.answer_fields(it):
            if path[0] in ('answers', 'labels'):
                continue
            new = txt
            for eid, toks in self._typos.items():
                for tok, cor in toks:
                    # class B: an answer written with the intended form goes back to the printed wording
                    if not self._use.get(eid, False) and tok in ftext and cor not in ftext:
                        mc = re.search(r'(?<!\w)' + re.escape(cor) + r'(?!\w)', new)
                        while mc:
                            new = new[:mc.start()] + tok + new[mc.end():]
                            mc = re.search(r'(?<!\w)' + re.escape(cor) + r'(?!\w)', new)
                    m = typoguard.find(tok, new)
                    while m:
                        use = self._use.get(eid, False)
                        if (eid, tok) not in [(x['erratum'], x['printed']) for x in shown]:
                            shown.append({'erratum': eid, 'printed': tok, 'used': cor if use else tok})
                        if not use:
                            break
                        rep = cor[0].upper() + cor[1:] if m.group(0)[:1].isupper() and cor[:1].islower() else cor
                        new = new[:m.start()] + rep + new[m.end():]
                        m = typoguard.find(tok, new)
            if new != txt:
                obj = it
                for k in path[:-1]:
                    obj = obj[k]
                obj[path[-1]] = new
                # a keyed option that is a registered answer to q keeps being one in its intended form
                if path[0] == 'options' and it.get('q'):
                    for f in self.facts:
                        if f['id'] in it['fact_ids'] and [it['q'], txt] in f['qa'] and [it['q'], new] not in f['qa']:
                            f['qa'].append([it['q'], new])
        if shown:
            it['typo_shown'] = shown
            notes = ' '.join(f'Textul tipărește „{x["printed"]}” ({x["erratum"]}: {self._eissue.get(x["erratum"], "")})'
                             + (f'; răspunsul folosește forma „{x["used"]}”.' if x['used'] != x['printed'] else '; răspunsul păstrează forma tipărită.')
                             for x in shown)
            it['explanation'] = notes + ' ' + it['explanation']
            if 'needs_review' not in it['flags']:
                it['flags'].append('needs_review')
            it.setdefault('review_note', 'misprint shown: ' + ', '.join(x['erratum'] for x in shown))

    def _shuffle(self, key, opts):
        h = int(hashlib.sha1(f'{self.topic}/{key}'.encode()).hexdigest(), 16)
        idx = list(range(len(opts)))
        for i in range(len(idx) - 1, 0, -1):
            h, j = divmod(h, i + 1)
            idx[i], idx[j] = idx[j], idx[i]
        return [opts[i] for i in idx]

    # ------------------------------------------------------------------ exam items
    def simplu(self, key, q, stem, correct, wrong, note, src, **kw):
        """correct = (text, 'factkey factkey'); wrong = [(text, 'from_factkey' or None)] x4"""
        ctext, cfacts = correct
        tested = self.F(cfacts)
        for fk in (cfacts.split() if isinstance(cfacts, str) else cfacts):
            self.answers(q, fk, ctext)
        assert len(wrong) == 4, key
        opts = [{'t': ctext, 'ok': True, 'fact': tested[0]}] + [{'t': t, 'ok': False, 'from_fact': self.F(fk)[0] if fk else None} for t, fk in wrong]
        opts = self._shuffle(key, opts)
        for k, o in zip(LETTERS, opts):
            o['k'] = k
        return self._item(key, 'simplu', tested, stem, note, src, q=q, options=opts,
                          correct=[o['k'] for o in opts if o['ok']], distractor_facts=[o['from_fact'] for o in opts if not o['ok'] and o.get('from_fact')], **kw)

    def multiplu(self, key, q, stem, correct, wrong, note, src, **kw):
        """list style: correct = [(text, 'factkeys')] x2-4 (each an answer to q); wrong = [(text, 'from_factkey')]
        (a wrong option's from_fact is the book sentence that shows it is not an answer to q)"""
        tested = []
        opts = []
        for t, fk in correct:
            ids = self.F(fk)
            tested += ids
            for k in (fk.split() if isinstance(fk, str) else fk):
                self.answers(q, k, t)
            opts.append({'t': t, 'ok': True, 'fact': ids[0]})
        for t, fk in wrong:
            assert fk, f'{key}: a multiplu wrong option needs the fact that makes it wrong'
            opts.append({'t': t, 'ok': False, 'from_fact': self.F(fk)[0], 'why': f'Fals: {self.cite_fact(fk.split()[0])}.'})
        assert len(opts) == 5 and 2 <= len(correct) <= 4, key
        opts = self._shuffle(key, opts)
        for k, o in zip(LETTERS, opts):
            o['k'] = k
        return self._item(key, 'multiplu', tested, stem, note, src, q=q, style='list', options=opts,
                          correct=[o['k'] for o in opts if o['ok']], **kw)

    def multiplu_tf(self, key, stem, statements, note, src, **kw):
        """statement style: statements = [(text, True, 'fact'), (text, False, 'refuting_fact')] x5, 2-4 true.
        Every statement is judged, so every listed fact is tested."""
        tested, opts = [], []
        for t, ok, fk in statements:
            fid = self.F(fk)[0]
            tested.append(fid)
            o = {'t': t, 'ok': ok, 'fact': fid}
            if not ok:
                o['why'] = f'Fals: {self.cite_fact(fk.split()[0])}.'
            opts.append(o)
        assert len(opts) == 5 and 2 <= sum(1 for o in opts if o['ok']) <= 4, key
        opts = self._shuffle(key, opts)
        for k, o in zip(LETTERS, opts):
            o['k'] = k
        return self._item(key, 'multiplu', tested, stem, note, src, style='statements', options=opts,
                          correct=[o['k'] for o in opts if o['ok']], **kw)

    # ------------------------------------------------------------------ other items
    def card(self, key, facts, front, back, note, src, **kw):
        return self._item(key, 'card', self.F(facts), front, note, src, answer=back, **kw)

    def cloze(self, key, facts, text, answers, note, src, list_gap=False, **kw):
        assert text.count('____') == len(answers), key
        assert len(answers) == 1 or list_gap, f'{key}: one gap per cloze unless the sentence is a list'
        return self._item(key, 'cloze', self.F(facts), text, note, src, answers=list(answers), list_gap=list_gap, **kw)

    def sort(self, key, stem, groups, entries, note, src, **kw):
        """entries: (text, group, 'factkey')"""
        assert all(g in groups for _, g, _ in entries)
        tested = [self.F(fk)[0] for _, _, fk in entries]
        return self._item(key, 'sort', tested, stem, note, src, groups=list(groups),
                          entries=[{'t': t, 'group': g, 'fact': self.F(fk)[0]} for t, g, fk in entries], **kw)

    def match(self, key, stem, pairs, note, src, **kw):
        """pairs: (left, right, 'factkey')"""
        tested = [self.F(fk)[0] for _, _, fk in pairs]
        return self._item(key, 'match', tested, stem, note, src, pairs=[{'left': l, 'right': r, 'fact': self.F(fk)[0]} for l, r, fk in pairs], **kw)

    def order(self, key, facts, stem, sequence, note, src, **kw):
        return self._item(key, 'order', self.F(facts), stem, note, src, sequence=list(sequence), **kw)

    def label(self, key, stem, figure, labels, note, src, **kw):
        """figure with blanks: labels = [(label text exactly as printed, 'factkey')]; the player puts each label back"""
        tested = [self.F(fk)[0] for _, fk in labels]
        return self._item(key, 'label', tested, stem, note, src, figure=figure,
                          labels=[{'t': t, 'fact': self.F(fk)[0]} for t, fk in labels], **kw)

    # ------------------------------------------------------------------ world
    def prop(self, floor, room, pid, model, x, z, yaw, label, on_model=False):
        """on_model: the prop is a piece of furniture the institute model already has at (x, z) (no new mesh)"""
        assert pid.startswith(self.topic + '_') or pid.startswith(self.topic.split('_')[0]), f'prop id {pid} should start with the topic'
        self.props.append({'floor': floor, 'room': room, 'id': pid, 'model': model, 'x': x, 'z': z, 'yaw': yaw, 'label': label, 'topic': self.topic,
                           'on_model': on_model})

    def slot(self, floor, room, sid, kind, x, z, yaw, on_model=False):
        self.slots.append({'floor': floor, 'room': room, 'id': sid, 'kind': kind, 'x': x, 'z': z, 'yaw': yaw, 'topic': self.topic, 'on_model': on_model})

    def script(self, sid, kind, room, title, steps, required=True, slot=None, prop=None, trigger='interact', npc=None):
        covers, S = [], []
        for st in steps:
            if st[0] == 'teach':
                S.append({'stage': 'teach', 'atoms': st[1]})
            else:
                stage, ikey = st
                iid = f'i-{self.topic}-{ikey}'
                it = next(i for i in self.items if i['id'] == iid)
                for f in it['fact_ids']:
                    if f not in covers:
                        covers.append(f)
                S.append({'stage': stage, 'item': iid})
        parts = _split_steps(S) if required else [S]
        base = f's-{self.topic}-{sid}'
        for k, P in enumerate(parts, 1):
            pc = []
            for st in P:
                for f in (self.item_by_id(st['item'])['fact_ids'] if 'item' in st else []):
                    if f not in pc:
                        pc.append(f)
            sc = {'id': base if len(parts) == 1 else f'{base}-p{k}', 'topic': self.topic, 'room': room, 'kind': kind,
                  'title': title if len(parts) == 1 else f'{title} · Partea {k}', 'trigger': trigger, 'required': required,
                  'steps': P, 'covers': [f for f in covers if f in pc]}
            if len(parts) > 1:
                sc.update(group=base, part=k, parts=len(parts))
            if slot:
                sc['slot'] = slot
            if prop:
                sc['prop'] = prop
            if npc:
                sc['npc'] = npc
            self.scripts.append(sc)
        return base

    def item_by_id(self, iid):
        return next(i for i in self.items if i['id'] == iid)

    # ------------------------------------------------------------------ save + local checks
    def save(self):
        out = os.path.join(self.pack, 'topics')
        os.makedirs(out, exist_ok=True)
        json.dump({'topic': self.topic, 'facts': self.facts, 'items': self.items, 'scripts': self.scripts,
                   'props': self.props, 'slots': self.slots, 'exam_exempt': self.exempt},
                  open(os.path.join(out, f'{self.topic}.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        types, exam = {}, set()
        for it in self.items:
            for f in it['fact_ids']:
                types.setdefault(f, set()).add(it['type'])
                if it['type'] in ('simplu', 'multiplu'):
                    exam.add(f)
        weak = [f['id'] for f in self.facts if len(types.get(f['id'], ())) < 2]
        noexam = [f['id'] for f in self.facts if f['id'] not in exam and f['id'] not in self.exempt]
        return len(self.facts), len(self.items), len(self.scripts), weak, noexam

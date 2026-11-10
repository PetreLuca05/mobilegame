#!/usr/bin/env python3
"""§2 word rule (shared by check.py's copy and inflections.py): every word of a short keyed answer (simplu keyed option,
list-style multiplu keyed options, match pairs, sort entries, order steps, cards of at most 6 words) is a printed word of
its source sentence or an inflection of one. Source sentence(s) = the sentences of the atoms that hold the spans of the
item's facts, plus the item's quoted source sentence.
Classes: verbatim (a substring of the source), shortened (every word printed or inflected, in the printed order, as one
contiguous run), inflection (every word printed or inflected, not one contiguous run), reworded (a word not in the source)."""
import re

W = re.compile(r'[0-9A-Za-zĂÂÎȘȚăâîșțŞŢşţöüéÖÜÉαβγκ]+')
ALT = [('ş', 'ș'), ('ţ', 'ț'), ('oa', 'o'), ('ea', 'e'), ('ă', 'a'), ('â', 'a'), ('î', 'i'), ('ș', 's'), ('ț', 't')]


def norm(s):
    s = s.lower()
    for a, b in ALT[:2]:
        s = s.replace(a, b)
    return s


def root(w):
    w = norm(w)
    for a, b in ALT[2:]:
        w = w.replace(a, b)
    return w


def same_word(a, b):
    """b is a or an inflection of a (Romanian endings and vowel / consonant alternations; numbers exact)"""
    na, nb = norm(a), norm(b)
    if na == nb or any(na in g and nb in g for g in IRREG):
        return True
    if na.isdigit() or nb.isdigit() or len(na) < 3 or len(nb) < 3:
        return False
    ra, rb = root(a), root(b)
    k = 0
    while k < min(len(ra), len(rb)) and ra[k] == rb[k]:
        k += 1
    return k >= max(3, int(0.6 * min(len(ra), len(rb))) + (1 if min(len(ra), len(rb)) >= 8 else 0)) and abs(len(ra) - len(rb)) <= 5


def source_text(it, F, A):
    parts = []
    for fid in it['fact_ids']:
        f = F.get(fid)
        if not f or f['atom'] not in A:
            continue
        a = A[f['atom']]
        s0, s1 = f['span']
        for x0, x1 in a.get('sent_spans') or [[0, len(a['text'])]]:
            if x0 < s1 and s0 < x1:
                parts.append(a['text'][x0:x1])
        if not any(p for p in parts):
            parts.append(f['text'])
    boxes = {A[F[f]['atom']].get('in_box') for f in it['fact_ids'] if f in F and F[f]['atom'] in A} - {None}
    if boxes:                                      # an answer that lists a box's or table's entries: the whole box
        parts += [a['text'] for a in A.values() if a.get('in_box') in boxes]
    parts.append(it['source'].get('quote', ''))
    return ' \n '.join(dict.fromkeys(parts))


def short_answers(it):
    t = it['type']
    if t == 'simplu' or (t == 'multiplu' and it.get('style') == 'list'):
        return [(('options', i), o['t']) for i, o in enumerate(it['options']) if o.get('ok')]
    if t == 'match':
        return [(('pairs', i, 'left'), p['left']) for i, p in enumerate(it['pairs'])] + [(('pairs', i, 'right'), p['right']) for i, p in enumerate(it['pairs'])]
    if t == 'sort':
        return [(('entries', i), e['t']) for i, e in enumerate(it['entries'])]
    if t == 'order':
        return [(('sequence', i), s) for i, s in enumerate(it['sequence'])]
    if t == 'card' and len(W.findall(it['answer'])) <= 6:
        return [(('answer',), it['answer'])]
    return []


# grammar words with no meaning of their own (articles, case particles, linking prepositions, copula, relative pronoun):
# the only words an answer may add, and only where the stem's grammar needs them. Negation, quantity and meaning-bearing
# prepositions (nu, mai, fără, după, ...) are not here.
GRAMMAR = set('a al ale ai alor lui lor cel cea cei cele un o unei unui unor de la cu în pe din prin pentru și sau ca că '
              'care căreia cărei cărui cărora ce se îl le este sunt e fi au are'.split())
# page words: items about the printed page itself (contents, reading lists, figure panels, cross-references) name its
# parts; these words label a place on the page, never a medical claim.
PAGE = set('p pagina paginile pagini volumul volum titlu titlul revista revistă an anul autori autorii subsecțiunea '
           'secțiunea fragmentul imaginea panoul panouri panourile eticheta trimiterea medicul departamentul spitalul'.split())
PRINTED_PAGES = {1147} | set(range(1162, 1167)) | set(range(1172, 1177)) | set(range(1198, 1209))
# Romanian inflections with a changed stem that the prefix test cannot see (same lemma)
IRREG = [{'cale', 'calea', 'căii', 'căile', 'căilor'}, {'boală', 'boala', 'bolii', 'bolile', 'bolilor'}, {'masă', 'masa', 'mese', 'mesei', 'mesele'}]


def classify(txt, src, shown=(), book=''):
    """-> (class, words not in the source). book: all printed text (a verbatim printed span anywhere is verbatim)."""
    probe = txt.strip().rstrip('.')
    for used, printed in shown:
        probe = probe.replace(used, printed)
    probe = re.sub(r'\bp\. (\d{4})\b', lambda m: ' ' if int(m.group(1)) in PRINTED_PAGES else m.group(0), probe)   # a printed page number
    if book:                                       # a quotation of printed text („…”) counts as printed
        for q in re.findall(r'„([^”]+)”', probe):
            if norm(q.strip('… .')) in book:
                probe = probe.replace('„' + q + '”', ' ')
    if norm(probe) in norm(src) or (book and len(probe) >= 4 and norm(probe) in book):
        return 'verbatim', []
    words = W.findall(probe)
    sw = W.findall(src)
    pos, missing = [], []
    for w in words:
        hit = [i for i, s in enumerate(sw) if same_word(s, w)]
        if hit:
            pos.append(hit)
        elif norm(w) in GRAMMAR or norm(w) in PAGE:
            pos.append(None)
        else:
            missing.append(w)
    if missing:
        return 'reworded', missing
    # one contiguous run in the printed order?
    if all(p is not None for p in pos):
        for start in pos[0] if pos else []:
            if all(start + k in pos[k] for k in range(len(pos))):
                return 'shortened', []
    return 'inflection', []

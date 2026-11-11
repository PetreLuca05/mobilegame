#!/usr/bin/env python3
"""BOOK-1 Phase A build: PDF words + layout/*.txt + fixes.json -> cleaned.txt, atoms.jsonl, wordmap.json.

Layout DSL (one segment per line, '#' comments):
  <kind> <source>... [key=value ...] [flags]
  kinds : hdr (running header / page number / chapter tab), h1..h6 (heading), p (paragraph flow),
          li (list: one atom per bullet item), bt (box title: opens a box), be (closes the box),
          row (table / box row: cells separated by '|'), cap (figure caption), lab (figure-label group),
          toc (contents-list line), ref (reference entry), fig (figure crop: bbox=x0,y0,x1,y1 id=...)
  source: bNNN (whole block) | bNNN:a-b (lines a..b, either end optional) | r(x0,y0,x1,y1) (words with centre
          inside the rectangle, any block) | '|' (cell separator for row)
  keys  : name=  split=i,j (line indices, within the segment, where a new atom starts) path=  topic=
          frag=start|end|both  lvl= (for bt/cap: heading level of the box)  id= bbox= (fig)
  flags : cont (first atom continues the previous text atom) nosplit (one atom) keep (no auto split)
Words are assigned to segments; every word of the text layer must be used exactly once (word ledger).
"""
import json, re, os, sys, unicodedata

ROOT = os.path.dirname(os.path.abspath(__file__))

PDF2PRINT = {1: 1147}
for i, p in enumerate(range(1162, 1167)): PDF2PRINT[2 + i] = p
for i, p in enumerate(range(1172, 1177)): PDF2PRINT[7 + i] = p
for i, p in enumerate(range(1198, 1209)): PDF2PRINT[12 + i] = p

BULLETS = '•■▪●◦'
DIACRITICS = {'ş': 'ș', 'ţ': 'ț', 'Ş': 'Ș', 'Ţ': 'Ț'}


def norm_word(t):
    for a, b in DIACRITICS.items():
        t = t.replace(a, b)
    return t


class Seg:
    def __init__(s, page, idx, kind, srcs, opts, flags, raw):
        s.page, s.idx, s.kind, s.srcs, s.opts, s.flags, s.raw = page, idx, kind, srcs, opts, flags, raw
        s.id = f'p{page:02d}s{idx:02d}'
        s.lines = []      # list of lines; line = list of word dicts
        s.cells = []      # for row: list of list-of-lines


def parse_layout(path, page):
    segs = []
    for raw in open(path, encoding='utf-8'):
        line = raw.split('#', 1)[0].strip() if not raw.lstrip().startswith('add') else raw.strip()
        if not line:
            continue
        toks = re.findall(r'r\([^)]*\)|\w+="[^"]*"|"[^"]*"|\S+', line)
        kind, rest = toks[0], toks[1:]
        srcs, opts, flags = [], {}, set()
        for t in rest:
            if t == '|' or re.fullmatch(r'b\d{3}(:\d*-?\d*)?', t) or t.startswith('r('):
                srcs.append(t)
            elif '=' in t and not t.startswith('"'):
                k, v = t.split('=', 1)
                opts[k] = v.strip('"')
            else:
                flags.add(t)
        segs.append(Seg(page, len(segs), kind, srcs, opts, flags, line))
    return segs


def group_lines(words):
    """cluster words into lines by vertical centre, then sort by x"""
    ws = sorted(words, key=lambda w: ((w['bbox'][1] + w['bbox'][3]) / 2, w['bbox'][0]))
    lines = []
    for w in ws:
        cy = (w['bbox'][1] + w['bbox'][3]) / 2
        h = w['bbox'][3] - w['bbox'][1]
        if lines:
            L = lines[-1]
            ly = sum((x['bbox'][1] + x['bbox'][3]) / 2 for x in L) / len(L)
            lh = max(x['bbox'][3] - x['bbox'][1] for x in L)
            if abs(cy - ly) < 0.45 * max(h, lh):
                L.append(w)
                continue
        lines.append([w])
    return [sorted(L, key=lambda w: w['bbox'][0]) for L in lines]


def resolve(page, src, used):
    """-> list of lines (list of word dicts)"""
    m = re.fullmatch(r'b(\d{3})(?::(\d*)-?(\d*))?', src)
    out = []
    if m:
        B = page['blocks'][int(m.group(1))]
        n = len(B['lines'])
        if m.group(2) is None and ':' not in src:
            a, b = 0, n - 1
        else:
            spec = src.split(':', 1)[1]
            if '-' in spec:
                a = int(m.group(2)) if m.group(2) else 0
                b = int(m.group(3)) if m.group(3) else n - 1
            else:
                a = b = int(spec)
        for li in range(a, b + 1):
            L = B['lines'][li]
            ws = []
            for wi, w in enumerate(L['words']):
                key = (B['id'], li, wi)
                if key in used:
                    continue  # already taken by an earlier rect source
                ws.append(w); used[key] = True
            if ws:
                out.append(ws)
        return out
    m = re.fullmatch(r'r\(([^)]*)\)', src)
    x0, y0, x1, y1 = [float(v) for v in m.group(1).split(',')]
    picked = []
    for B in page['blocks']:
        for li, L in enumerate(B['lines']):
            for wi, w in enumerate(L['words']):
                cx = (w['bbox'][0] + w['bbox'][2]) / 2
                cy = (w['bbox'][1] + w['bbox'][3]) / 2
                key = (B['id'], li, wi)
                if x0 <= cx <= x1 and y0 <= cy <= y1 and key not in used:
                    picked.append(w); used[key] = True
    return group_lines(picked)


def join_lines(lines):
    """join word lines into one string, removing hyphenation soft hyphens"""
    out = ''
    for L in lines:
        t = ' '.join(norm_word(w['t']) for w in L)
        if not out:
            out = t
        elif out.endswith('­'):
            out = out[:-1] + t
        else:
            out = out + ' ' + t
    return out.replace('­', '')


def cell_text(lines):
    """a table cell: lines at the cell's left margin start a new entry (hanging indent); entries joined by newline"""
    if not lines:
        return ''
    m = min(left(L) for L in lines)
    entries, cur = [], []
    for L in lines:
        if cur and left(L) - m < 3.0 and not cur[-1][-1]['t'].endswith('\u00ad'):
            entries.append(cur); cur = []
        cur.append(L)
    entries.append(cur)
    return '\n'.join(join_lines(e) for e in entries)


def is_bullet_line(L):
    t = L[0]['t']
    return t[:1] in BULLETS or t in ('-', '–')


def left(L):
    return L[0]['bbox'][0]


def split_paragraphs(seg):
    """-> list of (lines) groups"""
    lines = seg.lines
    if not lines:
        return [[]]
    if 'nosplit' in seg.flags or seg.kind in ('cap', 'lab', 'toc', 'hdr', 'ref', 'by') and 'split' not in seg.opts:
        return [lines]
    forced = set(int(x) for x in seg.opts.get('split', '').split(',') if x != '')
    if seg.kind in ('cap', 'lab', 'toc', 'hdr', 'ref', 'by') or 'keep' in seg.flags:
        groups, cur = [], []
        for i, L in enumerate(lines):
            if i in forced and cur:
                groups.append(cur); cur = []
            cur.append(L)
        groups.append(cur)
        return groups
    margin = min(left(L) for L in lines)
    groups, cur = [], []
    for i, L in enumerate(lines):
        new = False
        if i in forced:
            new = True
        elif i > 0:
            if seg.kind in ('li', 'p') and is_bullet_line(L):
                new = True
            elif seg.kind == 'p' and left(L) - margin > 4.0 and not is_bullet_line(lines[i - 1]) and not any(is_bullet_line(x) for x in cur):
                new = True
        if new and cur:
            groups.append(cur); cur = []
        cur.append(L)
    groups.append(cur)
    return groups


def main(pack):
    pages = json.load(open(os.path.join(pack, '..', 'src', 'words.json'), encoding='utf-8'))
    # a word under a white overlay rectangle is hidden even when stray ink (an icon) overlaps it
    wo = os.path.join(pack, '..', 'src', 'white_overlays.json')
    if os.path.exists(wo):
        white = {int(k): v for k, v in json.load(open(wo)).items()}
        for P in pages:
            for B in P['blocks']:
                for L in B['lines']:
                    for w in L['words']:
                        cx, cy = (w['bbox'][0] + w['bbox'][2]) / 2, (w['bbox'][1] + w['bbox'][3]) / 2
                        if w['ink'] and any(a <= cx <= c and b <= cy <= d for a, b, c, d in white.get(P['pdf_page'], [])):
                            w['ink'] = 0
                            w['ink_overridden'] = True
    fixes = {'fixes': [], 'rules': json.load(open(os.path.join(pack, 'rules.json'), encoding='utf-8'))}
    fdir = os.path.join(pack, 'fixes')
    for fn in sorted(os.listdir(fdir)) if os.path.isdir(fdir) else []:
        if fn.endswith('.json'):
            for f in json.load(open(os.path.join(fdir, fn), encoding='utf-8')):
                f.setdefault('source', fn)
                fixes['fixes'].append(f)
    used_all = {}
    segs_all = []
    atoms = []
    heading = []          # stack of (level, text)
    decor = []
    last_bullet = None
    box = None            # (title, path) while inside a box
    for P in pages:
        pno = P['pdf_page']
        lp = os.path.join(pack, 'layout', f'p{pno:02d}.txt')
        if not os.path.exists(lp):
            continue
        used = {}
        segs = parse_layout(lp, pno)
        for s in segs:
            if s.kind in ('row', 'toc'):
                cells, cur = [], []
                for src in s.srcs:
                    if src == '|':
                        cells.append(cur); cur = []
                    else:
                        cur += resolve(P, src, used)
                cells.append(cur)
                s.cells = cells
                s.lines = [L for c in cells for L in c]
            else:
                for src in s.srcs:
                    s.lines += resolve(P, src, used)
        for k in used:
            used_all[(pno,) + k] = True
        segs_all += segs
    # word ledger
    missing = []
    for P in pages:
        for B in P['blocks']:
            for li, L in enumerate(B['lines']):
                for wi, w in enumerate(L['words']):
                    if (P['pdf_page'], B['id'], li, wi) not in used_all:
                        missing.append((P['pdf_page'], B['id'], li, wi, w['t']))
    # atoms
    prev_text_atom = None
    for s in segs_all:
        pno = s.page
        if s.kind == 'be':
            box = None
            last_bullet = None
            continue
        if s.kind == 'fig':
            continue
        if s.kind == 'decor':
            decor.append({'pdf_page': pno, 'seg': s.id, 'why': s.opts.get('why', ''),
                          'text': ' '.join(w['t'] for L in s.lines for w in L)})
            continue
        groups = [s.lines] if s.kind in ('row', 'toc') else split_paragraphs(s)
        if s.kind == 'ctx':
            lvl = int(s.opts['lvl'])
            heading = [h for h in heading if h[0] < lvl] + [(lvl, s.opts['text'])]
            last_bullet = None
            continue
        href = None
        if s.kind == 'bh':
            text = join_lines(s.lines)
            href = {'_text': text}
            box = (box[0], box[1], href)
            path = box[1] + [box[0], href]
            groups = [s.lines]
        if s.kind in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6') or s.kind == 'bt':
            text = join_lines(s.lines)
            href = {'_text': text}
            lvl = int(s.opts.get('lvl', s.kind[1] if s.kind[0] == 'h' else 9))
            if s.kind == 'bt':
                box = (href, [h[1] for h in heading], None)
                path = box[1] + [href]
            else:
                heading = [h for h in heading if h[0] < lvl] + [(lvl, href)]
                path = [h[1] for h in heading]
            groups = [s.lines]
        for gi, g in enumerate(groups):
            if s.kind in ('row', 'toc'):
                text = '\t'.join(cell_text(c) for c in s.cells)
            else:
                text = join_lines(g)
            words = [w for L in g for w in L]
            hidden = sum(1 for w in words if w['ink'] == 0)
            if 'cont' in s.flags and gi == 0 and prev_text_atom is not None:
                a = prev_text_atom
                sep = '' if a['_text'].endswith('­') else ' '
                a['_text'] = (a['_text'] + sep + text).replace('­', '')
                a['_words'] += len(words); a['_hidden'] += hidden
                a['pdf_pages'] = sorted(set(a['pdf_pages'] + [pno]))
                a['segs'].append(s.id)
                continue
            if s.kind in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'bt', 'bh'):
                kind = 'heading'
                hpath = path
            else:
                hpath = (box[1] + [box[0]] + ([box[2]] if box[2] else [])) if box else [h[1] for h in heading]
                kind = {'hdr': 'running_header', 'p': 'paragraph', 'by': 'byline', 'li': 'list_item', 'row': 'box_row' if box else 'table_row',
                        'cap': 'figure_caption', 'lab': 'figure_labels', 'toc': 'contents_line', 'ref': 'reference'}[s.kind]
            if s.opts.get('path'):
                hpath = hpath + [s.opts['path']]
            if kind == 'paragraph' and g and is_bullet_line(g[0]):
                kind = 'list_item'
            if s.kind == 'li':
                kind = 'list_item'
            set_bullet = False
            if kind == 'list_item' and g and 'sub' in s.flags and last_bullet:
                hpath = hpath + [last_bullet]          # nested items printed with a small bullet: parent = last top bullet
            elif kind == 'list_item' and g:
                first = g[0][0]['t']
                if first[:1] in BULLETS:
                    set_bullet = True
                elif first in ('-', '–') and last_bullet:
                    hpath = hpath + [last_bullet]
            a = href if href is not None else {}
            a.update({'_text': text, '_words': len(words), '_hidden': hidden, 'pdf_page': pno, 'pdf_pages': [pno],
                 'kind': kind, 'path': hpath, 'segs': [s.id], 'seg_name': s.opts.get('name'),
                 'frag': s.opts.get('frag'), 'topic': s.opts.get('topic'), 'in_box': box[0] if box else None,
                 'lvl': s.opts.get('lvl'), 'fig': s.opts.get('fig')})
            atoms.append(a)
            if set_bullet:
                last_bullet = a
            if kind in ('paragraph', 'list_item', 'figure_caption', 'box_row', 'table_row', 'reference'):
                prev_text_atom = a
    # rules (global) first, so that a page fix can undo a rule where the print differs (e.g. a printed hyphen range)
    for r in fixes.get('rules', []):
        for a in atoms:
            if r.get('skip_hidden') and a['_words'] and a['_hidden'] == a['_words']:
                continue   # hidden text: no print to justify a typographic change, keep the OCR as it is
            a['_text'] = re.sub(r['find'], r['replace'], a['_text'])
    # fixes
    fixlog = []
    for f in fixes['fixes']:
        cand = [a for a in atoms if (f.get('pdf_page') is None or f['pdf_page'] in a['pdf_pages'])
                and (f.get('seg') is None or a['seg_name'] == f['seg'])]
        if f['find'] == '*' and len(cand) != 1:
            fixlog.append((f['id'], len(cand), 1, False)); continue
        n = 0
        for a in cand:
            if f.get('regex'):
                a['_text'], k = re.subn(f['find'], f['replace'], a['_text'])
            elif f['find'] == '*':
                k = 1
                f['_was'] = a['_text']
                a['_text'] = f['replace']
            elif f['find'] == '':
                k = 1 if a['_text'] == '' else 0
                if k: a['_text'] = f['replace']
            else:
                k = a['_text'].count(f['find'])
                a['_text'] = a['_text'].replace(f['find'], f['replace'])
            n += k
        exp = f.get('count', 1)
        ok = (n == exp) if exp != '*' else n >= 1
        fixlog.append((f['id'], n, exp, ok))
    # resolve heading references now that every fix is applied (paths use the corrected heading text)
    for a in atoms:
        a['path'] = [x['_text'] if isinstance(x, dict) else x for x in a['path']]
        if isinstance(a.get('in_box'), dict):
            a['in_box'] = a['in_box']['_text']
    fixes_out = [{k: v for k, v in f.items()} for f in fixes['fixes']]
    return atoms, missing, fixlog, segs_all, decor, fixes_out


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('pack'); ap.add_argument('--pages', default=''); ap.add_argument('--out', default='')
    args = ap.parse_args()
    pack = args.pack
    atoms, missing, fixlog, segs, decor, fixes_out = main(pack)
    if args.pages:
        want = set(int(x) for x in args.pages.split(','))
        missing = [m for m in missing if m[0] in want]
        atoms_show = [a for a in atoms if set(a['pdf_pages']) & want]
    else:
        atoms_show = atoms
    bad = [x for x in fixlog if not x[3]]
    print('atoms', len(atoms), 'unused words', len(missing), 'fixes', len(fixlog), 'fix count mismatches', len(bad))
    for m in missing[:40]:
        print('  UNUSED', m)
    for b in bad[:40]:
        print('  FIX', b)
    out = args.out or os.path.join(pack, '..', 'build', 'atoms_raw.json')
    json.dump(atoms_show, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    if not args.pages:
        total = sum(len(L['words']) for P in json.load(open(os.path.join(pack, '..', 'src', 'words.json'), encoding='utf-8')) for B in P['blocks'] for L in B['lines'])
        json.dump({'total': total, 'used': total - len(missing), 'unused': [list(m) for m in missing]},
                  open(os.path.join(pack, 'wordledger.json'), 'w', encoding='utf-8'), ensure_ascii=False)
        json.dump({'fixes': fixes_out, 'decor': decor}, open(os.path.join(pack, '..', 'build', 'fixes_merged.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

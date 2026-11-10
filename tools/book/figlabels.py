#!/usr/bin/env python3
"""Locate each printed label of every figure on its crop, for `label` items (figure with blanks).
For each label of a figure_labels atom (TAB-separated), find the text-layer words inside the figure's bbox that spell it
(OCR spelling normalised, best contiguous match on the same or adjacent lines) and store the label's box in crop pixels.
Labels that exist only in the image (no text-layer words) keep bbox null and are listed for manual placement.
  figlabels.py PACK            -> updates PACK/fig/figures.json (labels: [{t, bbox|null}])"""
import difflib, json, os, re, sys

DPI = 200


def norm(s):
    s = s.lower().replace('ş', 'ș').replace('ţ', 'ț')
    return re.sub(r'[^0-9a-zăâîșțαβγ%+<>=]+', '', s)


def main(pack):
    figs = json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8'))
    atoms = {json.loads(l)['id']: json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')}
    pages = json.load(open(os.path.join(pack, '..', 'src', 'words.json'), encoding='utf-8'))
    mp = os.path.join(pack, 'fig', 'label_positions_manual.json')
    manual = json.load(open(mp, encoding='utf-8')) if os.path.exists(mp) else {}
    missing = 0
    total = 0
    for f in figs:
        x0, y0, x1, y1 = f['bbox']
        P = pages[f['pdf_page'] - 1]
        words = []
        for B in P['blocks']:
            for L in B['lines']:
                for w in L['words']:
                    cx, cy = (w['bbox'][0] + w['bbox'][2]) / 2, (w['bbox'][1] + w['bbox'][3]) / 2
                    if x0 <= cx <= x1 and y0 <= cy <= y1 and norm(w['t']):
                        words.append(w)
        words.sort(key=lambda w: (round(w['bbox'][1] / 3), w['bbox'][0]))
        labels = []
        used = set()
        for aid in f['label_atoms']:
            for t in atoms[aid]['text'].split('\t'):
                t = t.strip()
                if not t:
                    continue
                total += 1
                target = norm(t)
                best = (0.0, None)
                n = len(words)
                for i in range(n):
                    if i in used:
                        continue
                    acc = ''
                    for j in range(i, min(n, i + 12)):
                        if j in used:
                            break
                        acc += norm(words[j]['t'])
                        r = difflib.SequenceMatcher(None, acc, target).ratio()
                        if r > best[0]:
                            best = (r, (i, j))
                        if len(acc) > len(target) * 1.6:
                            break
                bbox = None
                if best[1] and best[0] >= 0.72:
                    i, j = best[1]
                    ws = words[i:j + 1]
                    used.update(range(i, j + 1))
                    s = DPI / 72.0
                    bbox = [round((min(w['bbox'][0] for w in ws) - x0) * s), round((min(w['bbox'][1] for w in ws) - y0) * s),
                            round((max(w['bbox'][2] for w in ws) - x0) * s), round((max(w['bbox'][3] for w in ws) - y0) * s)]
                # manual positions win: {fig: {label text: [bbox for the 1st occurrence, bbox for the 2nd, ...]}}
                m = manual.get(f['id'], {}).get(t)
                if m:
                    k = sum(1 for x in labels if x['t'] == t)
                    if k < len(m) and m[k]:
                        bbox = m[k]
                if bbox is None:
                    missing += 1
                else:   # a glyph box can reach a pixel or two past the crop edge (descenders): keep it inside the crop
                    from PIL import Image
                    cw, ch = Image.open(os.path.join(pack, f['file'])).size
                    bbox = [max(0, min(cw, bbox[0])), max(0, min(ch, bbox[1])), max(0, min(cw, bbox[2])), max(0, min(ch, bbox[3]))]
                labels.append({'t': t, 'bbox': bbox})
        f['labels'] = labels
        f['labels_without_position'] = sum(1 for l in labels if l['bbox'] is None)
    json.dump(figs, open(os.path.join(pack, 'fig', 'figures.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(f'labels {total}, placed {total - missing}, without position {missing}')
    for f in figs:
        if f['labels_without_position']:
            print(f"  {f['id']}: {f['labels_without_position']} of {len(f['labels'])} need manual placement")


if __name__ == '__main__':
    main(sys.argv[1])

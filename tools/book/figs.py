#!/usr/bin/env python3
"""Crop every figure declared in PACK/layout/pNN.txt (`fig id=... bbox=x0,y0,x1,y1`) to PACK/fig/<id>.png (200 dpi),
and write PACK/fig/figures.json (id, pdf page, printed page, bbox, caption atom, label atom)."""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PDF2PRINT = {1: 1147}
for i, p in enumerate(range(1162, 1167)): PDF2PRINT[2 + i] = p
for i, p in enumerate(range(1172, 1177)): PDF2PRINT[7 + i] = p
for i, p in enumerate(range(1198, 1209)): PDF2PRINT[12 + i] = p


def main(pack, dpi=200):
    pdf = os.path.join(pack, '..', 'src', 'Gastro.pdf')
    out = os.path.join(pack, 'fig')
    os.makedirs(out, exist_ok=True)
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    figs = []
    for fn in sorted(os.listdir(os.path.join(pack, 'layout'))):
        m = re.match(r'p(\d\d)\.txt$', fn)
        if not m:
            continue
        pno = int(m.group(1))
        for line in open(os.path.join(pack, 'layout', fn), encoding='utf-8'):
            line = line.split('#', 1)[0]
            if not line.strip().startswith('fig '):
                continue
            fid = re.search(r'id=(\S+)', line).group(1)
            x0, y0, x1, y1 = [float(v) for v in re.search(r'bbox=([\d.,]+)', line).group(1).split(',')]
            s = dpi / 72.0
            base = os.path.join(out, fid)
            subprocess.run(['pdftoppm', '-r', str(dpi), '-f', str(pno), '-l', str(pno), '-x', str(int(x0 * s)), '-y', str(int(y0 * s)),
                            '-W', str(int((x1 - x0) * s)), '-H', str(int((y1 - y0) * s)), '-png', '-singlefile', pdf, base], check=True)
            figs.append({'id': fid, 'pdf_page': pno, 'page': PDF2PRINT[pno], 'bbox': [x0, y0, x1, y1], 'file': f'fig/{fid}.png',
                         'caption_atoms': [a['id'] for a in atoms if a.get('fig') == fid and a['kind'] == 'figure_caption'],
                         'label_atoms': [a['id'] for a in atoms if a.get('fig') == fid and a['kind'] == 'figure_labels']})
    json.dump(figs, open(os.path.join(out, 'figures.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('figures', len(figs))


if __name__ == '__main__':
    main(sys.argv[1])

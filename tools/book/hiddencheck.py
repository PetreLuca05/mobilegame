#!/usr/bin/env python3
"""Cross-check the hidden-text classification (ink = 0 under the word in the rendered page) against the white overlay
images in the PDF: every hidden word must lie under a white overlay, and every word under a white overlay must be hidden.
Writes build/hiddencheck.json.  Usage: hiddencheck.py ROOT (holding src/ and build/)"""
import json, os, sys
import numpy as np
import pdfplumber
from PIL import Image


def main(root):
    pdf = pdfplumber.open(os.path.join(root, 'src', 'Gastro.pdf'))
    pages = json.load(open(os.path.join(root, 'src', 'words.json'), encoding='utf-8'))
    out = {'pages': {}, 'hidden_outside_overlay': [], 'inked_inside_overlay': []}
    white = {}
    for P in pages:
        pno = P['pdf_page']
        p = pdf.pages[pno - 1]
        overlays = []
        for im in p.images:
            w, h = im['x1'] - im['x0'], im['bottom'] - im['top']
            if w > 0.9 * float(p.width) and h > 0.9 * float(p.height):
                continue                                   # the page scan itself
            if w < 60:
                continue                                   # the green chapter strip
            try:
                data = p.within_bbox((max(0, im['x0']), max(0, im['top']), min(float(p.width), im['x1']), min(float(p.height), im['bottom'])))
            except Exception:
                data = None
            # keep only white rectangles: the rendered page is (almost) blank inside them
            r = np.asarray(Image.open(os.path.join(root, 'render', f'p-{pno:02d}.png')).convert('L'))
            sx, sy = r.shape[1] / float(p.width), r.shape[0] / float(p.height)
            reg = r[max(0, int(im['top'] * sy)):int(im['bottom'] * sy), max(0, int(im['x0'] * sx)):int(im['x1'] * sx)]
            if reg.size and (reg > 235).mean() > 0.97:
                overlays.append((im['x0'], im['top'], im['x1'], im['bottom']))
        white[pno] = overlays
        n = hid = inside = 0
        for B in P['blocks']:
            for L in B['lines']:
                for wd in L['words']:
                    n += 1
                    x0, y0, x1, y1 = wd['bbox']
                    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
                    under = any(a <= cx <= c and b <= cy <= d for a, b, c, d in overlays)
                    if wd['ink'] == 0:
                        hid += 1
                    if under:
                        inside += 1
                    if wd['ink'] == 0 and not under:
                        out['hidden_outside_overlay'].append([pno, wd['t']])
                    if under and wd['ink'] > 0:
                        out['inked_inside_overlay'].append([pno, wd['t'], wd['ink']])
        out['pages'][pno] = {'words': n, 'hidden': hid, 'under_overlay': inside, 'overlays': len(overlays)}
    json.dump(out, open(os.path.join(root, 'build', 'hiddencheck.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    json.dump(white, open(os.path.join(root, 'src', 'white_overlays.json'), 'w'), indent=0)
    print({k: v for k, v in out['pages'].items() if v['hidden'] or v['under_overlay']})
    print('hidden outside overlay:', len(out['hidden_outside_overlay']), out['hidden_outside_overlay'][:10])
    print('inked inside overlay:', len(out['inked_inside_overlay']), out['inked_inside_overlay'][:10])


if __name__ == '__main__':
    main(sys.argv[1])

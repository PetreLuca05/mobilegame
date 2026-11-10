#!/usr/bin/env python3
"""Draw a labelled 50-px grid over a figure crop (and boxes of the labels already placed) to read label positions.
  figgrid.py PACK FIG_ID OUT.png"""
import json, os, sys
from PIL import Image, ImageDraw
pack, fid, out = sys.argv[1:4]
figs = {f['id']: f for f in json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8'))}
im = Image.open(os.path.join(pack, 'fig', fid + '.png')).convert('RGB')
d = ImageDraw.Draw(im)
W, H = im.size
for x in range(0, W, 50):
    d.line([(x, 0), (x, H)], fill=(255, 0, 0) if x % 100 == 0 else (255, 170, 170), width=1)
    if x % 100 == 0:
        d.text((x + 2, 2), str(x), fill=(200, 0, 0))
for y in range(0, H, 50):
    d.line([(0, y), (W, y)], fill=(0, 0, 255) if y % 100 == 0 else (170, 170, 255), width=1)
    if y % 100 == 0:
        d.text((2, y + 2), str(y), fill=(0, 0, 200))
for l in figs[fid].get('labels', []):
    if l['bbox']:
        d.rectangle(l['bbox'], outline=(0, 160, 0), width=2)
im.save(out)
print(out, im.size, 'missing:', [l['t'] for l in figs[fid].get('labels', []) if not l['bbox']])

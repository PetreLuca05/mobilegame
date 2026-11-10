import json,sys
pages=json.load(open('src/words.json'))
want=[int(x) for x in sys.argv[1:]] or range(1,23)
for P in pages:
    if P['pdf_page'] not in want: continue
    print('=== PDF page',P['pdf_page'])
    for B in P['blocks']:
        ws=[w for L in B['lines'] for w in L['words']]
        hid=sum(1 for w in ws if w['ink']==0)
        fonts={}
        for w in ws: fonts[w['font']]=fonts.get(w['font'],0)+1
        f=max(fonts,key=fonts.get) if fonts else ''
        x0,y0,x1,y1=[round(v) for v in B['bbox']]
        txt=' '.join(w['t'] for w in ws)
        print(f"{B['id'][3:]} [{x0:3d},{y0:3d},{x1:3d},{y1:3d}] L{len(B['lines']):2d} H{hid:3d}/{len(ws):3d} {f[:22]:22s} {txt[:90]}")

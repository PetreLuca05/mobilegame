import json,sys
pages=json.load(open('src/words.json'))
pno=int(sys.argv[1]); y0=float(sys.argv[2]); y1=float(sys.argv[3]); x0=float(sys.argv[4]) if len(sys.argv)>4 else 0; x1=float(sys.argv[5]) if len(sys.argv)>5 else 9999
P=pages[pno-1]
rows=[]
for B in P['blocks']:
    for li,L in enumerate(B['lines']):
        for wi,w in enumerate(L['words']):
            cx=(w['bbox'][0]+w['bbox'][2])/2; cy=(w['bbox'][1]+w['bbox'][3])/2
            if y0<=cy<=y1 and x0<=cx<=x1: rows.append((round(cy),round(w['bbox'][0]),round(w['bbox'][2]),B['id'][3:]+':'+str(li),w['t'],w['ink'],w['font'][:18],w['size']))
rows.sort()
for r in rows: print(*r)

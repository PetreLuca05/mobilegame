import json,sys
pages=json.load(open('src/words.json'))
pno=int(sys.argv[1]); want=set(sys.argv[2:])
P=pages[pno-1]
for B in P['blocks']:
    bid=B['id'][3:]
    if want and bid not in want: continue
    print('--',bid,[round(v) for v in B['bbox']])
    for li,L in enumerate(B['lines']):
        t=' '.join(w['t'] for w in L['words'])
        h=sum(1 for w in L['words'] if w['ink']==0)
        print(f"  {li:2d} x{round(L['bbox'][0]):3d} y{round(L['bbox'][1]):3d} {'H' if h else ' '} {t[:110]}")

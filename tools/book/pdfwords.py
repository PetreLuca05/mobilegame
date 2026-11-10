"""Read the PDF text layer (poppler bbox-layout) into pages -> blocks -> lines -> words,
with per-word ink (rendered page) and font (pdfplumber chars)."""
import re, json, html, sys, os
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
import pdfplumber

NS='{http://www.w3.org/1999/xhtml}'

def parse_bbox(path):
    tree=ET.parse(path)
    pages=[]
    for pi,pg in enumerate(tree.iter(NS+'page')):
        P={'pdf_page':pi+1,'w':float(pg.get('width')),'h':float(pg.get('height')),'blocks':[]}
        bi=0
        for fl in pg.iter(NS+'flow'):
            for bl in fl.iter(NS+'block'):
                B={'id':f'p{pi+1:02d}b{bi:03d}','bbox':[float(bl.get(k)) for k in ('xMin','yMin','xMax','yMax')],'lines':[]}
                li=0
                for ln in bl.iter(NS+'line'):
                    L={'id':f'{B["id"]}l{li:02d}','bbox':[float(ln.get(k)) for k in ('xMin','yMin','xMax','yMax')],'words':[]}
                    for w in ln.iter(NS+'word'):
                        L['words'].append({'t':w.text or '','bbox':[float(w.get(k)) for k in ('xMin','yMin','xMax','yMax')]})
                    B['lines'].append(L); li+=1
                P['blocks'].append(B); bi+=1
        pages.append(P)
    return pages

def annotate(pages, pdf_path, render_dir, dpi=150):
    pdf=pdfplumber.open(pdf_path)
    for P in pages:
        p=pdf.pages[P['pdf_page']-1]
        im=np.asarray(Image.open(os.path.join(render_dir,f'p-{P["pdf_page"]:02d}.png')).convert('L')).astype(np.int32)
        H,W=im.shape; sx=W/P['w']; sy=H/P['h']
        chars=p.chars
        cx=np.array([(c['x0']+c['x1'])/2 for c in chars]) if chars else np.zeros(0)
        cy=np.array([(c['top']+c['bottom'])/2 for c in chars]) if chars else np.zeros(0)
        for B in P['blocks']:
            for L in B['lines']:
                for w in L['words']:
                    x0,y0,x1,y1=w['bbox']
                    a,b,c,d=max(0,int(x0*sx)),max(0,int(y0*sy)),min(W,int(np.ceil(x1*sx))),min(H,int(np.ceil(y1*sy)))
                    reg=im[b:d,a:c]
                    w['ink']=int((reg<170).sum()) if reg.size else 0
                    sel=np.where((cx>=x0-0.5)&(cx<=x1+0.5)&(cy>=y0-0.5)&(cy<=y1+0.5))[0]
                    fonts={}
                    sizes=[]
                    for k in sel:
                        f=chars[k]['fontname'].split('+')[-1]
                        f=re.sub(r'-\d+$','',f).lstrip('*')
                        fonts[f]=fonts.get(f,0)+1; sizes.append(chars[k]['size'])
                    w['font']=max(fonts,key=fonts.get) if fonts else ''
                    w['size']=round(float(np.median(sizes)),1) if sizes else 0.0
    return pages

if __name__=='__main__':
    pages=annotate(parse_bbox(sys.argv[1]),sys.argv[2],sys.argv[3])
    json.dump(pages,open(sys.argv[4],'w',encoding='utf-8'),ensure_ascii=False)

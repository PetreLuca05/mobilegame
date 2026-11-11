import json,sys
A=json.load(open(sys.argv[1]))
want=set(int(x) for x in sys.argv[2:])
for i,a in enumerate(A):
    if want and not (set(a['pdf_pages'])&want): continue
    flag=('H' if a['_hidden']==a['_words'] and a['_words'] else ('h' if a['_hidden'] else ' '))
    print(f"[{a['pdf_page']:02d} {a['kind'][:9]:9s}{flag}] {' > '.join(a['path'][-2:])[:60]}")
    print('    '+a['_text'].replace('\n',' ⏎ ').replace('\t',' ⇥ '))

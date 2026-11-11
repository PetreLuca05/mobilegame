#!/usr/bin/env python3
"""Print a topic's atoms for authoring: id, kind, page, layer, path, and every sentence with its index.
  topicatoms.py PACK TOPIC"""
import json, os, sys
pack, topic = sys.argv[1], sys.argv[2]
errata = json.load(open(os.path.join(pack, 'errata.json'), encoding='utf-8'))
for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8'):
    a = json.loads(l)
    if a['topic'] != topic:
        continue
    flags = [a['layer']] + (['fragment:' + a['fragment']] if a['fragment'] else []) + (['fig:' + a['fig']] if a.get('fig') else [])
    print(f"== {a['id']}  {a['kind']}  p.{a['page']}  {' '.join(flags)}")
    print(f"   path: {' > '.join(a['path'][1:])}")
    for e in errata:
        if a['id'] in e['atoms']:
            print(f"   ERRATUM {e['id']} ({e['kind']}): printed {e['printed']!r} — {e['issue']}")
    for i, (s0, s1) in enumerate(a['sent_spans']):
        print(f"   [{i}] {a['text'][s0:s1]!r}")

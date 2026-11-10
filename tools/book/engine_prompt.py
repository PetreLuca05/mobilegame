#!/usr/bin/env python3
"""Write PACK/ENGINE_PROMPT.md (Phase F): the Claude Code prompt for the engine agent, with the measured pack figures.
  engine_prompt.py PACK MGPACK"""
import collections, json, os, re, sys


def main(pack, mgpack):
    J = lambda n: [json.loads(l) for l in open(os.path.join(pack, n), encoding='utf-8')]
    facts, items = J('facts.jsonl'), J('items.jsonl')
    scripts = json.load(open(os.path.join(pack, 'scripts.json'), encoding='utf-8'))['scripts']
    mp = open(os.path.join(pack, 'institute.map'), encoding='utf-8').read()
    props = re.findall(r'^iprop\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)', mp, re.M)
    slots = re.findall(r'^islot\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)', mp, re.M)
    figs = json.load(open(os.path.join(pack, 'fig', 'figures.json'), encoding='utf-8'))
    size = os.path.getsize(mgpack)
    types = collections.Counter(i['type'] for i in items)
    kinds = collections.Counter(s['kind'] for s in scripts)
    per_storey = collections.Counter(int(p[0]) for p in props)
    models_storey = collections.defaultdict(set)
    for p in props:
        models_storey[int(p[0])].add(p[3])
    models = sorted(set(p[3] for p in props))
    req = [s for s in scripts if s.get('required')]
    req_storey = collections.Counter()
    room_floor = {m.group(2): int(m.group(1)) for m in re.finditer(r'^room\s+(-?\d+)\s+(\S+)', mp, re.M)}
    for s in req:
        req_storey[room_floor.get(s['room'], -9)] += 1
    fig_px = sum(f['bbox'][2] - f['bbox'][0] for f in figs)
    tpl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'engine_prompt_template.md'), encoding='utf-8').read()
    vals = {
        'FACTS': f'{len(facts):,}', 'ITEMS': f'{len(items):,}',
        'ITEM_TYPES': ', '.join(f'{k} {v}' for k, v in types.most_common()),
        'SCRIPTS': f'{len(scripts)}', 'REQ_SCRIPTS': f'{len(req)}',
        'SCRIPT_KINDS': ', '.join(f'{k} {v}' for k, v in kinds.most_common()),
        'PROPS': f'{len(props)}', 'SLOTS': f'{len(slots)}', 'MODELS': ', '.join(models), 'NMODELS': str(len(models)),
        'PROPS_STOREY': ', '.join(f'storey {k}: {per_storey[k]} props / {len(models_storey[k])} models' for k in sorted(per_storey)),
        'REQ_STOREY': ', '.join(f'storey {k}: {v}' for k, v in sorted(req_storey.items())),
        'MAX_REQ_STOREY': str(max(req_storey.values()) if req_storey else 0),
        'MAX_MODELS_STOREY': str(max((len(v) for v in models_storey.values()), default=0)),
        'PACK_BYTES': f'{size:,}', 'PACK_MB': f'{size / 1048576:.2f}',
        'BITSET': f'{(len(facts) + 7) // 8}', 'FIGS': str(len(figs)),
        'FIG_MB': f'{len(figs) * 1024 * 1024 * 4 / 1048576 / 2:.0f}',
    }
    for k, v in vals.items():
        tpl = tpl.replace('{' + k + '}', v)
    open(os.path.join(pack, 'ENGINE_PROMPT.md'), 'w', encoding='utf-8').write(tpl)
    print('ENGINE_PROMPT.md', len(tpl), 'chars;', vals['FACTS'], 'facts,', vals['ITEMS'], 'items,', vals['PACK_BYTES'], 'bytes pack')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

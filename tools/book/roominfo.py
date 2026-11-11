#!/usr/bin/env python3
"""Room geometry for placing props / slots: bounds, doors, windows, existing slots and props, topics.
  roominfo.py PACK ROOM [ROOM...]      (coordinates in metres; x east, z SOUTH; yaw 0 faces +z / south, 90 east,
                                        180 north, 270 west)"""
import json, os, re, sys
pack = sys.argv[1]
src = open(os.path.join(pack, '..', 'src', 'institute.map'), encoding='utf-8').read().split('# ---- BOOK-1')[0].splitlines()
quests = json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))
taken = []
tdir = os.path.join(pack, 'topics')
for fn in sorted(os.listdir(tdir)) if os.path.isdir(tdir) else []:
    if fn.endswith('.json'):
        d = json.load(open(os.path.join(tdir, fn), encoding='utf-8'))
        taken += [('iprop', p) for p in d.get('props', [])] + [('islot', p) for p in d.get('slots', [])]
for room in sys.argv[2:]:
    cur = None
    out = []
    for line in src:
        m = re.match(r'room\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"([^"]*)"', line)
        if m:
            cur = m.group(2)
            if cur == room:
                f, x0, z0, x1, z1 = m.group(1), *[float(m.group(i)) for i in (4, 5, 6, 7)]
                out.append(f'room {room} floor {f} type {m.group(3)} x {x0}..{x1} z {z0}..{z1} ({x1 - x0:.1f} x {z1 - z0:.1f} m) "{m.group(8)}"')
            continue
        if cur == room and re.match(r'(door|window|slot|learn)\s', line):
            out.append('  ' + line.split('#')[0].strip())
    tops = [(ch['id'], st['topic']) for ch in quests['chapters'] for st in ch['steps'] if st['room'] == room]
    out.append(f'  topics: {tops}')
    for kind, p in taken:
        if p['room'] == room:
            out.append(f'  {kind} {p["id"]} {p.get("model", p.get("kind"))} x {p["x"]} z {p["z"]} yaw {p["yaw"]} (topic {p["topic"]})')
    print('\n'.join(out) if out else f'{room}: not found')
    print()
